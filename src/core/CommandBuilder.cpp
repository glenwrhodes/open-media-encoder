#include "CommandBuilder.h"

#include "EncoderCaps.h"
#include "Formats.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QUuid>
#include <cmath>

namespace CommandBuilder {

namespace {

bool isNvenc(const QString &c) { return c.endsWith("_nvenc"); }
bool isQsv(const QString &c) { return c.endsWith("_qsv"); }
bool isAmf(const QString &c) { return c.endsWith("_amf"); }
bool isVaapi(const QString &c) { return c.endsWith("_vaapi"); }
bool isVideoToolbox(const QString &c) { return c.endsWith("_videotoolbox"); }
bool isHevcHardware(const QString &c) { return c.startsWith("hevc_") && c != "hevc"; }
bool isHevc(const QString &c) { return c == "libx265" || c.startsWith("hevc_"); }

QString kbps(double mbps) { return QString::number(qMax<qint64>(1, qRound64(mbps * 1000.0))) + "k"; }

QString num(double v) { return QString::number(v, 'f', 3).remove(QRegularExpression("\\.?0+$")); }

bool isSubsampled(const QString &pix)
{
    return pix.startsWith("yuv420") || pix.startsWith("yuv422") || pix.startsWith("yuva420") ||
           pix.startsWith("yuvj420") || pix.startsWith("yuvj422") || pix == "nv12" || pix == "p010le" ||
           pix.startsWith("yuva422");
}

int bitDepth(const QString &pix)
{
    if (pix.contains("16") || pix.contains("64"))
        return 16;
    if (pix.contains("12"))
        return 12;
    if (pix.contains("10") || pix == "p010le")
        return 10;
    return 8;
}

// x/y expressions for a 9-grid anchor. W/H = frame, w/h = item size.
void anchorExpr(const QString &pos, int margin, const QString &itemW, const QString &itemH, QString *x, QString *y)
{
    const QString m = QString::number(margin);
    if (pos.endsWith("left"))
        *x = m;
    else if (pos.endsWith("right"))
        *x = QStringLiteral("W-%1-%2").arg(itemW, m);
    else
        *x = QStringLiteral("(W-%1)/2").arg(itemW);

    if (pos.startsWith("top"))
        *y = m;
    else if (pos.startsWith("bottom"))
        *y = QStringLiteral("H-%1-%2").arg(itemH, m);
    else
        *y = QStringLiteral("(H-%1)/2").arg(itemH);
}

QString writeTempText(const QString &dir, const QString &text, QStringList *tempFiles)
{
    QDir().mkpath(dir);
    const QString path =
        QDir(dir).filePath(QStringLiteral("text_%1.txt").arg(QUuid::createUuid().toString(QUuid::Id128)));
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return {};
    f.write(text.toUtf8());
    f.close();
    if (tempFiles)
        tempFiles->append(path);
    return path;
}

int nominalFps(double fps) { return qMax(1, int(std::lround(fps))); }

double sourceFps(const BuildRequest &req)
{
    const double fps = req.settings.effectiveFrameRate(req.source.fps);
    return fps > 0 ? fps : 25.0;
}

} // namespace

// ------------------------------------------------------------------ helpers

QString escapeFilterValue(const QString &value)
{
    QString v = value;
    v.replace('\\', '/');
    v.replace(':', QStringLiteral("\\:"));
    v.replace('\'', QStringLiteral("\\'"));
    return QStringLiteral("'%1'").arg(v);
}

QString fpsToRational(double fps)
{
    struct Known {
        double v;
        const char *r;
    };
    static const Known known[] = {{23.976, "24000/1001"},
                                  {29.97, "30000/1001"},
                                  {59.94, "60000/1001"},
                                  {47.952, "48000/1001"},
                                  {119.88, "120000/1001"}};
    for (const auto &k : known)
        if (std::abs(fps - k.v) < 0.01)
            return QString::fromLatin1(k.r);
    if (std::abs(fps - std::round(fps)) < 0.001)
        return QString::number(qRound(fps));
    return QStringLiteral("%1/1000").arg(qRound(fps * 1000));
}

qint64 timecodeToFrames(const QString &tc, int fps)
{
    const QStringList p = QString(tc).replace(';', ':').split(':');
    if (p.size() != 4)
        return 0;
    return ((p[0].toLongLong() * 60 + p[1].toLongLong()) * 60 + p[2].toLongLong()) * fps + p[3].toLongLong();
}

QString framesToTimecode(qint64 frames, int fps)
{
    if (fps <= 0)
        fps = 25;
    const qint64 ff = frames % fps;
    qint64 secs = frames / fps;
    const qint64 ss = secs % 60;
    secs /= 60;
    const qint64 mm = secs % 60;
    const qint64 hh = (secs / 60) % 24;
    return QStringLiteral("%1:%2:%3:%4")
        .arg(hh, 2, 10, QChar('0'))
        .arg(mm, 2, 10, QChar('0'))
        .arg(ss, 2, 10, QChar('0'))
        .arg(ff, 2, 10, QChar('0'));
}

QString vaapiDevice()
{
#ifdef Q_OS_LINUX
    const QDir dri(QStringLiteral("/dev/dri"));
    const QStringList nodes = dri.entryList({QStringLiteral("renderD*")}, QDir::System, QDir::Name);
    if (!nodes.isEmpty())
        return dri.filePath(nodes.first());
#endif
    return {};
}

QStringList hardwareDeviceArgs(const QString &codec)
{
    if (!isVaapi(codec))
        return {};
    const QString dev = vaapiDevice();
    return {"-vaapi_device", dev.isEmpty() ? QStringLiteral("/dev/dri/renderD128") : dev};
}

QString findDefaultFont()
{
    static const QStringList candidates = {"C:/Windows/Fonts/arial.ttf",
                                           "C:/Windows/Fonts/segoeui.ttf",
                                           "/System/Library/Fonts/Helvetica.ttc",
                                           "/System/Library/Fonts/Supplemental/Arial.ttf",
                                           "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
                                           "/usr/share/fonts/TTF/DejaVuSans.ttf"};
    for (const QString &c : candidates)
        if (QFileInfo::exists(c))
            return c;
    return {};
}

bool usesTwoPass(const ExportSettings &s)
{
    static const QStringList twoPass = {"libx264", "libx265", "libvpx", "libvpx-vp9", "libaom-av1", "mpeg2video"};
    return s.video.enabled && s.video.rateControl == "vbr2" && twoPass.contains(s.video.codec);
}

double outputDuration(const BuildRequest &req)
{
    const VideoSettings &v = req.settings.video;
    const FormatDef *f = Formats::format(req.settings.format);
    const double full = req.totalDuration > 0 ? req.totalDuration : req.source.duration;
    if (req.source.isStill) {
        if (f && f->imageFormat)
            return 0.0;
        if (req.outPoint > 0)
            return req.outPoint - qMax(0.0, req.inPoint);
        return v.stillDuration;
    }
    if (f && f->imageFormat && !v.imageSequence)
        return 0.0;
    const double in = qMax(0.0, req.inPoint);
    const double out = (req.outPoint > in) ? qMin(req.outPoint, full > 0 ? full : req.outPoint) : full;
    return qMax(0.0, out - in);
}

QString resolvePixelFormat(const ExportSettings &s, const MediaInfo &src)
{
    const VideoSettings &v = s.video;
    const QString c = v.codec;
    if (!v.pixelFormat.isEmpty())
        return v.pixelFormat;
    const bool alpha = src.hasAlpha();
    if (c == "prores_ks") {
        const int p = v.profile.toInt();
        if (p >= 4)
            return alpha ? QStringLiteral("yuva444p10le") : QStringLiteral("yuv444p10le");
        return QStringLiteral("yuv422p10le");
    }
    if (c == "dnxhd") {
        if (v.profile == "dnxhr_hqx")
            return QStringLiteral("yuv422p10le");
        if (v.profile == "dnxhr_444")
            return QStringLiteral("yuv444p10le");
        return QStringLiteral("yuv422p");
    }
    if (c == "png")
        return alpha ? QStringLiteral("rgba") : QStringLiteral("rgb24");
    if (c == "tiff")
        return alpha ? QStringLiteral("rgba") : QStringLiteral("rgb24");
    if (c == "qtrle")
        return alpha ? QStringLiteral("argb") : QStringLiteral("rgb24");
    if (c == "bmp" || c == "targa")
        return alpha ? QStringLiteral("bgra") : QStringLiteral("bgr24");
    if (c == "cfhd")
        return alpha ? QStringLiteral("gbrap12le") : QStringLiteral("yuv422p10le");
    if (c == "gif")
        return {};
    if (c == "libx264") {
        if (v.profile == "high10")
            return QStringLiteral("yuv420p10le");
        if (v.profile == "high422")
            return QStringLiteral("yuv422p");
    }
    if (c == "libx265") {
        if (v.profile == "main10")
            return QStringLiteral("yuv420p10le");
        if (v.profile == "main422-10")
            return QStringLiteral("yuv422p10le");
        if (v.profile == "main444-8")
            return QStringLiteral("yuv444p");
    }
    if (isHevcHardware(c) && v.profile == "main10")
        return QStringLiteral("p010le");
    const VideoCodecDef *def = Formats::videoCodec(c);
    return def ? def->defaultPixelFormat : QString();
}

QString resolveProfile(const ExportSettings &s, const QString &pix)
{
    const QString c = s.video.codec;
    QString profile = s.video.profile;
    if (c == "libx264") {
        if (pix.startsWith("yuv422"))
            return QStringLiteral("high422");
        if (bitDepth(pix) == 10)
            return QStringLiteral("high10");
        if (profile == "high10" || profile == "high422")
            return QStringLiteral("high");
    } else if (c == "libx265") {
        if (pix == "yuv420p10le")
            return QStringLiteral("main10");
        if (pix == "yuv422p10le")
            return QStringLiteral("main422-10");
        if (pix == "yuv444p")
            return QStringLiteral("main444-8");
        return QStringLiteral("main");
    } else if (isHevcHardware(c)) {
        return pix == "p010le" ? QStringLiteral("main10") : QStringLiteral("main");
    } else if (c == "h264_nvenc" && pix == "yuv444p") {
        return QStringLiteral("high444p");
    }
    return profile;
}

QString firstSequenceFile(const QString &pattern)
{
    QString p = pattern;
    return p.replace(QStringLiteral("%05d"), QStringLiteral("00000"));
}

bool samePath(const QString &a, const QString &b)
{
    // QFileInfo's operator== treats two missing files as equal (empty canonical paths), so compare
    // canonical paths only when both exist and fall back to cleaned absolute paths otherwise.
    const QFileInfo fa(a), fb(b);
    if (fa.exists() && fb.exists())
        return fa.canonicalFilePath() == fb.canonicalFilePath();
#ifdef Q_OS_WIN
    const Qt::CaseSensitivity cs = Qt::CaseInsensitive;
#else
    const Qt::CaseSensitivity cs = Qt::CaseSensitive;
#endif
    return QDir::cleanPath(fa.absoluteFilePath()).compare(QDir::cleanPath(fb.absoluteFilePath()), cs) == 0;
}

QString defaultOutputPath(const QString &sourcePath, const ExportSettings &s, const QString &outputDir)
{
    const QFileInfo fi(sourcePath);
    const QString dir = outputDir.isEmpty() ? fi.absolutePath() : outputDir;
    QString base = fi.completeBaseName();
    if (base.isEmpty())
        base = QStringLiteral("output");
    const QString ext = s.extension();
    if (s.isImageSequence())
        return QDir(dir).filePath(QStringLiteral("%1_%05d.%2").arg(base, ext));
    QString path = QDir(dir).filePath(QStringLiteral("%1.%2").arg(base, ext));
    // Never default to overwriting the source itself (e.g. mp4 -> mp4 in the same folder).
    if (samePath(path, sourcePath))
        path = QDir(dir).filePath(QStringLiteral("%1_1.%2").arg(base, ext));
    return path;
}

QString uniqueOutputPath(const QString &path)
{
    const bool seq = path.contains(QStringLiteral("%05d"));
    auto exists = [&](const QString &p) { return QFileInfo::exists(seq ? firstSequenceFile(p) : p); };
    if (!exists(path))
        return path;
    const QFileInfo fi(path);
    QString stem = fi.completeBaseName();
    QString suffix = QStringLiteral(".") + fi.suffix();
    if (seq) {
        stem.remove(QStringLiteral("_%05d"));
        suffix = QStringLiteral("_%05d.") + fi.suffix();
    }
    for (int i = 1; i < 10000; ++i) {
        const QString candidate = fi.dir().filePath(QStringLiteral("%1_%2%3").arg(stem).arg(i).arg(suffix));
        if (!exists(candidate))
            return candidate;
    }
    return path;
}

// ------------------------------------------------------------------ filters

QString videoFilterGraph(const BuildRequest &req, bool preview, QStringList *tempFiles, QString *error)
{
    const ExportSettings &s = req.settings;
    const VideoSettings &v = s.video;
    const EffectsSettings &fx = s.effects;
    const MediaInfo &src = req.source;
    const QString pix = resolvePixelFormat(s, src);
    const EncoderCaps &caps = EncoderCaps::instance();

    QStringList stmts;
    QStringList chain;
    QString cur = QStringLiteral("[0:v:0]");
    auto flush = [&](const QString &out) {
        stmts << cur + (chain.isEmpty() ? QStringLiteral("null") : chain.join(',')) + out;
        chain.clear();
        cur = out;
    };

    // Deinterlace when explicitly asked, or automatically for interlaced sources going progressive.
    if (!preview && (v.deinterlace || (src.isInterlaced() && v.fieldOrder == "progressive")))
        chain << QStringLiteral("yadif=mode=send_frame:parity=auto:deint=all");

    int w = src.displayWidth();
    int h = src.displayHeight();
    if (fx.cropEnabled) {
        const int cw = w - fx.cropLeft - fx.cropRight;
        const int ch = h - fx.cropTop - fx.cropBottom;
        if (cw < 16 || ch < 16) {
            if (error)
                *error = QStringLiteral("Crop leaves an image smaller than 16x16");
            return {};
        }
        chain << QStringLiteral("crop=%1:%2:%3:%4").arg(cw).arg(ch).arg(fx.cropLeft).arg(fx.cropTop);
        w = cw;
        h = ch;
    }

    const QString flags =
        s.maxRenderQuality ? QStringLiteral("lanczos+accurate_rnd+full_chroma_int") : QStringLiteral("bicubic");
    const bool needEven = isSubsampled(pix);
    if (v.width > 0 && v.height > 0) {
        const int W = needEven ? (v.width & ~1) : v.width;
        const int H = needEven ? (v.height & ~1) : v.height;
        if (W != w || H != h || src.sar != 1.0) {
            if (v.scaleMode == "fill") {
                chain << QStringLiteral("scale=%1:%2:force_original_aspect_ratio=increase:flags=%3")
                             .arg(W)
                             .arg(H)
                             .arg(flags)
                      << QStringLiteral("crop=%1:%2").arg(W).arg(H);
            } else if (v.scaleMode == "stretch") {
                chain << QStringLiteral("scale=%1:%2:flags=%3").arg(W).arg(H).arg(flags);
            } else {
                chain << QStringLiteral("scale=%1:%2:force_original_aspect_ratio=decrease:flags=%3")
                             .arg(W)
                             .arg(H)
                             .arg(flags)
                      << QStringLiteral("pad=%1:%2:(ow-iw)/2:(oh-ih)/2:color=black@0").arg(W).arg(H);
            }
            chain << QStringLiteral("setsar=1");
        }
    } else if (v.width > 0 || v.height > 0) {
        const QString W = v.width > 0 ? QString::number(needEven ? (v.width & ~1) : v.width) : QStringLiteral("-2");
        const QString H = v.height > 0 ? QString::number(needEven ? (v.height & ~1) : v.height) : QStringLiteral("-2");
        chain << QStringLiteral("scale=%1:%2:flags=%3").arg(W, H, flags) << QStringLiteral("setsar=1");
    } else if (needEven && ((w & 1) || (h & 1))) {
        chain << QStringLiteral("scale=%1:%2:flags=%3").arg(w & ~1).arg(h & ~1).arg(flags);
    }

    const double outFps = s.effectiveFrameRate(src.fps);
    if (!preview && outFps > 0 && std::abs(outFps - src.fps) > 0.001 && !src.isStill) {
        const QString r = fpsToRational(outFps);
        if (v.frameRateMode == "optical")
            chain << QStringLiteral("minterpolate=fps=%1:mi_mode=mci:mc_mode=aobmc:vsbmc=1").arg(r);
        else if (v.frameRateMode == "blend")
            chain << QStringLiteral("framerate=fps=%1").arg(r);
        else
            chain << QStringLiteral("fps=%1").arg(r);
    }

    if (fx.lutEnabled && !fx.lutPath.isEmpty()) {
        if (!QFileInfo::exists(fx.lutPath)) {
            if (error)
                *error = QStringLiteral("LUT file not found: %1").arg(fx.lutPath);
            return {};
        }
        chain << QStringLiteral("lut3d=file=%1:interp=tetrahedral").arg(escapeFilterValue(fx.lutPath));
    }

    // Image overlay (input #1).
    if (fx.imageOverlayEnabled && !fx.imagePath.isEmpty()) {
        if (!QFileInfo::exists(fx.imagePath)) {
            if (error)
                *error = QStringLiteral("Overlay image not found: %1").arg(fx.imagePath);
            return {};
        }
        flush(QStringLiteral("[base]"));
        QStringList wm{QStringLiteral("format=rgba")};
        if (fx.imageScale != 100)
            wm << QStringLiteral("scale=iw*%1:-1").arg(num(fx.imageScale / 100.0));
        if (fx.imageOpacity < 100)
            wm << QStringLiteral("colorchannelmixer=aa=%1").arg(num(fx.imageOpacity / 100.0));
        stmts << QStringLiteral("[1:v]%1[wm]").arg(wm.join(','));
        QString x, y;
        anchorExpr(fx.imagePosition, fx.imageOffset, QStringLiteral("w"), QStringLiteral("h"), &x, &y);
        cur = QStringLiteral("[base][wm]");
        chain << QStringLiteral("overlay=x=%1:y=%2:format=auto").arg(x, y);
    }

    const QString font = req.ffmpegFontFile.isEmpty() ? findDefaultFont() : req.ffmpegFontFile;
    const QString fontOpt = font.isEmpty() ? QString() : QStringLiteral("fontfile=%1:").arg(escapeFilterValue(font));

    if (((fx.textOverlayEnabled && !fx.text.isEmpty()) || fx.timecodeEnabled) && !caps.hasFilter("drawtext")) {
        if (error)
            *error = QStringLiteral("This FFmpeg build has no drawtext filter, needed for text and timecode overlays");
        return {};
    }

    if (fx.textOverlayEnabled && !fx.text.isEmpty()) {
        const QString textFile = writeTempText(req.workDir, fx.text, tempFiles);
        if (textFile.isEmpty()) {
            if (error)
                *error = QStringLiteral("Could not write overlay text file");
            return {};
        }
        QString x, y;
        anchorExpr(fx.textPosition, fx.textOffset, QStringLiteral("tw"), QStringLiteral("th"), &x, &y);
        QString dt = QStringLiteral("drawtext=%1textfile=%2:expansion=none:fontsize=%3:fontcolor=%4@%5:x=%6:y=%7")
                         .arg(fontOpt, escapeFilterValue(textFile))
                         .arg(fx.textSize)
                         .arg(fx.textColor)
                         .arg(num(fx.textOpacity / 100.0))
                         .arg(x, y);
        if (fx.textBox)
            dt += QStringLiteral(":box=1:boxcolor=black@0.45:boxborderw=%1").arg(qMax(4, fx.textSize / 4));
        chain << dt;
    }

    if (fx.timecodeEnabled) {
        const double fps = sourceFps(req);
        const int nfps = nominalFps(fps);
        QString start = fx.timecodeStart;
        if (fx.timecodeFromSource && !src.timecode.isEmpty())
            start = src.timecode;
        qint64 frames = timecodeToFrames(start, nfps);
        if (req.inPoint > 0)
            frames += qint64(std::llround(req.inPoint * nfps));
        const QString tc = framesToTimecode(frames, nfps);
        QString x, y;
        anchorExpr(fx.timecodePosition, 32, QStringLiteral("tw"), QStringLiteral("th"), &x, &y);
        chain << QStringLiteral("drawtext=%1timecode=%2:rate=%3:fontsize=%4:fontcolor=white:box=1:boxcolor=black@0.6:"
                                "boxborderw=%5:x=%6:y=%7")
                     .arg(fontOpt, escapeFilterValue(tc), fpsToRational(fps))
                     .arg(fx.timecodeSize)
                     .arg(qMax(4, fx.timecodeSize / 5))
                     .arg(x, y);
    }

    if (fx.captionsEnabled && !fx.captionFile.isEmpty()) {
        if (!QFileInfo::exists(fx.captionFile)) {
            if (error)
                *error = QStringLiteral("Caption file not found: %1").arg(fx.captionFile);
            return {};
        }
        if (!caps.hasFilter("subtitles")) {
            if (error)
                *error = QStringLiteral("This ffmpeg build has no subtitles filter (libass)");
            return {};
        }
        chain << QStringLiteral("subtitles=filename=%1:force_style=%2")
                     .arg(escapeFilterValue(fx.captionFile),
                          escapeFilterValue(QStringLiteral("Fontsize=%1").arg(fx.captionSize)));
    }

    if (preview) {
        chain << QStringLiteral("format=bgr24");
        flush(QStringLiteral("[vout]"));
        return stmts.join(';');
    }

    if (fx.limiterEnabled && !pix.isEmpty() && pix.startsWith("yuv")) {
        const int depth = bitDepth(pix);
        const int shift = depth - 8;
        chain << QStringLiteral("format=%1").arg(pix)
              << QStringLiteral("limiter=min=%1:max=%2").arg(16 << shift).arg(235 << shift);
    }

    if (v.fieldOrder == "tff" || v.fieldOrder == "bff")
        chain << QStringLiteral("setfield=%1").arg(v.fieldOrder == "tff" ? "tff" : "bff");

    if (v.codec == "gif") {
        chain << QStringLiteral("split");
        flush(QStringLiteral("[g0][g1]"));
        stmts << QStringLiteral("[g0]palettegen=stats_mode=diff[pal]");
        cur = QStringLiteral("[g1][pal]");
        chain << QStringLiteral("paletteuse=dither=bayer:bayer_scale=5:diff_mode=rectangle");
    } else if (!pix.isEmpty()) {
        chain << QStringLiteral("format=%1").arg(pix);
    }
    if (isVaapi(v.codec))
        chain << QStringLiteral("hwupload");
    flush(QStringLiteral("[vout]"));
    return stmts.join(';');
}

// ------------------------------------------------------------------ build

namespace {

void appendVideoCodecArgs(const BuildRequest &req, const QString &pix, QStringList &a)
{
    const ExportSettings &s = req.settings;
    const VideoSettings &v = s.video;
    const QString c = v.codec;
    const VideoCodecDef *def = Formats::videoCodec(c);

    a << "-c:v" << c;
    if (def && !def->speedArg.isEmpty() && !v.speedPreset.isEmpty())
        a << def->speedArg << v.speedPreset;

    const QString profile = resolveProfile(s, pix);
    if (!profile.isEmpty() && def && !def->profiles.isEmpty())
        a << "-profile:v" << profile;
    if (!v.level.isEmpty() && v.level != "auto" && def && !def->levels.isEmpty())
        a << "-level:v" << v.level;
    if (!pix.isEmpty() && !isVaapi(c)) // VA-API frames are already uploaded in the encoder's format
        a << "-pix_fmt" << pix;
    if (v.keyframeInterval > 0)
        a << "-g" << QString::number(v.keyframeInterval);

    // Presets can specify bitrates for <= 30 fps that rise for high-frame-rate output.
    const double k = s.bitrateScale(s.effectiveFrameRate(req.source.fps));
    const double target = v.targetBitrate * k;
    const double maxRate = qMax(v.maxBitrate, v.targetBitrate) * k;
    const QString T = kbps(target);
    const QString M = kbps(maxRate);
    const QString Tbuf = kbps(target * 2);
    const QString Mbuf = kbps(maxRate * 2);
    const QString Q = QString::number(v.quality);
    const QString rc = def && def->rateControls.contains(v.rateControl) ? v.rateControl
                       : def                                            ? def->defaultRateControl
                                                                        : v.rateControl;

    if (isNvenc(c)) {
        if (rc == "cbr")
            a << "-rc" << "cbr" << "-b:v" << T << "-maxrate" << T << "-bufsize" << Tbuf;
        else if (rc == "vbr1" || rc == "vbr2") {
            a << "-rc" << "vbr" << "-b:v" << T << "-maxrate" << M << "-bufsize" << Mbuf;
            if (rc == "vbr2")
                a << "-multipass" << "fullres";
        } else if (rc == "quality")
            a << "-rc" << "vbr" << "-cq" << Q << "-b:v" << "0";
    } else if (isQsv(c)) {
        if (rc == "cbr")
            a << "-b:v" << T << "-maxrate" << T << "-bufsize" << Tbuf;
        else if (rc == "vbr1" || rc == "vbr2")
            a << "-b:v" << T << "-maxrate" << M << "-bufsize" << Mbuf;
        else if (rc == "quality")
            a << "-global_quality" << Q;
    } else if (isAmf(c)) {
        if (rc == "cbr")
            a << "-rc" << "cbr" << "-b:v" << T << "-maxrate" << T << "-bufsize" << Tbuf;
        else if (rc == "vbr1" || rc == "vbr2")
            a << "-rc" << "vbr_peak" << "-b:v" << T << "-maxrate" << M << "-bufsize" << Mbuf;
        else if (rc == "quality") {
            a << "-rc" << "cqp" << "-qp_i" << Q << "-qp_p" << Q;
            if (c == "h264_amf")
                a << "-qp_b" << Q;
        }
    } else if (isVaapi(c)) {
        if (rc == "cbr")
            a << "-rc_mode" << "CBR" << "-b:v" << T << "-maxrate" << T << "-bufsize" << Tbuf;
        else if (rc == "vbr1" || rc == "vbr2")
            a << "-rc_mode" << "VBR" << "-b:v" << T << "-maxrate" << M << "-bufsize" << Mbuf;
        else if (rc == "quality")
            a << "-rc_mode" << "CQP" << "-qp" << Q;
    } else if (isVideoToolbox(c)) {
        if (rc == "cbr")
            a << "-b:v" << T << "-maxrate" << T << "-bufsize" << Tbuf;
        else if (rc == "vbr1" || rc == "vbr2")
            a << "-b:v" << T << "-maxrate" << M << "-bufsize" << Mbuf;
        else if (rc == "quality")
            a << "-q:v" << Q; // Apple silicon only; 1-100, higher is better
    } else if (c == "libaom-av1" || c == "libvpx" || c == "libvpx-vp9") {
        if (rc == "cbr")
            a << "-b:v" << T << "-minrate" << T << "-maxrate" << T;
        else if (rc == "vbr1" || rc == "vbr2")
            a << "-b:v" << T << "-maxrate" << M;
        else if (rc == "quality")
            a << def->qualityArg << Q << "-b:v" << "0";
        a << "-row-mt" << "1";
        if (v.pixelFormat == "yuva420p" || pix == "yuva420p")
            a << "-auto-alt-ref" << "0";
    } else if (rc == "cbr") {
        a << "-b:v" << T << "-minrate" << T << "-maxrate" << T << "-bufsize" << Tbuf;
    } else if (rc == "vbr1" || rc == "vbr2") {
        a << "-b:v" << T << "-maxrate" << M << "-bufsize" << Mbuf;
    } else if (rc == "quality" && def && !def->qualityArg.isEmpty()) {
        a << def->qualityArg << Q;
    }

    // Two-pass control.
    if (req.pass > 0 && usesTwoPass(s)) {
        if (c == "libx265") {
            a << "-x265-params"
              << QStringLiteral("log-level=error:pass=%1:stats=%2.x265").arg(req.pass).arg(req.passLogFile);
        } else {
            a << "-pass" << QString::number(req.pass) << "-passlogfile" << req.passLogFile;
        }
    } else if (c == "libx265") {
        a << "-x265-params" << "log-level=error";
    }

    // Interlaced output signalling.
    if (v.fieldOrder == "tff" || v.fieldOrder == "bff") {
        const bool tff = v.fieldOrder == "tff";
        if (c == "libx264")
            a << "-x264-params" << (tff ? "tff=1" : "bff=1");
        else if (c == "mpeg2video" || c == "prores_ks" || c == "dnxhd")
            a << "-flags" << "+ildct+ilme";
        a << "-field_order" << (tff ? "tt" : "bb");
    }

    if (c == "prores_ks")
        a << "-vendor" << "apl0";
    if (isHevc(c) && (s.format == "hevc" || s.format == "quicktime"))
        a << "-tag:v" << "hvc1";
    if (c == "mpeg2video")
        a << "-intra_vlc" << "1";

    if (!v.extraArgs.trimmed().isEmpty())
        a << QProcess::splitCommand(v.extraArgs);
}

void appendAudioArgs(const BuildRequest &req, QStringList &a)
{
    const AudioSettings &au = req.settings.audio;
    const EffectsSettings &fx = req.settings.effects;
    const AudioCodecDef *def = Formats::audioCodec(au.codec);
    a << "-map" << "0:a:0" << "-c:a" << au.codec;
    if (def && !def->lossless && au.bitrate > 0)
        a << "-b:a" << QStringLiteral("%1k").arg(au.bitrate);

    int rate = au.sampleRate;
    if (rate <= 0 && fx.loudnessEnabled)
        rate = req.source.sampleRate > 0 ? req.source.sampleRate : 48000;
    if (def && !def->sampleRates.isEmpty()) {
        const int effective = rate > 0 ? rate : req.source.sampleRate;
        if (effective > 0 && !def->sampleRates.contains(effective))
            rate = def->sampleRates.contains(48000) ? 48000 : def->sampleRates.last();
    }
    if (rate > 0)
        a << "-ar" << QString::number(rate);

    int ch = au.channels;
    if (def && ch <= 0 && req.source.channels > def->maxChannels)
        ch = def->maxChannels;
    if (ch > 0)
        a << "-ac" << QString::number(ch);

    if (fx.loudnessEnabled)
        a << "-af"
          << QStringLiteral("loudnorm=I=%1:TP=%2:LRA=%3")
                 .arg(num(fx.loudnessTarget), num(fx.truePeak), num(fx.loudnessRange));
    if (!au.extraArgs.trimmed().isEmpty())
        a << QProcess::splitCommand(au.extraArgs);
}

QString writeConcatList(const BuildRequest &req, QStringList *tempFiles)
{
    QDir().mkpath(req.workDir);
    const QString path =
        QDir(req.workDir).filePath(QStringLiteral("concat_%1.txt").arg(QUuid::createUuid().toString(QUuid::Id128)));
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return {};
    for (const QString &in : req.inputs) {
        QString p = QFileInfo(in).absoluteFilePath();
        p.replace('\\', '/');
        p.replace('\'', QStringLiteral("'\\''"));
        f.write(QStringLiteral("file '%1'\n").arg(p).toUtf8());
    }
    f.close();
    tempFiles->append(path);
    return path;
}

} // namespace

BuildResult build(const BuildRequest &req)
{
    BuildResult r;
    const ExportSettings &s = req.settings;
    const FormatDef *fmt = Formats::format(s.format);
    if (!fmt) {
        r.error = QStringLiteral("Unknown format '%1'").arg(s.format);
        return r;
    }
    if (req.inputs.isEmpty()) {
        r.error = QStringLiteral("No input file");
        return r;
    }
    const MediaInfo &src = req.source;
    const bool doVideo = s.video.enabled && fmt->hasVideo() && src.hasVideo && !s.video.codec.isEmpty();
    const bool doAudio = s.audio.enabled && fmt->hasAudio() && src.hasAudio && !s.audio.codec.isEmpty() &&
                         !(req.pass == 1) && !(src.isStill);
    if (!doVideo && !doAudio) {
        r.error = fmt->hasVideo() && !fmt->hasAudio()
                      ? QStringLiteral("The source has no video to export in %1 format").arg(fmt->displayName)
                  : !fmt->hasVideo() && fmt->hasAudio()
                      ? QStringLiteral("The source has no audio to export in %1 format").arg(fmt->displayName)
                      : QStringLiteral("Nothing to export: both video and audio are disabled or missing");
        return r;
    }
    if (doVideo && !EncoderCaps::instance().hasEncoder(s.video.codec)) {
        const VideoCodecDef *def = Formats::videoCodec(s.video.codec);
        r.error =
            def && def->hardware
                ? QStringLiteral("%1 isn't usable on this machine (no supported GPU, or the graphics driver is too "
                                 "old). Choose a software codec or update the driver.")
                      .arg(def->displayName)
                : QStringLiteral("Video encoder '%1' is not available on this system").arg(s.video.codec);
        return r;
    }
    if (doAudio && !EncoderCaps::instance().hasEncoder(s.audio.codec)) {
        r.error = QStringLiteral("Audio encoder '%1' is not available on this system").arg(s.audio.codec);
        return r;
    }

    r.duration = outputDuration(req);
    const bool stillToVideo = src.isStill && !fmt->imageFormat;
    const bool singleFrame = fmt->imageFormat && (!s.video.imageSequence || src.isStill);

    QStringList &a = r.args;
    a << "-hide_banner" << "-nostdin" << "-y" << "-loglevel" << "warning" << "-progress" << "pipe:1" << "-nostats";

    // ---- inputs
    if (req.hwDecode && doVideo && !src.isStill)
        a << "-hwaccel" << "auto";
    if (doVideo)
        a << hardwareDeviceArgs(s.video.codec);
    if (req.inputs.size() > 1) {
        const QString list = writeConcatList(req, &r.tempFiles);
        if (list.isEmpty()) {
            r.error = QStringLiteral("Could not write concat list");
            return r;
        }
        if (req.inPoint > 0)
            a << "-ss" << num(req.inPoint);
        a << "-f" << "concat" << "-safe" << "0" << "-i" << list;
    } else if (stillToVideo) {
        const double fps = s.video.frameRate > 0 ? s.video.frameRate : 30.0;
        a << "-loop" << "1" << "-framerate" << fpsToRational(fps) << "-t" << num(r.duration) << "-i"
          << req.inputs.first();
    } else {
        if (req.inPoint > 0 && !src.isStill)
            a << "-ss" << num(req.inPoint);
        a << req.inputArgs << "-i" << req.inputs.first();
    }
    if (doVideo && s.effects.imageOverlayEnabled && !s.effects.imagePath.isEmpty())
        a << "-i" << s.effects.imagePath;

    // ---- streams
    if (doVideo) {
        QString err;
        const QString graph = videoFilterGraph(req, false, &r.tempFiles, &err);
        if (!err.isEmpty()) {
            r.error = err;
            return r;
        }
        a << "-filter_complex" << graph << "-map" << "[vout]";
        appendVideoCodecArgs(req, resolvePixelFormat(s, src), a);
    } else {
        a << "-vn";
    }
    if (doAudio)
        appendAudioArgs(req, a);
    else
        a << "-an";
    a << "-sn" << "-dn";

    if (!src.isStill && req.outPoint > 0 && r.duration > 0)
        a << "-t" << num(r.duration);
    if (singleFrame)
        a << "-frames:v" << "1" << "-update" << "1";

    // ---- container
    if (req.pass == 1) {
        a << "-f" << "null" << "-";
        return r;
    }
    if (req.inputs.size() == 1)
        a << "-map_metadata" << "0";
    if (fmt->muxer == "mp4" || fmt->muxer == "mov" || fmt->muxer == "ipod")
        a << "-movflags" << "+faststart";
    if ((fmt->muxer == "mov" || fmt->muxer == "mxf") && doVideo && !src.timecode.isEmpty()) {
        const int nfps = nominalFps(sourceFps(req));
        qint64 frames = timecodeToFrames(src.timecode, nfps);
        if (req.inPoint > 0)
            frames += qint64(std::llround(req.inPoint * nfps));
        a << "-timecode" << framesToTimecode(frames, nfps);
    }
    if (fmt->muxer == "gif")
        a << "-loop" << "0";
    if (fmt->imageFormat && !singleFrame)
        a << "-start_number" << "0";
    a << "-f" << fmt->muxer << req.outputPath;
    return r;
}

BuildResult buildPreview(const BuildRequest &req, double time, const QString &outImage, bool applyEffects)
{
    BuildResult r;
    const MediaInfo &src = req.source;
    if (!src.hasVideo) {
        r.error = QStringLiteral("Source has no video");
        return r;
    }
    QStringList &a = r.args;
    a << "-hide_banner" << "-nostdin" << "-y" << "-loglevel" << "error";
    if (!src.isStill && time > 0)
        a << "-ss" << num(time);
    a << req.inputArgs << "-i" << (req.inputs.isEmpty() ? src.path : req.inputs.first());

    const ExportSettings &s = req.settings;
    if (applyEffects) {
        if (s.effects.imageOverlayEnabled && !s.effects.imagePath.isEmpty())
            a << "-i" << s.effects.imagePath;
        QString err;
        const QString graph = videoFilterGraph(req, true, &r.tempFiles, &err);
        if (!err.isEmpty()) {
            r.error = err;
            return r;
        }
        a << "-filter_complex" << graph << "-map" << "[vout]";
    } else {
        a << "-map" << "0:v:0" << "-vf" << "format=bgr24";
    }
    a << "-frames:v" << "1" << "-update" << "1" << "-c:v" << "bmp" << "-f" << "image2" << outImage;
    return r;
}

} // namespace CommandBuilder
