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
    // Closes once the main window is up, keeping the splash on screen at least `minimumMs` in total
    // so it doesn't just flicker on fast machines.
    void finishAfter(QWidget *mainWindow, int minimumMs = 1500);

protected:
    void drawContents(QPainter *painter) override;

private:
    static QPixmap render();
    qint64 m_shownAt = 0;
    QString m_status;
};
