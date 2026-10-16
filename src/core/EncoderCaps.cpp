#include "EncoderCaps.h"

#include "Formats.h"

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
        QProcess p;
        p.start(ffmpegPath,
                {"-hide_banner", "-v", "error", "-f", "lavfi", "-i", "color=c=black:s=320x240:r=25:d=0.2", "-pix_fmt",
                 def ? def->defaultPixelFormat : QStringLiteral("yuv420p"), "-c:v", enc, "-f", "null", "-"});
        bool ok = p.waitForStarted(5000) && p.waitForFinished(15000) && p.exitStatus() == QProcess::NormalExit &&
                  p.exitCode() == 0;
        if (p.state() != QProcess::NotRunning) {
            p.kill();
            p.waitForFinished(1000);
            ok = false;
        }
        works.insert(enc, ok);
    }

    QMutexLocker lock(&m_mutex);
    m_hwWorks = works;
    m_hwValidated = true;
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
