#include "EncoderEngine.h"

#include "AppSettings.h"
#include "Formats.h"
#include "QualityProbe.h"
#include "QueueModel.h"
#include "SystemIntegration.h"
#include "Whisper.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QThread>
#include <QTimer>
#include <memory>

namespace {

using SystemIntegration::suspendProcess;

QString quoteArg(const QString &a)
{
    if (a.isEmpty())
        return QStringLiteral("\"\"");
    if (a.contains(' ') || a.contains(';') || a.contains('[') || a.contains('\'') || a.contains('&'))
        return QStringLiteral("\"%1\"").arg(QString(a).replace('"', QStringLiteral("\\\"")));
    return a;
}

} // namespace

// ================================================================= EncodeTask

EncodeTask::EncodeTask(const QString &sourceId, const QString &outputId, const BuildRequest &req, const QString &ffmpeg,
                       QObject *parent)
    : QObject(parent)
    , m_sourceId(sourceId)
    , m_outputId(outputId)
    , m_req(req)
    , m_ffmpeg(ffmpeg)
{
    m_runner = new ProbeRunner(this, ffmpeg);
    m_passes = CommandBuilder::usesTwoPass(req.settings) ? 2 : 1;
    m_stats.passes = m_passes;

    // The steps this output needs, each with its rough share of the total time.
    const ExportSettings &s = req.settings;
    const MediaInfo &src = req.source;
    const FormatDef *fmt = Formats::format(s.format);
    const bool video = s.video.enabled && fmt && fmt->hasVideo() && src.hasVideo && !src.isStill;
    if (s.effects.removeSilence && src.hasAudio && !src.isStill) {
        m_steps << Step::Silence;
        m_weights << (s.effects.removeFillers ? 1.0 : 0.15);
    } else if (s.effects.removeSilence) {
        m_notes << QStringLiteral("Remove silence: skipped, the source has no audio");
    }
    if (video && s.video.rateControl == "vmaf") {
        m_steps << Step::Quality;
        m_weights << 1.5;
    }
    if (s.effects.autoCaptions && src.hasAudio && !src.isStill) {
        m_steps << Step::Captions;
        m_weights << 0.8;
    } else if (s.effects.autoCaptions) {
        m_notes << QStringLiteral("Captions: skipped, the source has no audio");
    }
    m_steps << Step::Encode;
    m_weights << 1.0 * m_passes;
}

EncodeTask::~EncodeTask()
{
    if (m_proc && m_proc->state() != QProcess::NotRunning) {
        if (m_paused)
            suspendProcess(m_proc->processId(), false);
        m_proc->kill();
        m_proc->waitForFinished(2000);
    }
    delete m_search;
    delete m_runner;
    cleanup();
}

void EncodeTask::start()
{
    m_timer.start();
    QDir().mkpath(m_req.workDir);
    nextStep();
}

void EncodeTask::nextStep()
{
    if (m_stopping || m_done)
        return;
    ++m_step;
    if (m_step >= m_steps.size())
        return;
    switch (m_steps[m_step]) {
    case Step::Silence:
        detectSilence();
        break;
    case Step::Quality:
        findQuality();
        break;
    case Step::Captions:
        transcribe();
        break;
    case Step::Encode:
        startEncode();
        break;
    }
}

// Progress of the current step (0..1) mapped onto the whole task.
void EncodeTask::stepProgress(double fraction, const QString &phase)
{
    double total = 0.0;
    double before = 0.0;
    for (int i = 0; i < m_weights.size(); ++i) {
        total += m_weights[i];
        if (i < m_step)
            before += m_weights[i];
    }
    const double cur = m_step >= 0 && m_step < m_weights.size() ? m_weights[m_step] : 0.0;
    m_stats.progress = total > 0 ? qBound(0.0, (before + qBound(0.0, fraction, 1.0) * cur) / total, 1.0) : 0.0;
    m_stats.phase = phase;
    m_stats.elapsed = double(m_timer.elapsed() - m_pausedMs) / 1000.0;
    m_stats.remaining = m_stats.progress > 0.02 ? m_stats.elapsed * (1.0 - m_stats.progress) / m_stats.progress : -1.0;
    emit progress(m_stats);
}

// ---------------------------------------------------------------- remove silence

void EncodeTask::detectSilence()
{
    const EffectsSettings &fx = m_req.settings.effects;
    BuildRequest req = m_req;
    req.keepRanges.clear();
    const double duration = CommandBuilder::outputDuration(req);
    m_scanDuration = duration;
    // Filler words need every short gap between sounds, not just pauses long enough to cut.
    const double shortest = fx.removeFillers ? qMin(0.1, fx.silenceMinDuration) : fx.silenceMinDuration;
    const double scanShare = fx.removeFillers ? 0.2 : 1.0;
    const BuildResult r = CommandBuilder::buildAudioExtract(
        req,
        QStringLiteral("silencedetect=noise=%1dB:d=%2")
            .arg(QString::number(fx.silenceThreshold, 'f', 1), QString::number(shortest, 'f', 2)),
        {"-f", "null", "-"});
    m_tempFiles << r.tempFiles;
    if (!r.ok()) {
        fail(r.error);
        return;
    }
    stepProgress(0.0, QStringLiteral("Finding pauses"));
    auto silences = std::make_shared<TimeRanges>();
    static const QRegularExpression startRe(QStringLiteral("silence_start: (-?[0-9.]+)"));
    static const QRegularExpression endRe(QStringLiteral("silence_end: ([0-9.]+)"));
    m_runner->run(
        r.args, m_req.workDir,
        [this, silences, duration](bool ok, const QString &log) {
            if (!ok) {
                fail(QStringLiteral("Couldn't scan the audio for pauses: %1").arg(log.trimmed().section('\n', -1)));
                return;
            }
            // A silence still open at the end of the clip runs to the end.
            if (!silences->isEmpty() && silences->last().second < 0)
                silences->last().second = duration;
            m_gaps = *silences;
            if (m_req.settings.effects.removeFillers)
                findFillers();
            else
                applyCuts({}, 0);
        },
        [this, silences, duration, scanShare](const QString &line) {
            const auto s = startRe.match(line);
            if (s.hasMatch()) {
                *silences << qMakePair(qMax(0.0, s.captured(1).toDouble()), -1.0);
                return;
            }
            const auto e = endRe.match(line);
            if (e.hasMatch() && !silences->isEmpty()) {
                silences->last().second = e.captured(1).toDouble();
                return;
            }
            if (line.startsWith(QStringLiteral("out_time_us=")) && duration > 0)
                stepProgress(scanShare * line.mid(12).toDouble() / 1e6 / duration, QStringLiteral("Finding pauses"));
        });
}

// Speech recognition with word timings finds the filler words; the gaps from the silence scan give
// the exact places to cut.
void EncodeTask::findFillers()
{
    BuildRequest req = m_req;
    req.keepRanges.clear();
    const QString wav = QDir(m_req.workDir).filePath(QStringLiteral("fillers.wav"));
    const BuildResult r = CommandBuilder::buildAudioExtract(req, QStringLiteral("aresample=16000"),
                                                            {"-ac", "1", "-c:a", "pcm_s16le", "-f", "wav", wav});
    m_tempFiles << r.tempFiles;
    if (!r.ok()) {
        fail(r.error);
        return;
    }
    stepProgress(0.2, QStringLiteral("Finding filler words"));
    m_runner->run(r.args, m_req.workDir, [this, wav](bool ok, const QString &log) {
        if (!ok) {
            fail(QStringLiteral("Couldn't read the audio for filler words: %1").arg(log.trimmed().section('\n', -1)));
            return;
        }
        const EffectsSettings &fx = m_req.settings.effects;
        const QString model = Whisper::activeModelPath();
        const QString base = QDir(m_req.workDir).filePath(QStringLiteral("words"));
        QStringList args{"-m", model, "-f", wav, "-ojf", "-of", base, "-pp", "-nfa", "-t",
                         QString::number(qBound(1, QThread::idealThreadCount(), 8)), "-l",
                         fx.captionLanguage.isEmpty() ? QStringLiteral("auto") : fx.captionLanguage,
                         // Without an example, whisper tidies "um" and "uh" out of what it hears.
                         "--prompt", Whisper::fillerPrompt()};
        const QString dtw = Whisper::dtwPreset(model);
        if (!dtw.isEmpty())
            args << "--dtw" << dtw; // word timings from the attention alignment: much steadier
        static const QRegularExpression progressRe(QStringLiteral("progress =\\s*([0-9]+)%"));
        m_runner->runProgram(
            AppSettings::whisperCliPath(), args, m_req.workDir,
            [this, base](bool ok, const QString &log) {
                if (!ok) {
                    fail(QStringLiteral("Finding filler words failed: %1").arg(log.trimmed().section('\n', -1)));
                    return;
                }
                const EffectsSettings &fx = m_req.settings.effects;
                int count = 0;
                const TimeRanges cuts =
                    Whisper::fillerCuts(Whisper::parseWords(base + QStringLiteral(".json")), m_gaps, m_scanDuration,
                                        fx.silencePadding, Whisper::fillerWords(fx.fillerWords), &count);
                applyCuts(cuts, count);
            },
            [this](const QString &line) {
                const auto p = progressRe.match(line);
                if (p.hasMatch())
                    stepProgress(0.3 + 0.7 * p.captured(1).toInt() / 100.0,
                                 QStringLiteral("Finding filler words %1%").arg(p.captured(1).toInt()));
            });
    });
}

void EncodeTask::applyCuts(const TimeRanges &fillerCuts, int fillerCount)
{
    const EffectsSettings &fx = m_req.settings.effects;
    const double duration = m_scanDuration;
    // Pauses: the gaps at least as long as the shortest pause to cut.
    TimeRanges pauses;
    for (const auto &g : std::as_const(m_gaps))
        if (g.second - g.first >= fx.silenceMinDuration - 0.001)
            pauses << g;
    TimeRanges keep = CommandBuilder::keepRangesFromSilences(pauses, duration, fx.silencePadding);
    if (!fillerCuts.isEmpty())
        keep = Whisper::subtractRanges(keep, fillerCuts);
    double kept = 0.0;
    for (const auto &k : keep)
        kept += k.second - k.first;
    if (keep.isEmpty() || kept < 0.1) {
        fail(QStringLiteral("The audio is silent throughout, so there is nothing left after removing silence"));
        return;
    }
    const QString fillers =
        fx.removeFillers ? QStringLiteral(" and %1 filler word%2").arg(fillerCount).arg(fillerCount == 1 ? "" : "s")
                         : QString();
    if (keep.size() == 1 && keep.first().first <= 0.001 && keep.first().second >= duration - 0.001) {
        m_notes << (fx.removeFillers ? QStringLiteral("Remove silence: no pauses or filler words to cut")
                                     : QStringLiteral("Remove silence: no pauses long enough to cut"));
    } else {
        m_req.keepRanges = keep;
        m_notes << QStringLiteral("Remove silence: cut %1 of pauses%2 (%3 kept of %4)")
                       .arg(formatDuration(duration - kept), fillers, formatDuration(kept), formatDuration(duration));
    }
    nextStep();
}

// ---------------------------------------------------------------- target quality (VMAF)

void EncodeTask::findQuality()
{
    m_search = new QualitySearch(m_req, m_ffmpeg);
    connect(m_search, &QualitySearch::progress, this,
            [this](double fraction, const QString &status) { stepProgress(fraction, status); });
    connect(m_search, &QualitySearch::finished, this, [this](bool ok, int quality, double, const QString &message) {
        QualitySearch *search = m_search;
        m_search = nullptr;
        search->deleteLater();
        VideoSettings &v = m_req.settings.video;
        if (!ok && QualityProbe::vmafAvailable()) {
            fail(message);
            return;
        }
        v.rateControl = QStringLiteral("quality");
        v.quality = quality;
        m_notes << (ok ? QStringLiteral("Target quality: %1").arg(message)
                       : QStringLiteral("Target quality: %1; encoding at %2 %3")
                             .arg(message, QualityProbe::qualityName(v.codec))
                             .arg(quality));
        nextStep();
    });
    m_search->start();
}

// ---------------------------------------------------------------- captions (speech to text)

void EncodeTask::transcribe()
{
    const QString cli = AppSettings::whisperCliPath();
    if (cli.isEmpty()) {
        fail(QStringLiteral("Automatic captions need whisper-cli, which wasn't found next to the app"));
        return;
    }
    if (Whisper::activeModelPath().isEmpty()) {
        fail(QStringLiteral("Automatic captions need the speech model. Download it in Preferences > Captions."));
        return;
    }
    // whisper.cpp reads 16 kHz mono audio: decode exactly what gets encoded (range, stitching, cuts).
    const QString wav = QDir(m_req.workDir).filePath(QStringLiteral("speech.wav"));
    const double duration = CommandBuilder::outputDuration(m_req);
    const BuildResult r = CommandBuilder::buildAudioExtract(m_req, QStringLiteral("aresample=16000"),
                                                            {"-ac", "1", "-c:a", "pcm_s16le", "-f", "wav", wav});
    m_tempFiles << r.tempFiles;
    if (!r.ok()) {
        fail(r.error);
        return;
    }
    stepProgress(0.0, QStringLiteral("Preparing audio for captions"));
    m_runner->run(
        r.args, m_req.workDir,
        [this, wav](bool ok, const QString &log) {
            if (!ok) {
                fail(QStringLiteral("Couldn't read the audio for captions: %1").arg(log.trimmed().section('\n', -1)));
                return;
            }
            transcribeAudio(wav);
        },
        [this, duration](const QString &line) {
            if (line.startsWith(QStringLiteral("out_time_us=")) && duration > 0)
                stepProgress(0.1 * line.mid(12).toDouble() / 1e6 / duration,
                             QStringLiteral("Preparing audio for captions"));
        });
}

void EncodeTask::transcribeAudio(const QString &wav)
{
    const EffectsSettings &fx = m_req.settings.effects;
    const QString base = QDir(m_req.workDir).filePath(QStringLiteral("captions"));
    QStringList args{"-m", Whisper::activeModelPath(), "-f", wav, "-osrt", "-of", base, "-pp", "-t",
                     QString::number(qBound(1, QThread::idealThreadCount(), 8)),
                     // Caption-sized lines, split between words.
                     "-ml", "42", "-sow", "-l",
                     fx.captionLanguage.isEmpty() ? QStringLiteral("auto") : fx.captionLanguage};
    if (fx.captionTranslate)
        args << "-tr";
    m_req.captionsLanguage = fx.captionLanguage == "auto" ? QString() : fx.captionLanguage;
    static const QRegularExpression progressRe(QStringLiteral("progress =\\s*([0-9]+)%"));
    static const QRegularExpression langRe(QStringLiteral("auto-detected language: ([a-z]+)"));
    stepProgress(0.1, QStringLiteral("Transcribing speech"));
    m_runner->runProgram(
        AppSettings::whisperCliPath(), args, m_req.workDir,
        [this, base](bool ok, const QString &log) {
            if (!ok) {
                fail(QStringLiteral("Speech to text failed: %1").arg(log.trimmed().section('\n', -1)));
                return;
            }
            captionsReady(base + QStringLiteral(".srt"));
        },
        [this](const QString &line) {
            const auto p = progressRe.match(line);
            if (p.hasMatch()) {
                const int pct = p.captured(1).toInt();
                stepProgress(0.1 + 0.9 * pct / 100.0, QStringLiteral("Transcribing speech %1%").arg(pct));
                return;
            }
            const auto l = langRe.match(line);
            if (l.hasMatch())
                m_req.captionsLanguage = l.captured(1);
        });
}

void EncodeTask::captionsReady(const QString &srt)
{
    const EffectsSettings &fx = m_req.settings.effects;
    const QList<Whisper::Cue> cues = Whisper::cleanCues(Whisper::parseSrt(srt));
    if (cues.isEmpty()) {
        m_notes << QStringLiteral("Captions: no speech was found");
        nextStep();
        return;
    }
    m_captionsSrt = QDir(m_req.workDir).filePath(QStringLiteral("captions_clean.srt"));
    if (!Whisper::writeSrt(cues, m_captionsSrt)) {
        fail(QStringLiteral("Couldn't write the captions file"));
        return;
    }
    if (fx.captionEmbed && !CommandBuilder::subtitleCodecFor(m_req.settings).isEmpty())
        m_req.captionsSrt = m_captionsSrt;
    if (fx.captionBurn) {
        const QString ass = QDir(m_req.workDir).filePath(QStringLiteral("captions.ass"));
        if (!Whisper::writeAss(cues, ass, CommandBuilder::outputFrameSize(m_req), fx.autoCaptionSize)) {
            fail(QStringLiteral("Couldn't write the captions file"));
            return;
        }
        m_req.captionsAss = ass;
    }
    m_notes << QStringLiteral("Captions: %1 lines%2")
                   .arg(cues.size())
                   .arg(m_req.captionsLanguage.isEmpty() ? QString()
                                                         : QStringLiteral(" (%1)").arg(m_req.captionsLanguage));
    nextStep();
}

// ---------------------------------------------------------------- encode

void EncodeTask::startEncode()
{
    m_passes = CommandBuilder::usesTwoPass(m_req.settings) ? 2 : 1;
    m_stats.passes = m_passes;
    startPass(m_passes == 2 ? 1 : 0);
}

bool EncodeTask::startPass(int pass)
{
    m_pass = pass;
    m_stats.pass = qMax(1, pass);
    BuildRequest req = m_req;
    req.pass = pass;
    const BuildResult r = CommandBuilder::build(req);
    m_tempFiles << r.tempFiles;
    if (!r.ok()) {
        QTimer::singleShot(0, this, [this, err = r.error] { fail(err); });
        return false;
    }

    m_lastCommand = quoteArg(m_ffmpeg);
    for (const QString &a : r.args)
        m_lastCommand += ' ' + quoteArg(a);

    if (m_proc)
        m_proc->deleteLater();
    m_stdoutBuf.clear();
    m_proc = new QProcess(this);
    m_proc->setWorkingDirectory(m_req.workDir);
    m_proc->setProgram(m_ffmpeg);
    m_proc->setArguments(r.args);
    connect(m_proc, &QProcess::readyReadStandardOutput, this, &EncodeTask::onReadyStdout);
    connect(m_proc, &QProcess::readyReadStandardError, this, &EncodeTask::onReadyStderr);
    connect(m_proc, &QProcess::finished, this, &EncodeTask::onProcessFinished);
    connect(m_proc, &QProcess::started, this, [this] {
        if (m_paused) // paused between steps: hold this pass until resumed
            suspendProcess(m_proc->processId(), true);
    });
    connect(m_proc, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart)
            fail(QStringLiteral("Could not start ffmpeg (%1)").arg(m_ffmpeg));
    });
    m_proc->start();
    return true;
}

void EncodeTask::stop()
{
    m_stopping = true;
    if (m_proc && m_proc->state() != QProcess::NotRunning) {
        if (m_paused)
            suspendProcess(m_proc->processId(), false);
        m_proc->kill(); // onProcessFinished reports the stop
        return;
    }
    // Stopped during an analysis step (or before ffmpeg started).
    if (m_search)
        m_search->stop();
    m_runner->stop();
    QTimer::singleShot(0, this, [this] {
        cleanup();
        emitFinished(false, true, QStringLiteral("Stopped by user"));
    });
}

void EncodeTask::setPaused(bool paused)
{
    if (paused == m_paused)
        return;
    if (m_proc && m_proc->state() == QProcess::Running && !suspendProcess(m_proc->processId(), paused))
        return;
    // The analysis runners remember the state, so a step that starts while paused waits too.
    if (m_search)
        m_search->setPaused(paused);
    m_runner->setPaused(paused);
    m_paused = paused;
    if (paused)
        m_pauseTimer.start();
    else
        m_pausedMs += m_pauseTimer.elapsed();
}

void EncodeTask::onReadyStdout()
{
    m_stdoutBuf += m_proc->readAllStandardOutput();
    int nl;
    const double duration =
        m_req.settings.video.enabled || m_req.settings.audio.enabled ? CommandBuilder::outputDuration(m_req) : 0.0;
    while ((nl = m_stdoutBuf.indexOf('\n')) >= 0) {
        const QByteArray line = m_stdoutBuf.left(nl).trimmed();
        m_stdoutBuf.remove(0, nl + 1);
        const int eq = line.indexOf('=');
        if (eq <= 0)
            continue;
        const QByteArray key = line.left(eq);
        const QByteArray val = line.mid(eq + 1).trimmed();
        if (key == "out_time_us" || key == "out_time_ms") {
            bool ok = false;
            const qint64 us = val.toLongLong(&ok);
            if (ok && us >= 0)
                m_stats.outTime = double(us) / 1e6;
        } else if (key == "fps") {
            m_stats.fps = val.toDouble();
        } else if (key == "speed") {
            QByteArray v = val;
            v.replace("x", "");
            m_stats.speed = v.toDouble();
        } else if (key == "total_size") {
            m_stats.bytes = val.toLongLong();
        } else if (key == "progress") {
            const double passFrac =
                duration > 0 ? qBound(0.0, m_stats.outTime / duration, 1.0) : (val == "end" ? 1.0 : 0.0);
            const int passIndex = m_passes == 2 ? m_pass - 1 : 0;
            QString phase;
            if (m_sizeRetries > 0)
                phase = QStringLiteral("Re-encoding to fit under %1 MB").arg(m_req.settings.video.targetSizeMB);
            else if (m_passes == 2)
                phase = QStringLiteral("Pass %1 of 2").arg(m_pass);
            else if (m_steps.size() > 1)
                phase = QStringLiteral("Encoding");
            stepProgress((passIndex + passFrac) / m_passes, phase);
        }
    }
}

void EncodeTask::onReadyStderr()
{
    const QString text = QString::fromUtf8(m_proc->readAllStandardError());
    for (const QString &l : text.split('\n', Qt::SkipEmptyParts)) {
        m_stderrTail << l.trimmed();
        if (m_stderrTail.size() > 60)
            m_stderrTail.removeFirst();
    }
}

void EncodeTask::onProcessFinished(int exitCode, QProcess::ExitStatus status)
{
    onReadyStdout();
    onReadyStderr();
    if (m_stopping) {
        cleanup();
        emitFinished(false, true, QStringLiteral("Stopped by user"));
        return;
    }
    if (status != QProcess::NormalExit || exitCode != 0) {
        QString err;
        for (auto it = m_stderrTail.crbegin(); it != m_stderrTail.crend(); ++it) {
            if (!it->isEmpty()) {
                err = *it;
                break;
            }
        }
        if (err.isEmpty())
            err = QStringLiteral("ffmpeg exited with code %1").arg(exitCode);
        fail(err);
        return;
    }
    if (m_passes == 2 && m_pass == 1) {
        startPass(2);
        return;
    }
    encodeFinished();
}

void EncodeTask::encodeFinished()
{
    const VideoSettings &v = m_req.settings.video;
    if (v.rateControl == "size" && !m_req.outputPath.contains(QStringLiteral("%05d"))) {
        // Encoders land close to the requested bitrate but not exactly on it: if the file came out too
        // big, encode again a little lower (twice at most).
        const qint64 actual = QFileInfo(m_req.outputPath).size();
        const double target = v.targetSizeMB * 1e6;
        if (actual > target) {
            if (m_sizeRetries < 2) {
                ++m_sizeRetries;
                m_req.sizeScale *= target / double(actual) * 0.97;
                startEncode();
                return;
            }
            m_notes << QStringLiteral("Target file size: %1 MB is still over %2 MB")
                           .arg(actual / 1e6, 0, 'f', 2)
                           .arg(v.targetSizeMB);
        } else {
            m_notes << QStringLiteral("Target file size: %1 MB (limit %2 MB)")
                           .arg(actual / 1e6, 0, 'f', 2)
                           .arg(v.targetSizeMB);
        }
    }
    // Captions next to the output: movie.mp4 -> movie.srt
    if (!m_captionsSrt.isEmpty() && m_req.settings.effects.captionSaveSrt &&
        !m_req.outputPath.contains(QStringLiteral("%05d"))) {
        const QFileInfo fi(m_req.outputPath);
        const QString srt = fi.dir().filePath(fi.completeBaseName() + QStringLiteral(".srt"));
        QFile::remove(srt);
        if (QFile::copy(m_captionsSrt, srt))
            m_extraFiles << srt;
        else
            m_notes << QStringLiteral("Captions: couldn't save %1").arg(QDir::toNativeSeparators(srt));
    }
    succeed();
}

void EncodeTask::fail(const QString &error)
{
    if (m_stopping)
        return;
    cleanup();
    emitFinished(false, false, error);
}

void EncodeTask::succeed()
{
    m_stats.progress = 1.0;
    m_stats.phase.clear();
    m_stats.elapsed = double(m_timer.elapsed() - m_pausedMs) / 1000.0;
    m_stats.remaining = 0;
    emit progress(m_stats);
    cleanup();
    emitFinished(true, false, {});
}

void EncodeTask::emitFinished(bool ok, bool stopped, const QString &error)
{
    if (m_done)
        return;
    m_done = true;
    emit finished(ok, stopped, error);
}

void EncodeTask::cleanup()
{
    for (const QString &f : std::as_const(m_tempFiles))
        QFile::remove(f);
    m_tempFiles.clear();
    if (!m_req.workDir.isEmpty()) {
        QDir dir(m_req.workDir);
        if (dir.exists() && dir.absolutePath().contains(QStringLiteral("OpenMediaEncoder")))
            dir.removeRecursively();
    }
}

// ================================================================= EncoderEngine

EncoderEngine::EncoderEngine(QueueModel *model, QObject *parent)
    : QObject(parent)
    , m_model(model)
{
    connect(m_model, &QueueModel::queueChanged, this, [this] {
        if (m_running)
            requestSchedule();
    });
}

EncoderEngine::~EncoderEngine()
{
    for (EncodeTask *t : std::as_const(m_tasks)) {
        t->disconnect(this);
        t->stop();
        delete t;
    }
}

QList<EncodeTask *> EncoderEngine::activeTasks() const { return m_tasks.values(); }

void EncoderEngine::start()
{
    if (m_running) {
        if (m_paused)
            resume();
        return;
    }
    m_running = true;
    m_paused = false;
    m_doneCount = 0;
    m_failedCount = 0;
    emit runningChanged(true);
    emit pausedChanged(false);
    EncodingLog::append(
        QStringLiteral("=== Queue started %1 ===").arg(QDateTime::currentDateTime().toString(Qt::ISODate)));
    scheduleNext();
}

void EncoderEngine::pause()
{
    if (!m_running || m_paused)
        return;
    m_paused = true;
    for (EncodeTask *t : std::as_const(m_tasks)) {
        t->setPaused(true);
        m_model->setStatus(t->outputId(), OutputStatus::Paused);
    }
    emit pausedChanged(true);
}

void EncoderEngine::resume()
{
    if (!m_paused)
        return;
    m_paused = false;
    for (EncodeTask *t : std::as_const(m_tasks)) {
        t->setPaused(false);
        m_model->setStatus(t->outputId(), OutputStatus::Encoding);
    }
    emit pausedChanged(false);
    scheduleNext();
}

void EncoderEngine::stop()
{
    if (!m_running)
        return;
    m_running = false;
    m_paused = false;
    const auto tasks = m_tasks.values();
    for (EncodeTask *t : tasks)
        t->stop();
    emit pausedChanged(false);
    if (m_tasks.isEmpty()) {
        emit runningChanged(false);
        emit queueFinished(m_doneCount, m_failedCount);
    }
}

void EncoderEngine::stopOutput(const QString &outputId)
{
    if (EncodeTask *t = m_tasks.value(outputId))
        t->stop();
}

void EncoderEngine::requestSchedule()
{
    if (m_schedulePending)
        return;
    m_schedulePending = true;
    QTimer::singleShot(0, this, [this] {
        m_schedulePending = false;
        scheduleNext();
    });
}

bool EncoderEngine::waitingForProbe() const
{
    for (const SourceItem &s : m_model->sources()) {
        if (s.probed)
            continue;
        for (const OutputItem &o : s.outputs)
            if (o.status == OutputStatus::Ready)
                return true;
    }
    return false;
}

void EncoderEngine::scheduleNext()
{
    if (!m_running || m_paused)
        return;
    const int maxParallel = AppSettings::parallelEncodes();
    while (m_tasks.size() < maxParallel) {
        QString sid, oid;
        if (!m_model->nextReady(&sid, &oid))
            break;
        if (!startTask(sid, oid))
            continue; // failed immediately; try the next one
    }
    if (m_tasks.isEmpty() && !waitingForProbe()) {
        m_running = false;
        emit runningChanged(false);
        EncodingLog::append(
            QStringLiteral("=== Queue finished: %1 done, %2 failed ===\n").arg(m_doneCount).arg(m_failedCount));
        emit queueFinished(m_doneCount, m_failedCount);
    }
}

bool EncoderEngine::startTask(const QString &sourceId, const QString &outputId)
{
    SourceItem *src = m_model->source(sourceId);
    OutputItem *out = m_model->output(outputId);
    if (!src || !out)
        return false;

    QString path = out->outputPath;
    QDir().mkpath(QFileInfo(path).absolutePath());
    if (AppSettings::incrementOutputNames())
        path = CommandBuilder::uniqueOutputPath(path);
    for (const QString &in : src->files) {
        if (CommandBuilder::samePath(in, path)) {
            const QString err = QStringLiteral("Output file is the same as the source file");
            m_model->setStatus(outputId, OutputStatus::Failed, err);
            ++m_failedCount;
            emit taskFinished(sourceId, outputId, false, err);
            return false;
        }
    }

    BuildRequest req;
    req.inputs = src->files;
    req.inputArgs = src->inputArgs();
    req.source = src->info;
    req.totalDuration = src->totalDuration;
    req.settings = out->settings;
    req.outputPath = path;
    req.inPoint = out->inPoint;
    req.outPoint = out->outPoint;
    req.hwDecode = AppSettings::hardwareDecode();
    req.workDir = QDir(AppSettings::tempDir()).filePath(outputId);
    req.passLogFile = QStringLiteral("passlog");

    // Validate the command up front so configuration errors fail fast.
    BuildResult check = CommandBuilder::build(req);
    for (const QString &f : check.tempFiles)
        QFile::remove(f);
    const EffectsSettings &fx = req.settings.effects;
    if (check.ok() && (fx.autoCaptions || (fx.removeSilence && fx.removeFillers)) && req.source.hasAudio) {
        if (AppSettings::whisperCliPath().isEmpty())
            check.error = QStringLiteral("Captions and filler-word removal need whisper-cli, which wasn't found next "
                                         "to the app");
        else if (Whisper::activeModelPath().isEmpty())
            check.error = QStringLiteral("Captions and filler-word removal need the speech model. Download it in "
                                         "Preferences > Captions.");
    }
    if (!check.ok()) {
        m_model->setStatus(outputId, OutputStatus::Failed, check.error);
        ++m_failedCount;
        EncodingLog::appendError(QStringLiteral("%1  %2 -> %3\n  Error: %4")
                                     .arg(QDateTime::currentDateTime().toString(Qt::ISODate), src->displayName(),
                                          QFileInfo(path).fileName(), check.error));
        emit taskFinished(sourceId, outputId, false, check.error);
        return false;
    }

    m_model->setFinalPath(outputId, path);
    m_model->setStatus(outputId, OutputStatus::Encoding);
    m_model->setProgress(outputId, 0);

    auto *task = new EncodeTask(sourceId, outputId, req, AppSettings::ffmpegPath(), this);
    m_tasks.insert(outputId, task);
    connect(task, &EncodeTask::progress, this, [this, task](const EncodeStats &st) {
        m_model->setProgress(task->outputId(), st.progress);
        emit taskProgress(task->outputId(), st);
    });
    connect(task, &EncodeTask::finished, this,
            [this, task](bool ok, bool stopped, const QString &err) { onTaskFinished(task, ok, stopped, err); });
    emit taskStarted(sourceId, outputId);
    task->start();
    return true;
}

void EncoderEngine::onTaskFinished(EncodeTask *task, bool ok, bool stopped, const QString &error)
{
    if (!m_tasks.contains(task->outputId()))
        return;
    m_tasks.remove(task->outputId());
    const QString sid = task->sourceId();
    const QString oid = task->outputId();
    const SourceItem *src = m_model->source(sid);
    const OutputItem *out = m_model->output(oid);
    const QString srcName = src ? src->displayName() : sid;
    const QString preset = out ? out->settings.presetName : QString();
    const QString outPath = task->outputPath();
    const EncodeStats st = task->stats();

    if (ok) {
        ++m_doneCount;
        const QStringList notes = task->notes();
        m_model->setStatus(oid, OutputStatus::Done, notes.join(QStringLiteral("; ")));
        QString entry = QStringLiteral("%1  Encoded %2 -> %3\n  Preset: %4\n  Encoding time: %5\n  Command: %6")
                            .arg(QDateTime::currentDateTime().toString(Qt::ISODate), srcName, outPath, preset,
                                 formatDuration(st.elapsed), task->commandLine());
        for (const QString &n : notes)
            entry += QStringLiteral("\n  %1").arg(n);
        for (const QString &f : task->extraFiles())
            entry += QStringLiteral("\n  Also wrote: %1").arg(f);
        EncodingLog::append(entry);
    } else {
        if (!outPath.contains(QStringLiteral("%05d")) && AppSettings::deletePartialOnFailure())
            QFile::remove(outPath);
        if (stopped) {
            m_model->setStatus(oid, OutputStatus::Stopped, error);
            EncodingLog::append(QStringLiteral("%1  Stopped %2 -> %3")
                                    .arg(QDateTime::currentDateTime().toString(Qt::ISODate), srcName, outPath));
        } else {
            ++m_failedCount;
            m_model->setStatus(oid, OutputStatus::Failed, error);
            const QString entry =
                QStringLiteral(
                    "%1  FAILED %2 -> %3\n  Preset: %4\n  Error: %5\n  Command: %6\n  ffmpeg output:\n    %7")
                    .arg(QDateTime::currentDateTime().toString(Qt::ISODate), srcName, outPath, preset, error,
                         task->commandLine(), task->errorTail().replace('\n', "\n    "));
            EncodingLog::append(entry);
            EncodingLog::appendError(entry);
        }
    }
    emit taskFinished(sid, oid, ok, error);

    // Report when every output of a source has finished (used by watch folders).
    if (const SourceItem *s = m_model->source(sid)) {
        bool allFinished = true;
        bool allOk = true;
        for (const OutputItem &o : s->outputs) {
            if (o.status == OutputStatus::Ready || o.status == OutputStatus::Encoding ||
                o.status == OutputStatus::Paused)
                allFinished = false;
            if (o.status != OutputStatus::Done)
                allOk = false;
        }
        if (allFinished)
            emit sourceCompleted(sid, allOk);
    }

    task->deleteLater();
    if (m_running) {
        scheduleNext();
    } else if (m_tasks.isEmpty()) {
        emit runningChanged(false);
        emit queueFinished(m_doneCount, m_failedCount);
    }
}

// ================================================================= log

namespace EncodingLog {

QString logPath() { return QDir(AppSettings::logDir()).filePath(QStringLiteral("EncodingLog.txt")); }
QString errorLogPath() { return QDir(AppSettings::logDir()).filePath(QStringLiteral("ErrorLog.txt")); }

static void appendTo(const QString &path, const QString &text)
{
    QFile f(path);
    if (f.open(QIODevice::Append | QIODevice::Text))
        f.write((text + "\n").toUtf8());
}

void append(const QString &text) { appendTo(logPath(), text); }
void appendError(const QString &text) { appendTo(errorLogPath(), text); }

} // namespace EncodingLog
