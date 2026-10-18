#pragma once

#include "CommandBuilder.h"

#include <QElapsedTimer>
#include <QMap>
#include <QObject>
#include <functional>

class QProcess;

// Short test encodes: finding the quality value that reaches a VMAF score, and the before/after
// comparison in the export settings. VMAF compares each test encode with a lossless encode of the
// same frames, so scaling, LUTs and overlays are in both and only compression is measured.
namespace QualityProbe {

bool vmafAvailable();
// Up to `count` ranges of `length` seconds spread evenly over what gets encoded (source seconds).
TimeRanges sampleRanges(const BuildRequest &req, int count, double length);
// The output's settings applied to [start, start + length), without audio, captions or silence cuts.
BuildRequest sampleRequest(const BuildRequest &base, double start, double length, const QString &out);
// The same frames encoded losslessly (H.264 at QP 0): what VMAF compares against.
BuildRequest referenceRequest(const BuildRequest &base, double start, double length, const QString &out);
bool isTenBit(const BuildRequest &req);
QStringList vmafArgs(const QString &distorted, const QString &reference, bool tenBit);
double parseVmafScore(const QString &log); // -1 when the log has no score
// "CRF", "CQ", "QP"... for status messages.
QString qualityName(const QString &codec);

} // namespace QualityProbe

// Runs ffmpeg commands one at a time on behalf of the probes below.
class ProbeRunner
{
public:
    explicit ProbeRunner(QObject *owner, const QString &ffmpeg);
    ~ProbeRunner();

    using Done = std::function<void(bool ok, const QString &log)>;
    using LineFn = std::function<void(const QString &line)>; // each line of output, as it arrives
    void run(const QStringList &args, const QString &workDir, Done done, LineFn onLine = {});
    void runProgram(const QString &program, const QStringList &args, const QString &workDir, Done done,
                    LineFn onLine = {});
    bool isRunning() const { return m_proc != nullptr; }
    void stop();
    bool setPaused(bool paused);
    bool isStopped() const { return m_stopped; }

private:
    QObject *m_owner;
    QString m_ffmpeg;
    QProcess *m_proc = nullptr;
    bool m_stopped = false;
    bool m_paused = false;     // the running command is suspended
    bool m_wantPaused = false; // commands should be suspended (applies to ones started later too)
};

// Finds the highest quality value (smallest file) whose test encodes still score the target VMAF.
class QualitySearch : public QObject
{
    Q_OBJECT
public:
    QualitySearch(const BuildRequest &base, const QString &ffmpeg, QObject *parent = nullptr);
    ~QualitySearch() override;

    void start();
    void stop();
    void setPaused(bool paused);

signals:
    void progress(double fraction, const QString &status);
    // quality is the value to encode with; score the mean VMAF it reached (-1 when unknown).
    void finished(bool ok, int quality, double score, const QString &message);

private:
    void encodeReference(int index);
    void testValue(int t);
    void testSample(int index);
    void valueTested(double score);
    int toQuality(int t) const;
    int toSearch(int quality) const;
    QString label(int t) const;
    void step(const QString &status);
    void finish(bool ok, int quality, double score, const QString &message);

    BuildRequest m_base;
    ProbeRunner m_runner;
    QString m_dir;
    TimeRanges m_samples;
    QStringList m_references;
    QMap<int, double> m_scores; // search value -> mean VMAF
    QList<double> m_current;
    int m_t = 0;
    int m_lo = 0;
    int m_hi = 0;
    int m_iterations = 0;
    int m_units = 0;
    int m_unitsDone = 0;
    bool m_finished = false;
};

struct TestEncodeResult {
    QString file;        // the encoded sample
    double start = 0.0;  // source seconds
    double length = 0.0; // seconds
    qint64 bytes = 0;
    double mbps = 0.0;     // average video bitrate of the sample
    double vmaf = -1.0;    // -1 when VMAF isn't available
    double speed = 0.0;    // encoding speed, x realtime
    qint64 projected = -1; // estimated size of the whole output
};

// Encodes a few seconds with the output's settings for the before/after comparison and scores it.
class TestEncode : public QObject
{
    Q_OBJECT
public:
    TestEncode(const BuildRequest &full, double start, double length, const QString &ffmpeg, QObject *parent = nullptr);
    ~TestEncode() override;

    void start();
    void stop();
    const TestEncodeResult &result() const { return m_result; }

signals:
    void status(const QString &text);
    void finished(bool ok, const QString &error);

private:
    void runPass(int pass);
    void scoreSample();

    BuildRequest m_full;
    BuildRequest m_sample;
    ProbeRunner m_runner;
    QString m_dir;
    int m_passes = 1;
    QElapsedTimer m_timer;
    TestEncodeResult m_result;
};
