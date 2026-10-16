#include "AppSettings.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

namespace AppSettings {

namespace {

QSettings &store()
{
    static QSettings s(QSettings::IniFormat, QSettings::UserScope, QStringLiteral("OpenMediaEncoder"),
                       QStringLiteral("OpenMediaEncoder"));
    return s;
}

QString exeName(const QString &base)
{
#ifdef Q_OS_WIN
    return base + ".exe";
#else
    return base;
#endif
}

QString ensureDir(const QString &path)
{
    QDir().mkpath(path);
    return path;
}

} // namespace

QVariant value(const QString &key, const QVariant &def) { return store().value(key, def); }

void setValue(const QString &key, const QVariant &v)
{
    store().setValue(key, v);
    store().sync();
}

QString bundledFfmpegPath()
{
    const QString appLocal = QDir(QCoreApplication::applicationDirPath()).filePath(exeName("ffmpeg"));
    return QFileInfo::exists(appLocal) ? appLocal : QString();
}

QString ffmpegPath()
{
    const QString configured = value("tools/ffmpeg").toString();
    if (!configured.isEmpty() && QFileInfo::exists(configured))
        return configured;
    const QString bundled = bundledFfmpegPath();
    if (!bundled.isEmpty())
        return bundled;
    const QString onPath = QStandardPaths::findExecutable("ffmpeg");
    if (!onPath.isEmpty())
        return onPath;
#ifdef Q_OS_WIN
    for (const QString &c : {QStringLiteral("C:/ffmpeg/ffmpeg.exe"), QStringLiteral("C:/ffmpeg/bin/ffmpeg.exe")})
        if (QFileInfo::exists(c))
            return c;
#endif
    return exeName("ffmpeg");
}

QString ffprobePath()
{
    const QString configured = value("tools/ffprobe").toString();
    if (!configured.isEmpty() && QFileInfo::exists(configured))
        return configured;
    const QFileInfo ff(ffmpegPath());
    const QString sibling = ff.dir().filePath(exeName("ffprobe"));
    if (ff.isAbsolute() && QFileInfo::exists(sibling))
        return sibling;
    const QString onPath = QStandardPaths::findExecutable("ffprobe");
    return onPath.isEmpty() ? exeName("ffprobe") : onPath;
}

void setFfmpegPath(const QString &path)
{
    if (path.isEmpty() || path == bundledFfmpegPath()) {
        setValue("tools/ffmpeg", QString());
        setValue("tools/ffprobe", QString());
        return;
    }
    setValue("tools/ffmpeg", path);
    const QString probe = QFileInfo(path).dir().filePath(exeName("ffprobe"));
    setValue("tools/ffprobe", QFileInfo::exists(probe) ? probe : QString());
}

QString dataDir()
{
    return ensureDir(QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation))
                         .filePath(QStringLiteral("OpenMediaEncoder")));
}

QString tempDir()
{
    return ensureDir(QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
                         .filePath(QStringLiteral("OpenMediaEncoder")));
}

QString userPresetsDir() { return ensureDir(QDir(dataDir()).filePath(QStringLiteral("Presets"))); }
QString logDir() { return ensureDir(QDir(dataDir()).filePath(QStringLiteral("Logs"))); }

QString outputLocationMode() { return value("output/mode", "source").toString(); }
QString customOutputDir()
{
    return value("output/customDir", QStandardPaths::writableLocation(QStandardPaths::MoviesLocation)).toString();
}

QString defaultOutputDir(const QString &sourcePath)
{
    if (outputLocationMode() == "custom" && !customOutputDir().isEmpty())
        return customOutputDir();
    return QFileInfo(sourcePath).absolutePath();
}

bool incrementOutputNames() { return value("output/increment", true).toBool(); }
int parallelEncodes() { return qBound(1, value("encode/parallel", 1).toInt(), 8); }
bool hardwareDecode() { return value("encode/hwDecode", false).toBool(); }
bool notifyOnFinish() { return value("queue/notify", true).toBool(); }
bool preventSleepWhileEncoding() { return value("encode/preventSleep", true).toBool(); }
bool deletePartialOnFailure() { return value("encode/deletePartial", true).toBool(); }
int autoStartIdleMinutes() { return value("queue/autoStartIdleMinutes", 0).toInt(); }
bool autoEncodeWatchFolders() { return value("watch/autoEncode", false).toBool(); }
void setAutoEncodeWatchFolders(bool on) { setValue("watch/autoEncode", on); }
QString defaultPresetId() { return value("presets/default", "sys/h264/Match Source - High bitrate").toString(); }

} // namespace AppSettings
