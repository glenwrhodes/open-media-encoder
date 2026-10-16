#include "MediaBrowser.h"

#include "FrameGrabber.h"
#include "Theme.h"
#include "core/AppSettings.h"
#include "core/Formats.h"

#include <QDir>
#include <QFileSystemModel>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSplitter>
#include <QStandardPaths>
#include <QTimer>
#include <QToolButton>
#include <QTreeView>
#include <QVBoxLayout>
#include <QtConcurrent>

MediaBrowser::MediaBrowser(QWidget *parent)
    : QWidget(parent)
{
    m_dirModel = new QFileSystemModel(this);
    m_dirModel->setFilter(QDir::AllDirs | QDir::NoDotAndDotDot | QDir::Drives);
    m_dirModel->setRootPath(QString());

    m_fileModel = new QFileSystemModel(this);
    m_fileModel->setFilter(QDir::Files | QDir::NoDotAndDotDot);
    QStringList filters;
    for (const QString &ext : Formats::mediaFileExtensions())
        filters << "*." + ext;
    m_fileModel->setNameFilters(filters);
    m_fileModel->setNameFilterDisables(false);

    m_dirView = new QTreeView;
    m_dirView->setModel(m_dirModel);
    for (int c = 1; c < m_dirModel->columnCount(); ++c)
        m_dirView->hideColumn(c);
    m_dirView->setHeaderHidden(true);

    m_fileView = new QTreeView;
    m_fileView->setModel(m_fileModel);
    m_fileView->setRootIsDecorated(false);
    m_fileView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_fileView->setDragEnabled(true);
    m_fileView->setDragDropMode(QAbstractItemView::DragOnly);
    m_fileView->setAlternatingRowColors(true);
    m_fileView->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_fileView->setColumnWidth(1, 80);
    m_fileView->hideColumn(2);

    m_path = new QLineEdit;
    connect(m_path, &QLineEdit::returnPressed, this, [this] { setRootPath(m_path->text()); });

    auto *addBtn = new QPushButton(QStringLiteral("Add to Queue"));
    connect(addBtn, &QPushButton::clicked, this, [this] {
        const QStringList f = selectedFiles();
        if (!f.isEmpty())
            emit addToQueueRequested(f);
    });

    m_thumb = new QLabel;
    m_thumb->setFixedSize(176, 99);
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

    connect(&m_probe, &QFutureWatcher<MediaInfo>::finished, this, [this] {
        const MediaInfo info = m_probe.result();
        if (info.path != m_current)
            return;
        m_info->setText(info.describe());
        if (info.valid && info.hasVideo) {
            BuildRequest req;
            req.source = info;
            req.inputs = {info.path};
            m_grabber->request(req, info.isStill ? 0.0 : info.duration * 0.1, false);
        } else {
            m_thumb->setPixmap(Theme::icon(info.hasAudio ? "audio" : "info", QColor(110, 110, 110)).pixmap(48, 48));
        }
    });

    // The model loads directories asynchronously; keep the remembered folder in view as its parents appear.
    connect(m_dirModel, &QFileSystemModel::directoryLoaded, this, [this](const QString &loaded) {
        if (m_targetDir.isEmpty() || !m_targetDir.startsWith(loaded, Qt::CaseInsensitive))
            return;
        const QModelIndex idx = m_dirModel->index(m_targetDir);
        if (!idx.isValid())
            return;
        m_dirView->setCurrentIndex(idx);
        // Scroll after the view has laid out the newly loaded rows.
        QTimer::singleShot(0, this, [this, idx] {
            if (idx.isValid())
                m_dirView->scrollTo(idx, QAbstractItemView::PositionAtCenter);
        });
        if (QDir(loaded) == QDir(m_targetDir))
            m_targetDir.clear();
    });
    connect(m_dirView->selectionModel(), &QItemSelectionModel::currentChanged, this, [this](const QModelIndex &idx) {
        const QString p = m_dirModel->filePath(idx);
        m_path->setText(QDir::toNativeSeparators(p));
        m_fileView->setRootIndex(m_fileModel->setRootPath(p));
        AppSettings::setValue("browser/lastDir", p);
    });
    connect(m_fileView->selectionModel(), &QItemSelectionModel::currentChanged, this,
            [this](const QModelIndex &idx) { showInfoFor(m_fileModel->filePath(idx)); });
    connect(m_fileView, &QTreeView::doubleClicked, this,
            [this](const QModelIndex &idx) { emit addToQueueRequested({m_fileModel->filePath(idx)}); });

    auto *top = new QHBoxLayout;
    top->setContentsMargins(4, 4, 4, 4);
    top->addWidget(m_path, 1);
    top->addWidget(addBtn);

    auto *split = new QSplitter(Qt::Horizontal);
    split->addWidget(m_dirView);
    split->addWidget(m_fileView);
    split->setStretchFactor(0, 2);
    split->setStretchFactor(1, 3);

    auto *infoRow = new QHBoxLayout;
    infoRow->setContentsMargins(4, 4, 4, 4);
    infoRow->addWidget(m_thumb, 0, Qt::AlignTop);
    infoRow->addWidget(m_info, 1);
    auto *infoWidget = new QWidget;
    infoWidget->setLayout(infoRow);
    infoWidget->setMaximumHeight(170);

    auto *vs = new QSplitter(Qt::Vertical);
    vs->addWidget(split);
    vs->addWidget(infoWidget);
    vs->setStretchFactor(0, 3);
    vs->setStretchFactor(1, 1);

    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(0, 0, 0, 0);
    v->addLayout(top);
    v->addWidget(vs, 1);

    QString start = AppSettings::value("browser/lastDir").toString();
    if (start.isEmpty() || !QDir(start).exists())
        start = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    setRootPath(start);
}

void MediaBrowser::setRootPath(const QString &path)
{
    const QString p = QDir::fromNativeSeparators(path);
    if (!QDir(p).exists())
        return;
    m_targetDir = p;
    const QModelIndex idx = m_dirModel->index(p);
    m_dirView->setCurrentIndex(idx);
    m_dirView->scrollTo(idx, QAbstractItemView::PositionAtCenter);
    m_dirView->expand(idx);
    m_path->setText(QDir::toNativeSeparators(p));
    m_fileView->setRootIndex(m_fileModel->setRootPath(p));
}

QStringList MediaBrowser::selectedFiles() const
{
    QStringList out;
    for (const QModelIndex &i : m_fileView->selectionModel()->selectedRows(0))
        out << m_fileModel->filePath(i);
    return out;
}

void MediaBrowser::showInfoFor(const QString &path)
{
    if (path.isEmpty() || QFileInfo(path).isDir())
        return;
    m_current = path;
    m_info->setText(QStringLiteral("Reading %1…").arg(QFileInfo(path).fileName()));
    m_thumb->clear();
    const QString probe = AppSettings::ffprobePath();
    m_probe.setFuture(QtConcurrent::run([probe, path] { return MediaInfo::probe(probe, path); }));
}
