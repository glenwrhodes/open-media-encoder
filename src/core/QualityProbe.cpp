#include "QualityProbe.h"

#include "AppSettings.h"
#include "EncoderCaps.h"
#include "Formats.h"
#include "SystemIntegration.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QThread>
#include <QUuid>
#include <cmath>
#include <memory>

namespace {

QString lastLine(const QString &log)
{
    const QStringList lines = log.split('\n', Qt::SkipEmptyParts);
    for (auto it = lines.crbegin(); it != lines.crend(); ++it)
        if (!it->trimmed().isEmpty())
            return it->trimmed();
    return QStringLiteral("ffmpeg failed");
}

void removeFiles(const QStringList &files)
{
    for (const QString &f : files)
        QFile::remove(f);
}

} // namespace

// ================================================================= helpers

namespace QualityProbe {

bool vmafAvailable() { return EncoderCaps::instance().hasFilter(QStringLiteral("libvmaf")); }

TimeRanges sampleRanges(const BuildRequest &req, int count, double length)
{
    BuildRequest r = req;
    r.keepRanges.clear();
    const double in = qMax(0.0, req.inPoint);
    const double d = CommandBuilder::outputDuration(r);
    if (d <= 0)
        return {};
    if (d <= length * 1.5)
        return {qMakePair(in, in + d)};
    count = qBound(1, count, int(d / length));
    TimeRanges out;
    for (int i = 0; i < count; ++i) {
        const double s = qBound(in, in + d * (i + 0.5) / count - length / 2.0, in + d - length);
        out << qMakePair(s, s + length);
    }
    return out;
}

BuildRequest sampleRequest(const BuildRequest &base, double start, double length, const QString &out)
{
    BuildRequest r = base;
    r.inPoint = start;
    r.outPoint = start + length;
    r.keepRanges.clear();
    r.captionsSrt.clear();
    r.captionsAss.clear();
    r.sizeScale = 1.0;
    r.pass = 0;
    r.outputPath = out;
    r.settings.audio.enabled = false;
    r.settings.effects.autoCaptions = false;
    r.settings.effects.removeSilence = false;
    return r;
}

bool isTenBit(const BuildRequest &req)
{
    const QString pix = CommandBuilder::resolvePixelFormat(req.settings, req.source);
    return pix.contains(QStringLiteral("10")) || pix.contains(QStringLiteral("12"));
}

BuildRequest referenceRequest(const BuildRequest &base, double start, double length, const QString &out)
{
    BuildRequest r = sampleRequest(base, start, length, out);
    const bool tenBit = isTenBit(base);
    ExportSettings &s = r.settings;
    s.format = QStringLiteral("h264");
    VideoSettings &v = s.video;
    v.codec = QStringLiteral("libx264");
    v.rateControl = QStringLiteral("quality");
    v.quality = 0;
    v.speedPreset = QStringLiteral("ultrafast");
    v.profile.clear();
    v.level.clear();
    v.keyframeInterval = 0;
    v.pixelFormat = tenBit ? QStringLiteral("yuv420p10le") : QStringLiteral("yuv420p");
    // QP 0 is lossless; it needs the High 4:4:4 Predictive profile, which x264 only picks by itself for 8-bit.
    v.extraArgs = tenBit ? QStringLiteral("-qp 0 -profile:v high444") : QStringLiteral("-qp 0");
    return r;
}

QStringList vmafArgs(const QString &distorted, const QString &reference, bool tenBit)
{
    const QString fmt = tenBit ? QStringLiteral("yuv420p10le") : QStringLiteral("yuv420p");
    const int threads = qBound(1, QThread::idealThreadCount(), 16);
    return {"-hide_banner",
            "-nostdin",
            "-loglevel",
            "info",
            "-i",
            distorted,
            "-i",
            reference,
            "-lavfi",
            QStringLiteral("[0:v]setpts=PTS-STARTPTS,format=%1[d];[1:v]setpts=PTS-STARTPTS,format=%1[r];"
                           "[d][r]libvmaf=n_threads=%2")
                .arg(fmt)
                .arg(threads),
            "-f",
            "null",
            "-"};
}

double parseVmafScore(const QString &log)
{
    static const QRegularExpression re(QStringLiteral("VMAF score: ([0-9]+(?:\\.[0-9]+)?)"));
    double score = -1.0;
    auto it = re.globalMatch(log);
    while (it.hasNext())
        score = it.next().captured(1).toDouble();
    return score;
}

QString qualityName(const QString &codec)
{
    const VideoCodecDef *def = Formats::videoCodec(codec);
    const QString arg = def ? def->qualityArg : QString();
    if (arg == "-crf")
        return QStringLiteral("CRF");
    if (arg == "-cq")
        return QStringLiteral("CQ");
    if (arg == "-qp")
        return QStringLiteral("QP");
    return QStringLiteral("quality");
}

} // namespace QualityProbe

// ================================================================= ProbeRunner

ProbeRunner::ProbeRunner(QObject *owner, const QString &ffmpeg)
    : m_owner(owner)
    , m_ffmpeg(ffmpeg)
{}

ProbeRunner::~ProbeRunner() { stop(); }

void ProbeRunner::run(const QStringList &args, const QString &workDir, Done done, LineFn onLine)
{
    runProgram(m_ffmpeg, args, workDir, std::move(done), std::move(onLine));
}

void ProbeRunner::runProgram(const QString &program, const QStringList &args, const QString &workDir, Done done,
                             LineFn onLine)
{
    if (m_stopped)
        return;
    auto *p = new QProcess(m_owner);
    m_proc = p;
    m_paused = false;
    p->setWorkingDirectory(workDir);
    p->setProcessChannelMode(QProcess::MergedChannels);
    auto log = std::make_shared<QByteArray>();
    auto partial = std::make_shared<QByteArray>();
    auto handled = std::make_shared<bool>(false);
    auto consume = [p, log, partial, onLine] {
        const QByteArray chunk = p->readAll();
        *log += chunk;
        if (log->size() > 256 * 1024)
            log->remove(0, log->size() - 128 * 1024);
        if (!onLine)
            return;
        *partial += chunk;
        for (;;) {
            // Split on whichever line break comes first (ffmpeg and whisper also use bare CRs).
            const int a = int(partial->indexOf('\n'));
            const int b = int(partial->indexOf('\r'));
            if (a < 0 && b < 0)
                break;
            const int cut = (a >= 0 && b >= 0) ? qMin(a, b) : qMax(a, b);
            const QByteArray line = partial->left(cut).trimmed();
            partial->remove(0, cut + 1);
            if (!line.isEmpty())
                onLine(QString::fromUtf8(line));
        }
    };
    auto complete = [this, p, log, handled, done, consume](bool ok) {
        if (*handled)
            return;
        *handled = true;
        consume();
        if (m_proc == p)
            m_proc = nullptr;
        p->deleteLater();
        if (!m_stopped)
            done(ok, QString::fromUtf8(*log));
    };
    QObject::connect(p, &QProcess::started, m_owner, [this, p] {
        if (m_wantPaused && m_proc == p && SystemIntegration::suspendProcess(p->processId(), true))
            m_paused = true;
    });
    QObject::connect(p, &QProcess::readyRead, m_owner, consume);
    QObject::connect(p, &QProcess::finished, m_owner, [complete](int code, QProcess::ExitStatus st) {
        complete(st == QProcess::NormalExit && code == 0);
    });
    QObject::connect(p, &QProcess::errorOccurred, m_owner, [complete](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart)
            complete(false);
    });
    p->start(program, args);
}

void ProbeRunner::stop()
{
    m_stopped = true;
    if (QProcess *p = m_proc) {
        m_proc = nullptr;
        if (m_paused)
            SystemIntegration::suspendProcess(p->processId(), false);
        p->disconnect(m_owner);
        p->kill();
        p->waitForFinished(3000);
        p->deleteLater();
    }
}

bool ProbeRunner::setPaused(bool paused)
{
    // Remembered, so a command that starts while paused is suspended as soon as it starts.
    m_wantPaused = paused;
    if (!m_proc || m_proc->state() != QProcess::Running || paused == m_paused)
        return true;
    if (!SystemIntegration::suspendProcess(m_proc->processId(), paused))
        return false;
    m_paused = paused;
    return true;
}

// ================================================================= QualitySearch

QualitySearch::QualitySearch(const BuildRequest &base, const QString &ffmpeg, QObject *parent)
    : QObject(parent)
    , m_base(base)
    , m_runner(this, ffmpeg)
{
    m_dir = QDir(base.workDir.isEmpty() ? AppSettings::tempDir() : base.workDir).filePath(QStringLiteral("vmaf"));
}

QualitySearch::~QualitySearch()
{
    m_runner.stop();
    QDir(m_dir).removeRecursively();
}

int QualitySearch::toQuality(int t) const
{
    const VideoCodecDef *def = Formats::videoCodec(m_base.settings.video.codec);
    return def->qualityHigherIsBetter ? def->qualityMax - t : def->qualityMin + t;
}

int QualitySearch::toSearch(int quality) const
{
    const VideoCodecDef *def = Formats::videoCodec(m_base.settings.video.codec);
    return def->qualityHigherIsBetter ? def->qualityMax - quality : quality - def->qualityMin;
}

QString QualitySearch::label(int t) const
{
    return QStringLiteral("%1 %2").arg(QualityProbe::qualityName(m_base.settings.video.codec)).arg(toQuality(t));
}

void QualitySearch::step(const QString &status)
{
    ++m_unitsDone;
    emit progress(qMin(0.99, double(m_unitsDone) / qMax(1, m_units)), status);
}

void QualitySearch::finish(bool ok, int quality, double score, const QString &message)
{
    if (m_finished)
        return;
    m_finished = true;
    emit finished(ok, quality, score, message);
}

void QualitySearch::start()
{
    const VideoCodecDef *def = Formats::videoCodec(m_base.settings.video.codec);
    const int fallback = m_base.settings.video.quality;
    if (!def || def->qualityArg.isEmpty()) {
        finish(false, fallback, -1, QStringLiteral("This codec has no quality setting to search"));
        return;
    }
    if (!QualityProbe::vmafAvailable()) {
        finish(false, fallback, -1, QStringLiteral("This FFmpeg build has no VMAF filter"));
        return;
    }
    const double duration = CommandBuilder::outputDuration(m_base);
    m_samples = QualityProbe::sampleRanges(m_base, duration < 120 ? 3 : 5, 4.0);
    if (m_samples.isEmpty()) {
        finish(false, fallback, -1, QStringLiteral("The source is too short to analyze"));
        return;
    }
    QDir().mkpath(m_dir);
    // Search the useful middle of the scale: the extremes are either lossless or unwatchable.
    const int span = def->qualityMax - def->qualityMin;
    m_lo = int(std::lround(span * 0.15));
    m_hi = int(std::lround(span * 0.8));
    m_units = int(m_samples.size()) * (1 + 2 * 6);
    emit progress(0.0, QStringLiteral("Finding quality: preparing samples"));
    encodeReference(0);
}

void QualitySearch::encodeReference(int index)
{
    if (index >= m_samples.size()) {
        testValue(qBound(m_lo, toSearch(m_base.settings.video.quality), m_hi));
        return;
    }
    const auto range = m_samples[index];
    const QString out = QDir(m_dir).filePath(QStringLiteral("reference_%1.mp4").arg(index));
    BuildRequest req = QualityProbe::referenceRequest(m_base, range.first, range.second - range.first, out);
    req.workDir = m_dir;
    const BuildResult r = CommandBuilder::build(req);
    if (!r.ok()) {
        finish(false, m_base.settings.video.quality, -1, r.error);
        return;
    }
    m_runner.run(r.args, m_dir, [this, index, out, temp = r.tempFiles](bool ok, const QString &log) {
        removeFiles(temp);
        if (!ok) {
            finish(false, m_base.settings.video.quality, -1,
                   QStringLiteral("Couldn't prepare the quality analysis: %1").arg(lastLine(log)));
            return;
        }
        m_references << out;
        step(QStringLiteral("Finding quality: prepared sample %1 of %2").arg(index + 1).arg(m_samples.size()));
        encodeReference(index + 1);
    });
}

void QualitySearch::testValue(int t)
{
    m_t = t;
    m_current.clear();
    ++m_iterations;
    testSample(0);
}

void QualitySearch::testSample(int index)
{
    if (index >= m_samples.size()) {
        double sum = 0.0;
        for (double s : std::as_const(m_current))
            sum += s;
        valueTested(m_current.isEmpty() ? -1.0 : sum / m_current.size());
        return;
    }
    const auto range = m_samples[index];
    const FormatDef *f = Formats::format(m_base.settings.format);
    const QString out =
        QDir(m_dir).filePath(QStringLiteral("test_%1.%2").arg(index).arg(f ? f->extension : QStringLiteral("mp4")));
    BuildRequest req = QualityProbe::sampleRequest(m_base, range.first, range.second - range.first, out);
    req.workDir = m_dir;
    req.settings.video.rateControl = QStringLiteral("quality");
    req.settings.video.quality = toQuality(m_t);
    const BuildResult r = CommandBuilder::build(req);
    if (!r.ok()) {
        finish(false, m_base.settings.video.quality, -1, r.error);
        return;
    }
    const QString ref = m_references.value(index);
    m_runner.run(r.args, m_dir, [this, index, out, ref, temp = r.tempFiles](bool ok, const QString &log) {
        removeFiles(temp);
        if (!ok) {
            finish(false, m_base.settings.video.quality, -1,
                   QStringLiteral("Test encode at %1 failed: %2").arg(label(m_t), lastLine(log)));
            return;
        }
        step(QStringLiteral("Finding quality: measuring %1 (sample %2 of %3)")
                 .arg(label(m_t))
                 .arg(index + 1)
                 .arg(m_samples.size()));
        m_runner.run(QualityProbe::vmafArgs(out, ref, QualityProbe::isTenBit(m_base)), m_dir,
                     [this, index, out](bool ok, const QString &log) {
                         QFile::remove(out);
                         const double score = QualityProbe::parseVmafScore(log);
                         if (!ok || score < 0) {
                             finish(false, m_base.settings.video.quality, -1,
                                    QStringLiteral("VMAF measurement failed: %1").arg(lastLine(log)));
                             return;
                         }
                         m_current << score;
                         step(QStringLiteral("Finding quality: %1 sample %2 scored VMAF %3")
                                  .arg(label(m_t))
                                  .arg(index + 1)
                                  .arg(score, 0, 'f', 1));
                         testSample(index + 1);
                     });
    });
}

void QualitySearch::valueTested(double score)
{
    m_scores.insert(m_t, score);
    const double target = m_base.settings.video.targetVmaf;
    // Higher search values are lower quality: move up while the target is still met.
    if (score >= target)
        m_lo = m_t + 1;
    else
        m_hi = m_t - 1;
    emit progress(qMin(0.99, double(m_unitsDone) / qMax(1, m_units)),
                  QStringLiteral("Finding quality: %1 scored VMAF %2").arg(label(m_t)).arg(score, 0, 'f', 1));
    if (m_lo <= m_hi && m_iterations < 8) {
        testValue((m_lo + m_hi) / 2);
        return;
    }
    int best = -1;
    for (auto it = m_scores.cbegin(); it != m_scores.cend(); ++it)
        if (it.value() >= target)
            best = qMax(best, it.key());
    if (best >= 0) {
        finish(true, toQuality(best), m_scores.value(best),
               QStringLiteral("%1 reaches VMAF %2 (target %3)")
                   .arg(label(best))
                   .arg(m_scores.value(best), 0, 'f', 1)
                   .arg(target, 0, 'g', 4));
        return;
    }
    // Nothing reached the target: use the best quality that was tried.
    const int top = m_scores.firstKey();
    finish(true, toQuality(top), m_scores.value(top),
           QStringLiteral("VMAF %1 wasn't reached; the best was %2 at %3")
               .arg(target, 0, 'g', 4)
               .arg(m_scores.value(top), 0, 'f', 1)
               .arg(label(top)));
}

void QualitySearch::stop() { m_runner.stop(); }

void QualitySearch::setPaused(bool paused) { m_runner.setPaused(paused); }

// ================================================================= TestEncode

TestEncode::TestEncode(const BuildRequest &full, double start, double length, const QString &ffmpeg, QObject *parent)
    : QObject(parent)
    , m_full(full)
    , m_runner(this, ffmpeg)
{
    m_dir = QDir(AppSettings::tempDir())
                .filePath(QStringLiteral("compare_%1").arg(QUuid::createUuid().toString(QUuid::Id128)));
    m_result.start = start;
    m_result.length = length;
}

TestEncode::~TestEncode()
{
    m_runner.stop();
    QDir(m_dir).removeRecursively();
}

void TestEncode::start()
{
    QDir().mkpath(m_dir);
    const FormatDef *f = Formats::format(m_full.settings.format);
    m_result.file = QDir(m_dir).filePath(QStringLiteral("sample.%1").arg(f ? f->extension : QStringLiteral("mp4")));
    m_sample = QualityProbe::sampleRequest(m_full, m_result.start, m_result.length, m_result.file);
    m_sample.workDir = m_dir;
    m_sample.passLogFile = QStringLiteral("passlog");
    VideoSettings &v = m_sample.settings.video;
    if (v.rateControl == "size") {
        // The bitrate comes from the whole output's length, not the sample's.
        QString err;
        const double mbps = CommandBuilder::targetSizeBitrate(m_full, &err);
        if (mbps <= 0) {
            emit finished(false, err);
            return;
        }
        v.rateControl = CommandBuilder::usesTwoPass(m_full.settings) ? QStringLiteral("vbr2") : QStringLiteral("vbr1");
        v.targetBitrate = mbps;
        v.maxBitrate = mbps * 2.0;
        v.scaleBitrateWithFps = false;
    } else if (v.rateControl == "vmaf") {
        v.rateControl = QStringLiteral("quality");
    }
    m_passes = CommandBuilder::usesTwoPass(m_sample.settings) ? 2 : 1;
    m_timer.start();
    runPass(m_passes == 2 ? 1 : 0);
}

void TestEncode::stop() { m_runner.stop(); }

void TestEncode::runPass(int pass)
{
    BuildRequest req = m_sample;
    req.pass = pass;
    const BuildResult r = CommandBuilder::build(req);
    if (!r.ok()) {
        emit finished(false, r.error);
        return;
    }
    emit status(pass == 1 ? QStringLiteral("Test encode: first pass…") : QStringLiteral("Test encode…"));
    m_runner.run(r.args, m_dir, [this, pass, temp = r.tempFiles](bool ok, const QString &log) {
        removeFiles(temp);
        if (!ok) {
            emit finished(false, QStringLiteral("Test encode failed: %1").arg(lastLine(log)));
            return;
        }
        if (pass == 1) {
            runPass(2);
            return;
        }
        const double secs = m_timer.elapsed() / 1000.0;
        m_result.bytes = QFileInfo(m_result.file).size();
        m_result.mbps = m_result.length > 0 ? m_result.bytes * 8.0 / m_result.length / 1e6 : 0.0;
        m_result.speed = secs > 0 ? m_result.length / secs : 0.0;
        const double total = CommandBuilder::outputDuration(m_full);
        if (m_full.settings.video.rateControl == "size") {
            m_result.projected = qint64(m_full.settings.video.targetSizeMB * 1e6);
        } else if (total > 0 && m_result.length > 0) {
            ExportSettings audioOnly = m_full.settings;
            audioOnly.video.enabled = false;
            const qint64 audio = m_full.source.hasAudio ? audioOnly.estimateSize(total, &m_full.source) : 0;
            m_result.projected = qint64(m_result.bytes / m_result.length * total) + qMax<qint64>(0, audio);
        }
        scoreSample();
    });
}

void TestEncode::scoreSample()
{
    if (!QualityProbe::vmafAvailable()) {
        emit finished(true, {});
        return;
    }
    emit status(QStringLiteral("Measuring quality (VMAF)…"));
    const QString ref = QDir(m_dir).filePath(QStringLiteral("reference.mp4"));
    BuildRequest req = QualityProbe::referenceRequest(m_full, m_result.start, m_result.length, ref);
    req.workDir = m_dir;
    const BuildResult r = CommandBuilder::build(req);
    if (!r.ok()) {
        emit finished(true, {}); // the comparison still works without a score
        return;
    }
    m_runner.run(r.args, m_dir, [this, ref, temp = r.tempFiles](bool ok, const QString &) {
        removeFiles(temp);
        if (!ok) {
            emit finished(true, {});
            return;
        }
        m_runner.run(QualityProbe::vmafArgs(m_result.file, ref, QualityProbe::isTenBit(m_full)), m_dir,
                     [this](bool, const QString &log) {
                         m_result.vmaf = QualityProbe::parseVmafScore(log);
                         emit finished(true, {});
                     });
    });
}
