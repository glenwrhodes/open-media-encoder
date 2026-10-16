#include "SystemIntegration.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QUrl>

#ifdef Q_OS_WIN
#include <windows.h>

#include <powrprof.h>
#endif

namespace SystemIntegration {

#ifndef Q_OS_WIN
namespace {
// The helper that holds the keep-awake lock; it lets go as soon as it exits.
QProcess *g_awake = nullptr;
} // namespace
#endif

void setPreventSleep(bool on)
{
#ifdef Q_OS_WIN
    // Execution state is per thread; this is always called from the GUI thread.
    SetThreadExecutionState(on ? (ES_CONTINUOUS | ES_SYSTEM_REQUIRED) : ES_CONTINUOUS);
#else
    const bool held = g_awake && g_awake->state() != QProcess::NotRunning;
    if (on == held)
        return;
    if (!on) {
        g_awake->closeWriteChannel(); // `cat` sees end of input and exits
        g_awake->terminate();
        g_awake->waitForFinished(2000);
        delete g_awake;
        g_awake = nullptr;
        return;
    }
    delete g_awake;
    g_awake = new QProcess;
#ifdef Q_OS_MACOS
    // -w: also released if this app exits or crashes.
    g_awake->start(QStringLiteral("caffeinate"),
                   {QStringLiteral("-i"), QStringLiteral("-w"), QString::number(QCoreApplication::applicationPid())});
#else
    // Holds a systemd-logind inhibitor while `cat` runs; `cat` ends when our end of its input closes,
    // including when this app exits or crashes.
    g_awake->start(QStringLiteral("systemd-inhibit"),
                   {QStringLiteral("--what=idle:sleep"), QStringLiteral("--who=Open Media Encoder"),
                    QStringLiteral("--why=Encoding or recording"), QStringLiteral("--mode=block"),
                    QStringLiteral("cat")});
#endif
    if (!g_awake->waitForStarted(2000)) {
        delete g_awake; // not available on this system: nothing to hold
        g_awake = nullptr;
    }
#endif
}

bool sleepComputer()
{
#ifdef Q_OS_WIN
    return SetSuspendState(FALSE, FALSE, FALSE) != 0;
#elif defined(Q_OS_MACOS)
    return QProcess::startDetached(QStringLiteral("pmset"), {QStringLiteral("sleepnow")});
#else
    return QProcess::startDetached(QStringLiteral("systemctl"), {QStringLiteral("suspend")});
#endif
}

bool shutDownComputer()
{
#ifdef Q_OS_WIN
    return QProcess::startDetached(QStringLiteral("shutdown.exe"),
                                   {QStringLiteral("/s"), QStringLiteral("/t"), QStringLiteral("0")});
#elif defined(Q_OS_MACOS)
    return QProcess::startDetached(QStringLiteral("osascript"),
                                   {QStringLiteral("-e"), QStringLiteral("tell app \"System Events\" to shut down")});
#else
    return QProcess::startDetached(QStringLiteral("systemctl"), {QStringLiteral("poweroff")});
#endif
}

void revealInFileManager(const QString &path)
{
    const QFileInfo fi(path);
    if (fi.exists()) {
#if defined(Q_OS_WIN)
        if (QProcess::startDetached(QStringLiteral("explorer.exe"),
                                    {QStringLiteral("/select,"), QDir::toNativeSeparators(fi.absoluteFilePath())}))
            return;
#elif defined(Q_OS_MACOS)
        if (QProcess::startDetached(QStringLiteral("open"), {QStringLiteral("-R"), fi.absoluteFilePath()}))
            return;
#else
        // The freedesktop.org file manager interface (Nautilus, Dolphin, Nemo, Thunar, ...).
        QProcess p;
        p.start(QStringLiteral("dbus-send"),
                {QStringLiteral("--session"), QStringLiteral("--print-reply"),
                 QStringLiteral("--dest=org.freedesktop.FileManager1"), QStringLiteral("--type=method_call"),
                 QStringLiteral("/org/freedesktop/FileManager1"),
                 QStringLiteral("org.freedesktop.FileManager1.ShowItems"),
                 QStringLiteral("array:string:") + QUrl::fromLocalFile(fi.absoluteFilePath()).toString(),
                 QStringLiteral("string:")});
        if (p.waitForFinished(3000) && p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0)
            return;
#endif
    }
    const QString dir = fi.isDir() ? fi.absoluteFilePath() : fi.absolutePath();
    QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}

} // namespace SystemIntegration
