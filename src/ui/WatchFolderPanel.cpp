#include "WatchFolderPanel.h"

#include "Theme.h"
#include "core/Formats.h"
#include "core/PresetManager.h"
#include "core/WatchFolderManager.h"

#include <QDir>
#include <QDragEnterEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMimeData>
#include <QToolButton>
#include <QVBoxLayout>

namespace {
constexpr int FolderRole = Qt::UserRole + 1;
constexpr int IndexRole = Qt::UserRole + 2;
} // namespace

void WatchFolderTree::dragEnterEvent(QDragEnterEvent *e)
{
    if (e->mimeData()->hasFormat(PresetManager::MimeType) || e->mimeData()->hasUrls())
        e->acceptProposedAction();
}

void WatchFolderTree::dragMoveEvent(QDragMoveEvent *e)
{
    if (e->mimeData()->hasUrls() ||
        (e->mimeData()->hasFormat(PresetManager::MimeType) && itemAt(e->position().toPoint())))
        e->acceptProposedAction();
    else
        e->ignore();
}

void WatchFolderTree::dropEvent(QDropEvent *e)
{
    if (e->mimeData()->hasFormat(PresetManager::MimeType)) {
        if (QTreeWidgetItem *it = itemAt(e->position().toPoint()))
            emit presetDropped(it, QString::fromUtf8(e->mimeData()->data(PresetManager::MimeType)));
    } else if (e->mimeData()->hasUrls()) {
        QStringList dirs;
        for (const QUrl &u : e->mimeData()->urls())
            if (u.isLocalFile() && QFileInfo(u.toLocalFile()).isDir())
                dirs << u.toLocalFile();
        emit foldersDropped(dirs);
    }
    e->acceptProposedAction();
}

WatchFolderPanel::WatchFolderPanel(WatchFolderManager *manager, QWidget *parent)
    : QWidget(parent)
    , m_manager(manager)
{
    auto mk = [](const QString &icon, const QString &tip) {
        auto *b = new QToolButton;
        b->setIcon(Theme::icon(icon));
        b->setIconSize(QSize(16, 16));
        b->setToolTip(tip);
        b->setAutoRaise(true);
        return b;
    };
    auto *bar = new QHBoxLayout;
    bar->setContentsMargins(4, 4, 4, 4);
    auto *addF = mk("watch", QStringLiteral("Add Watch Folder"));
    auto *addO = mk("plus", QStringLiteral("Add Output to selected folder"));
    auto *rem = mk("minus", QStringLiteral("Remove"));
    auto *settings = mk("settings", QStringLiteral("Output Settings"));
    connect(addF, &QToolButton::clicked, this, [this] { addFolder(); });
    connect(addO, &QToolButton::clicked, this, &WatchFolderPanel::addOutput);
    connect(rem, &QToolButton::clicked, this, &WatchFolderPanel::removeSelected);
    connect(settings, &QToolButton::clicked, this, [this] {
        QTreeWidgetItem *it = m_tree->currentItem();
        if (it && it->parent())
            emit editOutputRequested(it->data(0, FolderRole).toString(), it->data(0, IndexRole).toInt());
    });
    for (auto *b : {addF, addO, rem, settings})
        bar->addWidget(b);
    auto *hint = new QLabel(QStringLiteral("Media dropped into a watch folder is encoded with its outputs; "
                                           "sources move to /Source when done."));
    hint->setObjectName("muted");
    bar->addWidget(hint, 1);

    m_tree = new WatchFolderTree;
    m_tree->setColumnCount(3);
    m_tree->setHeaderLabels({QStringLiteral("Format"), QStringLiteral("Preset"), QStringLiteral("Output To")});
    m_tree->setAcceptDrops(true);
    m_tree->setDropIndicatorShown(true);
    m_tree->header()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_tree->setColumnWidth(0, 240);
    m_tree->setColumnWidth(1, 260);
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *it, int col) {
        if (!it->parent())
            return;
        const QString fid = it->data(0, FolderRole).toString();
        const int idx = it->data(0, IndexRole).toInt();
        if (col == 2)
            chooseOutputDir(fid, idx);
        else
            emit editOutputRequested(fid, idx);
    });
    connect(m_tree, &WatchFolderTree::presetDropped, this, [this](QTreeWidgetItem *it, const QString &presetId) {
        const Preset *p = PresetManager::instance().find(presetId);
        if (!p)
            return;
        const QString fid = it->data(0, FolderRole).toString();
        WatchOutput w{p->settings, p->id, {}};
        if (it->parent())
            m_manager->setOutput(fid, it->data(0, IndexRole).toInt(), w);
        else
            m_manager->addOutput(fid, w);
    });
    connect(m_tree, &WatchFolderTree::foldersDropped, this, [this](const QStringList &dirs) {
        for (const QString &d : dirs)
            addFolder(d);
    });

    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);
    v->addLayout(bar);
    v->addWidget(m_tree, 1);

    connect(m_manager, &WatchFolderManager::foldersChanged, this, &WatchFolderPanel::reload);
    reload();
}

void WatchFolderPanel::reload()
{
    m_tree->clear();
    for (const WatchFolder &f : m_manager->folders()) {
        auto *top = new QTreeWidgetItem(m_tree, {QDir::toNativeSeparators(f.path)});
        top->setFirstColumnSpanned(true);
        top->setIcon(0, Theme::icon("watch", QDir(f.path).exists() ? QColor(180, 180, 180) : QColor(220, 90, 90)));
        top->setData(0, FolderRole, f.id);
        if (!QDir(f.path).exists())
            top->setToolTip(0, QStringLiteral("Folder not found"));
        for (int i = 0; i < f.outputs.size(); ++i) {
            const WatchOutput &o = f.outputs[i];
            const FormatDef *fd = Formats::format(o.settings.format);
            auto *it =
                new QTreeWidgetItem(top, {fd ? fd->displayName : o.settings.format,
                                          o.presetId.isEmpty() ? QStringLiteral("Custom") : o.settings.presetName,
                                          QDir::toNativeSeparators(f.outputDirFor(o))});
            it->setData(0, FolderRole, f.id);
            it->setData(0, IndexRole, i);
            for (int c = 0; c < 3; ++c)
                it->setForeground(c, QColor(0x4d, 0xa3, 0xff));
        }
        top->setExpanded(true);
    }
}

void WatchFolderPanel::addFolder(const QString &pathIn)
{
    QString path = pathIn;
    if (path.isEmpty())
        path = QFileDialog::getExistingDirectory(this, QStringLiteral("Add Watch Folder"));
    if (path.isEmpty())
        return;
    const Preset p = PresetManager::instance().defaultPreset();
    m_manager->addFolder(path, WatchOutput{p.settings, p.id, {}});
}

void WatchFolderPanel::addOutput()
{
    QTreeWidgetItem *it = m_tree->currentItem();
    if (!it)
        return;
    const Preset p = PresetManager::instance().defaultPreset();
    m_manager->addOutput(it->data(0, FolderRole).toString(), WatchOutput{p.settings, p.id, {}});
}

void WatchFolderPanel::removeSelected()
{
    QTreeWidgetItem *it = m_tree->currentItem();
    if (!it)
        return;
    const QString fid = it->data(0, FolderRole).toString();
    if (it->parent())
        m_manager->removeOutput(fid, it->data(0, IndexRole).toInt());
    else
        m_manager->removeFolder(fid);
}

void WatchFolderPanel::chooseOutputDir(const QString &folderId, int index)
{
    const WatchFolder *f = m_manager->folder(folderId);
    if (!f || index < 0 || index >= f->outputs.size())
        return;
    WatchOutput o = f->outputs[index];
    const QString dir = QFileDialog::getExistingDirectory(this, QStringLiteral("Output Folder"), f->outputDirFor(o));
    if (dir.isEmpty())
        return;
    o.outputDir = dir;
    m_manager->setOutput(folderId, index, o);
}
