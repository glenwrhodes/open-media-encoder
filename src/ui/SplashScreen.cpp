#include "SplashScreen.h"

#include "Theme.h"

#include <QDate>
#include <QDateTime>
#include <QGuiApplication>
#include <QLinearGradient>
#include <QLocale>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>
#include <QTimer>

namespace {
const QSize LogicalSize(720, 405);
}

SplashScreen::SplashScreen()
    : QSplashScreen(render(), Qt::WindowStaysOnTopHint)
{
    setCursor(Qt::PointingHandCursor);
    setToolTip(QStringLiteral("Click to close"));
    m_shownAt = QDateTime::currentMSecsSinceEpoch();
}

QPixmap SplashScreen::render()
{
    // Drawn at the screen's pixel density so the text stays sharp on high-DPI displays.
    const QScreen *screen = QGuiApplication::primaryScreen();
    const qreal dpr = screen ? screen->devicePixelRatio() : 1.0;
    QPixmap pm(LogicalSize * dpr);
    pm.setDevicePixelRatio(dpr);
    pm.fill(QColor(0x0b, 0x10, 0x1c));

    QPainter p(&pm);
    p.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform | QPainter::TextAntialiasing);
    const QRectF r(QPointF(0, 0), QSizeF(LogicalSize));

    // Artwork, cropped to fill.
    const QImage art(QStringLiteral(":/splash.png"));
    if (!art.isNull()) {
        const QSizeF scaled = QSizeF(art.size()).scaled(r.size(), Qt::KeepAspectRatioByExpanding);
        p.drawImage(QRectF(QPointF((r.width() - scaled.width()) / 2, (r.height() - scaled.height()) / 2), scaled), art);
    }
    // Darken the left side so the text reads over any artwork.
    QLinearGradient shade(r.topLeft(), r.topRight());
    shade.setColorAt(0.0, QColor(5, 8, 16, 235));
    shade.setColorAt(0.45, QColor(5, 8, 16, 150));
    shade.setColorAt(0.75, QColor(5, 8, 16, 0));
    p.fillRect(r, shade);

    const qreal x = 40;
    const QPixmap logo = QPixmap(QStringLiteral(":/icon-512.png"));
    if (!logo.isNull())
        p.drawPixmap(QRectF(x, 46, 56, 56), logo, logo.rect());

    QFont title = p.font();
    title.setPixelSize(38);
    title.setWeight(QFont::Bold);
    p.setFont(title);
    p.setPen(Qt::white);
    p.drawText(QRectF(x, 112, 420, 100), Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap,
               QStringLiteral("Open Media\nEncoder"));

    QFont tag = p.font();
    tag.setPixelSize(15);
    tag.setWeight(QFont::Normal);
    p.setFont(tag);
    p.setPen(QColor(0x9f, 0xd0, 0xff));
    p.drawText(QRectF(x, 224, 420, 22), Qt::AlignLeft, QStringLiteral("A free alternative to Adobe Media Encoder"));

    QFont info = p.font();
    info.setPixelSize(13);
    p.setFont(info);
    p.setPen(QColor(220, 225, 235));
    const QDate released = QDate::fromString(QStringLiteral(OME_RELEASE_DATE), Qt::ISODate);
    const QString date = released.isValid()
                             ? QLocale(QLocale::English).toString(released, QStringLiteral("MMMM d, yyyy"))
                             : QStringLiteral(OME_RELEASE_DATE);
    p.drawText(QRectF(x, 272, 420, 20), Qt::AlignLeft, QStringLiteral("Version %1").arg(QStringLiteral(OME_VERSION)));
    p.drawText(QRectF(x, 292, 420, 20), Qt::AlignLeft, QStringLiteral("Released %1").arg(date));
    p.setPen(QColor(160, 170, 185));
    p.drawText(QRectF(x, 312, 420, 20), Qt::AlignLeft, QStringLiteral("by Glen Rhodes"));

    // Thin accent line along the bottom, and a hairline border.
    p.fillRect(QRectF(0, r.height() - 3, r.width(), 3), Theme::accent());
    p.setPen(QPen(QColor(255, 255, 255, 40), 1));
    p.setBrush(Qt::NoBrush);
    p.drawRect(r.adjusted(0.5, 0.5, -0.5, -0.5));
    return pm;
}

void SplashScreen::setStatus(const QString &text)
{
    m_status = text;
    repaint();
    QGuiApplication::processEvents();
}

void SplashScreen::drawContents(QPainter *painter)
{
    if (m_status.isEmpty())
        return;
    QFont f = painter->font();
    f.setPixelSize(12);
    painter->setFont(f);
    painter->setPen(QColor(140, 150, 165));
    painter->drawText(QRectF(40, LogicalSize.height() - 40, 400, 20), Qt::AlignLeft | Qt::AlignVCenter, m_status);
}

void SplashScreen::mousePressEvent(QMouseEvent *)
{
    close();
    deleteLater();
}

void SplashScreen::finishAfter(QWidget *mainWindow, int minimumMs)
{
    m_status.clear();
    repaint();
    const qint64 shown = QDateTime::currentMSecsSinceEpoch() - m_shownAt;
    const int wait = int(qMax<qint64>(0, minimumMs - shown));
    QTimer::singleShot(wait, this, [this, mainWindow] {
        finish(mainWindow);
        deleteLater();
    });
}
