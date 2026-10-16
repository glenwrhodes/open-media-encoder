#pragma once

// Operating-system hooks used around long encodes.
namespace SystemIntegration {

// Keeps the computer from going to sleep while `on` (the display may still turn off).
void setPreventSleep(bool on);
// Puts the computer to sleep. Returns false if the request was refused.
bool sleepComputer();
// Starts a normal shutdown. Returns false if it could not be requested.
bool shutDownComputer();

} // namespace SystemIntegration
