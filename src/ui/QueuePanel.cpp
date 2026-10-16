#include "QueuePanel.h"

#include "Theme.h"
#include "core/AppSettings.h"
#include "core/EncoderCaps.h"
#include "core/Formats.h"
#include "core/PresetManager.h"
#include "core/QueueModel.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QToolButton>
#include <QVBoxLayout>

// ================================================================= view

void QueueView::paintEvent(QPaintEvent *e)
{
    QTreeView::paintEvent(e);
    if (model() && model()->rowCount() == 0) {
        QPainter p(viewport());
        p.setPen(QColor(120, 120, 120));
        QFont f = font();
        f.setPointSizeF(f.pointSizeF() + 2);
        p.setFont(f);
        p.drawText(viewport()->rect().adjusted(40, 40, -40, -40), Qt::AlignCenter | Qt::TextWordWrap,
                   QStringLiteral("To add items to the queue, drag media files here\nor from the Media Browser, "
                                  "or click + Add Source.\n\nDrag presets from the Preset Browser onto a source "
                                  "to add more outputs."));
    }
}

void QueueView::mouseReleaseEvent(QMouseEvent *e)
{
    const QModelIndex idx = indexAt(e->pos());
    const bool wasEditing = state() == EditingState;
    QTreeView::mouseReleaseEvent(e);
    if (e->button() == Qt::LeftButton && idx.isValid() && !wasEditing)
        emit itemClicked(idx, e->pos());
}

// ================================================================= delegate

namespace {
// Draws only the row background / selection for a cell we paint ourselves.
void paintBackground(QPainter *p, const QStyleOptionViewItem &opt)
{
    QStyleOptionViewItem bg(opt);
    bg.text.clear();
    bg.icon = QIcon();
    const QWidget *w = opt.widget;
    (w ? w->style() : QApplication::style())->drawControl(QStyle::CE_ItemViewItem, &bg, p, w);
}
} // namespace

void QueueItemDelegate::paint(QPainter *p, const QStyleOptionViewItem &optIn, const QModelIndex &index) const
{
    QStyleOptionViewItem opt(optIn);
    initStyleOption(&opt, index);
    const bool isSource = index.data(QueueModel::IsSourceRole).toBool();

    if (isSource) {
        p->save();
        p->fillRect(opt.rect,
                    (opt.state & QStyle::State_Selected) ? QColor(0x1f, 0x4f, 0x80) : QColor(0x22, 0x22, 0x22));
        const QIcon ic = Theme::icon("film", QColor(160, 160, 160));
        const QRect iconRect(opt.rect.left() + 4, opt.rect.center().y() - 8, 16, 16);
        ic.paint(p, iconRect);
        p->setPen(opt.palette.color(QPalette::Text));
        QFont f = opt.font;
        f.setBold(true);
        p->setFont(f);
        const QString text = index.data(Qt::DisplayRole).toString();
        const QString name = text.section(QStringLiteral("    —    "), 0, 0);
        const QString rest = text.section(QStringLiteral("    —    "), 1);
        QRect r = opt.rect.adjusted(26, 0, -6, 0);
        p->drawText(r, Qt::AlignVCenter | Qt::AlignLeft, name);
        const int w = QFontMetrics(f).horizontalAdvance(name);
        p->setFont(opt.font);
        const QVariant fg = index.data(Qt::ForegroundRole);
        p->setPen(fg.isValid() ? fg.value<QColor>() : QColor(130, 130, 130));
        p->drawText(r.adjusted(w + 16, 0, 0, 0), Qt::AlignVCenter | Qt::AlignLeft,
                    QFontMetrics(opt.font).elidedText(rest, Qt::ElideRight, r.width() - w - 16));
        p->restore();
        return;
    }

    const int col = index.column();
    if (col == QueueModel::ColStatus) {
        const auto status = OutputStatus(index.data(QueueModel::StatusRole).toInt());
        if (status == OutputStatus::Encoding || status == OutputStatus::Paused) {
            paintBackground(p, opt);
            const double prog = index.data(QueueModel::ProgressRole).toDouble();
            QRect bar = opt.rect.adjusted(6, 6, -6, -6);
            p->save();
            p->setRenderHint(QPainter::Antialiasing);
            p->setPen(Qt::NoPen);
            p->setBrush(QColor(0x11, 0x11, 0x11));
            p->drawRoundedRect(bar, 3, 3);
            QRect fill = bar;
            fill.setWidth(int(bar.width() * prog));
            p->setBrush(status == OutputStatus::Paused ? QColor(0xc8, 0x96, 0x2d) : Theme::accent());
            p->drawRoundedRect(fill, 3, 3);
            p->setPen(Qt::white);
            p->drawText(bar, Qt::AlignCenter,
                        status == OutputStatus::Paused ? QStringLiteral("Paused %1%").arg(int(prog * 100))
                                                       : QStringLiteral("%1%").arg(int(prog * 100)));
            p->restore();
            return;
        }
    }

    if (col == QueueModel::ColFormat || col == QueueModel::ColPreset) {
        // Drop-down arrow on the left, like a combo affordance.
        paintBackground(p, opt);
        p->save();
        const QRect arrow(opt.rect.left() + 4, opt.rect.top(), ArrowWidth - 6, opt.rect.height());
        p->setRenderHint(QPainter::Antialiasing);
        p->setPen(Qt::NoPen);
        p->setBrush(QColor(170, 170, 170));
        const QPointF c = arrow.center();
        p->drawPolygon(
            QPolygonF({QPointF(c.x() - 4, c.y() - 2), QPointF(c.x() + 4, c.y() - 2), QPointF(c.x(), c.y() + 3)}));
        const QVariant fg = index.data(Qt::ForegroundRole);
        p->setPen(fg.isValid() ? fg.value<QColor>() : opt.palette.color(QPalette::Text));
        const QRect tr = opt.rect.adjusted(ArrowWidth + 2, 0, -4, 0);
        p->drawText(tr, Qt::AlignVCenter | Qt::AlignLeft,
                    opt.fontMetrics.elidedText(index.data().toString(), Qt::ElideRight, tr.width()));
        p->restore();
        return;
    }
    if (col == QueueModel::ColOutput)
        opt.textElideMode = Qt::ElideMiddle;
    QStyledItemDelegate::paint(p, opt, index);
}

QSize QueueItemDelegate::sizeHint(const QStyleOptionViewItem &opt, const QModelIndex &index) const
{
    QSize s = QStyledItemDelegate::sizeHint(opt, index);
    s.setHeight(qMax(s.height(), index.data(QueueModel::IsSourceRole).toBool() ? 30 : 26));
    return s;
}

QWidget *QueueItemDelegate::createEditor(QWidget *parent, const QStyleOptionViewItem &, const QModelIndex &index) const
{
    auto *combo = new QComboBox(parent);
    if (index.column() == QueueModel::ColFormat) {
        for (const FormatDef &f : Formats::all()) {
            const bool usable = (!f.hasVideo() || !EncoderCaps::instance().availableVideoCodecs(f).isEmpty()) &&
                                (f.hasVideo() || !EncoderCaps::instance().availableAudioCodecs(f).isEmpty());
            if (usable)
                combo->addItem(f.displayName, f.id);
        }
    } else {
        const QString fmt = index.data(QueueModel::FormatIdRole).toString();
        for (const Preset &p : PresetManager::instance().presetsForFormat(fmt))
            combo->addItem(p.system ? p.name() : p.name() + QStringLiteral("  (user)"), p.id);
    }
    // Commit as soon as the user picks something.
    connect(combo, &QComboBox::activated, this, [this, combo] {
        auto *self = const_cast<QueueItemDelegate *>(this);
        emit self->commitData(combo);
        emit self->closeEditor(combo);
    });
    return combo;
}

void QueueItemDelegate::setEditorData(QWidget *editor, const QModelIndex &index) const
{
    auto *combo = static_cast<QComboBox *>(editor);
    const QVariant key = index.column() == QueueModel::ColFormat ? index.data(QueueModel::FormatIdRole)
                                                                 : index.data(QueueModel::PresetIdRole);
    const int i = combo->findData(key);
    if (i >= 0)
        combo->setCurrentIndex(i);
    combo->showPopup();
}

void QueueItemDelegate::setModelData(QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const
{
    auto *combo = static_cast<QComboBox *>(editor);
    model->setData(index, combo->currentData(), Qt::EditRole);
}

// ================================================================= panel

namespace {
QToolButton *toolButton(const QString &icon, const QString &tip, const QColor &color = {})
{
    auto *b = new QToolButton;
    b->setIcon(Theme::icon(icon, color));
    b->setIconSize(QSize(18, 18));
    b->setToolTip(tip);
    b->setAutoRaise(true);
    return b;
}
} // namespace

QueuePanel::QueuePanel(QueueModel *model, QWidget *parent)
    : QWidget(parent)
    , m_model(model)
{
    auto *bar = new QHBoxLayout;
    bar->setContentsMargins(6, 4, 6, 4);
    auto *add = toolButton("plus", QStringLiteral("Add Source (Ctrl+I)"));
    auto *addOut = toolButton("preset", QStringLiteral("Add Output to selected source"));
    auto *dup = toolButton("duplicate", QStringLiteral("Duplicate (Ctrl+D)"));
    auto *rem = toolButton("minus", QStringLiteral("Remove (Delete)"));
    connect(add, &QToolButton::clicked, this, &QueuePanel::addSourceClicked);
    connect(addOut, &QToolButton::clicked, this, &QueuePanel::addOutputClicked);
    connect(dup, &QToolButton::clicked, this, &QueuePanel::duplicateClicked);
    connect(rem, &QToolButton::clicked, this, &QueuePanel::removeClicked);
    bar->addWidget(add);
    bar->addWidget(addOut);
    bar->addWidget(dup);
    bar->addWidget(rem);
    bar->addSpacing(12);

    m_summary = new QLabel;
    m_summary->setObjectName("muted");
    bar->addWidget(m_summary, 1);

    m_autoEncode = new QCheckBox(QStringLiteral("Auto-Encode Watch Folders"));
    connect(m_autoEncode, &QCheckBox::toggled, this, &QueuePanel::autoEncodeToggled);
    bar->addWidget(m_autoEncode);
    bar->addSpacing(8);

    bar->addWidget(new QLabel(QStringLiteral("Renderer:")));
    m_renderer = new QComboBox;
    m_renderer->addItem(QStringLiteral("GPU Accelerated Decoding"), true);
    m_renderer->addItem(QStringLiteral("Software Only"), false);
    m_renderer->setCurrentIndex(AppSettings::hardwareDecode() ? 0 : 1);
    connect(m_renderer, &QComboBox::activated, this,
            [this] { AppSettings::setValue("encode/hwDecode", m_renderer->currentData().toBool()); });
    bar->addWidget(m_renderer);
    bar->addSpacing(8);

    m_start = toolButton("play", QStringLiteral("Start Queue (Enter)"), QColor(0x3d, 0xc7, 0x5a));
    m_pause = toolButton("pause", QStringLiteral("Pause"));
    m_stop = toolButton("stop", QStringLiteral("Stop"), QColor(0xe0, 0x55, 0x55));
    for (QToolButton *b : {m_start, m_pause, m_stop})
        b->setIconSize(QSize(24, 24));
    connect(m_start, &QToolButton::clicked, this, &QueuePanel::startClicked);
    connect(m_pause, &QToolButton::clicked, this, &QueuePanel::pauseClicked);
    connect(m_stop, &QToolButton::clicked, this, &QueuePanel::stopClicked);
    bar->addWidget(m_pause);
    bar->addWidget(m_stop);
    bar->addWidget(m_start);

    m_view = new QueueView;
    m_view->setModel(model);
    m_view->setItemDelegate(new QueueItemDelegate(m_view));
    m_view->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_view->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_view->setAcceptDrops(true);
    m_view->setDragDropMode(QAbstractItemView::DropOnly);
    m_view->setDropIndicatorShown(true);
    m_view->setDefaultDropAction(Qt::CopyAction);
    m_view->setRootIsDecorated(true);
    m_view->setUniformRowHeights(false);
    m_view->setAllColumnsShowFocus(true);
    m_view->setContextMenuPolicy(Qt::CustomContextMenu);
    m_view->header()->setStretchLastSection(false);
    m_view->header()->setSectionResizeMode(QueueModel::ColFormat, QHeaderView::Interactive);
    m_view->header()->setSectionResizeMode(QueueModel::ColPreset, QHeaderView::Interactive);
    m_view->header()->setSectionResizeMode(QueueModel::ColOutput, QHeaderView::Stretch);
    m_view->header()->setSectionResizeMode(QueueModel::ColStatus, QHeaderView::Interactive);
    m_view->setColumnWidth(QueueModel::ColFormat, 170);
    m_view->setColumnWidth(QueueModel::ColPreset, 280);
    m_view->setColumnWidth(QueueModel::ColStatus, 170);

    connect(m_view, &QWidget::customContextMenuRequested, this,
            [this](const QPoint &pos) { emit contextMenuRequested(m_view->viewport()->mapToGlobal(pos)); });
    connect(m_view, &QueueView::itemClicked, this, [this](const QModelIndex &idx, const QPoint &pos) {
        if (idx.data(QueueModel::IsSourceRole).toBool())
            return;
        const auto status = OutputStatus(idx.data(QueueModel::StatusRole).toInt());
        if (status == OutputStatus::Encoding || status == OutputStatus::Paused)
            return;
        const QString id = idx.data(QueueModel::IdRole).toString();
        const QRect r = m_view->visualRect(idx);
        if (idx.column() == QueueModel::ColFormat || idx.column() == QueueModel::ColPreset) {
            if (pos.x() < r.left() + QueueItemDelegate::ArrowWidth)
                m_view->edit(idx);
            else
                emit editOutputRequested(id);
        } else if (idx.column() == QueueModel::ColOutput) {
            emit chooseOutputPathRequested(id);
        }
    });
    connect(m_view, &QTreeView::doubleClicked, this, [this](const QModelIndex &idx) {
        if (!idx.data(QueueModel::IsSourceRole).toBool() && idx.column() == QueueModel::ColStatus)
            emit editOutputRequested(idx.data(QueueModel::IdRole).toString());
    });

    connect(model, &QAbstractItemModel::rowsInserted, this, [this](const QModelIndex &parent) {
        spanSourceRows();
        if (parent.isValid())
            m_view->expand(parent);
    });
    connect(model, &QAbstractItemModel::modelReset, this, [this] {
        spanSourceRows();
        m_view->expandAll();
    });
    connect(model, &QAbstractItemModel::rowsRemoved, this, &QueuePanel::spanSourceRows);
    connect(model, &QueueModel::sourceAdded, this,
            [this](const QString &sid) { m_view->expand(m_model->indexForSource(sid)); });
    connect(model, &QueueModel::queueChanged, this, &QueuePanel::updateSummary);

    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);
    auto *barWidget = new QWidget;
    barWidget->setLayout(bar);
    barWidget->setStyleSheet(QStringLiteral("background:#232323; border-bottom:1px solid #111;"));
    v->addWidget(barWidget);
    v->addWidget(m_view, 1);

    setEngineState(false, false);
    updateSummary();
}

void QueuePanel::spanSourceRows()
{
    for (int r = 0; r < m_model->rowCount(); ++r)
        m_view->setFirstColumnSpanned(r, QModelIndex(), true);
}

QModelIndexList QueuePanel::selectedRows() const { return m_view->selectionModel()->selectedRows(0); }

void QueuePanel::setEngineState(bool running, bool paused)
{
    m_start->setEnabled(!running || paused);
    m_start->setToolTip(paused ? QStringLiteral("Resume Queue") : QStringLiteral("Start Queue (Enter)"));
    m_pause->setEnabled(running && !paused);
    m_stop->setEnabled(running);
}

void QueuePanel::setAutoEncode(bool on)
{
    QSignalBlocker b(m_autoEncode);
    m_autoEncode->setChecked(on);
}

void QueuePanel::updateSummary()
{
    const int total = m_model->outputCount();
    const int ready = m_model->countWithStatus(OutputStatus::Ready);
    const int done = m_model->countWithStatus(OutputStatus::Done);
    const int failed = m_model->countWithStatus(OutputStatus::Failed);
    QString s = QStringLiteral("%1 output%2  ·  %3 ready  ·  %4 done")
                    .arg(total)
                    .arg(total == 1 ? "" : "s")
                    .arg(ready)
                    .arg(done);
    if (failed)
        s += QStringLiteral("  ·  %1 failed").arg(failed);
    m_summary->setText(total ? s : QString());
}
