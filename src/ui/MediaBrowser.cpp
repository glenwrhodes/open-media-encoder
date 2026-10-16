#include "MediaBrowser.h"

#include "FrameGrabber.h"
#include "Theme.h"
#include "core/AppSettings.h"
#include "core/Formats.h"
#include "core/ImageSequence.h"
#include "core/MediaFileModel.h"
#include "core/MediaInfoCache.h"
#include "core/PresetManager.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QCompleter>
#include <QDesktopServices>
#include <QDir>
#include <QFileIconProvider>
#include <QFileSystemModel>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QProcess>
#include <QPushButton>
#include <QScrollArea>
#include <QShortcut>
#include <QSplitter>
#include <QStandardPaths>
#include <QTimer>
#include <QToolButton>
#include <QTreeView>
#include <QUrl>
#include <QVBoxLayout>

namespace {

// Bump when the column set changes so stale saved header layouts are ignored.
constexpr int HeaderStateVersion = 2;

QToolButton *toolButton(const QString &icon, const QString &tip)
{
    auto *b = new QToolButton;
    b->setIcon(Theme::icon(icon));
    b->setIconSize(QSize(16, 16));
    b->setToolTip(tip);
    b->setAutoRaise(true);
    return b;
}

void revealInExplorer(const QString &path)
{
#ifdef Q_OS_WIN
    QProcess::startDetached(QStringLiteral("explorer.exe"),
                            {QStringLiteral("/select,") + QDir::toNativeSeparators(path)});
#else
    QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
#endif
}

} // namespace

MediaBrowser::MediaBrowser(QWidget *parent)
    : QWidget(parent)
{
    // ---- folder tree
    m_dirModel = new QFileSystemModel(this);
    m_dirModel->setFilter(QDir::AllDirs | QDir::NoDotAndDotDot | QDir::Drives);
    m_dirModel->setRootPath(QString());
    m_dirView = new QTreeView;
    m_dirView->setModel(m_dirModel);
    for (int c = 1; c < m_dirModel->columnCount(); ++c)
        m_dirView->hideColumn(c);
    m_dirView->setHeaderHidden(true);

    // ---- file list
    m_iconProvider = new QFileIconProvider;
    m_fileModel = new MediaFileModel(this);
    m_fileModel->setIconProvider(m_iconProvider);
    m_fileModel->setShowFolders(AppSettings::value("browser/showFolders", true).toBool());
    m_fileModel->setShowAllFiles(AppSettings::value("browser/showAll", false).toBool());
    m_headerSaveTimer = new QTimer(this);
    m_headerSaveTimer->setSingleShot(true);
    m_headerSaveTimer->setInterval(500);
    connect(m_headerSaveTimer, &QTimer::timeout, this, [this] {
        AppSettings::setValue("browser/headerState", m_fileView->header()->saveState());
        AppSettings::setValue("browser/headerVersion", HeaderStateVersion);
    });
    m_proxy = new MediaFileSortProxy(this);
    m_proxy->setSourceModel(m_fileModel);

    m_fileView = new QTreeView;
    m_fileView->setModel(m_proxy);
    m_fileView->setRootIsDecorated(false);
    m_fileView->setUniformRowHeights(true);
    m_fileView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_fileView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_fileView->setDragEnabled(true);
    m_fileView->setDragDropMode(QAbstractItemView::DragOnly);
    m_fileView->setAlternatingRowColors(true);
    m_fileView->setSortingEnabled(true);
    m_fileView->setContextMenuPolicy(Qt::CustomContextMenu);
    QHeaderView *header = m_fileView->header();
    header->setSectionsMovable(true);
    header->setStretchLastSection(false);
    header->setContextMenuPolicy(Qt::CustomContextMenu);
    header->setToolTip(QStringLiteral("Click to sort. Right-click to choose columns."));
    restoreHeaderState();

    connect(header, &QWidget::customContextMenuRequested, this, &MediaBrowser::showHeaderMenu);
    connect(header, &QHeaderView::sectionResized, this, &MediaBrowser::saveHeaderState);
    connect(header, &QHeaderView::sectionMoved, this, &MediaBrowser::saveHeaderState);
    connect(header, &QHeaderView::sortIndicatorChanged, this, &MediaBrowser::saveHeaderState);
    connect(m_fileView, &QWidget::customContextMenuRequested, this, &MediaBrowser::showFileMenu);
    connect(m_fileView, &QTreeView::doubleClicked, this, &MediaBrowser::activate);
    connect(m_fileView->selectionModel(), &QItemSelectionModel::currentChanged, this, [this](const QModelIndex &idx) {
        const QModelIndex src = m_proxy->mapToSource(idx);
        if (!m_fileModel->isDir(src))
            showInfoFor(m_fileModel->filePath(src));
    });

    // Keyboard: Enter adds to queue / opens folder, Backspace goes up.
    auto *enter = new QShortcut(QKeySequence(Qt::Key_Return), m_fileView, nullptr, nullptr, Qt::WidgetShortcut);
    connect(enter, &QShortcut::activated, this, [this] {
        const QModelIndexList rows = m_fileView->selectionModel()->selectedRows(0);
        if (rows.size() == 1 && rows.first().data(MediaFileModel::IsDirRole).toBool())
            activate(rows.first());
        else if (!selectedFiles().isEmpty())
            emit addToQueueRequested(selectedFiles());
    });
    auto *back = new QShortcut(QKeySequence(Qt::Key_Backspace), m_fileView, nullptr, nullptr, Qt::WidgetShortcut);
    connect(back, &QShortcut::activated, this, &MediaBrowser::goUp);

    // ---- filter row
    m_filter = new QLineEdit;
    m_filter->setPlaceholderText(QStringLiteral("Filter by name"));
    m_filter->setClearButtonEnabled(true);
    m_filter->addAction(Theme::icon("search", QColor(130, 130, 130)), QLineEdit::LeadingPosition);
    connect(m_filter, &QLineEdit::textChanged, m_proxy, &MediaFileSortProxy::setNameFilterText);
    m_kind = new QComboBox;
    m_kind->addItem(QStringLiteral("All Media"), MediaFileSortProxy::AllMedia);
    m_kind->addItem(QStringLiteral("Video"), MediaFileSortProxy::VideoOnly);
    m_kind->addItem(QStringLiteral("Audio"), MediaFileSortProxy::AudioOnly);
    m_kind->addItem(QStringLiteral("Images"), MediaFileSortProxy::ImagesOnly);
    connect(m_kind, &QComboBox::activated, this,
            [this] { m_proxy->setKindFilter(MediaFileSortProxy::KindFilter(m_kind->currentData().toInt())); });
    m_count = new QLabel;
    m_count->setObjectName("muted");
    auto updateCount = [this] {
        int files = 0;
        for (int r = 0; r < m_proxy->rowCount(); ++r)
            files += m_proxy->index(r, 0).data(MediaFileModel::IsDirRole).toBool() ? 0 : 1;
        m_count->setText(QStringLiteral("%1 item%2").arg(files).arg(files == 1 ? "" : "s"));
    };
    connect(m_proxy, &QAbstractItemModel::rowsInserted, this, updateCount);
    connect(m_proxy, &QAbstractItemModel::rowsRemoved, this, updateCount);
    connect(m_proxy, &QAbstractItemModel::modelReset, this, updateCount);
    connect(m_proxy, &QAbstractItemModel::layoutChanged, this, updateCount);

    auto *filterRow = new QHBoxLayout;
    filterRow->setContentsMargins(0, 0, 0, 2);
    filterRow->addWidget(m_filter, 1);
    filterRow->addWidget(m_kind);
    filterRow->addWidget(m_count);
    auto *fileSide = new QWidget;
    auto *fileLayout = new QVBoxLayout(fileSide);
    fileLayout->setContentsMargins(0, 0, 0, 0);
    fileLayout->setSpacing(0);
    fileLayout->addLayout(filterRow);
    fileLayout->addWidget(m_fileView, 1);

    // ---- info pane
    m_thumb = new QLabel;
    m_thumb->setFixedSize(160, 90);
    m_thumb->setAlignment(Qt::AlignCenter);
    m_thumb->setStyleSheet(QStringLiteral("background:#0e0e0e; border:1px solid #2a2a2a;"));
    m_info = new QLabel;
    m_info->setObjectName("muted");
    m_info->setWordWrap(true);
    m_info->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_info->setTextInteractionFlags(Qt::TextSelectableByMouse);

    m_grabber = new FrameGrabber(this);
    connect(m_grabber, &FrameGrabber::frameReady, this, [this](const QImage &img) {
        m_thumb->setPixmap(
            QPixmap::fromImage(img.scaled(m_thumb->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation)));
    });
    connect(m_grabber, &FrameGrabber::failed, this, [this] { m_thumb->setText(QStringLiteral("No preview")); });
    connect(&MediaInfoCache::instance(), &MediaInfoCache::infoReady, this, [this](const QString &path) {
        if (path != m_current)
            return;
        if (const auto info = MediaInfoCache::instance().lookup(path, false))
            showInfo(*info);
    });

    // ---- folder tree <-> list
    // The tree loads asynchronously; keep the remembered folder in view as its parents appear.
    connect(m_dirModel, &QFileSystemModel::directoryLoaded, this, [this](const QString &loaded) {
        if (m_targetDir.isEmpty() || !m_targetDir.startsWith(loaded, Qt::CaseInsensitive))
            return;
        const QModelIndex idx = m_dirModel->index(m_targetDir);
        if (!idx.isValid())
            return;
        {
            QSignalBlocker block(m_dirView->selectionModel());
            m_dirView->setCurrentIndex(idx);
        }
        // Give the view time to lay out the rows that just loaded before scrolling.
        const QPersistentModelIndex target(idx);
        QTimer::singleShot(150, this, [this, target] {
            if (target.isValid())
                m_dirView->scrollTo(target, QAbstractItemView::PositionAtCenter);
        });
        if (QDir(loaded) == QDir(m_targetDir))
            m_targetDir.clear();
    });
    connect(m_dirView->selectionModel(), &QItemSelectionModel::currentChanged, this,
            [this](const QModelIndex &idx) { setRootPath(m_dirModel->filePath(idx)); });

    // ---- layout
    auto *split = new QSplitter(Qt::Horizontal);
    split->addWidget(m_dirView);
    split->addWidget(fileSide);
    split->setStretchFactor(0, 2);
    split->setStretchFactor(1, 5);

    auto *infoScroll = new QScrollArea;
    infoScroll->setWidget(m_info);
    infoScroll->setWidgetResizable(true);
    infoScroll->setFrameShape(QFrame::NoFrame);
    auto *infoRow = new QHBoxLayout;
    infoRow->setContentsMargins(4, 4, 4, 4);
    infoRow->addWidget(m_thumb, 0, Qt::AlignTop);
    infoRow->addWidget(infoScroll, 1);
    auto *infoWidget = new QWidget;
    infoWidget->setLayout(infoRow);
    infoWidget->setMaximumHeight(130);

    auto *vs = new QSplitter(Qt::Vertical);
    vs->addWidget(split);
    vs->addWidget(infoWidget);
    vs->setStretchFactor(0, 4);
    vs->setStretchFactor(1, 0);

    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(0, 0, 0, 0);
    v->addWidget(buildToolbar());
    v->addWidget(vs, 1);

    QString start = AppSettings::value("browser/lastDir").toString();
    if (start.isEmpty() || !QDir(start).exists())
        start = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    setRootPath(start);
    updateMetadataEnabled();
}

MediaBrowser::~MediaBrowser()
{
    m_fileModel->setIconProvider(nullptr);
    delete m_iconProvider;
}

QWidget *MediaBrowser::buildToolbar()
{
    auto *up = toolButton("up", QStringLiteral("Up one folder (Backspace)"));
    connect(up, &QToolButton::clicked, this, &MediaBrowser::goUp);
    auto *refresh = toolButton("reset", QStringLiteral("Refresh"));
    connect(refresh, &QToolButton::clicked, m_fileModel, &MediaFileModel::refresh);

    m_favorites = toolButton("star", QStringLiteral("Favorite folders"));
    m_favoritesMenu = new QMenu(m_favorites);
    m_favorites->setMenu(m_favoritesMenu);
    m_favorites->setPopupMode(QToolButton::InstantPopup);
    connect(m_favoritesMenu, &QMenu::aboutToShow, this, &MediaBrowser::rebuildFavoritesMenu);

    m_path = new QLineEdit;
    auto *completerModel = new QFileSystemModel(this);
    completerModel->setFilter(QDir::AllDirs | QDir::NoDotAndDotDot | QDir::Drives);
    completerModel->setRootPath(QString());
    auto *completer = new QCompleter(completerModel, this);
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    m_path->setCompleter(completer);
    connect(m_path, &QLineEdit::returnPressed, this, [this] { setRootPath(m_path->text()); });

    auto *addBtn = new QPushButton(QStringLiteral("Add to Queue"));
    connect(addBtn, &QPushButton::clicked, this, [this] {
        const QStringList f = selectedFiles();
        if (!f.isEmpty())
            emit addToQueueRequested(f);
    });

    auto *w = new QWidget;
    auto *top = new QHBoxLayout(w);
    top->setContentsMargins(4, 4, 4, 4);
    top->addWidget(up);
    top->addWidget(refresh);
    top->addWidget(m_favorites);
    top->addWidget(m_path, 1);
    top->addWidget(addBtn);
    return w;
}

// ------------------------------------------------------------------ navigation

void MediaBrowser::setRootPath(const QString &path)
{
    const QString p = QDir::fromNativeSeparators(QDir::cleanPath(path));
    if (p.isEmpty() || !QDir(p).exists())
        return;
    if (QDir(p) == QDir(m_fileModel->directory()) && !m_fileModel->directory().isEmpty())
        return;
    m_targetDir = p;
    const QModelIndex idx = m_dirModel->index(p);
    if (idx.isValid()) {
        QSignalBlocker block(m_dirView->selectionModel());
        m_dirView->setCurrentIndex(idx);
        m_dirView->scrollTo(idx, QAbstractItemView::PositionAtCenter);
        m_dirView->expand(idx);
    }
    m_path->setText(QDir::toNativeSeparators(p));
    m_fileModel->setDirectory(p);
    m_fileView->sortByColumn(m_fileView->header()->sortIndicatorSection(), m_fileView->header()->sortIndicatorOrder());
    AppSettings::setValue("browser/lastDir", p);
}

QString MediaBrowser::currentPath() const { return m_fileModel->directory(); }

void MediaBrowser::goUp()
{
    QDir d(m_fileModel->directory());
    if (d.cdUp())
        setRootPath(d.absolutePath());
}

void MediaBrowser::activate(const QModelIndex &proxyIndex)
{
    const QModelIndex src = m_proxy->mapToSource(proxyIndex);
    if (m_fileModel->isDir(src))
        setRootPath(m_fileModel->filePath(src));
    else
        emit addToQueueRequested({m_fileModel->filePath(src)});
}

QStringList MediaBrowser::selectedPaths() const
{
    QStringList out;
    for (const QModelIndex &i : m_fileView->selectionModel()->selectedRows(0))
        out << m_fileModel->filePath(m_proxy->mapToSource(i));
    return out;
}

QStringList MediaBrowser::selectedFiles() const
{
    QStringList out;
    for (const QModelIndex &i : m_fileView->selectionModel()->selectedRows(0)) {
        const QModelIndex src = m_proxy->mapToSource(i);
        if (!m_fileModel->isDir(src))
            out << m_fileModel->filePath(src);
    }
    return out;
}

// ------------------------------------------------------------------ columns

void MediaBrowser::restoreHeaderState()
{
    m_restoringHeader = true;
    QHeaderView *header = m_fileView->header();
    const QByteArray state = AppSettings::value("browser/headerState").toByteArray();
    const bool restored = AppSettings::value("browser/headerVersion").toInt() == HeaderStateVersion &&
                          !state.isEmpty() && header->restoreState(state);
    if (!restored) {
        for (int c = 0; c < MediaFileModel::ColumnCount; ++c) {
            const bool visible = c == MediaFileModel::Name || c == MediaFileModel::Type || c == MediaFileModel::Size ||
                                 c == MediaFileModel::Modified;
            header->setSectionHidden(c, !visible);
        }
        header->resizeSection(MediaFileModel::Name, 230);
        header->resizeSection(MediaFileModel::Type, 60);
        header->resizeSection(MediaFileModel::Size, 80);
        header->resizeSection(MediaFileModel::Modified, 140);
        header->resizeSection(MediaFileModel::Kind, 60);
        header->resizeSection(MediaFileModel::Duration, 95);
        header->resizeSection(MediaFileModel::Resolution, 100);
        header->resizeSection(MediaFileModel::FrameRate, 75);
        header->resizeSection(MediaFileModel::VideoCodec, 75);
        header->resizeSection(MediaFileModel::Audio, 170);
        header->setSortIndicator(MediaFileModel::Name, Qt::AscendingOrder);
    }
    header->setSectionHidden(MediaFileModel::Name, false);
    m_restoringHeader = false;
}

void MediaBrowser::saveHeaderState()
{
    if (!m_restoringHeader)
        m_headerSaveTimer->start();
}

void MediaBrowser::updateMetadataEnabled()
{
    bool any = false;
    for (int c = 0; c < MediaFileModel::ColumnCount; ++c)
        any = any || (MediaFileModel::isMetadataColumn(c) && !m_fileView->header()->isSectionHidden(c));
    m_fileModel->setMetadataEnabled(any);
}

void MediaBrowser::showHeaderMenu(const QPoint &pos)
{
    QHeaderView *header = m_fileView->header();
    QMenu menu(this);
    menu.addAction(QStringLiteral("Show Columns"))->setEnabled(false);
    menu.addSeparator();
    for (int c = 0; c < MediaFileModel::ColumnCount; ++c) {
        if (c == MediaFileModel::Name)
            continue;
        QAction *a = menu.addAction(MediaFileModel::columnTitle(c));
        a->setCheckable(true);
        a->setChecked(!header->isSectionHidden(c));
        if (MediaFileModel::isMetadataColumn(c))
            a->setToolTip(QStringLiteral("Read from the media; filled in as files are scanned"));
        connect(a, &QAction::toggled, this, [this, header, c](bool on) {
            header->setSectionHidden(c, !on);
            if (on && header->sectionSize(c) < 40)
                header->resizeSection(c, 100);
            updateMetadataEnabled();
            saveHeaderState();
            // Bring a newly shown column into view; it may sit past the right edge.
            if (on)
                QTimer::singleShot(0, this, [this, c] {
                    const QModelIndex top = m_fileView->indexAt(QPoint(1, 1));
                    const QModelIndex cell = m_proxy->index(top.isValid() ? top.row() : 0, c);
                    if (cell.isValid())
                        m_fileView->scrollTo(cell);
                });
        });
    }
    menu.addSeparator();
    QAction *folders = menu.addAction(QStringLiteral("Show Folders in List"));
    folders->setCheckable(true);
    folders->setChecked(AppSettings::value("browser/showFolders", true).toBool());
    connect(folders, &QAction::toggled, this, [this](bool on) {
        AppSettings::setValue("browser/showFolders", on);
        m_fileModel->setShowFolders(on);
    });
    QAction *all = menu.addAction(QStringLiteral("Show Non-Media Files"));
    all->setCheckable(true);
    all->setChecked(AppSettings::value("browser/showAll", false).toBool());
    connect(all, &QAction::toggled, this, [this](bool on) {
        AppSettings::setValue("browser/showAll", on);
        m_fileModel->setShowAllFiles(on);
    });
    menu.addSeparator();
    menu.addAction(QStringLiteral("Reset Columns"), this, [this] {
        AppSettings::setValue("browser/headerState", QByteArray());
        QHeaderView *h = m_fileView->header();
        for (int c = 0; c < MediaFileModel::ColumnCount; ++c)
            h->moveSection(h->visualIndex(c), c);
        restoreHeaderState();
        updateMetadataEnabled();
        saveHeaderState();
    });
    menu.exec(header->mapToGlobal(pos));
}

// ------------------------------------------------------------------ context menu

void MediaBrowser::showFileMenu(const QPoint &pos)
{
    const QStringList files = selectedFiles();
    const QStringList paths = selectedPaths();
    const QModelIndex at = m_fileView->indexAt(pos);
    QMenu menu(this);

    if (!files.isEmpty()) {
        menu.addAction(Theme::icon("plus"), QStringLiteral("Add to Queue"), this,
                       [this, files] { emit addToQueueRequested(files); });

        QMenu *withPreset = menu.addMenu(Theme::icon("preset"), QStringLiteral("Add to Queue with Preset"));
        const QList<Preset> &presets = PresetManager::instance().presets();
        bool anyUser = false;
        for (const Preset &p : presets) {
            if (p.system)
                continue;
            anyUser = true;
            withPreset->addAction(Theme::icon("preset", Theme::accent()), p.name(), this,
                                  [this, files, id = p.id] { emit addToQueueWithPresetRequested(files, id); });
        }
        if (anyUser)
            withPreset->addSeparator();
        for (const FormatDef &f : Formats::all()) {
            QMenu *sub = nullptr;
            for (const Preset &p : presets) {
                if (!p.system || p.settings.format != f.id)
                    continue;
                if (!sub)
                    sub = withPreset->addMenu(f.displayName);
                sub->addAction(p.name(), this,
                               [this, files, id = p.id] { emit addToQueueWithPresetRequested(files, id); });
            }
        }
        if (files.size() > 1)
            menu.addAction(Theme::icon("stitch"), QStringLiteral("Stitch Together and Add to Queue"), this,
                           [this, files] { emit stitchRequested(files); });
        if (ImageSequence::isImageFile(files.first())) {
            const ImageSequence seq = ImageSequence::detect(files.first());
            if (seq.valid)
                menu.addAction(Theme::icon("image"),
                               QStringLiteral("Add as Image Sequence (%1 frames)…").arg(seq.count), this,
                               [this, f = files.first()] { emit imageSequenceRequested(f); });
        }
        menu.addSeparator();
        if (files.size() == 1)
            menu.addAction(Theme::icon("play"), QStringLiteral("Open in Default Player"), this,
                           [f = files.first()] { QDesktopServices::openUrl(QUrl::fromLocalFile(f)); });
        menu.addAction(Theme::icon("folder"), QStringLiteral("Reveal in Explorer"), this,
                       [f = files.first()] { revealInExplorer(f); });
    }

    const bool folderClicked = at.isValid() && at.data(MediaFileModel::IsDirRole).toBool() && paths.size() == 1;
    const QString folder = folderClicked ? paths.first() : m_fileModel->directory();
    if (folderClicked) {
        menu.addAction(Theme::icon("folder"), QStringLiteral("Open Folder"), this,
                       [this, folder] { setRootPath(folder); });
        menu.addAction(Theme::icon("plus"), QStringLiteral("Add All Media in Folder to Queue"), this,
                       [this, folder] { emit addToQueueRequested({folder}); });
    }
    if (!paths.isEmpty())
        menu.addAction(QStringLiteral("Copy Path"), this, [paths] {
            QStringList native;
            for (const QString &p : paths)
                native << QDir::toNativeSeparators(p);
            QApplication::clipboard()->setText(native.join('\n'));
        });
    if (!menu.isEmpty())
        menu.addSeparator();
    menu.addAction(Theme::icon("watch"), QStringLiteral("Add \"%1\" as Watch Folder").arg(QDir(folder).dirName()), this,
                   [this, folder] { emit watchFolderRequested(folder); });
    menu.addAction(Theme::icon("star"), QStringLiteral("Add \"%1\" to Favorites").arg(QDir(folder).dirName()), this,
                   [folder] {
                       QStringList fav = AppSettings::value("browser/favorites").toStringList();
                       if (!fav.contains(folder, Qt::CaseInsensitive)) {
                           fav << folder;
                           AppSettings::setValue("browser/favorites", fav);
                       }
                   });
    menu.addAction(Theme::icon("reset"), QStringLiteral("Refresh"), m_fileModel, &MediaFileModel::refresh);
    menu.exec(m_fileView->viewport()->mapToGlobal(pos));
}

void MediaBrowser::rebuildFavoritesMenu()
{
    m_favoritesMenu->clear();
    const QString current = m_fileModel->directory();
    QStringList fav = AppSettings::value("browser/favorites").toStringList();
    const bool isFav = fav.contains(current, Qt::CaseInsensitive);
    if (isFav)
        m_favoritesMenu->addAction(QStringLiteral("Remove Current Folder from Favorites"), this, [current] {
            QStringList f = AppSettings::value("browser/favorites").toStringList();
            f.removeIf([&](const QString &s) { return s.compare(current, Qt::CaseInsensitive) == 0; });
            AppSettings::setValue("browser/favorites", f);
        });
    else
        m_favoritesMenu->addAction(Theme::icon("star"), QStringLiteral("Add Current Folder to Favorites"), this,
                                   [current] {
                                       QStringList f = AppSettings::value("browser/favorites").toStringList();
                                       f << current;
                                       AppSettings::setValue("browser/favorites", f);
                                   });
    if (!fav.isEmpty()) {
        m_favoritesMenu->addSection(QStringLiteral("Favorites"));
        for (const QString &f : fav)
            m_favoritesMenu->addAction(Theme::icon("folder"), QDir::toNativeSeparators(f), this,
                                       [this, f] { setRootPath(f); });
    }
    m_favoritesMenu->addSection(QStringLiteral("Places"));
    const QList<QPair<QString, QStandardPaths::StandardLocation>> places = {
        {QStringLiteral("Videos"), QStandardPaths::MoviesLocation},
        {QStringLiteral("Music"), QStandardPaths::MusicLocation},
        {QStringLiteral("Pictures"), QStandardPaths::PicturesLocation},
        {QStringLiteral("Downloads"), QStandardPaths::DownloadLocation},
        {QStringLiteral("Desktop"), QStandardPaths::DesktopLocation},
    };
    for (const auto &place : places) {
        const QString p = QStandardPaths::writableLocation(place.second);
        if (!p.isEmpty() && QDir(p).exists())
            m_favoritesMenu->addAction(Theme::icon("folder"), place.first, this, [this, p] { setRootPath(p); });
    }
}

// ------------------------------------------------------------------ info pane

void MediaBrowser::showInfoFor(const QString &path)
{
    if (path.isEmpty())
        return;
    m_current = path;
    m_thumb->clear();
    if (const auto info = MediaInfoCache::instance().lookup(path, true))
        showInfo(*info);
    else
        m_info->setText(QStringLiteral("Reading %1…").arg(QFileInfo(path).fileName()));
}

void MediaBrowser::showInfo(const MediaInfo &info)
{
    m_info->setText(info.describe());
    if (info.valid && info.hasVideo) {
        BuildRequest req;
        req.source = info;
        req.inputs = {info.path};
        m_grabber->request(req, info.isStill ? 0.0 : info.duration * 0.1, false);
    } else {
        m_thumb->setPixmap(Theme::icon(info.hasAudio ? "audio" : "info", QColor(110, 110, 110)).pixmap(48, 48));
    }
}
