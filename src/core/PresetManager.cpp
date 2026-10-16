#include "PresetManager.h"

#include "AppSettings.h"
#include "EncoderCaps.h"
#include "Formats.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRegularExpression>

namespace {

ExportSettings base(const QString &format, const QString &name)
{
    ExportSettings s;
    s.setFormat(format);
    s.presetName = name;
    return s;
}

QString firstHardware(const QStringList &candidates)
{
    for (const QString &c : candidates)
        if (EncoderCaps::instance().hasEncoder(c))
            return c;
    return {};
}

void bitrate(ExportSettings &s, double target, double max, const QString &rc = "vbr1")
{
    s.video.rateControl = rc;
    s.video.targetBitrate = target;
    s.video.maxBitrate = max;
}

void frame(ExportSettings &s, int w, int h, double fps = 0, const QString &scale = "fit")
{
    s.video.width = w;
    s.video.height = h;
    s.video.frameRate = fps;
    s.video.scaleMode = scale;
}

void pcm(ExportSettings &s, const QString &codec, int rate = 48000, int ch = 0)
{
    s.audio.codec = codec;
    s.audio.sampleRate = rate;
    s.audio.channels = ch;
}

QString safeFileName(const QString &name)
{
    QString n = name;
    n.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|]")), QStringLiteral("_"));
    return n.trimmed();
}

} // namespace

PresetManager &PresetManager::instance()
{
    static PresetManager pm;
    return pm;
}

void PresetManager::load()
{
    m_presets.clear();
    buildSystemPresets();
    loadUserPresets();
    emit presetsChanged();
}

void PresetManager::buildSystemPresets()
{
    QList<ExportSettings> list;
    auto add = [&](const ExportSettings &s) { list << s; };
    auto withCodec = [](ExportSettings s, const QString &codec) {
        s.video.codec = codec;
        s.applyVideoCodecDefaults();
        return s;
    };

    // ---------------- H.264
    {
        auto s = base("h264", "Match Source - High bitrate");
        bitrate(s, 10, 12);
        add(s);
        s = base("h264", "Match Source - Medium bitrate");
        bitrate(s, 5, 6);
        s.audio.bitrate = 192;
        add(s);
        const QString hw = firstHardware({"h264_nvenc", "h264_qsv", "h264_amf"});
        if (!hw.isEmpty()) {
            s = withCodec(base("h264", "Match Source - Hardware Encoding"), hw);
            bitrate(s, 10, 12);
            add(s);
            s = withCodec(base("h264", "Fast 1080p - Hardware Encoding"), hw);
            frame(s, 1920, 1080);
            bitrate(s, 16, 20);
            add(s);
        }
        s = base("h264", "High Quality 2160p 4K");
        frame(s, 3840, 2160);
        bitrate(s, 40, 48);
        add(s);
        s = base("h264", "High Quality 1080p HD");
        frame(s, 1920, 1080);
        bitrate(s, 20, 24);
        add(s);
        s = base("h264", "High Quality 720p HD");
        frame(s, 1280, 720);
        bitrate(s, 10, 12);
        add(s);
        s = base("h264", "YouTube 2160p 4K");
        frame(s, 3840, 2160);
        bitrate(s, 40, 48);
        s.video.keyframeInterval = 0;
        add(s);
        s = base("h264", "YouTube 1080p HD");
        frame(s, 1920, 1080);
        bitrate(s, 16, 20);
        add(s);
        s = base("h264", "YouTube 720p HD");
        frame(s, 1280, 720);
        bitrate(s, 8, 10);
        add(s);
        s = base("h264", "YouTube 480p SD Wide");
        frame(s, 854, 480);
        bitrate(s, 2.5, 4);
        s.audio.bitrate = 192;
        add(s);
        s = base("h264", "Vimeo 2160p 4K");
        frame(s, 3840, 2160);
        bitrate(s, 30, 40);
        add(s);
        s = base("h264", "Vimeo 1080p HD");
        frame(s, 1920, 1080);
        bitrate(s, 10, 12);
        add(s);
        s = base("h264", "Vimeo 720p HD");
        frame(s, 1280, 720);
        bitrate(s, 5, 6);
        add(s);
        s = base("h264", "Facebook 1080p HD");
        frame(s, 1920, 1080);
        bitrate(s, 8, 10);
        s.audio.bitrate = 192;
        add(s);
        s = base("h264", "Facebook 720p HD");
        frame(s, 1280, 720);
        bitrate(s, 4, 5);
        s.audio.bitrate = 192;
        add(s);
        s = base("h264", "Twitter 1080p HD");
        frame(s, 1920, 1080);
        bitrate(s, 8, 10);
        s.audio.bitrate = 192;
        add(s);
        s = base("h264", "Twitter 720p HD");
        frame(s, 1280, 720);
        bitrate(s, 5, 6);
        s.audio.bitrate = 192;
        add(s);
        s = base("h264", "Twitter 640x640");
        frame(s, 640, 640, 0, "fill");
        bitrate(s, 2, 3);
        s.audio.bitrate = 128;
        add(s);
        s = base("h264", "Vertical 9x16 1080x1920");
        frame(s, 1080, 1920, 0, "fill");
        bitrate(s, 12, 16);
        add(s);
        s = base("h264", "Square 1x1 1080x1080");
        frame(s, 1080, 1080, 0, "fill");
        bitrate(s, 10, 12);
        add(s);
        s = base("h264", "Low Resolution Proxy 1280x720");
        frame(s, 1280, 720);
        s.video.rateControl = "quality";
        s.video.quality = 28;
        s.video.speedPreset = "veryfast";
        s.audio.bitrate = 128;
        add(s);
    }
    // ---------------- HEVC
    {
        auto s = base("hevc", "Match Source - High bitrate");
        bitrate(s, 10, 12);
        add(s);
        s = base("hevc", "Match Source - Medium bitrate");
        bitrate(s, 5, 6);
        s.audio.bitrate = 192;
        add(s);
        s = base("hevc", "Match Source - Main 10");
        s.video.profile = "main10";
        bitrate(s, 12, 15);
        add(s);
        const QString hw = firstHardware({"hevc_nvenc", "hevc_qsv", "hevc_amf"});
        if (!hw.isEmpty()) {
            s = withCodec(base("hevc", "Match Source - Hardware Encoding"), hw);
            bitrate(s, 10, 12);
            add(s);
            s = withCodec(base("hevc", "4K UHD - Hardware Encoding"), hw);
            frame(s, 3840, 2160);
            bitrate(s, 25, 30);
            add(s);
            s = withCodec(base("hevc", "Match Source - Hardware Main 10"), hw);
            s.video.profile = "main10";
            bitrate(s, 12, 15);
            add(s);
        }
        s = base("hevc", "8K UHD");
        frame(s, 7680, 4320);
        bitrate(s, 60, 72);
        add(s);
        s = base("hevc", "4K UHD");
        frame(s, 3840, 2160);
        bitrate(s, 25, 30);
        add(s);
        s = base("hevc", "HD 1080p");
        frame(s, 1920, 1080);
        bitrate(s, 8, 10);
        add(s);
        s = base("hevc", "HD 720p");
        frame(s, 1280, 720);
        bitrate(s, 4, 5);
        add(s);
    }
    // ---------------- AV1
    {
        const QString hw = firstHardware({"av1_nvenc", "av1_qsv"});
        if (!hw.isEmpty()) {
            auto s = withCodec(base("av1", "Match Source - Hardware Encoding"), hw);
            bitrate(s, 8, 10);
            add(s);
            s = withCodec(base("av1", "YouTube 2160p 4K - Hardware Encoding"), hw);
            frame(s, 3840, 2160);
            bitrate(s, 30, 36);
            add(s);
        }
        if (EncoderCaps::instance().hasEncoder("libaom-av1")) {
            auto s = withCodec(base("av1", "Match Source - Software Quality"), "libaom-av1");
            s.video.rateControl = "quality";
            s.video.quality = 30;
            add(s);
        }
    }
    // ---------------- QuickTime
    {
        static const char *prores[] = {"Apple ProRes 422 Proxy", "Apple ProRes 422 LT", "Apple ProRes 422",
                                       "Apple ProRes 422 HQ",    "Apple ProRes 4444",   "Apple ProRes 4444 XQ"};
        for (int i = 0; i < 6; ++i) {
            auto s = withCodec(base("quicktime", prores[i]), "prores_ks");
            s.video.profile = QString::number(i);
            pcm(s, "pcm_s16le");
            if (i >= 4)
                s.video.pixelFormat = "yuv444p10le";
            add(s);
        }
        for (int i = 4; i < 6; ++i) {
            auto s = withCodec(base("quicktime", QStringLiteral("%1 with alpha").arg(prores[i])), "prores_ks");
            s.video.profile = QString::number(i);
            s.video.pixelFormat = "yuva444p10le";
            pcm(s, "pcm_s16le");
            add(s);
        }
        auto s = withCodec(base("quicktime", "GoPro CineForm YUV 10-bit"), "cfhd");
        pcm(s, "pcm_s16le");
        add(s);
        s = withCodec(base("quicktime", "GoPro CineForm RGB 12-bit with alpha"), "cfhd");
        s.video.pixelFormat = "gbrap12le";
        pcm(s, "pcm_s16le");
        add(s);
        static const QPair<const char *, const char *> dnx[] = {{"dnxhr_lb", "DNxHR LB"},
                                                                {"dnxhr_sq", "DNxHR SQ"},
                                                                {"dnxhr_hq", "DNxHR HQ"},
                                                                {"dnxhr_hqx", "DNxHR HQX 10-bit"},
                                                                {"dnxhr_444", "DNxHR 444 10-bit"}};
        for (const auto &d : dnx) {
            s = withCodec(base("quicktime", QString::fromLatin1(d.second)), "dnxhd");
            s.video.profile = QString::fromLatin1(d.first);
            pcm(s, "pcm_s24le");
            add(s);
        }
        s = withCodec(base("quicktime", "Animation with Alpha (RLE)"), "qtrle");
        s.video.pixelFormat = "argb";
        pcm(s, "pcm_s16le");
        add(s);
        s = withCodec(base("quicktime", "PNG with Alpha"), "png");
        s.video.pixelFormat = "rgba";
        pcm(s, "pcm_s16le");
        add(s);
        s = withCodec(base("quicktime", "H.264 Match Source"), "libx264");
        bitrate(s, 15, 20);
        pcm(s, "pcm_s16le", 48000, 2);
        add(s);
    }
    // ---------------- MXF OP1a
    {
        static const QPair<const char *, const char *> dnx[] = {{"dnxhr_lb", "DNxHR LB MXF OP1a"},
                                                                {"dnxhr_sq", "DNxHR SQ MXF OP1a"},
                                                                {"dnxhr_hq", "DNxHR HQ MXF OP1a"},
                                                                {"dnxhr_hqx", "DNxHR HQX MXF OP1a"}};
        for (const auto &d : dnx) {
            auto s = withCodec(base("mxf", QString::fromLatin1(d.second)), "dnxhd");
            s.video.profile = QString::fromLatin1(d.first);
            pcm(s, "pcm_s24le", 48000, 0);
            add(s);
        }
    }
    // ---------------- MPEG-2
    {
        auto s = base("mpeg2", "Match Source - High Quality");
        bitrate(s, 15, 20);
        s.audio.codec = "mp2";
        s.audio.bitrate = 256;
        add(s);
        s = base("mpeg2", "HD 1080i 29.97");
        frame(s, 1920, 1080, 29.97);
        s.video.fieldOrder = "tff";
        bitrate(s, 25, 25, "cbr");
        s.audio.codec = "mp2";
        s.audio.bitrate = 256;
        add(s);
        s = base("mpeg2", "HD 1080p 25");
        frame(s, 1920, 1080, 25);
        bitrate(s, 20, 25);
        s.audio.codec = "mp2";
        s.audio.bitrate = 256;
        add(s);
    }
    // ---------------- WebM
    {
        auto s = base("webm", "VP9 Match Source - Quality");
        s.video.rateControl = "quality";
        add(s);
        s = base("webm", "VP9 1080p - 2 Pass");
        frame(s, 1920, 1080);
        bitrate(s, 6, 9, "vbr2");
        add(s);
        s = base("webm", "VP9 with Alpha");
        s.video.pixelFormat = "yuva420p";
        s.video.rateControl = "quality";
        add(s);
    }
    // ---------------- AVI
    {
        auto s = base("avi", "Motion JPEG (Match Source)");
        s.video.rateControl = "quality";
        s.video.quality = 3;
        pcm(s, "pcm_s16le", 48000, 2);
        add(s);
    }
    // ---------------- GIF
    {
        auto s = base("gif", "Animated GIF (Match Source)");
        add(s);
        s = base("gif", "Animated GIF 480px 15 fps");
        frame(s, 480, 0, 15);
        add(s);
        s = base("gif", "Animated GIF 720px 24 fps");
        frame(s, 720, 0, 24);
        add(s);
    }
    // ---------------- Audio
    {
        for (int kbps : {320, 256, 192, 128}) {
            auto s = base("mp3", kbps >= 192 ? QStringLiteral("MP3 %1kbps High Quality").arg(kbps)
                                             : QStringLiteral("MP3 %1kbps").arg(kbps));
            s.audio.bitrate = kbps;
            s.audio.sampleRate = 44100;
            add(s);
        }
        for (int kbps : {320, 192, 128}) {
            auto s = base("aac", QStringLiteral("AAC %1kbps 48kHz").arg(kbps));
            s.audio.bitrate = kbps;
            add(s);
        }
        auto s = base("wav", "Waveform Audio 48kHz 16-bit");
        pcm(s, "pcm_s16le", 48000, 2);
        add(s);
        s = base("wav", "Waveform Audio 48kHz 24-bit");
        pcm(s, "pcm_s24le", 48000, 2);
        add(s);
        s = base("wav", "Waveform Audio (Match Source)");
        pcm(s, "pcm_s24le", 0, 0);
        add(s);
        s = base("aiff", "AIFF 48kHz 16-bit");
        pcm(s, "pcm_s16be", 48000, 2);
        add(s);
        s = base("aiff", "AIFF 48kHz 24-bit");
        pcm(s, "pcm_s24be", 48000, 2);
        add(s);
        s = base("flac", "FLAC (Match Source)");
        s.audio.sampleRate = 0;
        s.audio.channels = 0;
        add(s);
    }
    // ---------------- Stills and sequences
    {
        auto s = base("png", "PNG Sequence (Match Source)");
        add(s);
        s = base("png", "PNG Sequence with Alpha (Match Source)");
        s.video.pixelFormat = "rgba";
        add(s);
        s = base("png", "PNG Still (First Frame)");
        s.video.imageSequence = false;
        add(s);
        s = base("jpeg", "JPEG Sequence (Match Source)");
        add(s);
        s = base("jpeg", "JPEG Still (First Frame)");
        s.video.imageSequence = false;
        add(s);
        s = base("tiff", "TIFF Sequence (Match Source)");
        add(s);
        s = base("dpx", "DPX Sequence 10-bit (Match Source)");
        add(s);
        s = base("bmp", "BMP Sequence (Match Source)");
        add(s);
        s = base("targa", "Targa Sequence (Match Source)");
        add(s);
    }

    for (const ExportSettings &s : list) {
        const FormatDef *f = Formats::format(s.format);
        if (!f)
            continue;
        // Skip presets whose codecs this ffmpeg build can't produce.
        if (f->hasVideo() && (s.video.codec.isEmpty() || !EncoderCaps::instance().hasEncoder(s.video.codec)))
            continue;
        if (!f->hasVideo() && (s.audio.codec.isEmpty() || !EncoderCaps::instance().hasEncoder(s.audio.codec)))
            continue;
        Preset p;
        p.id = QStringLiteral("sys/%1/%2").arg(s.format, s.presetName);
        p.settings = s;
        p.system = true;
        m_presets << p;
    }
}

void PresetManager::loadUserPresets()
{
    QDir dir(AppSettings::userPresetsDir());
    const auto files = dir.entryInfoList({"*.json"}, QDir::Files, QDir::Name);
    for (const QFileInfo &fi : files) {
        QFile f(fi.absoluteFilePath());
        if (!f.open(QIODevice::ReadOnly))
            continue;
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        if (!doc.isObject())
            continue;
        Preset p;
        p.settings = ExportSettings::fromJson(doc.object());
        if (p.settings.presetName.isEmpty())
            p.settings.presetName = fi.completeBaseName();
        p.system = false;
        p.filePath = fi.absoluteFilePath();
        p.id = QStringLiteral("user/%1").arg(fi.fileName());
        m_presets << p;
    }
}

QList<Preset> PresetManager::presetsForFormat(const QString &formatId) const
{
    QList<Preset> out;
    for (const Preset &p : m_presets)
        if (p.settings.format == formatId)
            out << p;
    return out;
}

const Preset *PresetManager::find(const QString &id) const
{
    for (const Preset &p : m_presets)
        if (p.id == id)
            return &p;
    return nullptr;
}

const Preset *PresetManager::findByName(const QString &formatId, const QString &name) const
{
    for (const Preset &p : m_presets)
        if (p.settings.format == formatId && p.name() == name)
            return &p;
    return nullptr;
}

Preset PresetManager::defaultPreset() const
{
    if (const Preset *p = find(AppSettings::defaultPresetId()))
        return *p;
    if (!m_presets.isEmpty())
        return m_presets.first();
    Preset p;
    p.settings.setFormat("h264");
    p.settings.presetName = QStringLiteral("Custom");
    return p;
}

Preset PresetManager::firstPresetForFormat(const QString &formatId) const
{
    for (const Preset &p : m_presets)
        if (p.settings.format == formatId)
            return p;
    Preset p;
    p.settings.setFormat(formatId);
    p.settings.presetName = QStringLiteral("Custom");
    return p;
}

Preset PresetManager::saveUserPreset(const ExportSettings &settings, QString *error)
{
    Preset p;
    p.settings = settings;
    if (p.settings.presetName.trimmed().isEmpty())
        p.settings.presetName = QStringLiteral("Custom Preset");
    const QString fileName =
        safeFileName(QStringLiteral("%1 - %2").arg(settings.format, p.settings.presetName)) + ".json";
    p.filePath = QDir(AppSettings::userPresetsDir()).filePath(fileName);
    p.id = QStringLiteral("user/%1").arg(fileName);
    p.system = false;

    QFile f(p.filePath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error)
            *error = f.errorString();
        return {};
    }
    f.write(QJsonDocument(p.settings.toJson()).toJson(QJsonDocument::Indented));
    f.close();

    for (int i = 0; i < m_presets.size(); ++i) {
        if (m_presets[i].id == p.id) {
            m_presets[i] = p;
            emit presetsChanged();
            return p;
        }
    }
    m_presets << p;
    emit presetsChanged();
    return p;
}

bool PresetManager::removeUserPreset(const QString &id)
{
    for (int i = 0; i < m_presets.size(); ++i) {
        if (m_presets[i].id == id && !m_presets[i].system) {
            QFile::remove(m_presets[i].filePath);
            m_presets.removeAt(i);
            emit presetsChanged();
            return true;
        }
    }
    return false;
}

bool PresetManager::importPreset(const QString &path, QString *error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error)
            *error = f.errorString();
        return false;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject() || !doc.object().contains("format")) {
        if (error)
            *error = QStringLiteral("Not a valid preset file");
        return false;
    }
    ExportSettings s = ExportSettings::fromJson(doc.object());
    if (!Formats::format(s.format)) {
        if (error)
            *error = QStringLiteral("Preset uses an unknown format '%1'").arg(s.format);
        return false;
    }
    if (s.presetName.isEmpty())
        s.presetName = QFileInfo(path).completeBaseName();
    return !saveUserPreset(s, error).id.isEmpty();
}

bool PresetManager::exportPreset(const QString &id, const QString &path, QString *error) const
{
    const Preset *p = find(id);
    if (!p) {
        if (error)
            *error = QStringLiteral("Preset not found");
        return false;
    }
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error)
            *error = f.errorString();
        return false;
    }
    f.write(QJsonDocument(p->settings.toJson()).toJson(QJsonDocument::Indented));
    return true;
}
