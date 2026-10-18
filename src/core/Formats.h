#pragma once

#include <QList>
#include <QString>
#include <QStringList>

// Static description of a video encoder and the controls it exposes in the UI.
struct VideoCodecDef {
    QString encoder; // ffmpeg encoder name
    QString displayName;
    bool hardware = false;
    QString speedArg; // e.g. "-preset", "-cpu-used"; empty when not applicable
    QString speedLabel = "Encoding Speed";
    QStringList speedPresets;
    QString defaultSpeed;
    QStringList profiles;      // values passed to ffmpeg
    QStringList profileLabels; // human-readable labels, same length as profiles
    QString defaultProfile;
    QStringList levels;       // "auto" first when present
    QStringList pixelFormats; // selectable output pixel formats ("" = auto is implicit)
    QString defaultPixelFormat;
    QStringList rateControls; // subset of: cbr, vbr1, vbr2, quality, size, vmaf, none
    QString defaultRateControl;
    QString qualityArg; // e.g. "-crf", "-cq", "-q:v"
    int qualityMin = 0;
    int qualityMax = 51;
    int defaultQuality = 23;
    bool qualityHigherIsBetter = false; // VideoToolbox's -q:v; CRF-style scales are lower = better
    bool alphaCapable = false;

    QString profileLabel(const QString &profile) const;
};

struct AudioCodecDef {
    QString encoder;
    QString displayName;
    bool lossless = false;
    QList<int> bitrates; // kbps
    int defaultBitrate = 0;
    QList<int> sampleRates;
    int maxChannels = 8;
};

// A container/output format as presented in the "Format" drop-down.
struct FormatDef {
    QString id;
    QString displayName;
    QString extension;
    QString muxer;
    QStringList videoCodecs;
    QStringList audioCodecs;
    bool imageFormat = false; // still image / image sequence output
    QString description;

    bool hasVideo() const { return !videoCodecs.isEmpty(); }
    bool hasAudio() const { return !audioCodecs.isEmpty(); }
};

enum class MediaKind { Video, Audio, Image, Other };

namespace Formats {
const QList<FormatDef> &all();
const FormatDef *format(const QString &id);
const QList<VideoCodecDef> &videoCodecs();
const QList<AudioCodecDef> &audioCodecs();
const VideoCodecDef *videoCodec(const QString &encoder);
const AudioCodecDef *audioCodec(const QString &encoder);
QString rateControlLabel(const QString &rc);
QStringList mediaFileExtensions();
bool isMediaFile(const QString &path);
// Classifies a file by extension (no probing).
MediaKind mediaKind(const QString &path);
QString mediaKindName(MediaKind kind);
} // namespace Formats
