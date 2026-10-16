#pragma once

#include <QWidget>

// Scrubber for the Export Settings preview. Shows the visible time span, the In/Out selection
// (shaded, with tick marks) and the playhead. Times are in source seconds.
class TimelineSlider : public QWidget
{
    Q_OBJECT
public:
    explicit TimelineSlider(QWidget *parent = nullptr);

    // The span of source time the slider covers (the whole clip, or just In..Out).
    void setSpan(double start, double end);
    double spanStart() const { return m_start; }
    double spanEnd() const { return m_end; }
    // In/Out marks in source seconds (< 0 = not set). Shown when showMarks is on.
    void setMarks(double in, double out);
    void setShowMarks(bool show);
    void setFrameRate(double fps) { m_fps = fps > 0 ? fps : 25.0; }

    double position() const { return m_pos; }
    void setPosition(double seconds); // clamps to the span; does not emit

    QSize sizeHint() const override { return {400, 30}; }
    QSize minimumSizeHint() const override { return {120, 30}; }

signals:
    // Emitted when the user scrubs or steps.
    void positionChanged(double seconds);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;

private:
    QRectF track() const;
    double xToTime(double x) const;
    double timeToX(double t) const;
    void userSet(double seconds);

    double m_start = 0.0;
    double m_end = 1.0;
    double m_pos = 0.0;
    double m_in = -1.0;
    double m_out = -1.0;
    double m_fps = 25.0;
    bool m_showMarks = true;
};
