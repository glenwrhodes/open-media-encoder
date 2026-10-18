#pragma once

#include <QString>

class QWidget;

// Downloading the speech-to-text model, with a confirmation and a progress dialog.
namespace SpeechModelPrompt {

// Downloads the model now (progress dialog, can be cancelled). Returns true when it is installed.
bool download(QWidget *parent, const QString &modelId);
// Returns true when automatic captions can run; otherwise explains that the model is needed, offers
// to download the one selected in Preferences, and returns whether that worked.
bool ensureInstalled(QWidget *parent);
// Downloads NVIDIA GPU support for speech recognition (Windows), with progress.
bool downloadGpu(QWidget *parent);

} // namespace SpeechModelPrompt
