#pragma once

#include "CommandBuilder.h"

#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QProcess>

class QueueModel;

struct EncodeStats {
    double progress = 0.0; // 0..1 over all steps and passes
    double fps = 0.0;
    double speed = 0.0; // x realtime
    qint64 bytes = 0;
    double outTime = 0.0;    // seconds encoded in current pass
    double elapsed = 0.0;    // seconds
    double remaining = -1.0; // seconds, -1 unknown
    int pass = 1;
    int passes = 1;
    QString phase; // what is happening now ("Finding quality...", "Pass 1 of 2"), empty for a plain encode
};

class ProbeRunner;
class QualitySearch;

// Runs a single output: the analysis steps its settings need (silence scan, quality search,
// speech-to-text captions), then ffmpeg in one or two passes, then a size check for file-size targets.
class EncodeTask : public QObject
{
    Q_OBJECT
public:
    EncodeTask(const QString &sourceId, const QString &outputId, const BuildRequest &req, const QString &ffmpeg,
               QObject *parent = nullptr);
    ~EncodeTask() override;

    void start();
    void stop();
    void setPaused(bool paused);
    bool isPaused() const { return m_paused; }

    QString sourceId() const { return m_sourceId; }
    QString outputId() const { return m_outputId; }
    QString outputPath() const { return m_req.outputPath; }
    QString commandLine() const { return m_lastCommand; }
    const EncodeStats &stats() const { return m_stats; }
    QString errorTail() const { return m_stderrTail.join('\n'); }
    // What the analysis steps found ("CRF 26 reaches VMAF 95.2", "Removed 14 pauses"), for the log.
    QStringList notes() const { return m_notes; }
    // Files written next to the output besides the output itself (captions).
    QStringList extraFiles() const { return m_extraFiles; }

signals:
    void progress(const EncodeStats &stats);
    void finished(bool ok, bool stopped, const QString &error);

private:
    enum class Step { Silence, Quality, Captions, Encode };

    void nextStep();
    void detectSilence();
    void findFillers();
    void applyCuts(const TimeRanges &fillerCuts, int fillerCount);
    void findQuality();
    void transcribe();
    void transcribeAudio(const QString &wav);
    void captionsReady(const QString &srt);
    void noteSpeechDevice(const QString &line);
    void startEncode();
    bool startPass(int pass);
    void encodeFinished();
    void onReadyStdout();
    void onReadyStderr();
    void onProcessFinished(int exitCode, QProcess::ExitStatus status);
    void stepProgress(double fraction, const QString &phase);
    void fail(const QString &error);
    void succeed();
    void emitFinished(bool ok, bool stopped, const QString &error);
    void cleanup();

    QString m_sourceId;
    QString m_outputId;
    BuildRequest m_req;
    QString m_ffmpeg;
    QProcess *m_proc = nullptr;
    ProbeRunner *m_runner = nullptr;
    QualitySearch *m_search = nullptr;
    QList<Step> m_steps;
    QList<double> m_weights; // progress share of each step
    int m_step = -1;
    int m_pass = 0;
    int m_passes = 1;
    int m_sizeRetries = 0;
    bool m_stopping = false;
    bool m_paused = false;
    bool m_done = false;
    QElapsedTimer m_timer;
    qint64 m_pausedMs = 0;
    QElapsedTimer m_pauseTimer;
    QByteArray m_stdoutBuf;
    QStringList m_stderrTail;
    QStringList m_tempFiles;
    QString m_lastCommand;
    TimeRanges m_gaps;           // quiet stretches found by the silence scan
    double m_scanDuration = 0.0; // length of what was scanned
    QString m_speechDevice;      // "GPU (CUDA)" etc. when whisper.cpp used a GPU
    QString m_captionsSrt;       // cleaned captions, copied next to the output when asked for
    QStringList m_notes;
    QStringList m_extraFiles;
    EncodeStats m_stats;
};

class EncoderEngine : public QObject
{
    Q_OBJECT
public:
    explicit EncoderEngine(QueueModel *model, QObject *parent = nullptr);
    ~EncoderEngine() override;

    bool isRunning() const { return m_running; }
    bool isPaused() const { return m_paused; }
    QList<EncodeTask *> activeTasks() const;

public slots:
    void start();
    void pause();
    void resume();
    void stop();
    void stopOutput(const QString &outputId);

signals:
    void runningChanged(bool running);
    void pausedChanged(bool paused);
    void taskStarted(const QString &sourceId, const QString &outputId);
    void taskProgress(const QString &outputId, const EncodeStats &stats);
    void taskFinished(const QString &sourceId, const QString &outputId, bool ok, const QString &message);
    void sourceCompleted(const QString &sourceId, bool allOk);
    void queueFinished(int done, int failed);

private:
    void scheduleNext();
    void requestSchedule();
    bool startTask(const QString &sourceId, const QString &outputId);
    void onTaskFinished(EncodeTask *task, bool ok, bool stopped, const QString &error);
    bool waitingForProbe() const;

    QueueModel *m_model;
    QHash<QString, EncodeTask *> m_tasks; // by output id
    bool m_running = false;
    bool m_paused = false;
    bool m_schedulePending = false;
    int m_doneCount = 0;
    int m_failedCount = 0;
};

namespace EncodingLog {
QString logPath();
QString errorLogPath();
void append(const QString &text);
void appendError(const QString &text);
} // namespace EncodingLog
