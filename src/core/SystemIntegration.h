#pragma once

#include <QString>

// Operating-system hooks used around long encodes.
namespace SystemIntegration {

// Keeps the computer from going to sleep while `on` (the display may still turn off).
void setPreventSleep(bool on);
// Puts the computer to sleep. Returns false if the request was refused.
bool sleepComputer();
// Starts a normal shutdown. Returns false if it could not be requested.
bool shutDownComputer();
// Opens the system file manager with `path` selected (or the folder itself when it can't select).
void revealInFileManager(const QString &path);
// Pauses (suspend = true) or resumes a running child process. Returns false if that failed.
bool suspendProcess(qint64 pid, bool suspend);

} // namespace SystemIntegration
