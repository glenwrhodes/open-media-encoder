#include "MainWindow.h"

#include "EncodingPanel.h"
#include "ExportSettingsDialog.h"
#include "MediaBrowser.h"
#include "PreferencesDialog.h"
#include "PresetBrowser.h"
#include "QueuePanel.h"
#include "SpeechModelPrompt.h"
#include "StreamRecorderPanel.h"
#include "TaskbarProgress.h"
#include "Theme.h"
#include "WatchFolderPanel.h"
#include "core/AppSettings.h"
#include "core/CommandBuilder.h"
#include "core/EncoderCaps.h"
#include "core/EncoderEngine.h"
#include "core/Formats.h"
#include "core/ImageSequence.h"
#include "core/PresetManager.h"
#include "core/QueueModel.h"
#include "core/StreamRecorder.h"
#include "core/SystemIntegration.h"
#include "core/WatchFolderManager.h"

#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QRegularExpression>
#include <QShortcut>
#include <QStatusBar>
#include <QSystemTrayIcon>
#include <QThreadPool>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QtConcurrent>

namespace {

struct ProbeResult {
    MediaInfo info;
    double total = 0.0;
};

QString queueFile() { return QDir(AppSettings::dataDir()).filePath(QStringLiteral("queue.json")); }

QStringList expandMediaFiles(const QStringList &paths)
{
    QStringList out;
    for (const QString &p : paths) {
        const QFileInfo fi(p);
        if (fi.isDir()) {
            const auto entries = QDir(p).entryInfoList(QDir::Files, QDir::Name);
            for (const QFileInfo &e : entries)
                if (Formats::isMediaFile(e.absoluteFilePath()))
                    out << e.absoluteFilePath();
        } else if (fi.exists()) {
            out << fi.absoluteFilePath();
        }
    }
    return out;
}

void showTextDialog(QWidget *parent, const QString &title, const QString &text, const QString &filePath = {})
{
    QDialog dlg(parent);
    dlg.setWindowTitle(title);
    dlg.resize(900, 600);
    auto *edit = new QPlainTextEdit(text);
    edit->setReadOnly(true);
    edit->setLineWrapMode(QPlainTextEdit::NoWrap);
    QFont mono(QStringLiteral("Consolas"));
    mono.setStyleHint(QFont::Monospace);
    edit->setFont(mono);
    edit->moveCursor(QTextCursor::End);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    if (!filePath.isEmpty()) {
        auto *open = buttons->addButton(QStringLiteral("Open in Editor"), QDialogButtonBox::ActionRole);
        QObject::connect(open, &QPushButton::clicked, &dlg,
                         [filePath] { QDesktopServices::openUrl(QUrl::fromLocalFile(filePath)); });
        auto *clear = buttons->addButton(QStringLiteral("Clear Log"), QDialogButtonBox::ResetRole);
        QObject::connect(clear, &QPushButton::clicked, &dlg, [filePath, edit] {
            QFile::remove(filePath);
            edit->clear();
        });
    }
    auto *v = new QVBoxLayout(&dlg);
    v->addWidget(edit);
    v->addWidget(buttons);
    dlg.exec();
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("Open Media Encoder"));
    resize(1600, 960);
    setDockNestingEnabled(true);

    m_model = new QueueModel(this);
    m_engine = new EncoderEngine(m_model, this);
    m_watch = new WatchFolderManager(this);
    m_recorder = new StreamRecorder(this);

    m_queue = new QueuePanel(m_model);
    setCentralWidget(m_queue);
    buildDocks();
    buildMenus();
    wireEngine();

    m_taskbar = new TaskbarProgress(this);
    m_progressLabel = new QLabel;
    m_progressLabel->setObjectName("muted");
    statusBar()->addPermanentWidget(m_progressLabel);

    m_tray = new QSystemTrayIcon(windowIcon(), this);
    m_tray->setToolTip(windowTitle());
    if (QSystemTrayIcon::isSystemTrayAvailable())
        m_tray->show();

    m_saveTimer = new QTimer(this);
    m_saveTimer->setSingleShot(true);
    m_saveTimer->setInterval(1000);
    connect(m_saveTimer, &QTimer::timeout, this, [this] { m_model->save(queueFile()); });
    m_idleTimer = new QTimer(this);
    m_idleTimer->setSingleShot(true);
    connect(m_idleTimer, &QTimer::timeout, this, [this] {
        if (!m_engine->isRunning() && m_model->countWithStatus(OutputStatus::Ready) > 0)
            m_engine->start();
    });

    connect(m_model, &QueueModel::sourceAdded, this, &MainWindow::probeSource);
    connect(m_model, &QueueModel::filesDropped, this, qOverload<const QStringList &>(&MainWindow::addFiles));
    connect(m_model, &QueueModel::queueChanged, this, [this] {
        scheduleSave();
        restartIdleTimer();
        updateStatus();
    });

    // Queue panel
    connect(m_queue, &QueuePanel::addSourceClicked, this, &MainWindow::addSourceDialog);
    connect(m_queue, &QueuePanel::addOutputClicked, this, &MainWindow::addOutputToSelection);
    connect(m_queue, &QueuePanel::duplicateClicked, this, &MainWindow::duplicateSelection);
    connect(m_queue, &QueuePanel::removeClicked, this, &MainWindow::removeSelection);
    connect(m_queue, &QueuePanel::startClicked, this, &MainWindow::startQueue);
    connect(m_queue, &QueuePanel::pauseClicked, m_engine, &EncoderEngine::pause);
    connect(m_queue, &QueuePanel::stopClicked, this, &MainWindow::stopQueue);
    connect(m_queue, &QueuePanel::editOutputRequested, this, &MainWindow::editOutput);
    connect(m_queue, &QueuePanel::chooseOutputPathRequested, this, &MainWindow::chooseOutputPath);
    connect(m_queue, &QueuePanel::contextMenuRequested, this, &MainWindow::showQueueContextMenu);
    connect(m_queue, &QueuePanel::autoEncodeToggled, this, [this](bool on) {
        AppSettings::setAutoEncodeWatchFolders(on);
        m_watch->setActive(on);
        statusBar()->showMessage(
            on ? QStringLiteral("Watching folders for new media") : QStringLiteral("Watch folders paused"), 4000);
    });
    auto *startShortcut =
        new QShortcut(QKeySequence(Qt::Key_Return), m_queue->view(), nullptr, nullptr, Qt::WidgetShortcut);
    connect(startShortcut, &QShortcut::activated, this, &MainWindow::startQueue);
    auto *delShortcut = new QShortcut(QKeySequence::Delete, m_queue->view(), nullptr, nullptr, Qt::WidgetShortcut);
    connect(delShortcut, &QShortcut::activated, this, &MainWindow::removeSelection);
    auto *upShortcut =
        new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Up), m_queue->view(), nullptr, nullptr, Qt::WidgetShortcut);
    connect(upShortcut, &QShortcut::activated, this, [this] { moveSelectedSource(-1); });
    auto *downShortcut =
        new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Down), m_queue->view(), nullptr, nullptr, Qt::WidgetShortcut);
    connect(downShortcut, &QShortcut::activated, this, [this] { moveSelectedSource(1); });

    // Watch folders
    connect(m_watch, &WatchFolderManager::filesReady, this, &MainWindow::onWatchFilesReady);
    m_watch->load();
    m_queue->setAutoEncode(AppSettings::autoEncodeWatchFolders());
    m_watch->setActive(AppSettings::autoEncodeWatchFolders());

    // Restore previous session.
    m_model->load(queueFile());

    const QByteArray geo = AppSettings::value("ui/geometry").toByteArray();
    const QByteArray state = AppSettings::value("ui/state").toByteArray();
    if (!geo.isEmpty())
        restoreGeometry(geo);
    // Dock sizes only take effect once the window has its real size, so apply them after show.
    QTimer::singleShot(0, this, [this, state] {
        applyDefaultDockSizes();
        m_defaultState = saveState();
        if (!state.isEmpty()) {
            restoreState(state);
            // Layouts saved before the Stream Recorder existed leave it loose; tab it with Watch Folders.
            if (!AppSettings::value("ui/recorderDockPlaced").toBool()) {
                auto *watch = findChild<QDockWidget *>(QStringLiteral("watch"));
                auto *rec = findChild<QDockWidget *>(QStringLiteral("recorder"));
                if (watch && rec && !watch->isFloating()) {
                    tabifyDockWidget(watch, rec);
                    watch->raise();
                    if (auto *enc = findChild<QDockWidget *>(QStringLiteral("encoding")); enc && !enc->isFloating())
                        resizeDocks({watch, enc}, {width() / 4, width() * 2 / 5}, Qt::Horizontal);
                }
            }
        }
        AppSettings::setValue("ui/recorderDockPlaced", true);
    });
    updateStatus();

    if (!EncoderCaps::instance().probed()) {
        QTimer::singleShot(300, this, [this] {
            QMessageBox::warning(this, windowTitle(),
                                 QStringLiteral("ffmpeg could not be found or run (%1).\n\nInstall ffmpeg or set its "
                                                "location in Preferences > FFmpeg.")
                                     .arg(AppSettings::ffmpegPath()));
        });
    }
}

// ------------------------------------------------------------------ layout

void MainWindow::buildDocks()
{
    auto makeDock = [this](const QString &title, QWidget *w, const QString &name) {
        auto *d = new QDockWidget(title, this);
        d->setObjectName(name);
        d->setWidget(w);
        d->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable |
                       QDockWidget::DockWidgetClosable);
        m_docks << d;
        return d;
    };

    m_media = new MediaBrowser;
    connect(m_media, &MediaBrowser::addToQueueRequested, this, qOverload<const QStringList &>(&MainWindow::addFiles));
    connect(m_media, &MediaBrowser::addToQueueWithPresetRequested, this,
            qOverload<const QStringList &, const QString &>(&MainWindow::addFiles));
    connect(m_media, &MediaBrowser::stitchRequested, this, &MainWindow::stitchFiles);
    connect(m_media, &MediaBrowser::imageSequenceRequested, this, [this](const QString &f) { addImageSequence(f); });
    connect(m_media, &MediaBrowser::watchFolderRequested, this, [this](const QString &folder) {
        const Preset p = PresetManager::instance().defaultPreset();
        m_watch->addFolder(folder, WatchOutput{p.settings, p.id, {}});
        if (QDockWidget *d = findChild<QDockWidget *>(QStringLiteral("watch")))
            d->show();
        statusBar()->showMessage(QStringLiteral("Added watch folder %1").arg(QDir::toNativeSeparators(folder)), 5000);
    });
    m_presets = new PresetBrowser;
    connect(m_presets, &PresetBrowser::applyPresetRequested, this, &MainWindow::applyPreset);
    connect(m_presets, &PresetBrowser::newPresetRequested, this, &MainWindow::newPreset);
    connect(m_presets, &PresetBrowser::editPresetRequested, this, &MainWindow::editPreset);
    m_watchPanel = new WatchFolderPanel(m_watch);
    connect(m_watchPanel, &WatchFolderPanel::editOutputRequested, this, &MainWindow::editWatchOutput);
    m_encoding = new EncodingPanel(m_model);
    m_recorderPanel = new StreamRecorderPanel(m_recorder);
    connect(m_recorderPanel, &StreamRecorderPanel::addToQueueRequested, this,
            qOverload<const QStringList &>(&MainWindow::addFiles));

    auto *mediaDock = makeDock(QStringLiteral("Media Browser"), m_media, "media");
    auto *presetDock = makeDock(QStringLiteral("Preset Browser"), m_presets, "presets");
    auto *watchDock = makeDock(QStringLiteral("Watch Folders"), m_watchPanel, "watch");
    auto *encDock = makeDock(QStringLiteral("Encoding"), m_encoding, "encoding");
    auto *recDock = makeDock(QStringLiteral("Stream Recorder"), m_recorderPanel, "recorder");

    // Browsers run full height on the left; watch folders and encoding sit side by side under the queue.
    setCorner(Qt::BottomLeftCorner, Qt::LeftDockWidgetArea);
    setCorner(Qt::TopLeftCorner, Qt::LeftDockWidgetArea);
    addDockWidget(Qt::LeftDockWidgetArea, mediaDock);
    addDockWidget(Qt::LeftDockWidgetArea, presetDock);
    splitDockWidget(mediaDock, presetDock, Qt::Vertical);
    addDockWidget(Qt::BottomDockWidgetArea, watchDock);
    addDockWidget(Qt::BottomDockWidgetArea, encDock);
    splitDockWidget(watchDock, encDock, Qt::Horizontal);
    tabifyDockWidget(watchDock, recDock);
    watchDock->raise();
}

void MainWindow::applyDefaultDockSizes()
{
    auto dock = [this](const char *name) { return findChild<QDockWidget *>(QString::fromLatin1(name)); };
    resizeDocks({dock("media"), dock("presets")}, {height() * 9 / 20, height() * 11 / 20}, Qt::Vertical);
    resizeDocks({dock("media")}, {width() * 9 / 25}, Qt::Horizontal);
    resizeDocks({dock("encoding")}, {height() * 3 / 10}, Qt::Vertical);
    resizeDocks({dock("watch"), dock("encoding")}, {width() / 4, width() * 2 / 5}, Qt::Horizontal);
}

void MainWindow::buildMenus()
{
    QMenu *file = menuBar()->addMenu(QStringLiteral("&File"));
    file->addAction(Theme::icon("plus"), QStringLiteral("Add Source…"), QKeySequence(Qt::CTRL | Qt::Key_I), this,
                    &MainWindow::addSourceDialog);
    file->addAction(Theme::icon("folder"), QStringLiteral("Add Folder of Sources…"), this,
                    &MainWindow::addFolderDialog);
    file->addAction(Theme::icon("image"), QStringLiteral("Add Image Sequence…"),
                    QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_I), this, &MainWindow::imageSequenceDialog);
    file->addAction(Theme::icon("stitch"), QStringLiteral("Stitch Clips Together…"), this, &MainWindow::stitchDialog);
    file->addAction(Theme::icon("record", QColor(0xe5, 0x48, 0x48)), QStringLiteral("Record Stream…"),
                    QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_R), this, &MainWindow::showRecorder);
    file->addSeparator();
    file->addAction(Theme::icon("play"), QStringLiteral("Start Queue"), QKeySequence(Qt::CTRL | Qt::Key_Return), this,
                    &MainWindow::startQueue);
    file->addAction(Theme::icon("pause"), QStringLiteral("Pause Queue"), m_engine, &EncoderEngine::pause);
    file->addAction(Theme::icon("stop"), QStringLiteral("Stop Queue"), QKeySequence(Qt::CTRL | Qt::Key_Period), this,
                    &MainWindow::stopQueue);
    QMenu *after = file->addMenu(QStringLiteral("When Queue Finishes"));
    auto *afterGroup = new QActionGroup(after);
    const QList<QPair<QString, AfterQueue>> afterOptions = {{QStringLiteral("Do Nothing"), AfterQueue::Nothing},
                                                            {QStringLiteral("Sleep"), AfterQueue::Sleep},
                                                            {QStringLiteral("Shut Down"), AfterQueue::ShutDown}};
    for (const auto &opt : afterOptions) {
        QAction *a = after->addAction(opt.first);
        a->setCheckable(true);
        a->setChecked(opt.second == AfterQueue::Nothing);
        afterGroup->addAction(a);
        m_afterQueueActions << a;
        connect(a, &QAction::triggered, this, [this, v = opt.second] {
            m_afterQueue = v;
            updateQueueProgress();
        });
    }
    file->addSeparator();
    file->addAction(QStringLiteral("Open Queue…"), this, &MainWindow::openQueue);
    file->addAction(QStringLiteral("Save Queue As…"), this, &MainWindow::saveQueueAs);
    file->addAction(QStringLiteral("Clear Completed Items"), m_model, &QueueModel::clearDone);
    file->addSeparator();
    file->addAction(Theme::icon("settings"), QStringLiteral("Preferences…"), QKeySequence(Qt::CTRL | Qt::Key_Comma),
                    this, &MainWindow::showPreferences);
    file->addSeparator();
    file->addAction(QStringLiteral("Exit"), QKeySequence::Quit, this, &QWidget::close);

    QMenu *edit = menuBar()->addMenu(QStringLiteral("&Edit"));
    edit->addAction(Theme::icon("settings"), QStringLiteral("Export Settings…"), QKeySequence(Qt::CTRL | Qt::Key_E),
                    this, &MainWindow::editSelection);
    edit->addAction(Theme::icon("preset"), QStringLiteral("Add Output"), this, &MainWindow::addOutputToSelection);
    edit->addAction(Theme::icon("duplicate"), QStringLiteral("Duplicate"), QKeySequence(Qt::CTRL | Qt::Key_D), this,
                    &MainWindow::duplicateSelection);
    edit->addAction(Theme::icon("minus"), QStringLiteral("Remove"), this, &MainWindow::removeSelection);
    edit->addSeparator();
    edit->addAction(Theme::icon("reset"), QStringLiteral("Reset Status"), this, &MainWindow::resetSelection);
    edit->addAction(QStringLiteral("Skip Selection"), this, &MainWindow::skipSelection);
    edit->addSeparator();
    edit->addAction(Theme::icon("folder"), QStringLiteral("Reveal Output in Explorer"), this,
                    &MainWindow::revealSelection);
    edit->addAction(QStringLiteral("Select All"), QKeySequence::SelectAll, m_queue->view(), &QTreeView::selectAll);

    QMenu *preset = menuBar()->addMenu(QStringLiteral("&Preset"));
    preset->addAction(Theme::icon("plus"), QStringLiteral("New Preset…"), this, &MainWindow::newPreset);
    preset->addAction(Theme::icon("settings"), QStringLiteral("Preset Settings…"), this,
                      [this] { editPreset(m_presets->selectedPresetId()); });
    preset->addAction(Theme::icon("play"), QStringLiteral("Apply Preset to Selection"), this,
                      [this] { applyPreset(m_presets->selectedPresetId()); });

    QMenu *window = menuBar()->addMenu(QStringLiteral("&Window"));
    for (QDockWidget *d : std::as_const(m_docks))
        window->addAction(d->toggleViewAction());
    window->addSeparator();
    window->addAction(QStringLiteral("Reset Workspace"), this, [this] {
        restoreState(m_defaultState);
        for (QDockWidget *d : std::as_const(m_docks))
            d->show();
        applyDefaultDockSizes();
    });

    QMenu *help = menuBar()->addMenu(QStringLiteral("&Help"));
    help->addAction(Theme::icon("log"), QStringLiteral("Show Encoding Log"), this, [this] { showLog(false); });
    help->addAction(Theme::icon("log"), QStringLiteral("Show Error Log"), this, [this] { showLog(true); });
    help->addAction(Theme::icon("folder"), QStringLiteral("Open Logs Folder"), this,
                    [] { QDesktopServices::openUrl(QUrl::fromLocalFile(AppSettings::logDir())); });
    help->addSeparator();
    help->addAction(Theme::icon("info"), QStringLiteral("Encoder Capabilities…"), this, &MainWindow::showCapabilities);
    help->addAction(QStringLiteral("About Open Media Encoder"), this, &MainWindow::showAbout);
}

void MainWindow::wireEngine()
{
    connect(m_engine, &EncoderEngine::runningChanged, this, [this](bool running) {
        m_queue->setEngineState(m_engine->isRunning(), m_engine->isPaused());
        if (running) {
            m_userStopped = false;
            m_runFinished = 0;
            m_activeProgress.clear();
            m_runTimer.start();
            m_taskbar->setState(TaskbarProgress::State::Normal);
            m_taskbar->setProgress(0);
        } else {
            m_taskbar->setState(TaskbarProgress::State::None);
        }
        updatePreventSleep();
        updateStatus();
        updateQueueProgress();
    });
    connect(m_engine, &EncoderEngine::pausedChanged, this, [this](bool paused) {
        m_queue->setEngineState(m_engine->isRunning(), m_engine->isPaused());
        if (m_engine->isRunning())
            m_taskbar->setState(paused ? TaskbarProgress::State::Paused : TaskbarProgress::State::Normal);
        updateStatus();
        updateQueueProgress();
    });
    connect(m_engine, &EncoderEngine::taskStarted, this, [this](const QString &, const QString &oid) {
        m_activeProgress.insert(oid, 0.0);
        updateQueueProgress();
    });
    connect(m_engine, &EncoderEngine::taskProgress, this, [this](const QString &oid, const EncodeStats &st) {
        m_activeProgress.insert(oid, st.progress);
        updateQueueProgress();
    });
    connect(m_engine, &EncoderEngine::taskFinished, this, [this](const QString &, const QString &oid) {
        m_activeProgress.remove(oid);
        ++m_runFinished;
        updateQueueProgress();
    });
    connect(m_engine, &EncoderEngine::taskStarted, m_encoding, &EncodingPanel::onTaskStarted);
    connect(m_engine, &EncoderEngine::taskProgress, m_encoding, &EncodingPanel::onTaskProgress);
    connect(m_engine, &EncoderEngine::taskFinished, m_encoding, &EncodingPanel::onTaskFinished);
    connect(m_engine, &EncoderEngine::queueFinished, this, &MainWindow::onQueueFinished);

    connect(m_recorder, &StreamRecorder::activeCountChanged, this, [this](int active) {
        updatePreventSleep();
        updateStatus();
        if (active == 0 && m_afterQueueWaiting) {
            m_afterQueueWaiting = false;
            if (!m_engine->isRunning())
                runAfterQueueAction();
        }
    });
    connect(m_recorder, &StreamRecorder::recordingFinished, this, [this](StreamRecording *r) {
        const bool ok = r->state() == StreamRecording::State::Done;
        const QString name = r->files().isEmpty() ? r->options().url : QFileInfo(r->files().last()).fileName();
        statusBar()->showMessage(
            (ok ? QStringLiteral("Recording saved: ") : QStringLiteral("Recording failed: ")) + name, 10000);
        if (AppSettings::notifyOnFinish() && m_tray->isVisible())
            m_tray->showMessage(ok ? QStringLiteral("Recording saved") : QStringLiteral("Recording failed"),
                                ok ? QStringLiteral("%1 (%2)").arg(name, formatDuration(r->recordedSeconds()))
                                   : r->error(),
                                ok ? QSystemTrayIcon::Information : QSystemTrayIcon::Warning, 6000);
    });
    connect(m_engine, &EncoderEngine::sourceCompleted, this, [this](const QString &sid, bool allOk) {
        const SourceItem *s = m_model->source(sid);
        if (s && allOk && !s->watchFolderId.isEmpty())
            for (const QString &f : s->files)
                m_watch->archiveSource(s->watchFolderId, f);
    });
}

// ------------------------------------------------------------------ sources

void MainWindow::probeSource(const QString &sourceId)
{
    const SourceItem *s = m_model->source(sourceId);
    if (!s)
        return;
    const QStringList files = s->files;
    const QStringList inputArgs = s->inputArgs();
    const QString probe = AppSettings::ffprobePath();
    auto *watcher = new QFutureWatcher<ProbeResult>(this);
    connect(watcher, &QFutureWatcher<ProbeResult>::finished, this, [this, watcher, sourceId] {
        const ProbeResult r = watcher->result();
        m_model->setSourceInfo(sourceId, r.info, r.total);
        watcher->deleteLater();
    });
    watcher->setFuture(QtConcurrent::run([probe, files, inputArgs] {
        ProbeResult r;
        r.info = MediaInfo::probe(probe, files.first(), 20000, inputArgs);
        if (files.size() > 1) {
            r.total = r.info.duration;
            for (int i = 1; i < files.size(); ++i)
                r.total += MediaInfo::probe(probe, files[i]).duration;
        }
        return r;
    }));
}

void MainWindow::addFiles(const QStringList &paths) { addFiles(paths, QString()); }

void MainWindow::addFiles(const QStringList &paths, const QString &presetId)
{
    const QStringList files = expandMediaFiles(paths);
    if (files.isEmpty())
        return;
    // Several numbered frames of one sequence: offer to treat them as a single video source.
    ImageSequence seq;
    if (files.size() >= 2 && ImageSequence::filesFormSequence(files, &seq)) {
        const auto answer = QMessageBox::question(
            this, QStringLiteral("Image Sequence"),
            QStringLiteral("These %1 images look like a numbered image sequence (%2, %3 frames).\n\n"
                           "Add them as one video source?")
                .arg(files.size())
                .arg(seq.displayName())
                .arg(seq.count),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
        if (answer == QMessageBox::Yes) {
            addImageSequence(seq.firstFile);
            return;
        }
    }
    const Preset *chosen = PresetManager::instance().find(presetId);
    const Preset p = chosen ? *chosen : PresetManager::instance().defaultPreset();
    for (const QString &f : files) {
        const QString sid = m_model->addSource({f});
        const QString oid = m_model->addOutput(sid, p.settings, p.id);
        // The default output is a placeholder: dropping a preset on the source replaces it.
        m_model->setAutoAdded(oid, !chosen);
    }
    statusBar()->showMessage(QStringLiteral("Added %1 source%2 with preset \"%3\"")
                                 .arg(files.size())
                                 .arg(files.size() == 1 ? "" : "s")
                                 .arg(p.name()),
                             5000);
}

void MainWindow::addSourceDialog()
{
    QStringList exts;
    for (const QString &e : Formats::mediaFileExtensions())
        exts << "*." + e;
    const QStringList files = QFileDialog::getOpenFileNames(
        this, QStringLiteral("Add Source"), AppSettings::value("ui/lastAddDir").toString(),
        QStringLiteral("Media Files (%1);;All Files (*)").arg(exts.join(' ')));
    if (files.isEmpty())
        return;
    AppSettings::setValue("ui/lastAddDir", QFileInfo(files.first()).absolutePath());
    addFiles(files);
}

void MainWindow::addFolderDialog()
{
    const QString dir = QFileDialog::getExistingDirectory(this, QStringLiteral("Add Folder of Sources"),
                                                          AppSettings::value("ui/lastAddDir").toString());
    if (!dir.isEmpty())
        addFiles({dir});
}

void MainWindow::stitchDialog()
{
    QStringList exts;
    for (const QString &e : Formats::mediaFileExtensions())
        exts << "*." + e;
    QStringList files = QFileDialog::getOpenFileNames(this, QStringLiteral("Stitch Clips Together (in name order)"),
                                                      AppSettings::value("ui/lastAddDir").toString(),
                                                      QStringLiteral("Media Files (%1)").arg(exts.join(' ')));
    if (files.size() < 2) {
        if (files.size() == 1)
            addFiles(files);
        return;
    }
    stitchFiles(files);
}

void MainWindow::imageSequenceDialog()
{
    QStringList exts;
    for (const QString &e : Formats::mediaFileExtensions())
        if (Formats::mediaKind("x." + e) == MediaKind::Image)
            exts << "*." + e;
    const QString file = QFileDialog::getOpenFileName(this, QStringLiteral("Add Image Sequence — choose any frame"),
                                                      AppSettings::value("ui/lastAddDir").toString(),
                                                      QStringLiteral("Images (%1)").arg(exts.join(' ')));
    if (file.isEmpty())
        return;
    AppSettings::setValue("ui/lastAddDir", QFileInfo(file).absolutePath());
    addImageSequence(file);
}

bool MainWindow::addImageSequence(const QString &anyFrame, bool askFrameRate)
{
    ImageSequence seq = ImageSequence::detect(anyFrame);
    if (!seq.valid) {
        QMessageBox::information(this, QStringLiteral("Image Sequence"),
                                 QStringLiteral("No numbered image sequence was found for %1.\n\nFrames need "
                                                "consecutive numbers, e.g. shot_0001.png, shot_0002.png, …")
                                     .arg(QFileInfo(anyFrame).fileName()));
        return false;
    }
    seq.fps = AppSettings::value("sequence/fps", 24.0).toDouble();
    if (askFrameRate) {
        const QStringList rates = {"23.976", "24", "25", "29.97", "30", "48", "50", "59.94", "60"};
        QString current = QString::number(seq.fps, 'f', 3).remove(QRegularExpression("\\.?0+$"));
        QInputDialog dlg(this);
        dlg.setWindowTitle(QStringLiteral("Add Image Sequence"));
        dlg.setLabelText(QStringLiteral("%1\n%2 frames found.\n\nFrame rate:").arg(seq.displayName()).arg(seq.count));
        dlg.setComboBoxItems(rates);
        dlg.setComboBoxEditable(true);
        dlg.setTextValue(current);
        dlg.setMinimumWidth(420);
        if (dlg.exec() != QDialog::Accepted)
            return false;
        const QString chosen = dlg.textValue();
        const double fps = chosen.toDouble();
        if (fps <= 0 || fps > 1000) {
            QMessageBox::warning(this, QStringLiteral("Image Sequence"), QStringLiteral("Invalid frame rate."));
            return false;
        }
        seq.fps = fps;
        AppSettings::setValue("sequence/fps", fps);
    }
    const Preset p = PresetManager::instance().defaultPreset();
    const QString sid = m_model->addImageSequence(seq);
    const QString oid = m_model->addOutput(sid, p.settings, p.id);
    m_model->setAutoAdded(oid, true);
    statusBar()->showMessage(QStringLiteral("Added image sequence %1 (%2 frames at %3 fps)")
                                 .arg(seq.displayName())
                                 .arg(seq.count)
                                 .arg(seq.fps),
                             6000);
    return true;
}

void MainWindow::stitchFiles(QStringList files)
{
    if (files.size() < 2) {
        addFiles(files);
        return;
    }
    std::sort(files.begin(), files.end(), [](const QString &a, const QString &b) {
        return QString::compare(QFileInfo(a).fileName(), QFileInfo(b).fileName(), Qt::CaseInsensitive) < 0;
    });
    const Preset p = PresetManager::instance().defaultPreset();
    const QString sid = m_model->addSource(files);
    m_model->addOutput(sid, p.settings, p.id);
    statusBar()->showMessage(QStringLiteral("Stitching %1 clips into one output").arg(files.size()), 5000);
}

QStringList selectedSourceIds(QueueModel *model, const QModelIndexList &rows)
{
    QStringList ids;
    for (const QModelIndex &i : rows)
        ids << model->sourceIdFor(i);
    ids.removeDuplicates();
    return ids;
}

void MainWindow::addOutputToSelection()
{
    const QModelIndexList rows = m_queue->selectedRows();
    if (rows.isEmpty()) {
        addSourceDialog();
        return;
    }
    const Preset p = PresetManager::instance().defaultPreset();
    for (const QString &sid : selectedSourceIds(m_model, rows))
        m_model->addOutput(sid, p.settings, p.id);
}

void MainWindow::duplicateSelection()
{
    const QModelIndexList rows = m_queue->selectedRows();
    for (const QModelIndex &i : rows) {
        if (i.data(QueueModel::IsSourceRole).toBool()) {
            const SourceItem *s = m_model->source(i.data(QueueModel::IdRole).toString());
            if (!s)
                continue;
            const SourceItem copy = *s;
            const QString sid = m_model->addSource(copy.files, copy.watchFolderId);
            for (const OutputItem &o : copy.outputs) {
                const QString oid =
                    m_model->addOutput(sid, o.settings, o.presetId, QFileInfo(o.outputPath).absolutePath());
                m_model->setOutputRange(oid, o.inPoint, o.outPoint);
            }
        } else {
            m_model->duplicateOutput(i.data(QueueModel::IdRole).toString());
        }
    }
}

void MainWindow::removeSelection()
{
    const QModelIndexList rows = m_queue->selectedRows();
    for (const QString &oid : m_model->outputIdsFor(rows)) {
        const OutputItem *o = m_model->output(oid);
        if (o && (o->status == OutputStatus::Encoding || o->status == OutputStatus::Paused)) {
            if (QMessageBox::question(this, QStringLiteral("Remove"),
                                      QStringLiteral("Some selected items are encoding. Stop and remove them?")) !=
                QMessageBox::Yes)
                return;
            break;
        }
    }
    QStringList encoding;
    for (const QString &oid : m_model->outputIdsFor(rows)) {
        const OutputItem *o = m_model->output(oid);
        if (o && (o->status == OutputStatus::Encoding || o->status == OutputStatus::Paused))
            encoding << oid;
    }
    if (!encoding.isEmpty()) {
        // Removal happens once the stopped encodes report back.
        QStringList pendingRemoval = m_model->outputIdsFor(rows);
        auto *conn = new QMetaObject::Connection;
        *conn = connect(m_engine, &EncoderEngine::taskFinished, this,
                        [this, encoding, pendingRemoval, conn](const QString &, const QString &oid) {
                            for (const QString &id : encoding) {
                                const OutputItem *o = m_model->output(id);
                                if (o && (o->status == OutputStatus::Encoding || o->status == OutputStatus::Paused))
                                    return;
                            }
                            Q_UNUSED(oid);
                            disconnect(*conn);
                            delete conn;
                            QModelIndexList idx;
                            for (const QString &id : pendingRemoval)
                                idx << m_model->indexForOutput(id);
                            m_model->removeIndexes(idx);
                        });
        for (const QString &oid : encoding)
            m_engine->stopOutput(oid);
        return;
    }
    m_model->removeIndexes(rows);
}

void MainWindow::resetSelection() { m_model->resetStatus(m_model->outputIdsFor(m_queue->selectedRows())); }
void MainWindow::skipSelection() { m_model->skip(m_model->outputIdsFor(m_queue->selectedRows())); }

void MainWindow::revealSelection()
{
    const QStringList ids = m_model->outputIdsFor(m_queue->selectedRows());
    if (ids.isEmpty())
        return;
    const OutputItem *o = m_model->output(ids.first());
    QString path = o->finalPath.isEmpty() ? o->outputPath : o->finalPath;
    if (path.contains(QStringLiteral("%05d")))
        path = CommandBuilder::firstSequenceFile(path);
    SystemIntegration::revealInFileManager(path);
}

void MainWindow::editSelection()
{
    const QStringList ids = m_model->outputIdsFor(m_queue->selectedRows());
    if (!ids.isEmpty())
        editOutput(ids.first());
}

// ------------------------------------------------------------------ outputs

void MainWindow::editOutput(const QString &outputId)
{
    SourceItem *src = nullptr;
    OutputItem *o = m_model->output(outputId, &src);
    if (!o)
        return;
    ExportSettingsDialog dlg(ExportSettingsDialog::Mode::Output, o->settings, o->presetId, this);
    dlg.setInputArgs(src->inputArgs());
    dlg.setSource(src->files, src->info, src->totalDuration);
    dlg.setOutputPath(o->outputPath);
    dlg.setRange(o->inPoint, o->outPoint);
    if (dlg.exec() != QDialog::Accepted)
        return;
    // The item may have been removed while the dialog was open.
    if (!m_model->output(outputId))
        return;
    m_model->setOutputSettings(outputId, dlg.settings(), dlg.presetId());
    m_model->setOutputPath(outputId, dlg.outputPath());
    m_model->setOutputRange(outputId, dlg.inPoint(), dlg.outPoint());
}

void MainWindow::chooseOutputPath(const QString &outputId)
{
    OutputItem *o = m_model->output(outputId);
    if (!o)
        return;
    const QString ext = o->settings.extension();
    const bool seq = o->settings.isImageSequence();
    QString current = o->outputPath;
    if (seq)
        current.remove(QStringLiteral("_%05d"));
    QString path =
        QFileDialog::getSaveFileName(this, QStringLiteral("Output File"), current, QStringLiteral("*.%1").arg(ext));
    if (path.isEmpty())
        return;
    QFileInfo fi(path);
    const QString base = fi.suffix().compare(ext, Qt::CaseInsensitive) == 0 ? fi.completeBaseName() : fi.fileName();
    m_model->setOutputPath(outputId, fi.dir().filePath(seq ? QStringLiteral("%1_%05d.%2").arg(base, ext)
                                                           : QStringLiteral("%1.%2").arg(base, ext)));
}

void MainWindow::applyPreset(const QString &presetId)
{
    const Preset *p = PresetManager::instance().find(presetId);
    if (!p)
        return;
    const QModelIndexList rows = m_queue->selectedRows();
    if (rows.isEmpty()) {
        statusBar()->showMessage(QStringLiteral("Select items in the queue, or drag the preset onto a source."), 5000);
        return;
    }
    for (const QModelIndex &i : rows) {
        if (i.data(QueueModel::IsSourceRole).toBool())
            m_model->applyPresetToSource(i.data(QueueModel::IdRole).toString(), p->id);
        else
            m_model->applyPreset(i.data(QueueModel::IdRole).toString(), p->id);
    }
}

void MainWindow::newPreset()
{
    const Preset base = PresetManager::instance().defaultPreset();
    ExportSettingsDialog dlg(ExportSettingsDialog::Mode::Preset, base.settings, base.id, this);
    dlg.setWindowTitle(QStringLiteral("New Preset"));
    if (dlg.exec() != QDialog::Accepted)
        return;
    if (!dlg.presetId().isEmpty() && PresetManager::instance().find(dlg.presetId()))
        return; // saved from inside the dialog or unchanged
    ExportSettings s = dlg.settings();
    bool ok = false;
    const QString name = QInputDialog::getText(this, QStringLiteral("New Preset"), QStringLiteral("Preset name:"),
                                               QLineEdit::Normal, QString(), &ok)
                             .trimmed();
    if (!ok || name.isEmpty())
        return;
    s.presetName = name;
    PresetManager::instance().saveUserPreset(s);
}

void MainWindow::editPreset(const QString &presetId)
{
    const Preset *pp = PresetManager::instance().find(presetId);
    if (!pp)
        return;
    const Preset p = *pp;
    ExportSettingsDialog dlg(ExportSettingsDialog::Mode::Preset, p.settings, p.id, this);
    dlg.setWindowTitle(QStringLiteral("Preset Settings — %1").arg(p.name()));
    if (dlg.exec() != QDialog::Accepted || !dlg.presetId().isEmpty())
        return; // unchanged, or saved under a name from inside the dialog
    ExportSettings s = dlg.settings();
    if (p.system) {
        bool ok = false;
        const QString name =
            QInputDialog::getText(this, QStringLiteral("Save as User Preset"),
                                  QStringLiteral("System presets are read-only. Save your changes as:"),
                                  QLineEdit::Normal, p.name() + QStringLiteral(" (Custom)"), &ok)
                .trimmed();
        if (!ok || name.isEmpty())
            return;
        s.presetName = name;
    } else {
        s.presetName = p.name();
        if (s.format != p.settings.format)
            PresetManager::instance().removeUserPreset(p.id);
    }
    PresetManager::instance().saveUserPreset(s);
}

void MainWindow::editWatchOutput(const QString &folderId, int index)
{
    const WatchFolder *f = m_watch->folder(folderId);
    if (!f || index < 0 || index >= f->outputs.size())
        return;
    WatchOutput o = f->outputs[index];
    ExportSettingsDialog dlg(ExportSettingsDialog::Mode::Preset, o.settings, o.presetId, this);
    dlg.setWindowTitle(QStringLiteral("Watch Folder Output Settings"));
    if (dlg.exec() != QDialog::Accepted)
        return;
    o.settings = dlg.settings();
    o.presetId = dlg.presetId();
    m_watch->setOutput(folderId, index, o);
}

// ------------------------------------------------------------------ menus & dialogs

void MainWindow::showQueueContextMenu(const QPoint &globalPos)
{
    const QModelIndexList rows = m_queue->selectedRows();
    QMenu menu(this);
    if (!rows.isEmpty()) {
        menu.addAction(Theme::icon("settings"), QStringLiteral("Export Settings…"), this, &MainWindow::editSelection);
        menu.addAction(Theme::icon("preset"), QStringLiteral("Add Output"), this, &MainWindow::addOutputToSelection);
        menu.addAction(Theme::icon("duplicate"), QStringLiteral("Duplicate"), this, &MainWindow::duplicateSelection);
        menu.addAction(Theme::icon("minus"), QStringLiteral("Remove"), this, &MainWindow::removeSelection);
        menu.addSeparator();
        menu.addAction(Theme::icon("reset"), QStringLiteral("Reset Status"), this, &MainWindow::resetSelection);
        menu.addAction(QStringLiteral("Skip Selection"), this, &MainWindow::skipSelection);
        QStringList encoding;
        for (const QString &oid : m_model->outputIdsFor(rows)) {
            const OutputItem *o = m_model->output(oid);
            if (o && (o->status == OutputStatus::Encoding || o->status == OutputStatus::Paused))
                encoding << oid;
        }
        if (!encoding.isEmpty())
            menu.addAction(Theme::icon("stop"), QStringLiteral("Stop Encoding Selection"), this, [this, encoding] {
                for (const QString &id : encoding)
                    m_engine->stopOutput(id);
            });
        menu.addSeparator();
        QMenu *move = menu.addMenu(QStringLiteral("Move Source (Priority)"));
        move->addAction(QStringLiteral("To Top"), this, [this] { moveSelectedSource(-1000000); });
        move->addAction(QStringLiteral("Up\tCtrl+Up"), this, [this] { moveSelectedSource(-1); });
        move->addAction(QStringLiteral("Down\tCtrl+Down"), this, [this] { moveSelectedSource(1); });
        move->addAction(QStringLiteral("To Bottom"), this, [this] { moveSelectedSource(1000000); });
        menu.addSeparator();
        {
            const QStringList outs = m_model->outputIdsFor(rows);
            const OutputItem *first = outs.isEmpty() ? nullptr : m_model->output(outs.first());
            if (first && first->status == OutputStatus::Done)
                menu.addAction(Theme::icon("play"), QStringLiteral("Play Output"), this,
                               &MainWindow::playSelectedOutput);
        }
        menu.addAction(Theme::icon("folder"), QStringLiteral("Reveal Output in Explorer"), this,
                       &MainWindow::revealSelection);
        const QString sid = m_model->sourceIdFor(rows.first());
        menu.addAction(Theme::icon("info"), QStringLiteral("Source Media Info…"), this,
                       [this, sid] { showMediaInfo(sid); });
        const QStringList ids = m_model->outputIdsFor(rows);
        if (!ids.isEmpty()) {
            const OutputItem *o = m_model->output(ids.first());
            if (o && o->status == OutputStatus::Failed)
                menu.addAction(QStringLiteral("Show Error Details…"), this,
                               [this, msg = o->message] { showTextDialog(this, QStringLiteral("Encode Error"), msg); });
        }
        menu.addSeparator();
    }
    menu.addAction(Theme::icon("plus"), QStringLiteral("Add Source…"), this, &MainWindow::addSourceDialog);
    menu.addAction(Theme::icon("stitch"), QStringLiteral("Stitch Clips Together…"), this, &MainWindow::stitchDialog);
    menu.addAction(QStringLiteral("Clear Completed Items"), m_model, &QueueModel::clearDone);
    menu.exec(globalPos);
}

void MainWindow::showMediaInfo(const QString &sourceId)
{
    const SourceItem *s = m_model->source(sourceId);
    if (!s)
        return;
    QString text = s->info.describe();
    if (s->files.size() > 1)
        text = QStringLiteral("Stitched clips (%1, total %2):\n  %3\n\nFirst clip:\n%4")
                   .arg(s->files.size())
                   .arg(formatDuration(s->totalDuration))
                   .arg(s->files.join("\n  "), text);
    showTextDialog(this, QStringLiteral("Media Info — %1").arg(s->displayName()), text);
}

void MainWindow::showLog(bool errors)
{
    const QString path = errors ? EncodingLog::errorLogPath() : EncodingLog::logPath();
    QFile f(path);
    QString text;
    if (f.open(QIODevice::ReadOnly)) {
        if (f.size() > 2 * 1024 * 1024)
            f.seek(f.size() - 2 * 1024 * 1024);
        text = QString::fromUtf8(f.readAll());
    }
    if (text.isEmpty())
        text = errors ? QStringLiteral("No errors logged.") : QStringLiteral("The encoding log is empty.");
    showTextDialog(this, errors ? QStringLiteral("Error Log") : QStringLiteral("Encoding Log"), text, path);
}

void MainWindow::showPreferences()
{
    PreferencesDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    if (dlg.ffmpegChanged()) {
        EncoderCaps::instance().probe(AppSettings::ffmpegPath());
        PresetManager::instance().load();
        QThreadPool::globalInstance()->start(
            [] { EncoderCaps::instance().validateHardware(AppSettings::ffmpegPath()); });
    }
    restartIdleTimer();
}

void MainWindow::showCapabilities()
{
    QStringList lines;
    const EncoderCaps &caps = EncoderCaps::instance();
    lines << caps.version() << QStringLiteral("ffmpeg:  %1").arg(AppSettings::ffmpegPath())
          << QStringLiteral("ffprobe: %1").arg(AppSettings::ffprobePath()) << QString();
    lines << (caps.hardwareValidated() ? QStringLiteral("Hardware encoders tested on this machine.")
                                       : QStringLiteral("Hardware encoder test still running…"));
    lines << QString();
    for (const FormatDef &f : Formats::all()) {
        lines << QStringLiteral("%1 (.%2)").arg(f.displayName, f.extension);
        for (const QString &c : f.videoCodecs) {
            const VideoCodecDef *d = Formats::videoCodec(c);
            lines << QStringLiteral("    video  %1 %2").arg(caps.hasEncoder(c) ? "✓" : "✗", d ? d->displayName : c);
        }
        for (const QString &c : f.audioCodecs) {
            const AudioCodecDef *d = Formats::audioCodec(c);
            lines << QStringLiteral("    audio  %1 %2").arg(caps.hasEncoder(c) ? "✓" : "✗", d ? d->displayName : c);
        }
    }
    lines << QString() << QStringLiteral("Effects filters:");
    for (const QString &flt : {"lut3d", "drawtext", "subtitles", "loudnorm", "limiter", "minterpolate", "overlay"})
        lines << QStringLiteral("    %1 %2").arg(caps.hasFilter(flt) ? "✓" : "✗", flt);
    showTextDialog(this, QStringLiteral("Encoder Capabilities"), lines.join('\n'));
}

void MainWindow::showAbout()
{
    QMessageBox::about(this, QStringLiteral("About Open Media Encoder"),
                       QStringLiteral("<h3>Open Media Encoder %2</h3>"
                                      "<p>A batch media encoder built with Qt and FFmpeg: encode queue with multiple "
                                      "outputs per source, presets, watch folders, effects (LUTs, overlays, timecode, "
                                      "loudness normalization, captions), hardware encoding and stitching.</p>"
                                      "<p>Free and open source under the MIT License.<br>"
                                      "<a "
                                      "href=\"https://github.com/glenwrhodes/open-media-encoder\">github.com/"
                                      "glenwrhodes/open-media-encoder</a></p>"
                                      "<p>%1</p>")
                           .arg(EncoderCaps::instance().version().toHtmlEscaped(), QStringLiteral(OME_VERSION)));
}

void MainWindow::saveQueueAs()
{
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Save Queue"), QStringLiteral("queue.json"),
                                                      QStringLiteral("Queue (*.json)"));
    if (!path.isEmpty() && !m_model->save(path))
        QMessageBox::warning(this, QStringLiteral("Save Queue"), QStringLiteral("Could not write %1").arg(path));
}

void MainWindow::openQueue()
{
    if (m_engine->isRunning()) {
        QMessageBox::information(this, QStringLiteral("Open Queue"),
                                 QStringLiteral("Stop the queue before opening another one."));
        return;
    }
    const QString path =
        QFileDialog::getOpenFileName(this, QStringLiteral("Open Queue"), QString(), QStringLiteral("Queue (*.json)"));
    if (!path.isEmpty() && !m_model->load(path))
        QMessageBox::warning(this, QStringLiteral("Open Queue"), QStringLiteral("Not a valid queue file."));
}

// ------------------------------------------------------------------ events

void MainWindow::onWatchFilesReady(const QString &folderId, const QStringList &files)
{
    const WatchFolder *f = m_watch->folder(folderId);
    if (!f)
        return;
    for (const QString &file : files) {
        const QString sid = m_model->addSource({file}, folderId);
        for (const WatchOutput &o : f->outputs)
            m_model->addOutput(sid, o.settings, o.presetId, f->outputDirFor(o));
        m_watch->markHandled(file);
    }
    statusBar()->showMessage(
        QStringLiteral("Watch folder: queued %1 file%2").arg(files.size()).arg(files.size() == 1 ? "" : "s"), 5000);
    if (AppSettings::autoEncodeWatchFolders())
        m_engine->start();
}

void MainWindow::startQueue()
{
    // Outputs with automatic captions need the speech model: offer it now rather than fail them.
    bool needsSpeech = false;
    for (const SourceItem &s : m_model->sources())
        for (const OutputItem &o : s.outputs)
            if (o.status == OutputStatus::Ready && o.settings.effects.autoCaptions)
                needsSpeech = true;
    if (needsSpeech && !m_engine->isRunning())
        SpeechModelPrompt::ensureInstalled(this);
    m_engine->start();
}

void MainWindow::onQueueFinished(int done, int failed)
{
    setWindowTitle(QStringLiteral("Open Media Encoder"));
    updateStatus();
    if (done == 0 && failed == 0)
        return;
    const QString msg = failed ? QStringLiteral("%1 encoded, %2 failed").arg(done).arg(failed)
                               : QStringLiteral("%1 item%2 encoded").arg(done).arg(done == 1 ? "" : "s");
    statusBar()->showMessage(QStringLiteral("Queue finished: %1").arg(msg), 10000);
    if (AppSettings::notifyOnFinish()) {
        QApplication::alert(this);
        if (m_tray->isVisible())
            m_tray->showMessage(QStringLiteral("Queue finished"), msg,
                                failed ? QSystemTrayIcon::Warning : QSystemTrayIcon::Information, 6000);
    }
    if (!m_userStopped && m_afterQueue != AfterQueue::Nothing)
        QTimer::singleShot(0, this, &MainWindow::runAfterQueueAction);
}

void MainWindow::runAfterQueueAction()
{
    if (m_recorder->activeCount() > 0) {
        // Don't cut a recording short; carry out the action once the recordings end.
        m_afterQueueWaiting = true;
        statusBar()->showMessage(
            QStringLiteral("Queue finished. Waiting for stream recordings to end before %1.")
                .arg(m_afterQueue == AfterQueue::Sleep ? QStringLiteral("sleeping") : QStringLiteral("shutting down")));
        return;
    }
    const AfterQueue action = m_afterQueue;
    // One-shot: the next queue run won't sleep or shut down unless chosen again.
    m_afterQueue = AfterQueue::Nothing;
    if (!m_afterQueueActions.isEmpty())
        m_afterQueueActions.first()->setChecked(true);
    updateQueueProgress();

    const QString verb = action == AfterQueue::Sleep ? QStringLiteral("go to sleep") : QStringLiteral("shut down");
    QMessageBox box(QMessageBox::Information, windowTitle(), QString(), QMessageBox::Cancel, this);
    QPushButton *now =
        box.addButton(action == AfterQueue::Sleep ? QStringLiteral("Sleep Now") : QStringLiteral("Shut Down Now"),
                      QMessageBox::AcceptRole);
    int remaining = 60;
    bool timedOut = false;
    auto refresh = [&] {
        box.setText(
            QStringLiteral("The queue has finished.\n\nThe computer will %1 in %2 seconds.").arg(verb).arg(remaining));
    };
    refresh();
    QTimer tick;
    connect(&tick, &QTimer::timeout, &box, [&] {
        if (--remaining <= 0) {
            timedOut = true;
            box.close();
        } else {
            refresh();
        }
    });
    tick.start(1000);
    box.exec();
    tick.stop();
    if (!timedOut && box.clickedButton() != now)
        return; // cancelled
    m_model->save(queueFile());
    if (action == AfterQueue::Sleep)
        SystemIntegration::sleepComputer();
    else
        SystemIntegration::shutDownComputer();
}

void MainWindow::stopQueue()
{
    m_userStopped = true;
    m_engine->stop();
}

void MainWindow::moveSelectedSource(int delta)
{
    const QModelIndexList rows = m_queue->selectedRows();
    if (rows.isEmpty())
        return;
    const QString sid = m_model->sourceIdFor(rows.first());
    const QModelIndex idx = m_model->indexForSource(sid);
    if (!idx.isValid())
        return;
    const qint64 target = qint64(idx.row()) + delta;
    m_model->moveSource(sid, int(qBound<qint64>(0, target, qint64(m_model->rowCount()) - 1)));
    const QModelIndex moved = m_model->indexForSource(sid);
    m_queue->view()->selectionModel()->setCurrentIndex(moved,
                                                       QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    m_queue->view()->scrollTo(moved);
}

void MainWindow::playSelectedOutput()
{
    const QStringList ids = m_model->outputIdsFor(m_queue->selectedRows());
    if (ids.isEmpty())
        return;
    const OutputItem *o = m_model->output(ids.first());
    if (!o)
        return;
    const QString path = o->finalPath.isEmpty() ? o->outputPath : o->finalPath;
    if (path.contains(QStringLiteral("%05d")))
        revealSelection();
    else if (QFileInfo::exists(path))
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

void MainWindow::updateQueueProgress()
{
    QString after;
    if (m_afterQueue == AfterQueue::Sleep)
        after = QStringLiteral("then sleep");
    else if (m_afterQueue == AfterQueue::ShutDown)
        after = QStringLiteral("then shut down");

    if (!m_engine->isRunning()) {
        m_progressLabel->setText(after.isEmpty() ? QString()
                                                 : QStringLiteral("When the queue finishes: ") + after.mid(5));
        setWindowTitle(QStringLiteral("Open Media Encoder"));
        return;
    }
    double activeSum = 0.0;
    for (double v : std::as_const(m_activeProgress))
        activeSum += v;
    const int ready = m_model->countWithStatus(OutputStatus::Ready);
    const int total = m_runFinished + int(m_activeProgress.size()) + ready;
    const double fraction = total > 0 ? (m_runFinished + activeSum) / total : 0.0;
    QString text = QStringLiteral("%1 of %2 done  ·  %3%").arg(m_runFinished).arg(total).arg(int(fraction * 100));
    const double elapsed = m_runTimer.elapsed() / 1000.0;
    if (fraction > 0.02 && elapsed > 3)
        text += QStringLiteral("  ·  about %1 left").arg(formatDuration(elapsed * (1.0 - fraction) / fraction));
    if (!after.isEmpty())
        text += QStringLiteral("  ·  ") + after;
    m_progressLabel->setText(text);
    m_taskbar->setProgress(fraction);
    setWindowTitle(QStringLiteral("%1% — Open Media Encoder").arg(int(fraction * 100)));
}

void MainWindow::scheduleSave() { m_saveTimer->start(); }

void MainWindow::restartIdleTimer()
{
    const int minutes = AppSettings::autoStartIdleMinutes();
    if (minutes <= 0 || m_engine->isRunning()) {
        m_idleTimer->stop();
        return;
    }
    m_idleTimer->start(minutes * 60 * 1000);
}

void MainWindow::updateStatus()
{
    QString state;
    if (m_engine->isRunning())
        state = m_engine->isPaused() ? QStringLiteral("Paused") : QStringLiteral("Encoding");
    else
        state = QStringLiteral("Idle");
    const int ready = m_model->countWithStatus(OutputStatus::Ready);
    QString text = QStringLiteral("%1  ·  %2 item%3 ready").arg(state).arg(ready).arg(ready == 1 ? "" : "s");
    if (const int rec = m_recorder->activeCount())
        text += QStringLiteral("  ·  Recording %1 stream%2").arg(rec).arg(rec == 1 ? "" : "s");
    statusBar()->showMessage(text);
}

void MainWindow::updatePreventSleep()
{
    SystemIntegration::setPreventSleep((m_engine->isRunning() || m_recorder->activeCount() > 0) &&
                                       AppSettings::preventSleepWhileEncoding());
}

void MainWindow::showRecorder()
{
    if (QDockWidget *d = findChild<QDockWidget *>(QStringLiteral("recorder"))) {
        d->show();
        d->raise();
    }
    m_recorderPanel->focusUrl();
}

MainWindow::~MainWindow()
{
    SystemIntegration::setPreventSleep(false);
    delete m_taskbar;
}

void MainWindow::closeEvent(QCloseEvent *e)
{
    const int recording = m_recorder->activeCount();
    if (m_engine->isRunning() || recording > 0) {
        QString question;
        if (m_engine->isRunning() && recording > 0)
            question = QStringLiteral("Encoding and stream recording are in progress. Stop them and quit?\n\n"
                                      "What has been recorded so far is kept.");
        else if (recording > 0)
            question = QStringLiteral("%1 stream recording%2 in progress. Stop recording and quit?\n\n"
                                      "What has been recorded so far is kept.")
                           .arg(recording)
                           .arg(recording == 1 ? QStringLiteral(" is") : QStringLiteral("s are"));
        else
            question = QStringLiteral("Encoding is in progress. Stop encoding and quit?");
        if (QMessageBox::question(this, windowTitle(), question) != QMessageBox::Yes) {
            e->ignore();
            return;
        }
        m_engine->stop();
        if (recording > 0) {
            statusBar()->showMessage(QStringLiteral("Finishing recordings…"));
            m_recorder->stopAll();
            m_recorder->waitForAll(20000);
        }
    }
    m_model->save(queueFile());
    m_watch->save();
    AppSettings::setValue("ui/geometry", saveGeometry());
    AppSettings::setValue("ui/state", saveState());
    e->accept();
}
