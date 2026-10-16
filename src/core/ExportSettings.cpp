#include "ExportSettings.h"

#include "EncoderCaps.h"
#include "Formats.h"
#include "MediaInfo.h"

#include <QFileInfo>
#include <QJsonValue>
#include <QStringList>
#include <cmath>

bool EffectsSettings::anyEnabled() const
{
    return lutEnabled || imageOverlayEnabled || textOverlayEnabled || timecodeEnabled || limiterEnabled ||
           loudnessEnabled || captionsEnabled || cropEnabled;
}

// ---------------------------------------------------------------- JSON

QJsonObject ExportSettings::toJson() const
{
    QJsonObject v{
        {"enabled", video.enabled},
        {"codec", video.codec},
        {"width", video.width},
        {"height", video.height},
        {"scaleMode", video.scaleMode},
        {"frameRate", video.frameRate},
        {"frameRateMode", video.frameRateMode},
        {"fieldOrder", video.fieldOrder},
        {"deinterlace", video.deinterlace},
        {"pixelFormat", video.pixelFormat},
        {"speedPreset", video.speedPreset},
        {"profile", video.profile},
        {"level", video.level},
        {"rateControl", video.rateControl},
        {"targetBitrate", video.targetBitrate},
        {"maxBitrate", video.maxBitrate},
        {"quality", video.quality},
        {"keyframeInterval", video.keyframeInterval},
        {"imageSequence", video.imageSequence},
        {"stillDuration", video.stillDuration},
        {"extraArgs", video.extraArgs},
    };
    QJsonObject a{
        {"enabled", audio.enabled},   {"codec", audio.codec},     {"sampleRate", audio.sampleRate},
        {"channels", audio.channels}, {"bitrate", audio.bitrate}, {"extraArgs", audio.extraArgs},
    };
    const EffectsSettings &e = effects;
    QJsonObject fx{
        {"lutEnabled", e.lutEnabled},
        {"lutPath", e.lutPath},
        {"imageOverlayEnabled", e.imageOverlayEnabled},
        {"imagePath", e.imagePath},
        {"imagePosition", e.imagePosition},
        {"imageOffset", e.imageOffset},
        {"imageScale", e.imageScale},
        {"imageOpacity", e.imageOpacity},
        {"textOverlayEnabled", e.textOverlayEnabled},
        {"text", e.text},
        {"textPosition", e.textPosition},
        {"textSize", e.textSize},
        {"textColor", e.textColor},
        {"textOpacity", e.textOpacity},
        {"textBox", e.textBox},
        {"textOffset", e.textOffset},
        {"timecodeEnabled", e.timecodeEnabled},
        {"timecodePosition", e.timecodePosition},
        {"timecodeSize", e.timecodeSize},
        {"timecodeFromSource", e.timecodeFromSource},
        {"timecodeStart", e.timecodeStart},
        {"limiterEnabled", e.limiterEnabled},
        {"loudnessEnabled", e.loudnessEnabled},
        {"loudnessTarget", e.loudnessTarget},
        {"truePeak", e.truePeak},
        {"loudnessRange", e.loudnessRange},
        {"captionsEnabled", e.captionsEnabled},
        {"captionFile", e.captionFile},
        {"captionSize", e.captionSize},
        {"cropEnabled", e.cropEnabled},
        {"cropLeft", e.cropLeft},
        {"cropTop", e.cropTop},
        {"cropRight", e.cropRight},
        {"cropBottom", e.cropBottom},
    };
    return QJsonObject{
        {"version", 1},
        {"presetName", presetName},
        {"format", format},
        {"comment", comment},
        {"maxRenderQuality", maxRenderQuality},
        {"video", v},
        {"audio", a},
        {"effects", fx},
    };
}

ExportSettings ExportSettings::fromJson(const QJsonObject &o)
{
    ExportSettings s;
    s.presetName = o.value("presetName").toString();
    s.format = o.value("format").toString("h264");
    s.comment = o.value("comment").toString();
    s.maxRenderQuality = o.value("maxRenderQuality").toBool();

    const QJsonObject v = o.value("video").toObject();
    VideoSettings &vs = s.video;
    vs.enabled = v.value("enabled").toBool(true);
    vs.codec = v.value("codec").toString();
    vs.width = v.value("width").toInt();
    vs.height = v.value("height").toInt();
    vs.scaleMode = v.value("scaleMode").toString("fit");
    vs.frameRate = v.value("frameRate").toDouble();
    vs.frameRateMode = v.value("frameRateMode").toString("frame");
    vs.fieldOrder = v.value("fieldOrder").toString("progressive");
    vs.deinterlace = v.value("deinterlace").toBool();
    vs.pixelFormat = v.value("pixelFormat").toString();
    vs.speedPreset = v.value("speedPreset").toString();
    vs.profile = v.value("profile").toString();
    vs.level = v.value("level").toString();
    vs.rateControl = v.value("rateControl").toString("vbr1");
    vs.targetBitrate = v.value("targetBitrate").toDouble(10.0);
    vs.maxBitrate = v.value("maxBitrate").toDouble(12.0);
    vs.quality = v.value("quality").toInt(23);
    vs.keyframeInterval = v.value("keyframeInterval").toInt();
    vs.imageSequence = v.value("imageSequence").toBool(true);
    vs.stillDuration = v.value("stillDuration").toDouble(5.0);
    vs.extraArgs = v.value("extraArgs").toString();

    const QJsonObject a = o.value("audio").toObject();
    s.audio.enabled = a.value("enabled").toBool(true);
    s.audio.codec = a.value("codec").toString();
    s.audio.sampleRate = a.value("sampleRate").toInt(48000);
    s.audio.channels = a.value("channels").toInt(2);
    s.audio.bitrate = a.value("bitrate").toInt(320);
    s.audio.extraArgs = a.value("extraArgs").toString();

    const QJsonObject fx = o.value("effects").toObject();
    EffectsSettings &e = s.effects;
    e.lutEnabled = fx.value("lutEnabled").toBool();
    e.lutPath = fx.value("lutPath").toString();
    e.imageOverlayEnabled = fx.value("imageOverlayEnabled").toBool();
    e.imagePath = fx.value("imagePath").toString();
    e.imagePosition = fx.value("imagePosition").toString("bottom-right");
    e.imageOffset = fx.value("imageOffset").toInt(32);
    e.imageScale = fx.value("imageScale").toInt(100);
    e.imageOpacity = fx.value("imageOpacity").toInt(100);
    e.textOverlayEnabled = fx.value("textOverlayEnabled").toBool();
    e.text = fx.value("text").toString();
    e.textPosition = fx.value("textPosition").toString("bottom-center");
    e.textSize = fx.value("textSize").toInt(48);
    e.textColor = fx.value("textColor").toString("#ffffff");
    e.textOpacity = fx.value("textOpacity").toInt(100);
    e.textBox = fx.value("textBox").toBool(true);
    e.textOffset = fx.value("textOffset").toInt(48);
    e.timecodeEnabled = fx.value("timecodeEnabled").toBool();
    e.timecodePosition = fx.value("timecodePosition").toString("top-left");
    e.timecodeSize = fx.value("timecodeSize").toInt(40);
    e.timecodeFromSource = fx.value("timecodeFromSource").toBool(true);
    e.timecodeStart = fx.value("timecodeStart").toString("00:00:00:00");
    e.limiterEnabled = fx.value("limiterEnabled").toBool();
    e.loudnessEnabled = fx.value("loudnessEnabled").toBool();
    e.loudnessTarget = fx.value("loudnessTarget").toDouble(-23.0);
    e.truePeak = fx.value("truePeak").toDouble(-2.0);
    e.loudnessRange = fx.value("loudnessRange").toDouble(11.0);
    e.captionsEnabled = fx.value("captionsEnabled").toBool();
    e.captionFile = fx.value("captionFile").toString();
    e.captionSize = fx.value("captionSize").toInt(24);
    e.cropEnabled = fx.value("cropEnabled").toBool();
    e.cropLeft = fx.value("cropLeft").toInt();
    e.cropTop = fx.value("cropTop").toInt();
    e.cropRight = fx.value("cropRight").toInt();
    e.cropBottom = fx.value("cropBottom").toInt();
    return s;
}

// ---------------------------------------------------------------- defaults

void ExportSettings::setFormat(const QString &formatId)
{
    format = formatId;
    const FormatDef *f = Formats::format(formatId);
    if (!f)
        return;
    const QStringList vids = EncoderCaps::instance().availableVideoCodecs(*f);
    const QStringList auds = EncoderCaps::instance().availableAudioCodecs(*f);
    video.enabled = !vids.isEmpty();
    video.codec = vids.value(0);
    audio.enabled = !auds.isEmpty();
    audio.codec = auds.value(0);
    applyVideoCodecDefaults();
    applyAudioCodecDefaults();
}

void ExportSettings::applyVideoCodecDefaults()
{
    const VideoCodecDef *c = Formats::videoCodec(video.codec);
    if (!c)
        return;
    video.speedPreset = c->defaultSpeed;
    video.profile = c->defaultProfile;
    video.level = c->levels.isEmpty() ? QString() : QStringLiteral("auto");
    video.pixelFormat.clear();
    video.rateControl = c->defaultRateControl;
    video.quality = c->defaultQuality;
}

void ExportSettings::applyAudioCodecDefaults()
{
    const AudioCodecDef *c = Formats::audioCodec(audio.codec);
    if (!c)
        return;
    if (!c->lossless)
        audio.bitrate = c->defaultBitrate;
    if (!c->sampleRates.isEmpty() && audio.sampleRate != 0 && !c->sampleRates.contains(audio.sampleRate))
        audio.sampleRate = c->sampleRates.contains(48000) ? 48000 : c->sampleRates.first();
    if (audio.channels > c->maxChannels)
        audio.channels = c->maxChannels;
}

QString ExportSettings::extension() const
{
    const FormatDef *f = Formats::format(format);
    return f ? f->extension : QStringLiteral("mp4");
}

bool ExportSettings::isImageSequence() const
{
    const FormatDef *f = Formats::format(format);
    return f && f->imageFormat && video.imageSequence;
}

// ---------------------------------------------------------------- summary

QString ExportSettings::summary(const MediaInfo *source) const
{
    QStringList lines;
    const FormatDef *f = Formats::format(format);
    lines << QStringLiteral("Format: %1").arg(f ? f->displayName : format);
    if (video.enabled && f && f->hasVideo()) {
        const VideoCodecDef *c = Formats::videoCodec(video.codec);
        QString line = QStringLiteral("Video: %1").arg(c ? c->displayName : video.codec);
        if (c && !video.profile.isEmpty() && !c->profiles.isEmpty())
            line += QStringLiteral(", %1").arg(c->profileLabel(video.profile));
        lines << line;

        const int w = video.width > 0 ? video.width : (source ? source->displayWidth() : 0);
        const int h = video.height > 0 ? video.height : (source ? source->displayHeight() : 0);
        const double fps = video.frameRate > 0 ? video.frameRate : (source ? source->fps : 0);
        QString frame = (w > 0 && h > 0) ? QStringLiteral("%1x%2").arg(w).arg(h) : QStringLiteral("Match Source");
        if (fps > 0)
            frame += QStringLiteral(" (%1 fps)").arg(fps, 0, 'f', fps == std::floor(fps) ? 0 : 3);
        else
            frame += QStringLiteral(", Match Source frame rate");
        if (video.fieldOrder != "progressive")
            frame += video.fieldOrder == "tff" ? QStringLiteral(", Upper First") : QStringLiteral(", Lower First");
        else
            frame += QStringLiteral(", Progressive");
        lines << QStringLiteral("  %1").arg(frame);

        if (video.rateControl == "quality")
            lines << QStringLiteral("  Constant Quality %1").arg(video.quality);
        else if (video.rateControl == "cbr")
            lines << QStringLiteral("  CBR, Target %1 Mbps").arg(video.targetBitrate, 0, 'f', 1);
        else if (video.rateControl == "vbr1" || video.rateControl == "vbr2")
            lines << QStringLiteral("  %1, Target %2 Mbps, Max %3 Mbps")
                         .arg(Formats::rateControlLabel(video.rateControl))
                         .arg(video.targetBitrate, 0, 'f', 1)
                         .arg(video.maxBitrate, 0, 'f', 1);
        if (f->imageFormat)
            lines << (video.imageSequence ? QStringLiteral("  Image sequence")
                                          : QStringLiteral("  Single still frame"));
    } else if (f && f->hasVideo()) {
        lines << QStringLiteral("Video: disabled");
    }

    if (audio.enabled && f && f->hasAudio()) {
        const AudioCodecDef *c = Formats::audioCodec(audio.codec);
        QString line = QStringLiteral("Audio: %1").arg(c ? c->displayName : audio.codec);
        const QString rate =
            audio.sampleRate > 0 ? QStringLiteral("%1 Hz").arg(audio.sampleRate) : QStringLiteral("source rate");
        const QString ch = audio.channels == 1   ? QStringLiteral("Mono")
                           : audio.channels == 2 ? QStringLiteral("Stereo")
                           : audio.channels == 6 ? QStringLiteral("5.1")
                           : audio.channels == 0 ? QStringLiteral("source channels")
                                                 : QStringLiteral("%1 ch").arg(audio.channels);
        line += QStringLiteral(", %1, %2").arg(rate, ch);
        if (c && !c->lossless)
            line += QStringLiteral(", %1 kbps").arg(audio.bitrate);
        lines << line;
    } else if (f && f->hasAudio()) {
        lines << QStringLiteral("Audio: disabled");
    }

    QStringList fx;
    if (effects.lutEnabled)
        fx << QStringLiteral("LUT (%1)").arg(QFileInfo(effects.lutPath).fileName());
    if (effects.imageOverlayEnabled)
        fx << QStringLiteral("Image Overlay");
    if (effects.textOverlayEnabled)
        fx << QStringLiteral("Text Overlay");
    if (effects.timecodeEnabled)
        fx << QStringLiteral("Timecode Overlay");
    if (effects.limiterEnabled)
        fx << QStringLiteral("Video Limiter");
    if (effects.loudnessEnabled)
        fx << QStringLiteral("Loudness Normalization (%1 LUFS)").arg(effects.loudnessTarget);
    if (effects.captionsEnabled)
        fx << QStringLiteral("Burn-in Captions");
    if (effects.cropEnabled)
        fx << QStringLiteral("Crop");
    if (!fx.isEmpty())
        lines << QStringLiteral("Effects: %1").arg(fx.join(", "));
    return lines.join('\n');
}

qint64 ExportSettings::estimateSize(double seconds, const MediaInfo *source) const
{
    if (seconds <= 0)
        return -1;
    const FormatDef *f = Formats::format(format);
    if (!f)
        return -1;
    double videoBps = 0.0;
    if (video.enabled && f->hasVideo()) {
        const int w = video.width > 0 ? video.width : (source ? source->displayWidth() : 1920);
        const int h = video.height > 0 ? video.height : (source ? source->displayHeight() : 1080);
        const double fps = video.frameRate > 0 ? video.frameRate : (source && source->fps > 0 ? source->fps : 29.97);
        const double scale = (double(w) * h * fps) / (1920.0 * 1080.0 * 29.97);
        if (video.rateControl == "cbr" || video.rateControl == "vbr1" || video.rateControl == "vbr2") {
            videoBps = video.targetBitrate * 1e6;
        } else if (video.codec == "prores_ks") {
            static const double rates[] = {45, 102, 147, 220, 330, 500};
            const int p = qBound(0, video.profile.toInt(), 5);
            videoBps = rates[p] * 1e6 * scale;
        } else if (video.codec == "dnxhd") {
            double base = 220;
            if (video.profile == "dnxhr_lb")
                base = 45;
            else if (video.profile == "dnxhr_sq")
                base = 145;
            else if (video.profile == "dnxhr_444")
                base = 440;
            videoBps = base * 1e6 * scale;
        } else {
            return -1;
        }
    }
    double audioBps = 0.0;
    if (audio.enabled && f->hasAudio()) {
        const AudioCodecDef *c = Formats::audioCodec(audio.codec);
        if (c && !c->lossless) {
            audioBps = audio.bitrate * 1000.0;
        } else if (c) {
            const int rate = audio.sampleRate > 0 ? audio.sampleRate : (source ? source->sampleRate : 48000);
            const int ch = audio.channels > 0 ? audio.channels : (source ? source->channels : 2);
            int bits = 16;
            if (audio.codec.contains("24"))
                bits = 24;
            else if (audio.codec.contains("32"))
                bits = 32;
            audioBps = double(rate) * ch * bits * (audio.codec == "flac" ? 0.6 : 1.0);
        }
    }
    return qint64((videoBps + audioBps) * seconds / 8.0);
}
