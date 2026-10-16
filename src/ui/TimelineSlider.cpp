#include "TimelineSlider.h"

#include "Theme.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <cmath>

TimelineSlider::TimelineSlider(QWidget *parent)
    : QWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::PointingHandCursor);
    setToolTip(QStringLiteral("Drag to scrub. Left/Right step one frame (Shift: one second). I / O set In and Out."));
}

void TimelineSlider::setSpan(double start, double end)
{
    m_start = start;
    m_end = qMax(end, start + 0.001);
    m_pos = qBound(m_start, m_pos, m_end);
    update();
}

void TimelineSlider::setMarks(double in, double out)
{
    m_in = in;
    m_out = out;
    update();
}

void TimelineSlider::setShowMarks(bool show)
{
    m_showMarks = show;
    update();
}

void TimelineSlider::setPosition(double seconds)
{
    m_pos = qBound(m_start, seconds, m_end);
    update();
}

QRectF TimelineSlider::track() const { return QRectF(8, height() / 2.0 - 3, width() - 16, 6); }

double TimelineSlider::timeToX(double t) const
{
    const QRectF r = track();
    return r.left() + (t - m_start) / (m_end - m_start) * r.width();
}

double TimelineSlider::xToTime(double x) const
{
    const QRectF r = track();
    return m_start + qBound(0.0, (x - r.left()) / r.width(), 1.0) * (m_end - m_start);
}

void TimelineSlider::userSet(double seconds)
{
    const double t = qBound(m_start, seconds, m_end);
    if (std::abs(t - m_pos) < 1e-9)
        return;
    m_pos = t;
    update();
    emit positionChanged(m_pos);
}

void TimelineSlider::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = track();
    const QColor accent = Theme::accent();
    const bool enabled = isEnabled();

    // Track
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0x33, 0x33, 0x33));
    p.drawRoundedRect(r, 3, 3);

    // Selected range (In..Out) and dimmed remainder
    const bool hasIn = m_in >= 0;
    const bool hasOut = m_out > 0;
    if (m_showMarks && (hasIn || hasOut)) {
        const double x0 = timeToX(hasIn ? m_in : m_start);
        const double x1 = timeToX(hasOut ? m_out : m_end);
        p.setBrush(enabled ? QColor(accent.red(), accent.green(), accent.blue(), 150) : QColor(90, 90, 90));
        p.drawRect(QRectF(QPointF(x0, r.top()), QPointF(x1, r.bottom())));
    } else {
        p.setBrush(enabled ? QColor(accent.red(), accent.green(), accent.blue(), 110) : QColor(90, 90, 90));
        p.drawRoundedRect(r, 3, 3);
    }

    // Ruler ticks: one per second (or coarser) so the scale reads at a glance.
    const double span = m_end - m_start;
    double step = 1.0;
    for (double s : {1.0, 2.0, 5.0, 10.0, 15.0, 30.0, 60.0, 120.0, 300.0, 600.0, 1800.0, 3600.0}) {
        step = s;
        if (r.width() / (span / s) >= 12.0)
            break;
    }
    p.setPen(QPen(QColor(0x5a, 0x5a, 0x5a), 1));
    for (double t = std::ceil(m_start / step) * step; t <= m_end + 1e-9; t += step) {
        const double x = timeToX(t);
        p.drawLine(QPointF(x, r.bottom() + 3), QPointF(x, r.bottom() + 6));
    }

    // In / Out tick marks
    auto mark = [&](double t, bool isIn) {
        const double x = timeToX(t);
        p.setPen(QPen(QColor(0xf5, 0xc5, 0x42), 2));
        p.drawLine(QPointF(x, r.top() - 7), QPointF(x, r.bottom() + 7));
        QPainterPath flag;
        flag.moveTo(x, r.top() - 7);
        flag.lineTo(isIn ? x + 6 : x - 6, r.top() - 7);
        flag.lineTo(x, r.top() - 1);
        flag.closeSubpath();
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0xf5, 0xc5, 0x42));
        p.drawPath(flag);
    };
    if (m_showMarks) {
        if (hasIn)
            mark(m_in, true);
        if (hasOut)
            mark(m_out, false);
    }

    // Playhead
    const double x = timeToX(m_pos);
    p.setPen(QPen(enabled ? Qt::white : QColor(120, 120, 120), 2));
    p.drawLine(QPointF(x, 3), QPointF(x, height() - 3));
    p.setPen(Qt::NoPen);
    p.setBrush(enabled ? Qt::white : QColor(120, 120, 120));
    p.drawEllipse(QPointF(x, r.center().y()), 5, 5);
    if (hasFocus()) {
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor(accent.red(), accent.green(), accent.blue(), 120), 1));
        p.drawRoundedRect(rect().adjusted(1, 1, -1, -1), 3, 3);
    }
}

void TimelineSlider::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton)
        userSet(xToTime(e->position().x()));
}

void TimelineSlider::mouseMoveEvent(QMouseEvent *e)
{
    if (e->buttons() & Qt::LeftButton)
        userSet(xToTime(e->position().x()));
}

void TimelineSlider::keyPressEvent(QKeyEvent *e)
{
    const double frame = 1.0 / m_fps;
    const double step = (e->modifiers() & Qt::ShiftModifier) ? 1.0 : frame;
    switch (e->key()) {
    case Qt::Key_Left:
        userSet(m_pos - step);
        break;
    case Qt::Key_Right:
        userSet(m_pos + step);
        break;
    case Qt::Key_Home:
        userSet(m_start);
        break;
    case Qt::Key_End:
        userSet(m_end);
        break;
    default:
        QWidget::keyPressEvent(e);
        return;
    }
    e->accept();
}
