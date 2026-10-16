#include "FramePreview.h"

#include <QPainter>
#include <QPainterPath>

FramePreview::FramePreview(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(320, 180);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void FramePreview::setImage(const QImage &img)
{
    m_image = img;
    m_message.clear();
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

void FramePreview::setBusy(bool busy)
{
    m_busy = busy;
    update();
}

void FramePreview::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(0x0e, 0x0e, 0x0e));
    if (!m_image.isNull()) {
        const QSize sz = m_image.size().scaled(size() - QSize(16, 16), Qt::KeepAspectRatio);
        const QRect target(QPoint((width() - sz.width()) / 2, (height() - sz.height()) / 2), sz);
        // Checkerboard behind transparent frames.
        if (m_image.hasAlphaChannel()) {
            const int c = 10;
            for (int y = target.top(); y < target.bottom(); y += c)
                for (int x = target.left(); x < target.right(); x += c)
                    p.fillRect(QRect(x, y, c, c).intersected(target),
                               ((x / c + y / c) & 1) ? QColor(60, 60, 60) : QColor(90, 90, 90));
        }
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.drawImage(target, m_image);

        if (m_crop) {
            const double sx = double(target.width()) / m_image.width();
            const double sy = double(target.height()) / m_image.height();
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
        p.setPen(QColor(150, 150, 150));
        p.drawText(rect().adjusted(8, 4, -8, -4), Qt::AlignRight | Qt::AlignTop,
                   QStringLiteral("%1 × %2").arg(m_image.width()).arg(m_image.height()));
    }
    if (!m_message.isEmpty() || (m_image.isNull() && !m_busy)) {
        p.setPen(QColor(140, 140, 140));
        p.drawText(rect().adjusted(20, 20, -20, -20), Qt::AlignCenter | Qt::TextWordWrap,
                   m_message.isEmpty() ? QStringLiteral("No preview") : m_message);
    }
    if (m_busy) {
        p.setPen(QColor(0x2d, 0x8c, 0xeb));
        p.drawText(rect().adjusted(8, 4, -8, -4), Qt::AlignLeft | Qt::AlignTop, QStringLiteral("Rendering preview…"));
    }
}
