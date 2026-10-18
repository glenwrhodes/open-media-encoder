#include "EncoderCaps.h"

#include "CommandBuilder.h"
#include "Formats.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QMutexLocker>
#include <QProcess>
#include <QRegularExpression>

namespace {

QString runAndRead(const QString &program, const QStringList &args, int timeoutMs = 10000)
{
    QProcess p;
    p.start(program, args);
    if (!p.waitForStarted(5000))
        return {};
    if (!p.waitForFinished(timeoutMs)) {
        p.kill();
        p.waitForFinished(1000);
        return {};
    }
    return QString::fromUtf8(p.readAllStandardOutput());
}

} // namespace

EncoderCaps &EncoderCaps::instance()
{
    static EncoderCaps caps;
    return caps;
}

void EncoderCaps::probe(const QString &ffmpegPath)
{
    const QString versionOut = runAndRead(ffmpegPath, {"-hide_banner", "-version"});
    const QString encoderOut = runAndRead(ffmpegPath, {"-hide_banner", "-encoders"});
    const QString filterOut = runAndRead(ffmpegPath, {"-hide_banner", "-filters"});

    QSet<QString> encoders;
    // Lines look like: " V....D libx264              libx264 H.264 ..."
    static const QRegularExpression encLine(QStringLiteral("^\\s*[VAS][\\.F][\\.S][\\.X][\\.B][\\.D]\\s+(\\S+)"));
    for (const QString &line : encoderOut.split('\n')) {
        const auto m = encLine.match(line);
        if (m.hasMatch())
            encoders.insert(m.captured(1));
    }
    QSet<QString> filters;
    // Lines look like " TSC lut3d  V->V ..." (FFmpeg <= 7) or " TS lut3d  V->V ..." (FFmpeg 8+).
    static const QRegularExpression filtLine(QStringLiteral("^\\s*[T\\.][S\\.][C\\.]?\\s+(\\S+)\\s+\\S*->\\S*"));
    for (const QString &line : filterOut.split('\n')) {
        const auto m = filtLine.match(line);
        if (m.hasMatch())
            filters.insert(m.captured(1));
    }

    QMutexLocker lock(&m_mutex);
    m_version = versionOut.section('\n', 0, 0).trimmed();
    m_encoders = encoders;
    m_filters = filters;
    m_probed = !encoders.isEmpty();
}

void EncoderCaps::validateHardware(const QString &ffmpegPath)
{
    QStringList hw;
    for (const auto &c : Formats::videoCodecs())
        if (c.hardware && hasEncoder(c.encoder))
            hw << c.encoder;

    QHash<QString, bool> works;
    for (const QString &enc : hw) {
        const VideoCodecDef *def = Formats::videoCodec(enc);
        const QString pix = def ? def->defaultPixelFormat : QStringLiteral("yuv420p");
        QStringList args{"-hide_banner", "-v", "error"};
        args << CommandBuilder::hardwareDeviceArgs(enc) << "-f" << "lavfi" << "-i"
             << "color=c=black:s=320x240:r=25:d=0.2";
        if (enc.endsWith(QStringLiteral("_vaapi")))
            args << "-vf" << QStringLiteral("format=%1,hwupload").arg(pix);
        else
            args << "-pix_fmt" << pix;
        args << "-c:v" << enc << "-f" << "null" << "-";
        QProcess p;
        p.start(ffmpegPath, args);
        bool ok = p.waitForStarted(5000) && p.waitForFinished(15000) && p.exitStatus() == QProcess::NormalExit &&
                  p.exitCode() == 0;
        if (p.state() != QProcess::NotRunning) {
            p.kill();
            p.waitForFinished(1000);
            ok = false;
        }
        works.insert(enc, ok);
    }

    // Hardware decoding: decode a short H.264 clip with -hwaccel auto. Usually FFmpeg falls back to
    // software by itself, but some builds abort instead when a GPU library is missing (VA-API on Linux
    // without drivers), so it is only used where this works.
    bool decodeOk = false;
    const QString clip =
        QDir(QDir::tempPath()).filePath(QStringLiteral("ome-hwdecode-%1.mp4").arg(QCoreApplication::applicationPid()));
    QProcess make;
    make.start(ffmpegPath,
               {"-hide_banner", "-v", "error", "-y", "-f", "lavfi", "-i", "testsrc2=s=320x240:r=25:d=0.4", "-c:v",
                hasEncoder(QStringLiteral("libx264")) ? QStringLiteral("libx264") : QStringLiteral("mpeg4"), "-pix_fmt",
                "yuv420p", clip});
    if (make.waitForFinished(20000) && make.exitStatus() == QProcess::NormalExit && make.exitCode() == 0) {
        QProcess dec;
        dec.start(ffmpegPath, {"-hide_banner", "-v", "error", "-hwaccel", "auto", "-i", clip, "-f", "null", "-"});
        decodeOk = dec.waitForFinished(20000) && dec.exitStatus() == QProcess::NormalExit && dec.exitCode() == 0;
        if (dec.state() != QProcess::NotRunning) {
            dec.kill();
            dec.waitForFinished(1000);
        }
    }
    QFile::remove(clip);

    QMutexLocker lock(&m_mutex);
    m_hwWorks = works;
    m_hwDecodeWorks = decodeOk;
    m_hwValidated = true;
}

bool EncoderCaps::hardwareDecodeWorks() const
{
    QMutexLocker lock(&m_mutex);
    return m_hwValidated && m_hwDecodeWorks;
}

bool EncoderCaps::probed() const
{
    QMutexLocker lock(&m_mutex);
    return m_probed;
}

bool EncoderCaps::hasEncoder(const QString &encoder) const
{
    QMutexLocker lock(&m_mutex);
    if (!m_probed)
        return true;
    if (!m_encoders.contains(encoder))
        return false;
    if (m_hwValidated && m_hwWorks.contains(encoder))
        return m_hwWorks.value(encoder);
    return true;
}

bool EncoderCaps::hasFilter(const QString &filter) const
{
    QMutexLocker lock(&m_mutex);
    return !m_probed || m_filters.contains(filter);
}

bool EncoderCaps::hardwareValidated() const
{
    QMutexLocker lock(&m_mutex);
    return m_hwValidated;
}

QString EncoderCaps::version() const
{
    QMutexLocker lock(&m_mutex);
    return m_version;
}

QStringList EncoderCaps::availableVideoCodecs(const FormatDef &format) const
{
    QStringList out;
    for (const QString &c : format.videoCodecs)
        if (hasEncoder(c))
            out << c;
    return out;
}

QStringList EncoderCaps::availableAudioCodecs(const FormatDef &format) const
{
    QStringList out;
    for (const QString &c : format.audioCodecs)
        if (hasEncoder(c))
            out << c;
    return out;
}

void EncoderCaps::setAvailable(const QSet<QString> &encoders, const QSet<QString> &filters)
{
    QMutexLocker lock(&m_mutex);
    m_encoders = encoders;
    m_filters = filters;
    m_probed = true;
    m_hwValidated = false;
    m_hwWorks.clear();
}
