#include "EncoderEngine.h"

#include "AppSettings.h"
#include "QueueModel.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTimer>

#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <signal.h>
#endif

namespace {

#ifdef Q_OS_WIN
using NtProcFn = LONG(NTAPI *)(HANDLE);

bool suspendProcess(qint64 pid, bool suspend)
{
    static NtProcFn ntSuspend = reinterpret_cast<NtProcFn>(
        reinterpret_cast<void *>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtSuspendProcess")));
    static NtProcFn ntResume = reinterpret_cast<NtProcFn>(
        reinterpret_cast<void *>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtResumeProcess")));
    HANDLE h = OpenProcess(PROCESS_SUSPEND_RESUME, FALSE, DWORD(pid));
    if (!h)
        return false;
    LONG status = -1;
    if (suspend && ntSuspend)
        status = ntSuspend(h);
    else if (!suspend && ntResume)
        status = ntResume(h);
    CloseHandle(h);
    return status == 0;
}
#else
bool suspendProcess(qint64 pid, bool suspend) { return ::kill(pid_t(pid), suspend ? SIGSTOP : SIGCONT) == 0; }
#endif

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
    m_passes = CommandBuilder::usesTwoPass(req.settings) ? 2 : 1;
    m_stats.passes = m_passes;
}

EncodeTask::~EncodeTask()
{
    if (m_proc && m_proc->state() != QProcess::NotRunning) {
        if (m_paused)
            suspendProcess(m_proc->processId(), false);
        m_proc->kill();
        m_proc->waitForFinished(2000);
    }
    cleanup();
}

void EncodeTask::start()
{
    m_timer.start();
    QDir().mkpath(m_req.workDir);
    if (!startPass(m_passes == 2 ? 1 : 0))
        return;
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
        QTimer::singleShot(0, this, [this, err = r.error] { emit finished(false, false, err); });
        return false;
    }

    m_lastCommand = quoteArg(m_ffmpeg);
    for (const QString &a : r.args)
        m_lastCommand += ' ' + quoteArg(a);

    if (m_proc)
        m_proc->deleteLater();
    m_proc = new QProcess(this);
    m_proc->setWorkingDirectory(m_req.workDir);
    m_proc->setProgram(m_ffmpeg);
    m_proc->setArguments(r.args);
    connect(m_proc, &QProcess::readyReadStandardOutput, this, &EncodeTask::onReadyStdout);
    connect(m_proc, &QProcess::readyReadStandardError, this, &EncodeTask::onReadyStderr);
    connect(m_proc, &QProcess::finished, this, &EncodeTask::onProcessFinished);
    connect(m_proc, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart)
            emit finished(false, false, QStringLiteral("Could not start ffmpeg (%1)").arg(m_ffmpeg));
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
        m_proc->kill();
    }
}

void EncodeTask::setPaused(bool paused)
{
    if (!m_proc || m_proc->state() != QProcess::Running || paused == m_paused)
        return;
    if (suspendProcess(m_proc->processId(), paused)) {
        m_paused = paused;
        if (paused)
            m_pauseTimer.start();
        else
            m_pausedMs += m_pauseTimer.elapsed();
    }
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
            m_stats.progress = (passIndex + passFrac) / m_passes;
            m_stats.elapsed = double(m_timer.elapsed() - m_pausedMs) / 1000.0;
            if (m_stats.progress > 0.01)
                m_stats.remaining = m_stats.elapsed * (1.0 - m_stats.progress) / m_stats.progress;
            emit progress(m_stats);
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
        emit finished(false, true, QStringLiteral("Stopped by user"));
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
        cleanup();
        emit finished(false, false, err);
        return;
    }
    if (m_passes == 2 && m_pass == 1) {
        startPass(2);
        return;
    }
    m_stats.progress = 1.0;
    m_stats.elapsed = double(m_timer.elapsed() - m_pausedMs) / 1000.0;
    m_stats.remaining = 0;
    emit progress(m_stats);
    cleanup();
    emit finished(true, false, {});
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
        if (QFileInfo(in) == QFileInfo(path)) {
            const QString err = QStringLiteral("Output file is the same as the source file");
            m_model->setStatus(outputId, OutputStatus::Failed, err);
            ++m_failedCount;
            emit taskFinished(sourceId, outputId, false, err);
            return false;
        }
    }

    BuildRequest req;
    req.inputs = src->files;
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
    const BuildResult check = CommandBuilder::build(req);
    for (const QString &f : check.tempFiles)
        QFile::remove(f);
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
        m_model->setStatus(oid, OutputStatus::Done);
        EncodingLog::append(QStringLiteral("%1  Encoded %2 -> %3\n  Preset: %4\n  Encoding time: %5\n  Command: %6")
                                .arg(QDateTime::currentDateTime().toString(Qt::ISODate), srcName, outPath, preset,
                                     formatDuration(st.elapsed), task->commandLine()));
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
