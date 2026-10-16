#include "StreamRecorderPanel.h"

#include "Theme.h"
#include "core/AppSettings.h"
#include "core/MediaInfo.h"
#include "core/StreamRecorder.h"
#include "core/YtDlp.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QToolButton>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace {

constexpr int IdRole = Qt::UserRole + 1;
const QColor RecordRed(0xe5, 0x48, 0x48);

const char *Hint = "Paste a YouTube or Twitch page, or a stream link (.m3u8, RTMP, RTSP, SRT…), and press Record.";

QString stopAfterText(int seconds)
{
    if (seconds <= 0)
        return QStringLiteral("Until stopped");
    if (seconds % 3600 == 0)
        return QStringLiteral("Stop after %1 hour%2").arg(seconds / 3600).arg(seconds == 3600 ? "" : "s");
    if (seconds >= 3600)
        return QStringLiteral("Stop after %1 h %2 min").arg(seconds / 3600).arg(seconds % 3600 / 60);
    return QStringLiteral("Stop after %1 min").arg(seconds / 60);
}

} // namespace

StreamRecorderPanel::StreamRecorderPanel(StreamRecorder *recorder, QWidget *parent)
    : QWidget(parent)
    , m_recorder(recorder)
{
    m_url = new QLineEdit;
    m_url->setPlaceholderText(QStringLiteral("Paste a link to a live stream"));
    m_url->setClearButtonEnabled(true);
    connect(m_url, &QLineEdit::returnPressed, this, &StreamRecorderPanel::startRecording);
    m_record = new QToolButton;
    m_record->setText(QStringLiteral("Record"));
    m_record->setIcon(Theme::icon("record", RecordRed));
    m_record->setIconSize(QSize(14, 14));
    m_record->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_record->setToolTip(QStringLiteral("Start recording the link (Enter)"));
    connect(m_record, &QToolButton::clicked, this, &StreamRecorderPanel::startRecording);
    auto *row1 = new QHBoxLayout;
    row1->addWidget(m_url, 1);
    row1->addWidget(m_record);

    m_message = new QLabel(QString::fromUtf8(Hint));
    m_message->setObjectName("muted");
    m_message->setWordWrap(true);

    m_format = new QComboBox;
    m_format->addItem(QStringLiteral("MP4"), "mp4");
    m_format->setItemData(0, QStringLiteral("Recorded safely as MKV, then turned into an MP4 when the recording ends"),
                          Qt::ToolTipRole);
    m_format->addItem(QStringLiteral("MKV"), "mkv");
    m_format->setItemData(1, QStringLiteral("Matroska: plays even if the recording is cut off by a crash"),
                          Qt::ToolTipRole);
    m_format->addItem(QStringLiteral("MPEG-TS"), "ts");
    m_format->setItemData(2, QStringLiteral("Transport stream, as broadcast"), Qt::ToolTipRole);
    m_format->setToolTip(QStringLiteral("File format. Recording copies the stream as is, with no quality loss."));
    m_format->setCurrentIndex(qMax(0, m_format->findData(AppSettings::value("recorder/container", "mp4"))));
    connect(m_format, &QComboBox::currentIndexChanged, this,
            [this] { AppSettings::setValue("recorder/container", m_format->currentData()); });

    m_stopAfter = new QComboBox;
    for (int s : {0, 900, 1800, 3600, 5400, 7200, 10800, 14400, 21600, 28800, 43200})
        m_stopAfter->addItem(stopAfterText(s), s);
    m_stopAfter->addItem(QStringLiteral("Custom…"), -1);
    m_stopAfter->setToolTip(QStringLiteral("End new recordings automatically after this long"));
    const int saved = AppSettings::value("recorder/stopAfter", 0).toInt();
    if (m_stopAfter->findData(saved) < 0)
        m_stopAfter->insertItem(m_stopAfter->count() - 1, stopAfterText(saved), saved);
    m_stopAfter->setCurrentIndex(m_stopAfter->findData(saved));
    connect(m_stopAfter, &QComboBox::activated, this, [this] {
        if (m_stopAfter->currentData().toInt() < 0)
            chooseStopAfter();
        AppSettings::setValue("recorder/stopAfter", stopAfterSeconds());
    });

    m_folder = new QToolButton;
    m_folder->setIcon(Theme::icon("folder"));
    m_folder->setIconSize(QSize(16, 16));
    m_folder->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_folder->setPopupMode(QToolButton::InstantPopup);
    m_folder->setAutoRaise(true);
    auto *folderMenu = new QMenu(m_folder);
    folderMenu->addAction(QStringLiteral("Open Folder"), this, [] {
        QDir().mkpath(AppSettings::recordingDir());
        QDesktopServices::openUrl(QUrl::fromLocalFile(AppSettings::recordingDir()));
    });
    folderMenu->addAction(QStringLiteral("Change Folder…"), this, [this] {
        const QString dir =
            QFileDialog::getExistingDirectory(this, QStringLiteral("Save Recordings To"), AppSettings::recordingDir());
        if (!dir.isEmpty()) {
            AppSettings::setRecordingDir(dir);
            updateFolderButton();
        }
    });
    m_folder->setMenu(folderMenu);
    updateFolderButton();

    auto mk = [](const QString &icon, const QString &tip) {
        auto *b = new QToolButton;
        b->setIcon(Theme::icon(icon));
        b->setIconSize(QSize(16, 16));
        b->setToolTip(tip);
        b->setAutoRaise(true);
        return b;
    };
    m_stop = mk("stop", QStringLiteral("Stop the selected recording (what's recorded is kept)"));
    m_remove = mk("trash", QStringLiteral("Remove from the list (the files are kept)"));
    connect(m_stop, &QToolButton::clicked, this, [this] {
        for (StreamRecording *r : selectedRecordings())
            r->stop();
    });
    connect(m_remove, &QToolButton::clicked, this, [this] {
        for (StreamRecording *r : selectedRecordings())
            m_recorder->remove(r->id());
    });
    auto *row2 = new QHBoxLayout;
    row2->addWidget(m_format);
    row2->addWidget(m_stopAfter);
    row2->addWidget(m_folder, 1, Qt::AlignLeft);
    row2->addWidget(m_stop);
    row2->addWidget(m_remove);

    m_list = new QTreeWidget;
    m_list->setHeaderLabels(
        {QStringLiteral("Stream"), QStringLiteral("Status"), QStringLiteral("Length"), QStringLiteral("Size")});
    m_list->setRootIsDecorated(false);
    m_list->setUniformRowHeights(true);
    m_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    m_list->header()->setStretchLastSection(false);
    m_list->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int c = 1; c < 4; ++c)
        m_list->header()->setSectionResizeMode(c, QHeaderView::ResizeToContents);
    connect(m_list, &QTreeWidget::customContextMenuRequested, this, &StreamRecorderPanel::showContextMenu);
    connect(m_list, &QTreeWidget::itemSelectionChanged, this, &StreamRecorderPanel::updateButtons);
    connect(m_list, &QTreeWidget::itemDoubleClicked, this, [this] {
        if (StreamRecording *r = selected())
            playRecording(r);
    });

    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(4, 4, 4, 4);
    v->setSpacing(4);
    v->addLayout(row1);
    v->addWidget(m_message);
    v->addLayout(row2);
    v->addWidget(m_list, 1);

    connect(m_recorder, &StreamRecorder::recordingAdded, this, &StreamRecorderPanel::addItem);
    connect(m_recorder, &StreamRecorder::recordingChanged, this, &StreamRecorderPanel::updateItem);
    connect(m_recorder, &StreamRecorder::recordingFinished, this, [this](StreamRecording *r) {
        updateItem(r);
        if (r->state() == StreamRecording::State::Failed) {
            m_message->setText(QStringLiteral("Recording failed: ") + r->error());
            m_message->setStyleSheet(QStringLiteral("color: %1;").arg(RecordRed.name()));
        }
    });
    connect(m_recorder, &StreamRecorder::recordingRemoved, this, [this](const QString &id) {
        delete m_items.take(id);
        updateButtons();
    });
    for (StreamRecording *r : m_recorder->recordings())
        addItem(r);
    updateButtons();
}

void StreamRecorderPanel::focusUrl()
{
    if (m_url->text().trimmed().isEmpty()) {
        const QString clip = QApplication::clipboard()->text().trimmed();
        if (StreamLink::isStreamUrl(clip))
            m_url->setText(clip);
    }
    m_url->setFocus();
    m_url->selectAll();
}

void StreamRecorderPanel::updateFolderButton()
{
    const QString dir = AppSettings::recordingDir();
    m_folder->setText(QDir(dir).dirName());
    m_folder->setToolTip(QStringLiteral("Recordings are saved to %1").arg(QDir::toNativeSeparators(dir)));
}

int StreamRecorderPanel::stopAfterSeconds() const { return qMax(0, m_stopAfter->currentData().toInt()); }

void StreamRecorderPanel::chooseStopAfter()
{
    bool ok = false;
    const int minutes =
        QInputDialog::getInt(this, QStringLiteral("Stop After"),
                             QStringLiteral("End recordings after this many minutes:"), 90, 1, 7 * 24 * 60, 5, &ok);
    const int seconds = ok ? minutes * 60 : 0;
    int idx = m_stopAfter->findData(seconds);
    if (idx < 0) {
        m_stopAfter->insertItem(m_stopAfter->count() - 1, stopAfterText(seconds), seconds);
        idx = m_stopAfter->findData(seconds);
    }
    m_stopAfter->setCurrentIndex(idx);
}

void StreamRecorderPanel::startRecording()
{
    const QString url = m_url->text().trimmed();
    if (url.isEmpty()) {
        focusUrl();
        return;
    }
    if (!StreamLink::isStreamUrl(url)) {
        m_message->setText(
            QStringLiteral("That doesn't look like a link. Paste the address of a web page or stream, starting "
                           "with https://, rtmp://, rtsp:// or srt://."));
        m_message->setStyleSheet(QStringLiteral("color: %1;").arg(RecordRed.name()));
        return;
    }
    RecordingOptions o;
    o.url = url;
    o.outputDir = AppSettings::recordingDir();
    o.container = m_format->currentData().toString();
    o.maxSeconds = stopAfterSeconds();
    o.reconnect = AppSettings::reconnectRecordings();
    if (!StreamLink::isDirectMedia(url) && AppSettings::ytDlpPath().isEmpty()) {
        installYtDlp([this, o] { record(o); });
        return;
    }
    record(o);
}

void StreamRecorderPanel::record(const RecordingOptions &options)
{
    m_url->clear();
    m_message->setText(QString::fromUtf8(Hint));
    m_message->setStyleSheet(QString());
    m_recorder->record(options);
}

void StreamRecorderPanel::installYtDlp(const std::function<void()> &then)
{
    if (m_installing)
        return;
    QMessageBox box(QMessageBox::Question, QStringLiteral("Record from Web Pages"),
                    QStringLiteral("Recording from web pages such as YouTube and Twitch uses yt-dlp, a free, "
                                   "open-source helper that finds the video stream on the page.\n\n"
                                   "Download it now? It's about 18 MB, comes from the official yt-dlp project on "
                                   "GitHub, and keeps itself up to date."),
                    QMessageBox::Cancel, this);
    QPushButton *download = box.addButton(QStringLiteral("Download"), QMessageBox::AcceptRole);
    box.setDefaultButton(download);
    box.exec();
    if (box.clickedButton() != download)
        return;

    m_installing = true;
    m_record->setEnabled(false);
    m_message->setStyleSheet(QString());
    m_message->setText(QStringLiteral("Downloading yt-dlp…"));
    auto *installer = new YtDlpInstaller(this);
    connect(installer, &YtDlpInstaller::progress, this, [this](qint64 got, qint64 total) {
        if (total > 0)
            m_message->setText(QStringLiteral("Downloading yt-dlp… %1%").arg(int(got * 100 / total)));
    });
    connect(installer, &YtDlpInstaller::finished, this, [this, installer, then](bool ok, const QString &error) {
        installer->deleteLater();
        m_installing = false;
        m_record->setEnabled(true);
        if (ok) {
            then();
            return;
        }
        m_message->setText(QStringLiteral("Couldn't set up yt-dlp: ") + error);
        m_message->setStyleSheet(QStringLiteral("color: %1;").arg(RecordRed.name()));
    });
    installer->start();
}

void StreamRecorderPanel::addItem(StreamRecording *r)
{
    auto *it = new QTreeWidgetItem;
    it->setData(0, IdRole, r->id());
    m_list->insertTopLevelItem(0, it);
    m_items.insert(r->id(), it);
    m_list->clearSelection();
    it->setSelected(true);
    m_list->setCurrentItem(it);
    updateItem(r);
}

void StreamRecorderPanel::updateItem(StreamRecording *r)
{
    QTreeWidgetItem *it = m_items.value(r->id());
    if (!it)
        return;
    const QStringList files = r->files();
    const QString name = !files.isEmpty() ? QFileInfo(files.first()).completeBaseName()
                                          : (r->title().isEmpty() ? r->options().url : r->title());
    it->setText(0, name);
    it->setIcon(0, r->state() == StreamRecording::State::Recording ? Theme::icon("record", RecordRed) : QIcon());
    it->setText(1, r->stateText());
    it->setText(2, r->recordedSeconds() > 0 ? formatDuration(r->recordedSeconds()) : QString());
    it->setText(3, r->bytes() > 0 ? formatBytes(r->bytes()) : QString());

    QStringList tip{r->options().url};
    for (const QString &f : files)
        tip << QDir::toNativeSeparators(f);
    if (r->bitrateKbps() > 0 && r->isActive())
        tip << QStringLiteral("%1 Mbps").arg(r->bitrateKbps() / 1000.0, 0, 'f', 1);
    if (!r->error().isEmpty())
        tip << r->error();
    if (!r->note().isEmpty())
        tip << r->note();
    for (int c = 0; c < 4; ++c)
        it->setToolTip(c, tip.join('\n'));

    QColor color;
    if (r->state() == StreamRecording::State::Recording)
        color = RecordRed;
    else if (r->state() == StreamRecording::State::Failed)
        color = QColor(0xe0, 0x8a, 0x3c);
    it->setForeground(1, color.isValid() ? QBrush(color) : QBrush());
    updateButtons();
}

void StreamRecorderPanel::updateButtons()
{
    bool anyActive = false, anyInactive = false;
    for (StreamRecording *r : selectedRecordings()) {
        anyActive |= r->isActive();
        anyInactive |= !r->isActive();
    }
    m_stop->setEnabled(anyActive);
    m_remove->setEnabled(anyInactive);
}

StreamRecording *StreamRecorderPanel::selected() const
{
    const QList<StreamRecording *> all = selectedRecordings();
    return all.isEmpty() ? nullptr : all.first();
}

QList<StreamRecording *> StreamRecorderPanel::selectedRecordings() const
{
    QList<StreamRecording *> out;
    for (QTreeWidgetItem *it : m_list->selectedItems())
        if (StreamRecording *r = m_recorder->recording(it->data(0, IdRole).toString()))
            out << r;
    return out;
}

void StreamRecorderPanel::playRecording(StreamRecording *r)
{
    const QStringList files = r->files();
    if (!files.isEmpty() && QFileInfo::exists(files.last()))
        QDesktopServices::openUrl(QUrl::fromLocalFile(files.last()));
}

void StreamRecorderPanel::revealRecording(StreamRecording *r)
{
    const QStringList files = r->files();
    const QString file = files.isEmpty() ? QString() : files.last();
#ifdef Q_OS_WIN
    if (QFileInfo::exists(file)) {
        QProcess::startDetached(QStringLiteral("explorer.exe"),
                                {QStringLiteral("/select,"), QDir::toNativeSeparators(file)});
        return;
    }
#endif
    const QString dir = file.isEmpty() ? r->options().outputDir : QFileInfo(file).absolutePath();
    QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}

void StreamRecorderPanel::showContextMenu(const QPoint &pos)
{
    QMenu menu(this);
    StreamRecording *r = selected();
    if (r) {
        const QList<StreamRecording *> sel = selectedRecordings();
        const bool hasFiles = !r->files().isEmpty();
        if (r->isActive())
            menu.addAction(Theme::icon("stop"), QStringLiteral("Stop Recording"), this, [sel] {
                for (StreamRecording *x : sel)
                    x->stop();
            });
        menu.addAction(Theme::icon("play"), QStringLiteral("Play"), this, [this, r] { playRecording(r); })
            ->setEnabled(hasFiles);
        menu.addAction(Theme::icon("folder"), QStringLiteral("Show in Folder"), this,
                       [this, r] { revealRecording(r); });
        menu.addAction(Theme::icon("plus"), QStringLiteral("Add to Encoding Queue"), this,
                       [this, sel] {
                           QStringList files;
                           for (StreamRecording *x : sel)
                               if (!x->isActive())
                                   files << x->files();
                           emit addToQueueRequested(files);
                       })
            ->setEnabled(hasFiles && !r->isActive());
        menu.addSeparator();
        menu.addAction(Theme::icon("record", RecordRed), QStringLiteral("Record Again"), this, [this, r] {
            RecordingOptions o = r->options();
            o.name.clear();
            if (!StreamLink::isDirectMedia(o.url) && AppSettings::ytDlpPath().isEmpty())
                installYtDlp([this, o] { m_recorder->record(o); });
            else
                m_recorder->record(o);
        });
        menu.addAction(QStringLiteral("Copy Link"), this,
                       [r] { QApplication::clipboard()->setText(r->options().url); });
        if (!r->error().isEmpty() || !r->logTail().isEmpty())
            menu.addAction(Theme::icon("log"), QStringLiteral("Details…"), this, [this, r] {
                QMessageBox box(QMessageBox::Information, QStringLiteral("Recording Details"),
                                r->error().isEmpty() ? r->stateText() : r->error(), QMessageBox::Ok, this);
                box.setDetailedText(QStringLiteral("Link: %1\n\nCommand:\n%2\n\nffmpeg output:\n%3")
                                        .arg(r->options().url, r->commandLine(), r->logTail()));
                box.exec();
            });
        menu.addSeparator();
        menu.addAction(Theme::icon("trash"), QStringLiteral("Remove from List"), this,
                       [this, sel] {
                           for (StreamRecording *x : sel)
                               m_recorder->remove(x->id());
                       })
            ->setEnabled(!r->isActive());
    }
    menu.addAction(QStringLiteral("Clear Finished"), this, [this] {
        for (StreamRecording *x : m_recorder->recordings())
            if (!x->isActive())
                m_recorder->remove(x->id());
    });
    menu.exec(m_list->viewport()->mapToGlobal(pos));
}
