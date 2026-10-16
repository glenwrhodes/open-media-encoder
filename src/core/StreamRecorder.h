#pragma once

#include <QDateTime>
#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <QPair>
#include <QProcess>
#include <QStringList>
#include <QTimer>

using HttpHeaders = QList<QPair<QString, QString>>;

struct RecordingOptions {
    QString url;
    QString outputDir;
    QString name;              // file name without extension; empty = derived from the stream and the time
    QString container = "mkv"; // mkv, mp4 (recorded as MKV and converted when finished) or ts
    int maxSeconds = 0;        // 0 = until stopped
    bool reconnect = true;     // start a new part when the stream drops mid-recording
};

// A web page link turned into a stream ffmpeg can open (via yt-dlp).
struct ResolvedStream {
    QString url;      // the stream, or its picture when sound comes separately
    QString audioUrl; // set when the site sends sound as its own stream (YouTube live)
    QString title;
    HttpHeaders headers;
    bool isLive = false;
    QString error;
    bool ok() const { return error.isEmpty() && !url.isEmpty(); }
};

namespace StreamLink {
// True for text that looks like a stream or web link (supported scheme plus a host or path).
bool isStreamUrl(const QString &text);
// True when ffmpeg can open the link itself; false for web pages that need yt-dlp.
bool isDirectMedia(const QString &url);
// ffmpeg input options suited to the link's protocol.
QStringList inputArgs(const QString &url, const HttpHeaders &headers = {});
// "<title or host> yyyy-MM-dd HH-mm-ss", safe to use as a file name.
QString suggestedName(const QString &url, const QString &title, const QDateTime &when);
QString sanitizeFileName(const QString &name);
// Plain-language explanation for common ffmpeg and network errors (the original follows in brackets).
QString friendlyError(const QString &raw);
// Parses `yt-dlp -j` output.
ResolvedStream parseYtDlpJson(const QByteArray &json);
} // namespace StreamLink

// Records one live stream to disk without re-encoding.
class StreamRecording : public QObject
{
    Q_OBJECT
public:
    enum class State { Resolving, Connecting, Recording, Reconnecting, Converting, Done, Failed };

    StreamRecording(const QString &id, const RecordingOptions &options, const QString &ffmpeg,
                    const QString &ytDlp = {}, QObject *parent = nullptr);
    ~StreamRecording() override;

    void start();
    // Ends the recording, keeping everything recorded so far.
    void stop();

    QString id() const { return m_id; }
    const RecordingOptions &options() const { return m_options; }
    State state() const { return m_state; }
    bool isActive() const { return m_state != State::Done && m_state != State::Failed; }
    QString stateText() const;
    QString title() const { return m_title; }
    QStringList files() const; // finished parts plus the one being recorded
    double recordedSeconds() const { return m_prevSeconds + m_partSeconds; }
    qint64 bytes() const { return m_prevBytes + m_partBytes; }
    double bitrateKbps() const { return m_bitrate; }
    int part() const { return m_part; }
    QString error() const { return m_error; }
    QString note() const { return m_note; }
    QDateTime startedAt() const { return m_startedAt; }
    QString commandLine() const { return m_command; }
    QString logTail() const { return m_log.join('\n'); }

signals:
    void changed();
    void finished();

private:
    void resolve();
    void onResolved(int exitCode);
    void onUpdated();
    void startPart();
    void onReadyStdout();
    void onReadyStderr();
    void onPartFinished(int exitCode, QProcess::ExitStatus status);
    void retryOrFinish(const QString &error);
    void finish();
    void convertNext();
    void complete();
    void setState(State s);
    void fail(const QString &error);
    QString partPath(int part) const;
    QString recordExtension() const;
    static void prepare(QProcess *p);

    QString m_id;
    RecordingOptions m_options;
    QString m_ffmpeg;
    QString m_ytDlp;
    State m_state = State::Connecting;
    QString m_streamUrl;
    QString m_audioUrl;
    HttpHeaders m_headers;
    bool m_viaYtDlp = false;
    QString m_title;
    QString m_base; // output path without extension
    QProcess *m_proc = nullptr;
    QProcess *m_resolver = nullptr;
    QTimer m_retryTimer;
    QTimer m_killTimer;
    QByteArray m_stdoutBuf;
    QStringList m_log;
    QString m_command;
    QStringList m_files;
    struct FinishStep {
        QString file;
        double keepSeconds = 0; // > 0: trim to this length
    };
    QList<FinishStep> m_finishSteps; // MP4 conversion and/or trimming to the time limit
    QList<double> m_fileSeconds;     // length of each file in m_files
    QString m_partFile;
    int m_part = 0;
    int m_failedAttempts = 0;
    double m_prevSeconds = 0;
    double m_partSeconds = 0;
    qint64 m_prevBytes = 0;
    qint64 m_partBytes = 0;
    double m_bitrate = 0;
    bool m_stopRequested = false;
    bool m_limitStopSent = false;
    bool m_updating = false;
    QString m_endReason;
    QString m_error;
    QString m_note;
    QDateTime m_startedAt;
};

// Owns the recordings shown in the Stream Recorder panel.
class StreamRecorder : public QObject
{
    Q_OBJECT
public:
    explicit StreamRecorder(QObject *parent = nullptr);
    ~StreamRecorder() override;

    StreamRecording *record(const RecordingOptions &options);
    QList<StreamRecording *> recordings() const { return m_recordings; }
    StreamRecording *recording(const QString &id) const;
    void remove(const QString &id); // finished recordings only
    void stopAll();
    int activeCount() const;
    // Processes events until every recording has finished or the time runs out.
    bool waitForAll(int msecs);

signals:
    void recordingAdded(StreamRecording *recording);
    void recordingChanged(StreamRecording *recording);
    void recordingRemoved(const QString &id);
    void recordingFinished(StreamRecording *recording);
    void activeCountChanged(int count);

private:
    QList<StreamRecording *> m_recordings;
    int m_nextId = 1;
    int m_lastActive = 0;
};
