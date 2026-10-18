#pragma once

#include <QString>
#include <QVariant>

// Application preferences, persisted with QSettings.
namespace AppSettings {

QVariant value(const QString &key, const QVariant &def = {});
void setValue(const QString &key, const QVariant &v);

QString ffmpegPath();
QString ffprobePath();
QString bundledFfmpegPath(); // empty when no FFmpeg ships next to the executable
void setFfmpegPath(const QString &path);

QString dataDir(); // %LOCALAPPDATA%/OpenMediaEncoder
QString tempDir();
QString userPresetsDir();
QString logDir();

// "source" = next to the source file, "custom" = customOutputDir()
QString outputLocationMode();
QString customOutputDir();
QString defaultOutputDir(const QString &sourcePath);

bool incrementOutputNames();
int parallelEncodes();
bool hardwareDecode();
bool notifyOnFinish();
bool preventSleepWhileEncoding();
bool deletePartialOnFailure();
int autoStartIdleMinutes(); // 0 = disabled
bool autoEncodeWatchFolders();
void setAutoEncodeWatchFolders(bool on);
QString defaultPresetId();

// Stream recording
QString ytDlpPath(); // configured, next to the executable, or on PATH; empty when not installed
void setYtDlpPath(const QString &path);
QString recordingDir(); // where new stream recordings are saved
void setRecordingDir(const QString &dir);
bool reconnectRecordings(); // start a new part when a stream drops mid-recording

// Speech to text (automatic captions)
QString whisperCliPath(); // bundled whisper-cli, or one on PATH; empty when missing

} // namespace AppSettings
