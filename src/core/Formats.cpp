#include "Formats.h"

#include <QFileInfo>
#include <QSet>

QString VideoCodecDef::profileLabel(const QString &profile) const
{
    const int i = profiles.indexOf(profile);
    if (i >= 0 && i < profileLabels.size())
        return profileLabels.at(i);
    return profile;
}

namespace {

QStringList x26xSpeeds()
{
    return {"ultrafast", "superfast", "veryfast", "faster", "fast", "medium", "slow", "slower", "veryslow"};
}
QStringList nvencSpeeds() { return {"p1", "p2", "p3", "p4", "p5", "p6", "p7"}; }
QStringList qsvSpeeds() { return {"veryfast", "faster", "fast", "medium", "slow", "slower", "veryslow"}; }
QStringList amfSpeeds() { return {"speed", "balanced", "quality"}; }
QStringList h264Levels()
{
    return {"auto", "3.0", "3.1", "3.2", "4.0", "4.1", "4.2", "5.0", "5.1", "5.2", "6.0", "6.1", "6.2"};
}
QStringList hevcLevels() { return {"auto", "3.0", "3.1", "4.0", "4.1", "5.0", "5.1", "5.2", "6.0", "6.1", "6.2"}; }

VideoCodecDef make(const QString &enc, const QString &name)
{
    VideoCodecDef c;
    c.encoder = enc;
    c.displayName = name;
    return c;
}

QList<VideoCodecDef> buildVideoCodecs()
{
    QList<VideoCodecDef> l;
    const QStringList bitrateModes = {"cbr", "vbr1", "vbr2", "quality"};

    // ---- H.264 ----
    {
        auto c = make("libx264", "H.264 (Software Encoding)");
        c.speedArg = "-preset";
        c.speedPresets = x26xSpeeds();
        c.defaultSpeed = "medium";
        c.profiles = {"baseline", "main", "high", "high10", "high422"};
        c.profileLabels = {"Baseline", "Main", "High", "High 10", "High 4:2:2"};
        c.defaultProfile = "high";
        c.levels = h264Levels();
        c.pixelFormats = {"yuv420p", "yuv422p", "yuv420p10le", "yuv422p10le"};
        c.defaultPixelFormat = "yuv420p";
        c.rateControls = bitrateModes;
        c.defaultRateControl = "vbr1";
        c.qualityArg = "-crf";
        c.qualityMin = 0;
        c.qualityMax = 51;
        c.defaultQuality = 20;
        l << c;
    }
    {
        auto c = make("h264_nvenc", "H.264 (Hardware - NVIDIA NVENC)");
        c.hardware = true;
        c.speedArg = "-preset";
        c.speedPresets = nvencSpeeds();
        c.defaultSpeed = "p5";
        c.profiles = {"baseline", "main", "high"};
        c.profileLabels = {"Baseline", "Main", "High"};
        c.defaultProfile = "high";
        c.levels = h264Levels();
        c.pixelFormats = {"yuv420p", "yuv444p"};
        c.defaultPixelFormat = "yuv420p";
        c.rateControls = bitrateModes;
        c.defaultRateControl = "vbr1";
        c.qualityArg = "-cq";
        c.qualityMin = 1;
        c.qualityMax = 51;
        c.defaultQuality = 23;
        l << c;
    }
    {
        auto c = make("h264_qsv", "H.264 (Hardware - Intel Quick Sync)");
        c.hardware = true;
        c.speedArg = "-preset";
        c.speedPresets = qsvSpeeds();
        c.defaultSpeed = "medium";
        c.profiles = {"baseline", "main", "high"};
        c.profileLabels = {"Baseline", "Main", "High"};
        c.defaultProfile = "high";
        c.levels = h264Levels();
        c.pixelFormats = {"nv12"};
        c.defaultPixelFormat = "nv12";
        c.rateControls = {"cbr", "vbr1", "quality"};
        c.defaultRateControl = "vbr1";
        c.qualityArg = "-global_quality";
        c.qualityMin = 1;
        c.qualityMax = 51;
        c.defaultQuality = 23;
        l << c;
    }
    {
        auto c = make("h264_amf", "H.264 (Hardware - AMD AMF)");
        c.hardware = true;
        c.speedArg = "-quality";
        c.speedLabel = "Quality Preset";
        c.speedPresets = amfSpeeds();
        c.defaultSpeed = "balanced";
        c.profiles = {"main", "high", "constrained_baseline"};
        c.profileLabels = {"Main", "High", "Constrained Baseline"};
        c.defaultProfile = "high";
        c.levels = h264Levels();
        c.pixelFormats = {"nv12"};
        c.defaultPixelFormat = "nv12";
        c.rateControls = {"cbr", "vbr1", "quality"};
        c.defaultRateControl = "vbr1";
        c.qualityArg = "-qp";
        c.qualityMin = 0;
        c.qualityMax = 51;
        c.defaultQuality = 22;
        l << c;
    }

    // ---- HEVC ----
    {
        auto c = make("libx265", "HEVC (Software Encoding)");
        c.speedArg = "-preset";
        c.speedPresets = x26xSpeeds();
        c.defaultSpeed = "medium";
        c.profiles = {"main", "main10", "main422-10", "main444-8"};
        c.profileLabels = {"Main", "Main 10", "Main 4:2:2 10", "Main 4:4:4"};
        c.defaultProfile = "main";
        c.levels = hevcLevels();
        c.pixelFormats = {"yuv420p", "yuv420p10le", "yuv422p10le", "yuv444p"};
        c.defaultPixelFormat = "yuv420p";
        c.rateControls = bitrateModes;
        c.defaultRateControl = "vbr1";
        c.qualityArg = "-crf";
        c.qualityMin = 0;
        c.qualityMax = 51;
        c.defaultQuality = 24;
        l << c;
    }
    {
        auto c = make("hevc_nvenc", "HEVC (Hardware - NVIDIA NVENC)");
        c.hardware = true;
        c.speedArg = "-preset";
        c.speedPresets = nvencSpeeds();
        c.defaultSpeed = "p5";
        c.profiles = {"main", "main10"};
        c.profileLabels = {"Main", "Main 10"};
        c.defaultProfile = "main";
        c.levels = hevcLevels();
        c.pixelFormats = {"yuv420p", "p010le"};
        c.defaultPixelFormat = "yuv420p";
        c.rateControls = bitrateModes;
        c.defaultRateControl = "vbr1";
        c.qualityArg = "-cq";
        c.qualityMin = 1;
        c.qualityMax = 51;
        c.defaultQuality = 25;
        l << c;
    }
    {
        auto c = make("hevc_qsv", "HEVC (Hardware - Intel Quick Sync)");
        c.hardware = true;
        c.speedArg = "-preset";
        c.speedPresets = qsvSpeeds();
        c.defaultSpeed = "medium";
        c.profiles = {"main", "main10"};
        c.profileLabels = {"Main", "Main 10"};
        c.defaultProfile = "main";
        c.levels = hevcLevels();
        c.pixelFormats = {"nv12", "p010le"};
        c.defaultPixelFormat = "nv12";
        c.rateControls = {"cbr", "vbr1", "quality"};
        c.defaultRateControl = "vbr1";
        c.qualityArg = "-global_quality";
        c.qualityMin = 1;
        c.qualityMax = 51;
        c.defaultQuality = 25;
        l << c;
    }
    {
        auto c = make("hevc_amf", "HEVC (Hardware - AMD AMF)");
        c.hardware = true;
        c.speedArg = "-quality";
        c.speedLabel = "Quality Preset";
        c.speedPresets = amfSpeeds();
        c.defaultSpeed = "balanced";
        c.profiles = {"main", "main10"};
        c.profileLabels = {"Main", "Main 10"};
        c.defaultProfile = "main";
        c.levels = hevcLevels();
        c.pixelFormats = {"nv12", "p010le"};
        c.defaultPixelFormat = "nv12";
        c.rateControls = {"cbr", "vbr1", "quality"};
        c.defaultRateControl = "vbr1";
        c.qualityArg = "-qp";
        c.qualityMin = 0;
        c.qualityMax = 51;
        c.defaultQuality = 24;
        l << c;
    }

    // ---- AV1 ----
    {
        auto c = make("libaom-av1", "AV1 (Software - libaom)");
        c.speedArg = "-cpu-used";
        c.speedLabel = "CPU Used (0 = slowest)";
        c.speedPresets = {"0", "1", "2", "3", "4", "5", "6", "7", "8"};
        c.defaultSpeed = "6";
        c.pixelFormats = {"yuv420p", "yuv420p10le"};
        c.defaultPixelFormat = "yuv420p";
        c.rateControls = {"vbr1", "vbr2", "quality"};
        c.defaultRateControl = "quality";
        c.qualityArg = "-crf";
        c.qualityMin = 0;
        c.qualityMax = 63;
        c.defaultQuality = 30;
        l << c;
    }
    {
        auto c = make("av1_nvenc", "AV1 (Hardware - NVIDIA NVENC)");
        c.hardware = true;
        c.speedArg = "-preset";
        c.speedPresets = nvencSpeeds();
        c.defaultSpeed = "p5";
        c.pixelFormats = {"yuv420p", "p010le"};
        c.defaultPixelFormat = "yuv420p";
        c.rateControls = bitrateModes;
        c.defaultRateControl = "vbr1";
        c.qualityArg = "-cq";
        c.qualityMin = 1;
        c.qualityMax = 51;
        c.defaultQuality = 28;
        l << c;
    }
    {
        auto c = make("av1_qsv", "AV1 (Hardware - Intel Quick Sync)");
        c.hardware = true;
        c.speedArg = "-preset";
        c.speedPresets = qsvSpeeds();
        c.defaultSpeed = "medium";
        c.pixelFormats = {"nv12", "p010le"};
        c.defaultPixelFormat = "nv12";
        c.rateControls = {"cbr", "vbr1", "quality"};
        c.defaultRateControl = "vbr1";
        c.qualityArg = "-global_quality";
        c.qualityMin = 1;
        c.qualityMax = 51;
        c.defaultQuality = 28;
        l << c;
    }

    // ---- VP8 / VP9 ----
    {
        auto c = make("libvpx-vp9", "VP9");
        c.speedArg = "-deadline";
        c.speedLabel = "Deadline";
        c.speedPresets = {"realtime", "good", "best"};
        c.defaultSpeed = "good";
        c.pixelFormats = {"yuv420p", "yuva420p", "yuv420p10le"};
        c.defaultPixelFormat = "yuv420p";
        c.rateControls = bitrateModes;
        c.defaultRateControl = "quality";
        c.qualityArg = "-crf";
        c.qualityMin = 0;
        c.qualityMax = 63;
        c.defaultQuality = 31;
        c.alphaCapable = true;
        l << c;
    }
    {
        auto c = make("libvpx", "VP8");
        c.speedArg = "-deadline";
        c.speedLabel = "Deadline";
        c.speedPresets = {"realtime", "good", "best"};
        c.defaultSpeed = "good";
        c.pixelFormats = {"yuv420p", "yuva420p"};
        c.defaultPixelFormat = "yuv420p";
        c.rateControls = bitrateModes;
        c.defaultRateControl = "quality";
        c.qualityArg = "-crf";
        c.qualityMin = 4;
        c.qualityMax = 63;
        c.defaultQuality = 10;
        c.alphaCapable = true;
        l << c;
    }

    // ---- Intermediate / mastering codecs ----
    {
        auto c = make("prores_ks", "Apple ProRes");
        c.profiles = {"0", "1", "2", "3", "4", "5"};
        c.profileLabels = {"Apple ProRes 422 Proxy", "Apple ProRes 422 LT", "Apple ProRes 422",
                           "Apple ProRes 422 HQ",    "Apple ProRes 4444",   "Apple ProRes 4444 XQ"};
        c.defaultProfile = "3";
        c.pixelFormats = {"yuv422p10le", "yuv444p10le", "yuva444p10le"};
        c.rateControls = {"none"};
        c.defaultRateControl = "none";
        c.alphaCapable = true;
        l << c;
    }
    {
        auto c = make("dnxhd", "Avid DNxHR / DNxHD");
        c.profiles = {"dnxhr_lb", "dnxhr_sq", "dnxhr_hq", "dnxhr_hqx", "dnxhr_444"};
        c.profileLabels = {"DNxHR LB (8-bit 4:2:2)", "DNxHR SQ (8-bit 4:2:2)", "DNxHR HQ (8-bit 4:2:2)",
                           "DNxHR HQX (10-bit 4:2:2)", "DNxHR 444 (10-bit 4:4:4)"};
        c.defaultProfile = "dnxhr_hq";
        c.pixelFormats = {"yuv422p", "yuv422p10le", "yuv444p10le"};
        c.rateControls = {"none"};
        c.defaultRateControl = "none";
        l << c;
    }
    {
        auto c = make("cfhd", "GoPro CineForm");
        c.speedArg = "-quality";
        c.speedLabel = "Quality";
        c.speedPresets = {"film3+", "film3", "film2+",  "film2",  "film1.5", "film1+", "film1",
                          "high+",  "high",  "medium+", "medium", "low+",    "low"};
        c.defaultSpeed = "film1";
        c.pixelFormats = {"yuv422p10le", "gbrp12le", "gbrap12le"};
        c.defaultPixelFormat = "yuv422p10le";
        c.rateControls = {"none"};
        c.defaultRateControl = "none";
        c.alphaCapable = true;
        l << c;
    }
    {
        auto c = make("qtrle", "Animation (QuickTime RLE)");
        c.pixelFormats = {"rgb24", "argb"};
        c.rateControls = {"none"};
        c.defaultRateControl = "none";
        c.alphaCapable = true;
        l << c;
    }
    {
        auto c = make("mpeg2video", "MPEG-2 Video");
        c.pixelFormats = {"yuv420p", "yuv422p"};
        c.defaultPixelFormat = "yuv420p";
        c.rateControls = bitrateModes;
        c.defaultRateControl = "vbr1";
        c.qualityArg = "-q:v";
        c.qualityMin = 1;
        c.qualityMax = 31;
        c.defaultQuality = 3;
        l << c;
    }

    // ---- Image codecs ----
    {
        auto c = make("png", "PNG");
        c.pixelFormats = {"rgb24", "rgba", "rgb48be", "rgba64be"};
        c.rateControls = {"none"};
        c.defaultRateControl = "none";
        c.alphaCapable = true;
        l << c;
    }
    {
        auto c = make("mjpeg", "JPEG");
        c.pixelFormats = {"yuvj420p", "yuvj422p", "yuvj444p"};
        c.defaultPixelFormat = "yuvj444p";
        c.rateControls = {"quality"};
        c.defaultRateControl = "quality";
        c.qualityArg = "-q:v";
        c.qualityMin = 1;
        c.qualityMax = 31;
        c.defaultQuality = 2;
        l << c;
    }
    {
        auto c = make("tiff", "TIFF");
        c.speedArg = "-compression_algo";
        c.speedLabel = "Compression";
        c.speedPresets = {"raw", "packbits", "lzw", "deflate"};
        c.defaultSpeed = "lzw";
        c.pixelFormats = {"rgb24", "rgba", "rgb48le", "rgba64le"};
        c.rateControls = {"none"};
        c.defaultRateControl = "none";
        c.alphaCapable = true;
        l << c;
    }
    {
        auto c = make("dpx", "DPX");
        c.pixelFormats = {"rgb24", "gbrp10le", "gbrp12le", "rgb48le"};
        c.defaultPixelFormat = "gbrp10le";
        c.rateControls = {"none"};
        c.defaultRateControl = "none";
        l << c;
    }
    {
        auto c = make("bmp", "BMP");
        c.pixelFormats = {"bgr24", "bgra"};
        c.rateControls = {"none"};
        c.defaultRateControl = "none";
        c.alphaCapable = true;
        l << c;
    }
    {
        auto c = make("targa", "Targa");
        c.pixelFormats = {"bgr24", "bgra"};
        c.rateControls = {"none"};
        c.defaultRateControl = "none";
        c.alphaCapable = true;
        l << c;
    }
    {
        auto c = make("gif", "GIF");
        c.rateControls = {"none"};
        c.defaultRateControl = "none";
        c.alphaCapable = true;
        l << c;
    }
    return l;
}

AudioCodecDef makeAudio(const QString &enc, const QString &name, bool lossless, QList<int> bitrates, int def,
                        QList<int> rates, int maxCh)
{
    AudioCodecDef a;
    a.encoder = enc;
    a.displayName = name;
    a.lossless = lossless;
    a.bitrates = bitrates;
    a.defaultBitrate = def;
    a.sampleRates = rates;
    a.maxChannels = maxCh;
    return a;
}

QList<AudioCodecDef> buildAudioCodecs()
{
    const QList<int> pcmRates = {32000, 44100, 48000, 88200, 96000, 192000};
    QList<AudioCodecDef> l;
    l << makeAudio("aac", "AAC", false, {64, 96, 128, 160, 192, 256, 320, 384}, 320,
                   {32000, 44100, 48000, 88200, 96000}, 8);
    l << makeAudio("libmp3lame", "MP3", false, {96, 128, 160, 192, 224, 256, 320}, 320, {32000, 44100, 48000}, 2);
    l << makeAudio("libopus", "Opus", false, {64, 96, 128, 160, 192, 256, 320, 384, 510}, 192, {48000}, 8);
    l << makeAudio("libvorbis", "Vorbis", false, {96, 128, 160, 192, 256, 320}, 256, {44100, 48000}, 8);
    l << makeAudio("ac3", "Dolby Digital (AC-3)", false, {192, 224, 256, 320, 384, 448, 640}, 384,
                   {32000, 44100, 48000}, 6);
    l << makeAudio("mp2", "MPEG-1 Layer II", false, {128, 192, 224, 256, 320, 384}, 256, {32000, 44100, 48000}, 2);
    l << makeAudio("flac", "FLAC", true, {}, 0, pcmRates, 8);
    l << makeAudio("pcm_s16le", "Uncompressed 16-bit PCM", true, {}, 0, pcmRates, 16);
    l << makeAudio("pcm_s24le", "Uncompressed 24-bit PCM", true, {}, 0, pcmRates, 16);
    l << makeAudio("pcm_s32le", "Uncompressed 32-bit PCM", true, {}, 0, pcmRates, 16);
    l << makeAudio("pcm_f32le", "Uncompressed 32-bit Float PCM", true, {}, 0, pcmRates, 16);
    l << makeAudio("pcm_s16be", "Uncompressed 16-bit PCM (Big Endian)", true, {}, 0, pcmRates, 16);
    l << makeAudio("pcm_s24be", "Uncompressed 24-bit PCM (Big Endian)", true, {}, 0, pcmRates, 16);
    return l;
}

FormatDef makeFormat(const QString &id, const QString &name, const QString &ext, const QString &muxer, QStringList v,
                     QStringList a, const QString &desc, bool image = false)
{
    FormatDef f;
    f.id = id;
    f.displayName = name;
    f.extension = ext;
    f.muxer = muxer;
    f.videoCodecs = v;
    f.audioCodecs = a;
    f.description = desc;
    f.imageFormat = image;
    return f;
}

QList<FormatDef> buildFormats()
{
    QList<FormatDef> l;
    l << makeFormat("h264", "H.264", "mp4", "mp4", {"libx264", "h264_nvenc", "h264_qsv", "h264_amf"},
                    {"aac", "ac3", "libmp3lame"},
                    "MPEG-4 AVC in an MP4 container. Best for web delivery and playback.");
    l << makeFormat("hevc", "HEVC (H.265)", "mp4", "mp4", {"libx265", "hevc_nvenc", "hevc_qsv", "hevc_amf"},
                    {"aac", "ac3"},
                    "High Efficiency Video Coding in an MP4 container. Smaller files, 10-bit/HDR capable.");
    l << makeFormat("av1", "AV1", "mp4", "mp4", {"av1_nvenc", "libaom-av1", "av1_qsv"}, {"aac", "libopus"},
                    "AOMedia Video 1 in an MP4 container.");
    l << makeFormat(
        "quicktime", "QuickTime", "mov", "mov",
        {"prores_ks", "dnxhd", "cfhd", "qtrle", "png", "mjpeg", "libx264", "h264_nvenc", "libx265", "hevc_nvenc"},
        {"pcm_s16le", "pcm_s24le", "pcm_s32le", "pcm_f32le", "aac"},
        "Apple QuickTime movie. Mastering and intermediate codecs (ProRes, DNxHR, CineForm, Animation).");
    l << makeFormat("mxf", "MXF OP1a", "mxf", "mxf", {"dnxhd", "mpeg2video"}, {"pcm_s16le", "pcm_s24le"},
                    "Material eXchange Format (OP1a) for broadcast and editorial workflows.");
    l << makeFormat("mpeg2", "MPEG-2", "mpg", "mpeg", {"mpeg2video"}, {"mp2", "ac3"}, "MPEG-2 program stream.");
    l << makeFormat("webm", "WebM", "webm", "webm", {"libvpx-vp9", "libvpx", "libaom-av1"}, {"libopus", "libvorbis"},
                    "Open web video format (VP9/VP8/AV1 with Opus/Vorbis).");
    l << makeFormat("avi", "AVI", "avi", "avi", {"mjpeg", "dnxhd", "cfhd", "libx264"},
                    {"pcm_s16le", "pcm_s24le", "libmp3lame"}, "Audio Video Interleave.");
    l << makeFormat("gif", "Animated GIF", "gif", "gif", {"gif"}, {},
                    "Animated GIF with an optimized per-clip palette.");
    l << makeFormat("mp3", "MP3", "mp3", "mp3", {}, {"libmp3lame"}, "MPEG-1 Audio Layer 3.");
    l << makeFormat("aac", "AAC Audio", "m4a", "ipod", {}, {"aac"}, "AAC audio in an MPEG-4 (.m4a) container.");
    l << makeFormat("wav", "Waveform Audio", "wav", "wav", {}, {"pcm_s16le", "pcm_s24le", "pcm_s32le", "pcm_f32le"},
                    "Uncompressed WAV audio.");
    l << makeFormat("aiff", "AIFF", "aif", "aiff", {}, {"pcm_s16be", "pcm_s24be"}, "Audio Interchange File Format.");
    l << makeFormat("flac", "FLAC", "flac", "flac", {}, {"flac"}, "Free Lossless Audio Codec.");
    l << makeFormat("png", "PNG", "png", "image2", {"png"}, {}, "PNG still image or image sequence.", true);
    l << makeFormat("jpeg", "JPEG", "jpg", "image2", {"mjpeg"}, {}, "JPEG still image or image sequence.", true);
    l << makeFormat("tiff", "TIFF", "tif", "image2", {"tiff"}, {}, "TIFF still image or image sequence.", true);
    l << makeFormat("dpx", "DPX", "dpx", "image2", {"dpx"}, {}, "Digital Picture Exchange image sequence.", true);
    l << makeFormat("bmp", "BMP", "bmp", "image2", {"bmp"}, {}, "Windows Bitmap still image or sequence.", true);
    l << makeFormat("targa", "Targa", "tga", "image2", {"targa"}, {}, "Targa still image or sequence.", true);
    return l;
}

} // namespace

namespace Formats {

const QList<FormatDef> &all()
{
    static const QList<FormatDef> list = buildFormats();
    return list;
}

const FormatDef *format(const QString &id)
{
    for (const auto &f : all())
        if (f.id == id)
            return &f;
    return nullptr;
}

const QList<VideoCodecDef> &videoCodecs()
{
    static const QList<VideoCodecDef> list = buildVideoCodecs();
    return list;
}

const QList<AudioCodecDef> &audioCodecs()
{
    static const QList<AudioCodecDef> list = buildAudioCodecs();
    return list;
}

const VideoCodecDef *videoCodec(const QString &encoder)
{
    for (const auto &c : videoCodecs())
        if (c.encoder == encoder)
            return &c;
    return nullptr;
}

const AudioCodecDef *audioCodec(const QString &encoder)
{
    for (const auto &c : audioCodecs())
        if (c.encoder == encoder)
            return &c;
    return nullptr;
}

QString rateControlLabel(const QString &rc)
{
    if (rc == "cbr")
        return QStringLiteral("CBR");
    if (rc == "vbr1")
        return QStringLiteral("VBR, 1 pass");
    if (rc == "vbr2")
        return QStringLiteral("VBR, 2 pass");
    if (rc == "quality")
        return QStringLiteral("Constant Quality");
    return QStringLiteral("Codec Default");
}

QStringList mediaFileExtensions()
{
    return {"mp4", "m4v",  "mov",  "mxf",  "avi", "mkv",  "webm", "wmv",  "flv",  "mts", "m2ts",
            "ts",  "mpg",  "mpeg", "m2v",  "vob", "3gp",  "dv",   "ogv",  "gif",  "y4m", "mp3",
            "wav", "aif",  "aiff", "m4a",  "aac", "flac", "ogg",  "opus", "wma",  "ac3", "png",
            "jpg", "jpeg", "tif",  "tiff", "dpx", "exr",  "bmp",  "tga",  "webp", "psd"};
}

bool isMediaFile(const QString &path) { return mediaFileExtensions().contains(QFileInfo(path).suffix().toLower()); }

MediaKind mediaKind(const QString &path)
{
    static const QSet<QString> audio = {"mp3", "wav", "aif", "aiff", "m4a", "aac", "flac", "ogg", "opus", "wma", "ac3"};
    static const QSet<QString> image = {"png", "jpg", "jpeg", "tif", "tiff", "dpx", "exr", "bmp", "tga", "webp", "psd"};
    const QString ext = QFileInfo(path).suffix().toLower();
    if (audio.contains(ext))
        return MediaKind::Audio;
    if (image.contains(ext))
        return MediaKind::Image;
    if (mediaFileExtensions().contains(ext))
        return MediaKind::Video;
    return MediaKind::Other;
}

QString mediaKindName(MediaKind kind)
{
    switch (kind) {
    case MediaKind::Video:
        return QStringLiteral("Video");
    case MediaKind::Audio:
        return QStringLiteral("Audio");
    case MediaKind::Image:
        return QStringLiteral("Image");
    case MediaKind::Other:
        break;
    }
    return QStringLiteral("Other");
}

} // namespace Formats
