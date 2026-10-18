#pragma once

#include <QImage>
#include <QPointF>
#include <QRect>
#include <QWidget>

// Aspect-correct frame display with optional crop overlay (in source pixel coordinates), and a
// before/after mode: two frames shown side by side across a draggable divider, with zoom and pan.
class FramePreview : public QWidget
{
    Q_OBJECT
public:
    explicit FramePreview(QWidget *parent = nullptr);

    void setImage(const QImage &img);
    void setMessage(const QString &msg);
    void setCropOverlay(bool enabled, int left, int top, int right, int bottom);
    void setBusy(bool busy, const QString &text = {});

    // Before/after: the image set with setImage() is "before"; `after` fills the right of the divider.
    void setCompare(bool on);
    void setCompareImage(const QImage &after);
    void setCompareLabels(const QString &before, const QString &after);
    // 0 fits the frame to the view; 1 = 100% (one image pixel per screen pixel), 2 = 200%...
    void setZoom(double zoom);
    double zoom() const { return m_zoom; }

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;

private:
    QRectF imageRect() const;
    double dividerX() const;
    void clampPan();

    QImage m_image;
    QImage m_after;
    QString m_message;
    QString m_busyText;
    QString m_beforeLabel = QStringLiteral("Original");
    QString m_afterLabel = QStringLiteral("Encoded");
    bool m_crop = false;
    int m_cl = 0, m_ct = 0, m_cr = 0, m_cb = 0;
    bool m_busy = false;
    bool m_compare = false;
    double m_split = 0.5; // divider position across the image, 0..1
    double m_zoom = 0.0;
    QPointF m_center; // image point shown at the middle of the view when zoomed
    enum class Drag { None, Divider, Pan } m_drag = Drag::None;
    QPointF m_dragStart;
    QPointF m_centerStart;
};
