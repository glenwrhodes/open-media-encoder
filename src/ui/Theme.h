#pragma once

#include <QColor>
#include <QIcon>
#include <QString>

class QApplication;

namespace Theme {
void apply(QApplication &app);
QColor accent();
QColor panel();
// Simple vector icons drawn at runtime: play, pause, stop, plus, minus, duplicate, folder,
// settings, film, audio, image, watch, reset, import, export, search, log, stitch, preset, trash.
QIcon icon(const QString &name, const QColor &color = QColor());
} // namespace Theme
