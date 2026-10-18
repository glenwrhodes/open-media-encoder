#pragma once

#include <QJsonObject>
#include <QString>

struct MediaInfo;

struct VideoSettings {
    bool enabled = true;
    QString codec;
    int width = 0; // 0 = match source
    int height = 0;
    QString scaleMode = "fit";          // fit (letterbox), fill (crop), stretch, blur (fit on a blurred copy)
    int blurStrength = 60;              // blur scale mode: 1-100
    int backgroundDim = 25;             // blur scale mode: darken the background by this percent
    double frameRate = 0.0;             // 0 = match source
    QString frameRateMode = "frame";    // frame sampling, blend, optical (motion interpolation)
    QString fieldOrder = "progressive"; // progressive, tff, bff
    bool deinterlace = false;
    QString pixelFormat; // empty = auto
    QString speedPreset;
    QString profile;
    QString level;
    QString rateControl = "vbr1"; // cbr, vbr1, vbr2, quality, size (target file size), vmaf (target quality)
    double targetBitrate = 10.0;  // Mbps
    double maxBitrate = 12.0;     // Mbps; also caps the bitrate chosen for a target file size
    int quality = 23;
    double targetSizeMB = 10.0;       // rateControl "size": the output stays under this many megabytes (10^6 bytes)
    double targetVmaf = 95.0;         // rateControl "vmaf": smallest file that still scores this on VMAF
    int keyframeInterval = 0;         // frames, 0 = encoder default
    bool imageSequence = true;        // image formats: sequence vs. single still
    double stillDuration = 5.0;       // seconds of video generated from a still source
    double frameRateLimit = 0.0;      // 0 = none; faster sources are converted down to this rate
    bool scaleBitrateWithFps = false; // bitrates are for <= 30 fps and raised 1.5x above that
    QString extraArgs;
};

struct AudioSettings {
    bool enabled = true;
    QString codec;
    int sampleRate = 48000; // 0 = match source
    int channels = 2;       // 0 = match source
    int bitrate = 320;      // kbps
    QString extraArgs;
};

struct EffectsSettings {
    bool lutEnabled = false;
    QString lutPath;

    bool imageOverlayEnabled = false;
    QString imagePath;
    QString imagePosition = "bottom-right";
    int imageOffset = 32;
    int imageScale = 100;   // percent
    int imageOpacity = 100; // percent

    bool textOverlayEnabled = false;
    QString text;
    QString textPosition = "bottom-center";
    int textSize = 48;
    QString textColor = "#ffffff";
    int textOpacity = 100;
    bool textBox = true;
    int textOffset = 48;

    bool timecodeEnabled = false;
    QString timecodePosition = "top-left";
    int timecodeSize = 40;
    bool timecodeFromSource = true;
    QString timecodeStart = "00:00:00:00";

    bool limiterEnabled = false; // clamp luma/chroma to broadcast-legal range

    bool loudnessEnabled = false;  // EBU R128 / ITU BS.1770 normalization
    double loudnessTarget = -23.0; // LUFS
    double truePeak = -2.0;        // dBTP
    double loudnessRange = 11.0;   // LU

    bool captionsEnabled = false; // burn-in subtitles
    QString captionFile;
    int captionSize = 24;

    // Captions generated from the speech in the audio (whisper.cpp, offline).
    bool autoCaptions = false;
    QString captionLanguage = "auto"; // spoken language code, or "auto" to detect it
    bool captionTranslate = false;    // translate the captions to English
    bool captionSaveSrt = true;       // write an .srt file next to the output
    bool captionEmbed = false;        // add a subtitle track (MP4, MOV, WebM)
    bool captionBurn = false;         // burn the captions into the picture
    int autoCaptionSize = 56;         // burned caption text height in pixels on a 1080-line frame

    // Cut pauses out of the video and audio.
    bool removeSilence = false;
    double silenceThreshold = -40.0; // dB; quieter than this counts as silence
    double silenceMinDuration = 0.8; // seconds of silence before it is cut
    double silencePadding = 0.2;     // seconds of the pause kept on each side of the speech

    bool cropEnabled = false;
    int cropLeft = 0;
    int cropTop = 0;
    int cropRight = 0;
    int cropBottom = 0;

    bool anyEnabled() const;
};

// Everything needed to describe one output: format, codec settings and effects.
struct ExportSettings {
    QString presetName;
    QString group; // optional preset group, e.g. "YouTube" (shown in the Preset Browser)
    QString format;
    QString comment;
    VideoSettings video;
    AudioSettings audio;
    EffectsSettings effects;
    bool maxRenderQuality = false;

    QJsonObject toJson() const;
    static ExportSettings fromJson(const QJsonObject &o);

    // Selects the format and fills in its first available codecs with their defaults.
    void setFormat(const QString &formatId);
    // Resets codec-dependent fields (speed, profile, pixel format, rate control) to codec defaults.
    void applyVideoCodecDefaults();
    void applyAudioCodecDefaults();

    QString extension() const;
    bool isImageSequence() const;
    QString summary(const MediaInfo *source = nullptr) const;
    // Frame rate actually produced for a source (explicit rate, else the limit, else the source rate).
    double effectiveFrameRate(double sourceFps) const;
    // Bitrate multiplier from scaleBitrateWithFps for the given output frame rate.
    double bitrateScale(double outputFps) const;
    // Estimated size in bytes for the given duration, or -1 when it can't be estimated.
    qint64 estimateSize(double seconds, const MediaInfo *source = nullptr) const;
};
