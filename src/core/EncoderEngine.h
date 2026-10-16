#pragma once

#include "CommandBuilder.h"

#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QProcess>

class QueueModel;

struct EncodeStats {
    double progress = 0.0; // 0..1 over all passes
    double fps = 0.0;
    double speed = 0.0; // x realtime
    qint64 bytes = 0;
    double outTime = 0.0;    // seconds encoded in current pass
    double elapsed = 0.0;    // seconds
    double remaining = -1.0; // seconds, -1 unknown
    int pass = 1;
    int passes = 1;
};

// Runs a single output through ffmpeg (one or two passes).
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

signals:
    void progress(const EncodeStats &stats);
    void finished(bool ok, bool stopped, const QString &error);

private:
    bool startPass(int pass);
    void onReadyStdout();
    void onReadyStderr();
    void onProcessFinished(int exitCode, QProcess::ExitStatus status);
    void cleanup();

    QString m_sourceId;
    QString m_outputId;
    BuildRequest m_req;
    QString m_ffmpeg;
    QProcess *m_proc = nullptr;
    int m_pass = 0;
    int m_passes = 1;
    bool m_stopping = false;
    bool m_paused = false;
    QElapsedTimer m_timer;
    qint64 m_pausedMs = 0;
    QElapsedTimer m_pauseTimer;
    QByteArray m_stdoutBuf;
    QStringList m_stderrTail;
    QStringList m_tempFiles;
    QString m_lastCommand;
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
