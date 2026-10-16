#include "Theme.h"

#include <QApplication>
#include <QHash>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPixmap>
#include <QStyleFactory>
#include <QWidget>
#include <cmath>

#ifdef Q_OS_WIN
#include <dwmapi.h>
#endif

namespace {

// Gives every top-level window a dark native title bar on Windows.
class DarkTitleBarFilter : public QObject
{
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject *obj, QEvent *e) override
    {
#ifdef Q_OS_WIN
        if (e->type() == QEvent::Show) {
            auto *w = qobject_cast<QWidget *>(obj);
            if (w && w->isWindow()) {
                const BOOL dark = TRUE;
                DwmSetWindowAttribute(reinterpret_cast<HWND>(w->winId()), 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &dark,
                                      sizeof(dark));
            }
        }
#endif
        return QObject::eventFilter(obj, e);
    }
};

} // namespace

namespace Theme {

QColor accent() { return QColor(0x2d, 0x8c, 0xeb); }
QColor panel() { return QColor(0x23, 0x23, 0x23); }

void apply(QApplication &app)
{
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    app.installEventFilter(new DarkTitleBarFilter(&app));
    QPalette p;
    const QColor window(0x1d, 0x1d, 0x1d);
    const QColor base(0x16, 0x16, 0x16);
    const QColor text(0xd8, 0xd8, 0xd8);
    p.setColor(QPalette::Window, window);
    p.setColor(QPalette::WindowText, text);
    p.setColor(QPalette::Base, base);
    p.setColor(QPalette::AlternateBase, QColor(0x1f, 0x1f, 0x1f));
    p.setColor(QPalette::ToolTipBase, QColor(0x2b, 0x2b, 0x2b));
    p.setColor(QPalette::ToolTipText, text);
    p.setColor(QPalette::Text, text);
    p.setColor(QPalette::Button, QColor(0x2e, 0x2e, 0x2e));
    p.setColor(QPalette::ButtonText, text);
    p.setColor(QPalette::BrightText, Qt::white);
    p.setColor(QPalette::Highlight, accent());
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::Link, QColor(0x4d, 0xa3, 0xff));
    p.setColor(QPalette::PlaceholderText, QColor(0x80, 0x80, 0x80));
    p.setColor(QPalette::Disabled, QPalette::Text, QColor(0x6a, 0x6a, 0x6a));
    p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(0x6a, 0x6a, 0x6a));
    p.setColor(QPalette::Disabled, QPalette::WindowText, QColor(0x6a, 0x6a, 0x6a));
    app.setPalette(p);

    QFont f = app.font();
    f.setPointSizeF(9.0);
    app.setFont(f);

    app.setStyleSheet(QStringLiteral(R"(
QMainWindow::separator { background: #111; width: 4px; height: 4px; }
QDockWidget { color: #d8d8d8; titlebar-close-icon: none; }
QDockWidget::title { background: #262626; padding: 6px 8px; border-bottom: 1px solid #111; text-align: left; }
QToolBar { background: #232323; border: none; spacing: 4px; padding: 3px; }
QToolButton { border: 1px solid transparent; border-radius: 4px; padding: 4px; }
QToolButton:hover { background: #333; border-color: #3d3d3d; }
QToolButton:pressed, QToolButton:checked { background: #1a4d80; }
QPushButton { background: #2f2f2f; border: 1px solid #3d3d3d; border-radius: 4px; padding: 5px 14px; }
QPushButton:hover { background: #383838; }
QPushButton:pressed { background: #1a4d80; }
QPushButton:default { border-color: #2d8ceb; }
QPushButton#primary { background: #2d8ceb; border-color: #2d8ceb; color: white; font-weight: 600; }
QPushButton#primary:hover { background: #3d9bf5; }
QPushButton:disabled { color: #666; background: #262626; }
QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox, QPlainTextEdit, QTextEdit {
    background: #141414; border: 1px solid #383838; border-radius: 3px; padding: 3px 5px;
    selection-background-color: #2d8ceb;
}
QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus { border-color: #2d8ceb; }
QComboBox QAbstractItemView { background: #1e1e1e; border: 1px solid #3d3d3d; selection-background-color: #2d8ceb; }
QTreeView, QListView, QTableView { background: #171717; border: 1px solid #262626; alternate-background-color: #1b1b1b; }
QTreeView::item, QListView::item { padding: 3px 2px; }
QTreeView::item:selected, QListView::item:selected { background: #1f4f80; }
QHeaderView::section { background: #232323; color: #a8a8a8; border: none; border-right: 1px solid #1a1a1a;
    border-bottom: 1px solid #111; padding: 5px 6px; }
QTabWidget::pane { border: 1px solid #2c2c2c; top: -1px; }
QTabBar::tab { background: #1f1f1f; color: #a0a0a0; padding: 6px 14px; border: 1px solid #2c2c2c; border-bottom: none; }
QTabBar::tab:selected { background: #2a2a2a; color: #ffffff; border-bottom: 2px solid #2d8ceb; }
QGroupBox { border: 1px solid #2f2f2f; border-radius: 4px; margin-top: 14px; padding-top: 6px; }
QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 4px; color: #c8c8c8; }
QProgressBar { background: #111; border: 1px solid #2f2f2f; border-radius: 3px; text-align: center; color: #ddd; height: 14px; }
QProgressBar::chunk { background: #2d8ceb; border-radius: 2px; }
QSlider::groove:horizontal { height: 4px; background: #333; border-radius: 2px; }
QSlider::sub-page:horizontal { background: #2d8ceb; border-radius: 2px; }
QSlider::handle:horizontal { background: #e0e0e0; width: 12px; margin: -5px 0; border-radius: 6px; }
QScrollBar:vertical { background: #1a1a1a; width: 11px; }
QScrollBar::handle:vertical { background: #3a3a3a; min-height: 24px; border-radius: 4px; margin: 2px; }
QScrollBar:horizontal { background: #1a1a1a; height: 11px; }
QScrollBar::handle:horizontal { background: #3a3a3a; min-width: 24px; border-radius: 4px; margin: 2px; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QMenu { background: #252525; border: 1px solid #3a3a3a; }
QMenu::item { padding: 5px 24px 5px 22px; }
QMenu::item:selected { background: #2d8ceb; }
QMenu::separator { height: 1px; background: #3a3a3a; margin: 4px 8px; }
QMenuBar { background: #1d1d1d; }
QMenuBar::item:selected { background: #333; }
QStatusBar { background: #1a1a1a; color: #a0a0a0; }
QCheckBox::indicator, QGroupBox::indicator { width: 14px; height: 14px; }
QLabel#sectionTitle { color: #ffffff; font-weight: 600; font-size: 10pt; }
QLabel#muted { color: #8a8a8a; }
QFrame#card { background: #202020; border: 1px solid #2c2c2c; border-radius: 6px; }
)"));
}

QIcon icon(const QString &name, const QColor &colorIn)
{
    static QHash<QString, QIcon> cache;
    const QColor color = colorIn.isValid() ? colorIn : QColor(0xd0, 0xd0, 0xd0);
    const QString key = name + color.name();
    if (cache.contains(key))
        return cache.value(key);

    const int S = 64;
    QPixmap pm(S, S);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    QPen pen(color, 6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);

    if (name == "play") {
        QPainterPath path;
        path.moveTo(18, 10);
        path.lineTo(54, 32);
        path.lineTo(18, 54);
        path.closeSubpath();
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        p.drawPath(path);
    } else if (name == "pause") {
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        p.drawRoundedRect(QRectF(14, 10, 13, 44), 3, 3);
        p.drawRoundedRect(QRectF(37, 10, 13, 44), 3, 3);
    } else if (name == "stop") {
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        p.drawRoundedRect(QRectF(13, 13, 38, 38), 5, 5);
    } else if (name == "plus") {
        p.drawLine(32, 12, 32, 52);
        p.drawLine(12, 32, 52, 32);
    } else if (name == "minus") {
        p.drawLine(12, 32, 52, 32);
    } else if (name == "duplicate") {
        p.drawRoundedRect(QRectF(10, 18, 30, 36), 4, 4);
        p.drawPolyline(QPolygonF({QPointF(22, 10), QPointF(54, 10), QPointF(54, 44)}));
    } else if (name == "folder") {
        QPainterPath path;
        path.moveTo(8, 18);
        path.lineTo(26, 18);
        path.lineTo(31, 24);
        path.lineTo(56, 24);
        path.lineTo(56, 52);
        path.lineTo(8, 52);
        path.closeSubpath();
        p.drawPath(path);
    } else if (name == "settings") {
        p.drawEllipse(QPointF(32, 32), 9, 9);
        for (int i = 0; i < 8; ++i) {
            p.save();
            p.translate(32, 32);
            p.rotate(i * 45);
            p.drawLine(0, 15, 0, 23);
            p.restore();
        }
        p.drawEllipse(QPointF(32, 32), 17, 17);
    } else if (name == "film") {
        p.drawRoundedRect(QRectF(10, 12, 44, 40), 4, 4);
        p.drawLine(20, 12, 20, 52);
        p.drawLine(44, 12, 44, 52);
    } else if (name == "audio") {
        p.drawLine(26, 46, 26, 14);
        p.drawLine(26, 14, 48, 10);
        p.drawLine(48, 10, 48, 40);
        p.setBrush(color);
        p.drawEllipse(QPointF(19, 47), 7, 6);
        p.drawEllipse(QPointF(41, 42), 7, 6);
    } else if (name == "image") {
        p.drawRoundedRect(QRectF(8, 12, 48, 40), 4, 4);
        p.drawPolyline(
            QPolygonF({QPointF(12, 48), QPointF(26, 30), QPointF(36, 40), QPointF(44, 32), QPointF(54, 44)}));
        p.drawEllipse(QPointF(42, 22), 4, 4);
    } else if (name == "watch") {
        QPainterPath path;
        path.moveTo(6, 18);
        path.lineTo(24, 18);
        path.lineTo(29, 24);
        path.lineTo(52, 24);
        path.lineTo(52, 36);
        p.drawPath(path);
        p.drawLine(6, 18, 6, 52);
        p.drawLine(6, 52, 30, 52);
        p.drawEllipse(QPointF(45, 47), 9, 9);
        p.drawLine(45, 47, 45, 42);
        p.drawLine(45, 47, 49, 49);
    } else if (name == "reset") {
        p.drawArc(QRectF(12, 12, 40, 40), 60 * 16, 290 * 16);
        p.setBrush(color);
        p.setPen(Qt::NoPen);
        p.drawPolygon(QPolygonF({QPointF(38, 6), QPointF(48, 18), QPointF(34, 20)}));
    } else if (name == "import") {
        p.drawLine(32, 8, 32, 40);
        p.drawPolyline(QPolygonF({QPointF(20, 28), QPointF(32, 40), QPointF(44, 28)}));
        p.drawPolyline(QPolygonF({QPointF(10, 40), QPointF(10, 54), QPointF(54, 54), QPointF(54, 40)}));
    } else if (name == "export") {
        p.drawLine(32, 40, 32, 8);
        p.drawPolyline(QPolygonF({QPointF(20, 20), QPointF(32, 8), QPointF(44, 20)}));
        p.drawPolyline(QPolygonF({QPointF(10, 40), QPointF(10, 54), QPointF(54, 54), QPointF(54, 40)}));
    } else if (name == "search") {
        p.drawEllipse(QPointF(27, 27), 15, 15);
        p.drawLine(38, 38, 54, 54);
    } else if (name == "log") {
        p.drawRoundedRect(QRectF(14, 8, 36, 48), 4, 4);
        p.drawLine(22, 22, 42, 22);
        p.drawLine(22, 32, 42, 32);
        p.drawLine(22, 42, 34, 42);
    } else if (name == "stitch") {
        p.drawRoundedRect(QRectF(6, 20, 22, 24), 3, 3);
        p.drawRoundedRect(QRectF(36, 20, 22, 24), 3, 3);
        p.drawLine(28, 32, 36, 32);
    } else if (name == "preset") {
        p.drawRoundedRect(QRectF(10, 10, 44, 44), 6, 6);
        p.drawLine(20, 24, 44, 24);
        p.drawLine(20, 34, 44, 34);
        p.drawLine(20, 44, 34, 44);
    } else if (name == "trash") {
        p.drawLine(12, 18, 52, 18);
        p.drawLine(26, 10, 38, 10);
        p.drawPolyline(QPolygonF({QPointF(18, 18), QPointF(21, 54), QPointF(43, 54), QPointF(46, 18)}));
    } else if (name == "up") {
        p.drawLine(32, 52, 32, 14);
        p.drawPolyline(QPolygonF({QPointF(16, 28), QPointF(32, 12), QPointF(48, 28)}));
    } else if (name == "star") {
        QPolygonF star;
        for (int i = 0; i < 10; ++i) {
            const double r = (i % 2) ? 10.0 : 23.0;
            constexpr double pi = 3.14159265358979323846;
            const double a = -pi / 2 + i * pi / 5;
            star << QPointF(32 + r * std::cos(a), 34 + r * std::sin(a));
        }
        p.drawPolygon(star);
    } else if (name == "info") {
        p.drawEllipse(QPointF(32, 32), 22, 22);
        p.drawLine(32, 29, 32, 44);
        p.setBrush(color);
        p.drawEllipse(QPointF(32, 20), 2, 2);
    }
    p.end();
    QIcon ic(pm);
    cache.insert(key, ic);
    return ic;
}

} // namespace Theme
