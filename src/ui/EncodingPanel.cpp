#include "EncodingPanel.h"

#include "FrameGrabber.h"
#include "core/Formats.h"
#include "core/QueueModel.h"

#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QScrollArea>
#include <QVBoxLayout>

EncodingPanel::EncodingPanel(QueueModel *model, QWidget *parent)
    : QWidget(parent)
    , m_model(model)
{
    auto *content = new QWidget;
    m_list = new QVBoxLayout(content);
    m_list->setContentsMargins(8, 8, 8, 8);
    m_list->setSpacing(8);
    m_idle = new QLabel(QStringLiteral("Not currently encoding."));
    m_idle->setObjectName("muted");
    m_idle->setAlignment(Qt::AlignCenter);
    m_list->addWidget(m_idle);
    m_list->addStretch();

    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(content);
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(0, 0, 0, 0);
    v->addWidget(scroll);
}

void EncodingPanel::onTaskStarted(const QString &sourceId, const QString &outputId)
{
    const SourceItem *src = m_model->source(sourceId);
    const OutputItem *out = m_model->output(outputId);
    if (!src || !out)
        return;

    Row row;
    row.widget = new QFrame;
    row.widget->setObjectName("card");
    auto *h = new QHBoxLayout(row.widget);
    row.thumb = new QLabel;
    row.thumb->setFixedSize(144, 81);
    row.thumb->setAlignment(Qt::AlignCenter);
    row.thumb->setStyleSheet(QStringLiteral("background:#0e0e0e; border:none;"));
    h->addWidget(row.thumb);

    auto *v = new QVBoxLayout;
    row.title = new QLabel(QStringLiteral("<b>%1</b>").arg(src->displayName().toHtmlEscaped()));
    const FormatDef *f = Formats::format(out->settings.format);
    row.detail = new QLabel(QStringLiteral("%1  ·  %2  →  %3")
                                .arg(f ? f->displayName : out->settings.format, out->settings.presetName,
                                     QFileInfo(out->finalPath.isEmpty() ? out->outputPath : out->finalPath).fileName())
                                .toHtmlEscaped());
    row.detail->setObjectName("muted");
    row.bar = new QProgressBar;
    row.bar->setRange(0, 1000);
    row.bar->setTextVisible(false);
    row.stats = new QLabel(QStringLiteral("Starting…"));
    row.stats->setObjectName("muted");
    row.stats->setWordWrap(true);
    row.detail->setWordWrap(true);
    v->addWidget(row.title);
    v->addWidget(row.detail);
    v->addWidget(row.bar);
    v->addWidget(row.stats);
    h->addLayout(v, 1);

    if (src->info.hasVideo) {
        row.grabber = new FrameGrabber(row.widget);
        QLabel *thumb = row.thumb;
        connect(row.grabber, &FrameGrabber::frameReady, thumb, [thumb](const QImage &img) {
            thumb->setPixmap(
                QPixmap::fromImage(img.scaled(thumb->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation)));
        });
        BuildRequest req;
        req.inputs = {src->files.first()};
        req.inputArgs = src->inputArgs();
        req.source = src->info;
        req.settings = out->settings;
        const double t = out->inPoint > 0 ? out->inPoint : src->info.duration * 0.1;
        req.inPoint = t;
        row.grabber->request(req, src->info.isStill ? 0 : t, true);
    } else {
        row.thumb->setText(QStringLiteral("♪"));
    }

    m_list->insertWidget(m_list->count() - 1, row.widget);
    m_rows.insert(outputId, row);
    updateIdle();
}

void EncodingPanel::onTaskProgress(const QString &outputId, const EncodeStats &st)
{
    auto it = m_rows.find(outputId);
    if (it == m_rows.end())
        return;
    it->bar->setValue(int(st.progress * 1000));
    QStringList parts;
    parts << QStringLiteral("%1%").arg(st.progress * 100, 0, 'f', 1);
    if (!st.phase.isEmpty())
        parts << st.phase; // analysis steps, passes, re-encodes
    else if (st.passes > 1)
        parts << QStringLiteral("Pass %1 of %2").arg(st.pass).arg(st.passes);
    parts << QStringLiteral("Elapsed %1").arg(formatDuration(st.elapsed));
    if (st.remaining >= 0)
        parts << QStringLiteral("Remaining %1").arg(formatDuration(st.remaining));
    if (st.fps > 0)
        parts << QStringLiteral("%1 fps").arg(st.fps, 0, 'f', 1);
    if (st.speed > 0)
        parts << QStringLiteral("%1x").arg(st.speed, 0, 'f', 2);
    if (st.bytes > 0)
        parts << formatBytes(st.bytes);
    it->stats->setText(parts.join(QStringLiteral("   ·   ")));
}

void EncodingPanel::onTaskFinished(const QString &, const QString &outputId, bool, const QString &)
{
    auto it = m_rows.find(outputId);
    if (it == m_rows.end())
        return;
    it->widget->deleteLater();
    m_rows.erase(it);
    updateIdle();
}

void EncodingPanel::updateIdle() { m_idle->setVisible(m_rows.isEmpty()); }
