#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>

// Metadata for a source file, gathered with ffprobe.
struct MediaInfo {
    bool valid = false;
    QString error;
    QString path;

    QString formatName;
    double duration = 0.0; // seconds
    qint64 size = 0;       // bytes
    qint64 bitrate = 0;    // bits/s
    QString timecode;      // e.g. "01:00:00:00" when present

    bool hasVideo = false;
    bool isStill = false; // single image source
    QString videoCodec;
    int width = 0;
    int height = 0;
    double fps = 0.0;
    QString fpsRational; // e.g. "30000/1001"
    QString pixFmt;
    QString fieldOrder; // progressive, tt, bb, ...
    double sar = 1.0;
    QString colorSpace;
    QString colorTransfer;
    QString colorPrimaries;
    qint64 videoBitrate = 0;
    qint64 frameCount = 0;
    int rotation = 0;

    bool hasAudio = false;
    int audioStreams = 0;
    QString audioCodec;
    int sampleRate = 0;
    int channels = 0;
    QString channelLayout;
    qint64 audioBitrate = 0;

    bool hasAlpha() const;
    bool isInterlaced() const;
    int displayWidth() const; // after rotation
    int displayHeight() const;
    QString describe() const; // multi-line human-readable summary
    QString shortDescription() const;

    static MediaInfo fromFfprobeJson(const QJsonObject &root, const QString &path);
    // inputArgs are demuxer options placed before the input (used for image sequences).
    static MediaInfo probe(const QString &ffprobePath, const QString &path, int timeoutMs = 20000,
                           const QStringList &inputArgs = {});
};

QString formatDuration(double seconds, double fps = 0.0);
QString formatBytes(qint64 bytes);
