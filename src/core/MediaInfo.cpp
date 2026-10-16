#include "MediaInfo.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QStringList>
#include <cmath>

namespace {

double parseRational(const QString &s)
{
    const auto parts = s.split('/');
    if (parts.size() == 2) {
        const double d = parts[1].toDouble();
        return d != 0.0 ? parts[0].toDouble() / d : 0.0;
    }
    return s.toDouble();
}

} // namespace

bool MediaInfo::hasAlpha() const
{
    return pixFmt.startsWith("yuva") || pixFmt.startsWith("rgba") || pixFmt.startsWith("bgra") ||
           pixFmt.startsWith("argb") || pixFmt.startsWith("abgr") || pixFmt.startsWith("gbrap") || pixFmt == "ya8" ||
           pixFmt == "ya16be" || pixFmt == "ya16le" || pixFmt == "pal8";
}

bool MediaInfo::isInterlaced() const
{
    return fieldOrder == "tt" || fieldOrder == "bb" || fieldOrder == "tb" || fieldOrder == "bt";
}

int MediaInfo::displayWidth() const { return (std::abs(rotation) % 180 == 90) ? height : width; }

int MediaInfo::displayHeight() const { return (std::abs(rotation) % 180 == 90) ? width : height; }

MediaInfo MediaInfo::fromFfprobeJson(const QJsonObject &root, const QString &path)
{
    MediaInfo m;
    m.path = path;
    const QJsonObject fmt = root.value("format").toObject();
    m.formatName = fmt.value("format_long_name").toString(fmt.value("format_name").toString());
    m.duration = fmt.value("duration").toString().toDouble();
    m.size = fmt.value("size").toString().toLongLong();
    m.bitrate = fmt.value("bit_rate").toString().toLongLong();
    m.timecode = fmt.value("tags").toObject().value("timecode").toString();

    const QJsonArray streams = root.value("streams").toArray();
    for (const auto &sv : streams) {
        const QJsonObject s = sv.toObject();
        const QString type = s.value("codec_type").toString();
        const QJsonObject disp = s.value("disposition").toObject();
        if (type == "video" && !m.hasVideo) {
            // Skip embedded cover art in audio files.
            if (disp.value("attached_pic").toInt() == 1)
                continue;
            m.hasVideo = true;
            m.videoCodec = s.value("codec_name").toString();
            m.width = s.value("width").toInt();
            m.height = s.value("height").toInt();
            m.pixFmt = s.value("pix_fmt").toString();
            m.fieldOrder = s.value("field_order").toString();
            m.colorSpace = s.value("color_space").toString();
            m.colorTransfer = s.value("color_transfer").toString();
            m.colorPrimaries = s.value("color_primaries").toString();
            m.videoBitrate = s.value("bit_rate").toString().toLongLong();
            m.frameCount = s.value("nb_frames").toString().toLongLong();
            QString rate = s.value("avg_frame_rate").toString();
            if (parseRational(rate) <= 0.0)
                rate = s.value("r_frame_rate").toString();
            m.fps = parseRational(rate);
            m.fpsRational = rate;
            const QString sar = s.value("sample_aspect_ratio").toString();
            if (!sar.isEmpty() && sar != "0:1") {
                const auto p = sar.split(':');
                if (p.size() == 2 && p[1].toDouble() != 0.0)
                    m.sar = p[0].toDouble() / p[1].toDouble();
            }
            if (m.timecode.isEmpty())
                m.timecode = s.value("tags").toObject().value("timecode").toString();
            const QJsonArray sideData = s.value("side_data_list").toArray();
            for (const auto &sd : sideData) {
                const QJsonObject o = sd.toObject();
                if (o.contains("rotation"))
                    m.rotation = o.value("rotation").toInt();
            }
            const double streamDuration = s.value("duration").toString().toDouble();
            if (m.duration <= 0.0 && streamDuration > 0.0)
                m.duration = streamDuration;
        } else if (type == "audio") {
            ++m.audioStreams;
            if (!m.hasAudio) {
                m.hasAudio = true;
                m.audioCodec = s.value("codec_name").toString();
                m.sampleRate = s.value("sample_rate").toString().toInt();
                m.channels = s.value("channels").toInt();
                m.channelLayout = s.value("channel_layout").toString();
                m.audioBitrate = s.value("bit_rate").toString().toLongLong();
            }
        } else if (type == "data" && m.timecode.isEmpty()) {
            m.timecode = s.value("tags").toObject().value("timecode").toString();
        }
    }

    // A single image (png/jpg/...) reports as a video stream with no meaningful duration.
    const QString fmtShort = fmt.value("format_name").toString();
    if (m.hasVideo && (fmtShort.contains("_pipe") || fmtShort == "image2" || fmtShort == "png_pipe") &&
        (m.frameCount <= 1) && m.duration < 0.1) {
        m.isStill = true;
    }
    if (m.hasVideo && m.fps <= 0.0)
        m.fps = 25.0;

    m.valid = m.hasVideo || m.hasAudio;
    if (!m.valid)
        m.error = QStringLiteral("No audio or video streams found");
    return m;
}

MediaInfo MediaInfo::probe(const QString &ffprobePath, const QString &path, int timeoutMs)
{
    QProcess p;
    p.setProgram(ffprobePath);
    p.setArguments({"-v", "error", "-print_format", "json", "-show_format", "-show_streams", path});
    p.start();
    MediaInfo m;
    m.path = path;
    if (!p.waitForStarted(5000)) {
        m.error = QStringLiteral("Could not start ffprobe (%1)").arg(ffprobePath);
        return m;
    }
    if (!p.waitForFinished(timeoutMs)) {
        p.kill();
        p.waitForFinished(1000);
        m.error = QStringLiteral("ffprobe timed out");
        return m;
    }
    const QByteArray out = p.readAllStandardOutput();
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(out, &err);
    if (p.exitCode() != 0 || !doc.isObject()) {
        m.error = QString::fromUtf8(p.readAllStandardError()).trimmed();
        if (m.error.isEmpty())
            m.error = QStringLiteral("Unsupported or unreadable file");
        return m;
    }
    return fromFfprobeJson(doc.object(), path);
}

QString MediaInfo::shortDescription() const
{
    QStringList parts;
    if (hasVideo) {
        parts << QStringLiteral("%1x%2").arg(width).arg(height);
        if (!isStill)
            parts << QStringLiteral("%1 fps").arg(QString::number(fps, 'f', fps == std::floor(fps) ? 0 : 3));
        parts << videoCodec;
    }
    if (hasAudio)
        parts << QStringLiteral("%1 Hz %2ch").arg(sampleRate).arg(channels);
    if (!isStill && duration > 0)
        parts << formatDuration(duration, fps);
    return parts.join(QStringLiteral(", "));
}

QString MediaInfo::describe() const
{
    if (!valid)
        return QStringLiteral("Unable to read media: %1").arg(error);
    QStringList l;
    l << QStringLiteral("File: %1").arg(QFileInfo(path).fileName());
    l << QStringLiteral("Container: %1").arg(formatName);
    if (!isStill)
        l << QStringLiteral("Duration: %1").arg(formatDuration(duration, fps));
    l << QStringLiteral("Size: %1").arg(formatBytes(size));
    if (bitrate > 0)
        l << QStringLiteral("Overall bitrate: %1 Mbps").arg(bitrate / 1e6, 0, 'f', 2);
    if (!timecode.isEmpty())
        l << QStringLiteral("Start timecode: %1").arg(timecode);
    if (hasVideo) {
        l << QString();
        l << QStringLiteral("Video: %1").arg(videoCodec);
        l << QStringLiteral("  Frame size: %1 x %2").arg(width).arg(height);
        if (sar != 1.0)
            l << QStringLiteral("  Pixel aspect: %1").arg(sar, 0, 'f', 4);
        if (!isStill)
            l << QStringLiteral("  Frame rate: %1 fps").arg(fps, 0, 'f', 3);
        l << QStringLiteral("  Pixel format: %1").arg(pixFmt);
        l << QStringLiteral("  Field order: %1").arg(fieldOrder.isEmpty() ? QStringLiteral("unknown") : fieldOrder);
        if (!colorSpace.isEmpty())
            l << QStringLiteral("  Color: %1 / %2 / %3").arg(colorSpace, colorPrimaries, colorTransfer);
        if (videoBitrate > 0)
            l << QStringLiteral("  Bitrate: %1 Mbps").arg(videoBitrate / 1e6, 0, 'f', 2);
        if (rotation != 0)
            l << QStringLiteral("  Rotation: %1°").arg(rotation);
    }
    if (hasAudio) {
        l << QString();
        l << QStringLiteral("Audio: %1 (%2 stream%3)")
                 .arg(audioCodec)
                 .arg(audioStreams)
                 .arg(audioStreams > 1 ? "s" : "");
        l << QStringLiteral("  Sample rate: %1 Hz").arg(sampleRate);
        l << QStringLiteral("  Channels: %1 %2")
                 .arg(channels)
                 .arg(channelLayout.isEmpty() ? QString() : "(" + channelLayout + ")");
        if (audioBitrate > 0)
            l << QStringLiteral("  Bitrate: %1 kbps").arg(audioBitrate / 1000);
    }
    return l.join('\n');
}

QString formatDuration(double seconds, double fps)
{
    if (seconds < 0)
        seconds = 0;
    const qint64 total = static_cast<qint64>(seconds);
    const int h = int(total / 3600);
    const int m = int((total % 3600) / 60);
    const int s = int(total % 60);
    if (fps > 0) {
        const int f = int((seconds - double(total)) * std::round(fps));
        return QStringLiteral("%1:%2:%3:%4")
            .arg(h, 2, 10, QChar('0'))
            .arg(m, 2, 10, QChar('0'))
            .arg(s, 2, 10, QChar('0'))
            .arg(f, 2, 10, QChar('0'));
    }
    return QStringLiteral("%1:%2:%3").arg(h, 2, 10, QChar('0')).arg(m, 2, 10, QChar('0')).arg(s, 2, 10, QChar('0'));
}

QString formatBytes(qint64 bytes)
{
    const double b = double(bytes);
    if (b >= 1024.0 * 1024 * 1024)
        return QStringLiteral("%1 GB").arg(b / (1024.0 * 1024 * 1024), 0, 'f', 2);
    if (b >= 1024.0 * 1024)
        return QStringLiteral("%1 MB").arg(b / (1024.0 * 1024), 0, 'f', 1);
    if (b >= 1024.0)
        return QStringLiteral("%1 KB").arg(b / 1024.0, 0, 'f', 0);
    return QStringLiteral("%1 B").arg(bytes);
}
