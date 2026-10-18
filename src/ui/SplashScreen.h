#pragma once

#include <QSplashScreen>

// The start-up splash: artwork with the app's name, author, version and release date, shown the
// moment the app starts while FFmpeg is checked and presets load.
class SplashScreen : public QSplashScreen
{
    Q_OBJECT
public:
    SplashScreen();

    void setStatus(const QString &text);
    // Closes once the main window is up and the splash has been on screen at least `minimumMs`;
    // a click closes it straight away.
    void finishAfter(QWidget *mainWindow, int minimumMs = 4000);

protected:
    void drawContents(QPainter *painter) override;
    void mousePressEvent(QMouseEvent *e) override;

private:
    static QPixmap render();
    qint64 m_shownAt = 0;
    QString m_status;
};
