#include "FramePreview.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

FramePreview::FramePreview(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(320, 180);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMouseTracking(true);
}

void FramePreview::setImage(const QImage &img)
{
    if (img.size() != m_image.size())
        m_center = QPointF(img.width() / 2.0, img.height() / 2.0);
    m_image = img;
    m_message.clear();
    clampPan();
    update();
}

void FramePreview::setMessage(const QString &msg)
{
    m_message = msg;
    update();
}

void FramePreview::setCropOverlay(bool enabled, int left, int top, int right, int bottom)
{
    m_crop = enabled;
    m_cl = left;
    m_ct = top;
    m_cr = right;
    m_cb = bottom;
    update();
}

void FramePreview::setBusy(bool busy, const QString &text)
{
    m_busy = busy;
    m_busyText = text;
    update();
}

void FramePreview::setCompare(bool on)
{
    m_compare = on;
    if (!on) {
        m_after = QImage();
        m_zoom = 0.0;
    }
    setCursor(Qt::ArrowCursor);
    update();
}

void FramePreview::setCompareImage(const QImage &after)
{
    m_after = after;
    update();
}

void FramePreview::setCompareLabels(const QString &before, const QString &after)
{
    m_beforeLabel = before;
    m_afterLabel = after;
    update();
}

void FramePreview::setZoom(double zoom)
{
    m_zoom = qMax(0.0, zoom);
    clampPan();
    update();
}

QRectF FramePreview::imageRect() const
{
    if (m_image.isNull())
        return {};
    if (m_zoom <= 0.0) {
        const QSize sz = m_image.size().scaled(size() - QSize(16, 16), Qt::KeepAspectRatio);
        return QRectF(QPointF((width() - sz.width()) / 2.0, (height() - sz.height()) / 2.0), QSizeF(sz));
    }
    // Zoom is in physical pixels, so 100% shows every pixel of the frame on high-DPI screens too.
    const double scale = m_zoom / devicePixelRatioF();
    const QSizeF sz(m_image.width() * scale, m_image.height() * scale);
    return QRectF(QPointF(width() / 2.0 - m_center.x() * scale, height() / 2.0 - m_center.y() * scale), sz);
}

void FramePreview::clampPan()
{
    if (m_image.isNull() || m_zoom <= 0.0)
        return;
    const double scale = m_zoom / devicePixelRatioF();
    const double halfW = width() / 2.0 / scale;
    const double halfH = height() / 2.0 / scale;
    // Keep the frame covering the view where it is larger than the view, centred where it isn't.
    auto clampAxis = [](double c, double half, double extent) {
        return extent <= half * 2 ? extent / 2.0 : qBound(half, c, extent - half);
    };
    m_center.setX(clampAxis(m_center.x(), halfW, m_image.width()));
    m_center.setY(clampAxis(m_center.y(), halfH, m_image.height()));
}

double FramePreview::dividerX() const
{
    const QRectF r = imageRect();
    return qBound(0.0, r.left() + r.width() * m_split, double(width()));
}

void FramePreview::resizeEvent(QResizeEvent *e)
{
    QWidget::resizeEvent(e);
    clampPan();
}

void FramePreview::mousePressEvent(QMouseEvent *e)
{
    if (!m_compare || m_image.isNull() || e->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(e);
        return;
    }
    m_dragStart = e->position();
    m_centerStart = m_center;
    if (qAbs(e->position().x() - dividerX()) <= 10 || m_zoom <= 0.0) {
        m_drag = Drag::Divider;
        mouseMoveEvent(e);
    } else {
        m_drag = Drag::Pan;
        setCursor(Qt::ClosedHandCursor);
    }
}

void FramePreview::mouseMoveEvent(QMouseEvent *e)
{
    if (!m_compare || m_image.isNull()) {
        QWidget::mouseMoveEvent(e);
        return;
    }
    if (m_drag == Drag::Divider) {
        const QRectF r = imageRect();
        if (r.width() > 0)
            m_split = qBound(0.0, (e->position().x() - r.left()) / r.width(), 1.0);
        update();
    } else if (m_drag == Drag::Pan) {
        const double scale = m_zoom / devicePixelRatioF();
        m_center = m_centerStart - (e->position() - m_dragStart) / scale;
        clampPan();
        update();
    } else {
        setCursor(qAbs(e->position().x() - dividerX()) <= 10 ? Qt::SplitHCursor
                  : m_zoom > 0.0                             ? Qt::OpenHandCursor
                                                             : Qt::ArrowCursor);
    }
}

void FramePreview::mouseReleaseEvent(QMouseEvent *e)
{
    m_drag = Drag::None;
    if (m_compare)
        setCursor(qAbs(e->position().x() - dividerX()) <= 10 ? Qt::SplitHCursor : Qt::ArrowCursor);
    QWidget::mouseReleaseEvent(e);
}

void FramePreview::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(0x0e, 0x0e, 0x0e));
    if (!m_image.isNull()) {
        const QRectF target = imageRect();
        // Checkerboard behind transparent frames.
        if (m_image.hasAlphaChannel()) {
            const int c = 10;
            const QRect t = target.toAlignedRect().intersected(rect());
            for (int y = t.top(); y < t.bottom(); y += c)
                for (int x = t.left(); x < t.right(); x += c)
                    p.fillRect(QRect(x, y, c, c).intersected(t),
                               ((x / c + y / c) & 1) ? QColor(60, 60, 60) : QColor(90, 90, 90));
        }
        // Zoomed in, show real pixels so compression artefacts are visible; fitted, scale smoothly.
        p.setRenderHint(QPainter::SmoothPixmapTransform, m_zoom <= 0.0 || m_zoom < 1.0);
        if (m_compare && !m_after.isNull()) {
            const double x = dividerX();
            p.save();
            p.setClipRect(QRectF(0, 0, x, height()));
            p.drawImage(target, m_image);
            p.setClipRect(QRectF(x, 0, width() - x, height()));
            p.drawImage(target, m_after);
            p.restore();
            p.setPen(QPen(QColor(255, 255, 255, 220), 2));
            p.drawLine(QPointF(x, 0), QPointF(x, height()));
            p.setRenderHint(QPainter::Antialiasing);
            p.setBrush(QColor(0x2d, 0x8c, 0xeb));
            p.drawEllipse(QPointF(x, height() / 2.0), 9, 9);
            p.setPen(QPen(Qt::white, 2));
            const double cy = height() / 2.0;
            p.drawLine(QPointF(x - 4, cy - 3), QPointF(x - 6, cy));
            p.drawLine(QPointF(x - 6, cy), QPointF(x - 4, cy + 3));
            p.drawLine(QPointF(x + 4, cy - 3), QPointF(x + 6, cy));
            p.drawLine(QPointF(x + 6, cy), QPointF(x + 4, cy + 3));
            p.setRenderHint(QPainter::Antialiasing, false);

            auto tag = [&](const QString &text, bool right) {
                const QFontMetrics fm(font());
                const QRect box(0, 0, fm.horizontalAdvance(text) + 16, fm.height() + 8);
                QRect r = box.translated(right ? width() - box.width() - 10 : 10, 10);
                p.fillRect(r, QColor(0, 0, 0, 170));
                p.setPen(Qt::white);
                p.drawText(r, Qt::AlignCenter, text);
            };
            tag(m_beforeLabel, false);
            tag(m_afterLabel, true);
        } else {
            p.drawImage(target, m_image);
        }

        if (m_crop && !m_compare) {
            const double sx = target.width() / m_image.width();
            const double sy = target.height() / m_image.height();
            const QRectF inner(target.left() + m_cl * sx, target.top() + m_ct * sy, target.width() - (m_cl + m_cr) * sx,
                               target.height() - (m_ct + m_cb) * sy);
            QPainterPath outer;
            outer.addRect(target);
            QPainterPath in;
            in.addRect(inner);
            p.fillPath(outer.subtracted(in), QColor(0, 0, 0, 150));
            p.setPen(QPen(QColor(0x2d, 0x8c, 0xeb), 2));
            p.drawRect(inner);
        }
        if (!m_compare) {
            p.setPen(QColor(150, 150, 150));
            p.drawText(rect().adjusted(8, 4, -8, -4), Qt::AlignRight | Qt::AlignTop,
                       QStringLiteral("%1 × %2").arg(m_image.width()).arg(m_image.height()));
        }
    }
    if (!m_message.isEmpty() || (m_image.isNull() && !m_busy)) {
        const QRect box = rect().adjusted(20, 20, -20, -20);
        if (!m_image.isNull()) {
            // A message over a frame (e.g. "run a test encode") gets a backdrop to stay readable.
            const QRect tr = p.fontMetrics().boundingRect(box, Qt::AlignCenter | Qt::TextWordWrap, m_message);
            p.fillRect(tr.adjusted(-14, -10, 14, 10), QColor(0, 0, 0, 190));
        }
        p.setPen(QColor(170, 170, 170));
        p.drawText(box, Qt::AlignCenter | Qt::TextWordWrap,
                   m_message.isEmpty() ? QStringLiteral("No preview") : m_message);
    }
    if (m_busy) {
        p.setPen(QColor(0x2d, 0x8c, 0xeb));
        p.drawText(rect().adjusted(8, 4, -8, -4), Qt::AlignLeft | Qt::AlignBottom,
                   m_busyText.isEmpty() ? QStringLiteral("Rendering preview…") : m_busyText);
    }
}
