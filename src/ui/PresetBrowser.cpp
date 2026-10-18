#include "PresetBrowser.h"

#include "Theme.h"
#include "core/Formats.h"
#include "core/PresetManager.h"

#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLineEdit>
#include <QMessageBox>
#include <QMimeData>
#include <QToolButton>
#include <QVBoxLayout>
#include <cmath>
#include <functional>

namespace {
constexpr int IdRole = Qt::UserRole + 1;
constexpr int GroupRole = Qt::UserRole + 2;

QToolButton *tb(const QString &icon, const QString &tip)
{
    auto *b = new QToolButton;
    b->setIcon(Theme::icon(icon));
    b->setIconSize(QSize(16, 16));
    b->setToolTip(tip);
    b->setAutoRaise(true);
    return b;
}

QStringList columnsFor(const Preset &p)
{
    const ExportSettings &s = p.settings;
    const FormatDef *f = Formats::format(s.format);
    QString size = QStringLiteral("—"), fps = QStringLiteral("—"), rate = QStringLiteral("—");
    if (f && f->hasVideo()) {
        size = (s.video.width > 0 || s.video.height > 0)
                   ? QStringLiteral("%1x%2").arg(s.video.width > 0 ? QString::number(s.video.width) : "auto",
                                                 s.video.height > 0 ? QString::number(s.video.height) : "auto")
                   : QStringLiteral("Match Source");
        fps = s.video.frameRate > 0 ? QString::number(s.video.frameRate, 'g', 5) : QStringLiteral("Match Source");
        if (s.video.rateControl == "cbr" || s.video.rateControl == "vbr1" || s.video.rateControl == "vbr2")
            rate = QStringLiteral("%1 Mbps").arg(s.video.targetBitrate, 0, 'g', 4);
        else if (s.video.rateControl == "quality")
            rate = QStringLiteral("Quality %1").arg(s.video.quality);
    } else if (f && f->hasAudio()) {
        const AudioCodecDef *a = Formats::audioCodec(s.audio.codec);
        rate = a && !a->lossless ? QStringLiteral("%1 kbps").arg(s.audio.bitrate) : QStringLiteral("Uncompressed");
    }
    return {p.name(), f ? f->displayName : s.format, size, fps, rate, s.comment};
}
} // namespace

QStringList PresetTree::mimeTypes() const { return {QString::fromLatin1(PresetManager::MimeType)}; }

QMimeData *PresetTree::mimeData(const QList<QTreeWidgetItem *> &items) const
{
    for (QTreeWidgetItem *it : items) {
        const QString id = it->data(0, IdRole).toString();
        if (!id.isEmpty()) {
            auto *m = new QMimeData;
            m->setData(PresetManager::MimeType, id.toUtf8());
            return m;
        }
    }
    return nullptr;
}

PresetBrowser::PresetBrowser(QWidget *parent)
    : QWidget(parent)
{
    auto *bar = new QHBoxLayout;
    bar->setContentsMargins(4, 4, 4, 4);
    auto *add = tb("plus", QStringLiteral("New Preset"));
    auto *del = tb("minus", QStringLiteral("Delete User Preset"));
    auto *edit = tb("settings", QStringLiteral("Preset Settings"));
    auto *imp = tb("import", QStringLiteral("Import Preset"));
    auto *exp = tb("export", QStringLiteral("Export Preset"));
    auto *apply = tb("play", QStringLiteral("Apply Preset to queue selection"));
    connect(add, &QToolButton::clicked, this, &PresetBrowser::newPresetRequested);
    connect(del, &QToolButton::clicked, this, &PresetBrowser::deleteSelected);
    connect(edit, &QToolButton::clicked, this, [this] {
        if (!selectedPresetId().isEmpty())
            emit editPresetRequested(selectedPresetId());
    });
    connect(imp, &QToolButton::clicked, this, &PresetBrowser::importPreset);
    connect(exp, &QToolButton::clicked, this, &PresetBrowser::exportSelected);
    connect(apply, &QToolButton::clicked, this, [this] {
        if (!selectedPresetId().isEmpty())
            emit applyPresetRequested(selectedPresetId());
    });
    for (auto *b : {add, del, edit, imp, exp})
        bar->addWidget(b);
    bar->addStretch();
    m_search = new QLineEdit;
    m_search->setPlaceholderText(QStringLiteral("Search presets"));
    m_search->addAction(Theme::icon("search", QColor(130, 130, 130)), QLineEdit::LeadingPosition);
    m_search->setClearButtonEnabled(true);
    m_search->setMaximumWidth(220);
    connect(m_search, &QLineEdit::textChanged, this, &PresetBrowser::applyFilter);
    bar->addWidget(m_search);
    bar->addWidget(apply);

    m_tree = new PresetTree;
    m_tree->setColumnCount(6);
    m_tree->setHeaderLabels({QStringLiteral("Preset Name"), QStringLiteral("Format"), QStringLiteral("Frame Size"),
                             QStringLiteral("Frame Rate"), QStringLiteral("Target Rate"), QStringLiteral("Comment")});
    m_tree->setDragEnabled(true);
    m_tree->setDragDropMode(QAbstractItemView::DragOnly);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->setAlternatingRowColors(true);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Interactive);
    m_tree->setColumnWidth(0, 300);
    m_tree->setColumnWidth(1, 110);
    m_tree->setColumnWidth(2, 110);
    m_tree->setColumnWidth(3, 95);
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *it) {
        const QString id = it->data(0, IdRole).toString();
        if (!id.isEmpty())
            emit applyPresetRequested(id);
    });

    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);
    v->addLayout(bar);
    v->addWidget(m_tree, 1);

    connect(&PresetManager::instance(), &PresetManager::presetsChanged, this, &PresetBrowser::reload);
    reload();
}

void PresetBrowser::reload()
{
    const QString selected = selectedPresetId();
    QSet<QString> expanded;
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *top = m_tree->topLevelItem(i);
        if (top->isExpanded())
            expanded << top->text(0);
        for (int j = 0; j < top->childCount(); ++j)
            if (top->child(j)->isExpanded())
                expanded << top->text(0) + "/" + top->child(j)->text(0);
    }
    const bool first = m_tree->topLevelItemCount() == 0;
    m_tree->clear();

    auto *user = new QTreeWidgetItem(m_tree, {QStringLiteral("User Presets")});
    auto *web = new QTreeWidgetItem(m_tree, {QStringLiteral("Web & Social")});
    auto *system = new QTreeWidgetItem(m_tree, {QStringLiteral("System Presets")});
    for (auto *top : {user, web, system}) {
        QFont f = top->font(0);
        f.setBold(true);
        top->setFont(0, f);
        top->setFlags(Qt::ItemIsEnabled);
    }
    QHash<QString, QTreeWidgetItem *> formatGroups;
    for (const FormatDef &f : Formats::all()) {
        auto *g = new QTreeWidgetItem(system, {f.displayName});
        g->setFlags(Qt::ItemIsEnabled);
        g->setIcon(0, Theme::icon(f.hasVideo() ? (f.imageFormat ? "image" : "film") : "audio", QColor(150, 150, 150)));
        formatGroups.insert(f.id, g);
    }
    // Platform groups under "Web & Social" in a fixed, familiar order; user groups under "User Presets".
    static const QStringList platformOrder = {
        "YouTube",  "Vimeo",    "TikTok",    "Instagram",        "Facebook",        "X (Twitter)",
        "LinkedIn", "Snapchat", "Pinterest", "Social (Generic)", "Fit to File Size"};
    QHash<QString, QTreeWidgetItem *> webGroups;
    for (const QString &g : platformOrder) {
        auto *item = new QTreeWidgetItem(web, {g});
        item->setFlags(Qt::ItemIsEnabled);
        item->setIcon(0, Theme::icon("film", QColor(150, 150, 150)));
        webGroups.insert(g, item);
    }
    QHash<QString, QTreeWidgetItem *> userGroups;
    auto groupItem = [&](QHash<QString, QTreeWidgetItem *> &groups, QTreeWidgetItem *parent, const QString &name) {
        QTreeWidgetItem *item = groups.value(name);
        if (!item) {
            item = new QTreeWidgetItem(parent, {name});
            item->setFlags(Qt::ItemIsEnabled);
            item->setIcon(0, Theme::icon("folder", QColor(150, 150, 150)));
            groups.insert(name, item);
        }
        return item;
    };
    for (const Preset &p : PresetManager::instance().presets()) {
        QTreeWidgetItem *parentItem = nullptr;
        const QString group = p.settings.group;
        if (!p.system)
            parentItem = group.isEmpty() ? user : groupItem(userGroups, user, group);
        else if (!group.isEmpty())
            parentItem = groupItem(webGroups, web, group);
        else
            parentItem = formatGroups.value(p.settings.format);
        if (!parentItem)
            continue;
        auto *it = new QTreeWidgetItem(parentItem, columnsFor(p));
        it->setData(0, IdRole, p.id);
        it->setData(0, GroupRole, group);
        it->setIcon(0, Theme::icon("preset", p.system ? QColor(140, 140, 140) : Theme::accent()));
        it->setToolTip(0, p.settings.summary());
        it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled);
        if (p.id == selected)
            it->setSelected(true);
    }
    // Hide empty format groups.
    for (auto *g : std::as_const(formatGroups))
        if (g->childCount() == 0)
            delete g;
    for (auto *g : std::as_const(webGroups))
        if (g->childCount() == 0)
            delete g;

    if (first) {
        user->setExpanded(true);
        web->setExpanded(true);
        system->setExpanded(true);
    } else {
        for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
            QTreeWidgetItem *top = m_tree->topLevelItem(i);
            top->setExpanded(expanded.contains(top->text(0)));
            for (int j = 0; j < top->childCount(); ++j)
                top->child(j)->setExpanded(expanded.contains(top->text(0) + "/" + top->child(j)->text(0)));
        }
    }
    applyFilter();
}

void PresetBrowser::applyFilter()
{
    const QString q = m_search->text().trimmed();
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *top = m_tree->topLevelItem(i);
        std::function<bool(QTreeWidgetItem *)> visit = [&](QTreeWidgetItem *it) -> bool {
            if (it->childCount() == 0) {
                const bool match = q.isEmpty() || it->text(0).contains(q, Qt::CaseInsensitive) ||
                                   it->text(1).contains(q, Qt::CaseInsensitive) ||
                                   it->data(0, GroupRole).toString().contains(q, Qt::CaseInsensitive);
                it->setHidden(!match);
                return match;
            }
            bool any = false;
            for (int j = 0; j < it->childCount(); ++j)
                any = visit(it->child(j)) || any;
            it->setHidden(!any && !q.isEmpty());
            if (!q.isEmpty() && any)
                it->setExpanded(true);
            return any;
        };
        visit(top);
        top->setHidden(false);
    }
}

QString PresetBrowser::selectedPresetId() const
{
    const auto items = m_tree->selectedItems();
    return items.isEmpty() ? QString() : items.first()->data(0, IdRole).toString();
}

void PresetBrowser::deleteSelected()
{
    const Preset *p = PresetManager::instance().find(selectedPresetId());
    if (!p)
        return;
    if (p->system) {
        QMessageBox::information(this, QStringLiteral("Delete Preset"),
                                 QStringLiteral("System presets can't be deleted."));
        return;
    }
    if (QMessageBox::question(this, QStringLiteral("Delete Preset"), QStringLiteral("Delete \"%1\"?").arg(p->name())) ==
        QMessageBox::Yes)
        PresetManager::instance().removeUserPreset(p->id);
}

void PresetBrowser::importPreset()
{
    const QStringList files = QFileDialog::getOpenFileNames(this, QStringLiteral("Import Presets"), QString(),
                                                            QStringLiteral("Presets (*.json)"));
    for (const QString &f : files) {
        QString err;
        if (!PresetManager::instance().importPreset(f, &err))
            QMessageBox::warning(this, QStringLiteral("Import Preset"), QStringLiteral("%1:\n%2").arg(f, err));
    }
}

void PresetBrowser::exportSelected()
{
    const Preset *p = PresetManager::instance().find(selectedPresetId());
    if (!p)
        return;
    QString name = p->name();
    name.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|]")), QStringLiteral("_"));
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Export Preset"), name + ".json",
                                                      QStringLiteral("Presets (*.json)"));
    if (path.isEmpty())
        return;
    QString err;
    if (!PresetManager::instance().exportPreset(p->id, path, &err))
        QMessageBox::warning(this, QStringLiteral("Export Preset"), err);
}
