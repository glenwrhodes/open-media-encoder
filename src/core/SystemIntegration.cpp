#include "SystemIntegration.h"

#include <QProcess>

#ifdef Q_OS_WIN
#include <windows.h>

#include <powrprof.h>
#endif

namespace SystemIntegration {

void setPreventSleep(bool on)
{
#ifdef Q_OS_WIN
    // Execution state is per thread; this is always called from the GUI thread.
    SetThreadExecutionState(on ? (ES_CONTINUOUS | ES_SYSTEM_REQUIRED) : ES_CONTINUOUS);
#else
    Q_UNUSED(on);
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

} // namespace SystemIntegration
