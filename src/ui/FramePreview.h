#pragma once

#include <QImage>
#include <QRect>
#include <QWidget>

// Aspect-correct frame display with optional crop overlay (in source pixel coordinates).
class FramePreview : public QWidget
{
    Q_OBJECT
public:
    explicit FramePreview(QWidget *parent = nullptr);

    void setImage(const QImage &img);
    void setMessage(const QString &msg);
    void setCropOverlay(bool enabled, int left, int top, int right, int bottom);
    void setBusy(bool busy);

protected:
    void paintEvent(QPaintEvent *) override;

private:
    QImage m_image;
    QString m_message;
    bool m_crop = false;
    int m_cl = 0, m_ct = 0, m_cr = 0, m_cb = 0;
    bool m_busy = false;
};
