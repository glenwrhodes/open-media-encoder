#include "core/AppSettings.h"
#include "core/CommandBuilder.h"
#include "core/EncoderCaps.h"
#include "core/EncoderEngine.h"
#include "core/Formats.h"
#include "core/ImageSequence.h"
#include "core/MediaFileModel.h"
#include "core/MediaInfo.h"
#include "core/MediaInfoCache.h"
#include "core/PresetManager.h"
#include "core/QualityProbe.h"
#include "core/QueueModel.h"
#include "core/StreamRecorder.h"
#include "core/WatchFolderManager.h"
#include "core/Whisper.h"

#include <QColor>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QMimeData>
#include <QPainter>
#include <QProcess>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QtTest>

// Serves files from a folder over HTTP, enough for ffmpeg to read a live HLS stream.
class TinyHttpServer : public QTcpServer
{
public:
    explicit TinyHttpServer(const QString &root)
        : m_root(root)
    {
        connect(this, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *s = nextPendingConnection()) {
                connect(s, &QTcpSocket::disconnected, s, &QObject::deleteLater);
                connect(s, &QTcpSocket::readyRead, s, [this, s] {
                    QByteArray &buf = m_pending[s];
                    buf += s->readAll();
                    if (!buf.contains("\r\n\r\n"))
                        return;
                    const QByteArray path = buf.split(' ').value(1).split('?').value(0);
                    m_pending.remove(s);
                    QFile f(m_root + QString::fromUtf8(path));
                    if (!path.contains("..") && f.open(QIODevice::ReadOnly)) {
                        const QByteArray body = f.readAll();
                        const QByteArray type = path.endsWith(".m3u8") ? "application/vnd.apple.mpegurl" : "video/mp2t";
                        s->write("HTTP/1.1 200 OK\r\nContent-Type: " + type + "\r\nContent-Length: " +
                                 QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                    } else {
                        s->write("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
                    }
                    s->disconnectFromHost();
                });
            }
        });
        listen(QHostAddress::LocalHost);
    }
    QString url(const QString &file) const
    {
        return QStringLiteral("http://127.0.0.1:%1/%2").arg(serverPort()).arg(file);
    }

private:
    QString m_root;
    QHash<QTcpSocket *, QByteArray> m_pending;
};

// Tests needing optional tools (VMAF, whisper.cpp and its test model) are skipped locally without
// them; CI sets OME_REQUIRE_ALL_TESTS so a missing tool fails the build instead of passing quietly.
#define SKIP_OR_FAIL(why)                                                                                              \
    do {                                                                                                               \
        if (qEnvironmentVariableIsSet("OME_REQUIRE_ALL_TESTS"))                                                        \
            QFAIL(why);                                                                                                \
        QSKIP(why);                                                                                                    \
    } while (false)

class CoreTests : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void probe();
    void jsonRoundTrip();
    void timecode();
    void uniquePath();
    void allSystemPresetsEncode_data();
    void allSystemPresetsEncode();
    void effectsChain();
    void captionsBurnIn();
    void twoPass();
    void stitchedSources();
    void stillImageToVideo();
    void engineRunsQueue();
    void enginePauseResume();
    void watchFolderPicksUpFiles();
    void mediaFileModelListsAndSorts();
    void mediaFileModelMetadataColumns();
    void queueReorder();
    void imageSequenceDetection();
    void imageSequenceEncode();
    void presetReplacesAutoOutput();
    void frameRateLimitAndBitrateScaling();
    void platformPresetsPresent();
    void streamLinkHelpers();
    void ytDlpJsonParsing();
    void recordLiveHttpStreamToMp4();
    void recordHlsWithTimeLimit();
    void recordSplitVideoAndAudio();
    void recordDeadLinkFails();
    void targetFileSize();
    void targetQualityVmaf();
    void blurredBackground();
    void removeSilence();
    void captionHelpers();
    void automaticCaptions();
    void fillerWordHelpers();
    void removeFillerWords();
    void testEncodeCompare();
    void hardwareEncoderCommands(); // keep last: it swaps the encoder list and probes again afterwards

private:
    bool runFfmpeg(const QStringList &args, QString *err = nullptr, int timeoutMs = 180000);
    BuildRequest requestFor(const ExportSettings &s, const QString &out);
    QProcess *startLiveSource(const QStringList &outputArgs, const QStringList &inputs = {});
    bool runQueue(QueueModel &model, int timeoutMs);
    QString ffprobe(const QStringList &args);

    QTemporaryDir m_dir;
    QString m_clip;
    QString m_clip2;
    QString m_still;
    MediaInfo m_info;
};

bool CoreTests::runFfmpeg(const QStringList &args, QString *err, int timeoutMs)
{
    QProcess p;
    p.setWorkingDirectory(m_dir.path());
    p.start(AppSettings::ffmpegPath(), args);
    if (!p.waitForFinished(timeoutMs)) {
        p.kill();
        if (err)
            *err = "timeout";
        return false;
    }
    if (err)
        *err = QString::fromUtf8(p.readAllStandardError()).right(1500);
    return p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
}

BuildRequest CoreTests::requestFor(const ExportSettings &s, const QString &out)
{
    BuildRequest r;
    r.inputs = {m_clip};
    r.source = m_info;
    r.settings = s;
    r.outputPath = out;
    r.outPoint = 0.4;
    r.workDir = m_dir.filePath("work");
    r.passLogFile = "passlog";
    QDir().mkpath(r.workDir);
    return r;
}

void CoreTests::initTestCase()
{
    QVERIFY(m_dir.isValid());
    // Keep logs, presets and watch-folder config out of the user's real data directory.
    QStandardPaths::setTestModeEnabled(true);
    EncoderCaps::instance().probe(AppSettings::ffmpegPath());
    QVERIFY2(EncoderCaps::instance().probed(), "ffmpeg not found");
    for (const char *f : {"drawtext", "lut3d", "loudnorm", "overlay", "subtitles", "limiter", "palettegen"})
        QVERIFY2(EncoderCaps::instance().hasFilter(QString::fromLatin1(f)), f);
    EncoderCaps::instance().validateHardware(AppSettings::ffmpegPath());
    // Presets as defined, whatever GPU this machine has (GPU presets are tested separately).
    PresetManager::instance().setPreferHardware(false);
    PresetManager::instance().load();
    QVERIFY(!PresetManager::instance().presets().isEmpty());

    m_clip = m_dir.filePath("clip one.mov");
    m_clip2 = m_dir.filePath("clip2.mov");
    QString err;
    QVERIFY2(runFfmpeg({"-y",
                        "-f",
                        "lavfi",
                        "-i",
                        "testsrc2=s=1280x720:r=30000/1001:d=2",
                        "-f",
                        "lavfi",
                        "-i",
                        "sine=frequency=440:sample_rate=48000:d=2",
                        "-c:v",
                        "libx264",
                        "-pix_fmt",
                        "yuv420p",
                        "-c:a",
                        "pcm_s16le",
                        "-ac",
                        "2",
                        "-timecode",
                        "01:00:00:00",
                        "-shortest",
                        m_clip},
                       &err),
             qPrintable(err));
    QVERIFY2(runFfmpeg({"-y", "-f", "lavfi", "-i", "testsrc=s=1280x720:r=30000/1001:d=1", "-f", "lavfi", "-i",
                        "sine=frequency=880:sample_rate=48000:d=1", "-c:v", "libx264", "-pix_fmt", "yuv420p", "-c:a",
                        "pcm_s16le", "-ac", "2", "-shortest", m_clip2},
                       &err),
             qPrintable(err));
    QImage img(400, 300, QImage::Format_ARGB32);
    img.fill(QColor(30, 120, 200));
    m_still = m_dir.filePath("still.png");
    QVERIFY(img.save(m_still));

    m_info = MediaInfo::probe(AppSettings::ffprobePath(), m_clip);
    QVERIFY2(m_info.valid, qPrintable(m_info.error));
}

void CoreTests::probe()
{
    QCOMPARE(m_info.width, 1280);
    QCOMPARE(m_info.height, 720);
    QVERIFY(qAbs(m_info.fps - 29.97) < 0.01);
    QVERIFY(m_info.hasAudio);
    QCOMPARE(m_info.channels, 2);
    QCOMPARE(m_info.timecode, QString("01:00:00:00"));
    QVERIFY(qAbs(m_info.duration - 2.0) < 0.1);

    const MediaInfo still = MediaInfo::probe(AppSettings::ffprobePath(), m_still);
    QVERIFY(still.valid);
    QVERIFY(still.isStill);
    QVERIFY(still.hasAlpha());
}

void CoreTests::jsonRoundTrip()
{
    ExportSettings s;
    s.setFormat("hevc");
    s.presetName = "Round trip";
    s.video.width = 1920;
    s.video.height = 1080;
    s.video.rateControl = "vbr2";
    s.effects.textOverlayEnabled = true;
    s.effects.text = "Hello: world, it's 'quoted'";
    s.effects.cropEnabled = true;
    s.effects.cropLeft = 10;
    s.video.scaleMode = "blur";
    s.video.blurStrength = 30;
    s.video.backgroundDim = 40;
    s.video.targetSizeMB = 25;
    s.video.targetVmaf = 93.5;
    s.effects.autoCaptions = true;
    s.effects.captionLanguage = "fr";
    s.effects.captionTranslate = true;
    s.effects.captionSaveSrt = false;
    s.effects.captionEmbed = true;
    s.effects.captionBurn = true;
    s.effects.autoCaptionSize = 64;
    s.effects.removeSilence = true;
    s.effects.silenceThreshold = -35;
    s.effects.silenceMinDuration = 1.5;
    s.effects.silencePadding = 0.1;
    const ExportSettings r = ExportSettings::fromJson(s.toJson());
    QCOMPARE(r.toJson(), s.toJson());
}

void CoreTests::timecode()
{
    QCOMPARE(CommandBuilder::timecodeToFrames("01:00:00:00", 30), qint64(108000));
    QCOMPARE(CommandBuilder::framesToTimecode(108000 + 45, 30), QString("01:00:01:15"));
    QCOMPARE(CommandBuilder::fpsToRational(29.97), QString("30000/1001"));
    QCOMPARE(CommandBuilder::fpsToRational(25), QString("25"));
}

void CoreTests::uniquePath()
{
    const QString p = m_dir.filePath("dup.mp4");
    QCOMPARE(CommandBuilder::uniqueOutputPath(p), p);
    QFile f(p);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.close();
    QCOMPARE(CommandBuilder::uniqueOutputPath(p), m_dir.filePath("dup_1.mp4"));

    // samePath must not treat two different missing files as the same file.
    QVERIFY(!CommandBuilder::samePath(m_dir.filePath("missing_a.mp4"), m_dir.filePath("missing_b.mp4")));
    QVERIFY(CommandBuilder::samePath(m_dir.filePath("missing_a.mp4"), m_dir.filePath("sub/../missing_a.mp4")));
    QVERIFY(CommandBuilder::samePath(p, p));

    // Default output never points at the source file.
    ExportSettings s;
    s.setFormat("h264");
    QCOMPARE(CommandBuilder::defaultOutputPath(p, s, {}), m_dir.filePath("dup_1.mp4"));
    s.setFormat("quicktime");
    QCOMPARE(CommandBuilder::defaultOutputPath(p, s, {}), m_dir.filePath("dup.mov"));
}

void CoreTests::allSystemPresetsEncode_data()
{
    QTest::addColumn<QString>("presetId");
    for (const Preset &p : PresetManager::instance().presets())
        if (p.system)
            QTest::newRow(qPrintable(p.id)) << p.id;
}

void CoreTests::allSystemPresetsEncode()
{
    QFETCH(QString, presetId);
    const Preset *p = PresetManager::instance().find(presetId);
    QVERIFY(p);
    ExportSettings s = p->settings;
    QDir().mkpath(m_dir.filePath("presets"));
    QString name = presetId;
    name.replace('/', '_').replace(' ', '_');
    const QString out =
        CommandBuilder::defaultOutputPath(m_dir.filePath("presets/" + name), s, m_dir.filePath("presets"));
    BuildRequest req = requestFor(s, out);
    req.workDir = m_dir.filePath("work_" + name);
    QDir().mkpath(req.workDir);

    const int passes = CommandBuilder::usesTwoPass(s) ? 2 : 1;
    for (int pass = (passes == 2 ? 1 : 0); pass <= (passes == 2 ? 2 : 0); ++pass) {
        req.pass = pass;
        const BuildResult r = CommandBuilder::build(req);
        QVERIFY2(r.ok(), qPrintable(r.error));
        QProcess proc;
        proc.setWorkingDirectory(req.workDir);
        proc.start(AppSettings::ffmpegPath(), r.args);
        QVERIFY(proc.waitForFinished(900000));
        const QString err = QString::fromUtf8(proc.readAllStandardError());
        QVERIFY2(proc.exitCode() == 0, qPrintable(r.args.join(' ') + "\n" + err.right(2000)));
    }
    const QString check = s.isImageSequence() ? CommandBuilder::firstSequenceFile(out) : out;
    QVERIFY2(QFileInfo(check).size() > 0, qPrintable(check));

    // The output must be readable and contain the expected stream types.
    const MediaInfo outInfo = MediaInfo::probe(AppSettings::ffprobePath(), check);
    QVERIFY2(outInfo.valid, qPrintable(outInfo.error));
    const FormatDef *f = Formats::format(s.format);
    QCOMPARE(outInfo.hasVideo, f->hasVideo());
    if (f->hasVideo() && s.video.width > 0 && s.video.height > 0) {
        QCOMPARE(outInfo.width, s.video.width);
        QCOMPARE(outInfo.height, s.video.height);
    }
}

void CoreTests::effectsChain()
{
    // Identity 2x2x2 3D LUT.
    const QString lut = m_dir.filePath("ident ity.cube");
    {
        QFile f(lut);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("LUT_3D_SIZE 2\n0 0 0\n1 0 0\n0 1 0\n1 1 0\n0 0 1\n1 0 1\n0 1 1\n1 1 1\n");
    }
    ExportSettings s = PresetManager::instance().find("sys/h264/Match Source - High bitrate")->settings;
    s.video.width = 1920;
    s.video.height = 1080;
    s.video.frameRate = 25;
    EffectsSettings &e = s.effects;
    e.lutEnabled = true;
    e.lutPath = lut;
    e.imageOverlayEnabled = true;
    e.imagePath = m_still;
    e.imageScale = 50;
    e.imageOpacity = 70;
    e.textOverlayEnabled = true;
    e.text = "Client: \"Review\", v2 — 100% it's done";
    e.timecodeEnabled = true;
    e.limiterEnabled = true;
    e.loudnessEnabled = true;
    e.cropEnabled = true;
    e.cropLeft = 20;
    e.cropRight = 20;
    e.cropTop = 10;
    e.cropBottom = 10;
    const QString out = m_dir.filePath("effects.mp4");
    BuildRequest req = requestFor(s, out);
    req.inPoint = 0.5;
    req.outPoint = 1.5;
    const BuildResult r = CommandBuilder::build(req);
    QVERIFY2(r.ok(), qPrintable(r.error));
    // Every enabled effect must actually be in the filter graph, not silently dropped.
    const QString graph = r.args.at(r.args.indexOf("-filter_complex") + 1);
    for (const char *f : {"lut3d=", "overlay=", "drawtext=", "timecode=", "limiter=", "crop="})
        QVERIFY2(graph.contains(QLatin1String(f)), qPrintable(QStringLiteral("missing %1 in %2").arg(f, graph)));
    QVERIFY(r.args.contains("-af"));
    QString err;
    QVERIFY2(runFfmpeg(r.args, &err), qPrintable(r.args.join(' ') + "\n" + err));
    const MediaInfo o = MediaInfo::probe(AppSettings::ffprobePath(), out);
    QCOMPARE(o.width, 1920);
    QCOMPARE(o.height, 1080);
    QVERIFY(qAbs(o.fps - 25.0) < 0.01);
    QVERIFY2(qAbs(o.duration - 1.0) < 0.15, qPrintable(QString::number(o.duration)));

    // Preview frame with effects.
    const QString png = m_dir.filePath("preview.bmp");
    const BuildResult pr = CommandBuilder::buildPreview(req, 1.0, png, true);
    QVERIFY2(pr.ok(), qPrintable(pr.error));
    QVERIFY2(runFfmpeg(pr.args, &err), qPrintable(pr.args.join(' ') + "\n" + err));
    QImage img(png);
    QCOMPARE(img.width(), 1920);
}

void CoreTests::captionsBurnIn()
{
    const QString srt = m_dir.filePath("captions, v1.srt");
    {
        QFile f(srt);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("1\n00:00:00,000 --> 00:00:01,500\nHello: captions, burned in\n\n");
    }
    ExportSettings s;
    s.setFormat("h264");
    s.effects.captionsEnabled = true;
    s.effects.captionFile = srt;
    const QString out = m_dir.filePath("captions.mp4");
    BuildRequest req = requestFor(s, out);
    req.outPoint = 1.0;
    const BuildResult r = CommandBuilder::build(req);
    QVERIFY2(r.ok(), qPrintable(r.error));
    QVERIFY(r.args.at(r.args.indexOf("-filter_complex") + 1).contains("subtitles="));
    QString err;
    QVERIFY2(runFfmpeg(r.args, &err), qPrintable(r.args.join(' ') + "\n" + err));
    QVERIFY(QFileInfo(out).size() > 0);
}

void CoreTests::twoPass()
{
    ExportSettings s;
    s.setFormat("h264");
    s.video.rateControl = "vbr2";
    s.video.targetBitrate = 2;
    s.video.maxBitrate = 3;
    QVERIFY(CommandBuilder::usesTwoPass(s));
    const QString out = m_dir.filePath("twopass.mp4");
    BuildRequest req = requestFor(s, out);
    req.workDir = m_dir.filePath("tp");
    QDir().mkpath(req.workDir);
    for (int pass : {1, 2}) {
        req.pass = pass;
        const BuildResult r = CommandBuilder::build(req);
        QVERIFY(r.ok());
        QVERIFY(r.args.contains("-pass"));
        QProcess p;
        p.setWorkingDirectory(req.workDir);
        p.start(AppSettings::ffmpegPath(), r.args);
        QVERIFY(p.waitForFinished(60000));
        QVERIFY2(p.exitCode() == 0, qPrintable(p.readAllStandardError()));
    }
    QVERIFY(QFileInfo(out).size() > 0);
}

void CoreTests::stitchedSources()
{
    ExportSettings s;
    s.setFormat("h264");
    const QString out = m_dir.filePath("stitched.mp4");
    BuildRequest req = requestFor(s, out);
    req.inputs = {m_clip, m_clip2};
    req.totalDuration = 3.0;
    req.outPoint = -1;
    const BuildResult r = CommandBuilder::build(req);
    QVERIFY2(r.ok(), qPrintable(r.error));
    QString err;
    QVERIFY2(runFfmpeg(r.args, &err), qPrintable(err));
    const MediaInfo o = MediaInfo::probe(AppSettings::ffprobePath(), out);
    QVERIFY2(qAbs(o.duration - 3.0) < 0.2, qPrintable(QString::number(o.duration)));
}

void CoreTests::stillImageToVideo()
{
    ExportSettings s;
    s.setFormat("h264");
    s.video.stillDuration = 2.0;
    const MediaInfo still = MediaInfo::probe(AppSettings::ffprobePath(), m_still);
    const QString out = m_dir.filePath("still.mp4");
    BuildRequest req;
    req.inputs = {m_still};
    req.source = still;
    req.settings = s;
    req.outputPath = out;
    req.workDir = m_dir.filePath("work");
    const BuildResult r = CommandBuilder::build(req);
    QVERIFY2(r.ok(), qPrintable(r.error));
    QString err;
    QVERIFY2(runFfmpeg(r.args, &err), qPrintable(r.args.join(' ') + "\n" + err));
    const MediaInfo o = MediaInfo::probe(AppSettings::ffprobePath(), out);
    QVERIFY(qAbs(o.duration - 2.0) < 0.2);
    QVERIFY(!o.hasAudio);
}

void CoreTests::engineRunsQueue()
{
    QueueModel model;
    EncoderEngine engine(&model);
    const QString sid = model.addSource({m_clip});
    model.setSourceInfo(sid, m_info, 0);
    const QString outDir = m_dir.filePath("engine");
    QDir().mkpath(outDir);
    const QString a =
        model.addOutput(sid, PresetManager::instance().find("sys/h264/Match Source - Medium bitrate")->settings,
                        "sys/h264/Match Source - Medium bitrate", outDir);
    ExportSettings bad;
    bad.setFormat("quicktime");
    bad.effects.lutEnabled = true;
    bad.effects.lutPath = m_dir.filePath("missing.cube");
    const QString b = model.addOutput(sid, bad, {}, outDir);
    ExportSettings tp;
    tp.setFormat("h264");
    tp.video.rateControl = "vbr2";
    const QString c = model.addOutput(sid, tp, {}, outDir);
    model.setOutputRange(c, 0.2, 1.2);
    // Same output name as `a`: must be incremented, not overwritten.
    const QString d = model.duplicateOutput(a);

    QSignalSpy finished(&engine, &EncoderEngine::queueFinished);
    QSignalSpy progress(&engine, &EncoderEngine::taskProgress);
    engine.start();
    QVERIFY(finished.wait(600000));
    QCOMPARE(model.output(a)->status, OutputStatus::Done);
    QCOMPARE(model.output(b)->status, OutputStatus::Failed);
    QCOMPARE(model.output(c)->status, OutputStatus::Done);
    QCOMPARE(model.output(d)->status, OutputStatus::Done);
    QVERIFY(model.output(a)->finalPath != model.output(d)->finalPath);
    QVERIFY(QFileInfo::exists(model.output(d)->finalPath));
    QVERIFY(progress.count() > 0);
    QCOMPARE(finished.first().at(0).toInt(), 3);
    QCOMPARE(finished.first().at(1).toInt(), 1);

    // Save / load round trip keeps outputs.
    const QString qfile = m_dir.filePath("queue.json");
    QVERIFY(model.save(qfile));
    QueueModel other;
    QVERIFY(other.load(qfile));
    QCOMPARE(other.outputCount(), 4);
}

void CoreTests::enginePauseResume()
{
    const QString longClip = m_dir.filePath("long.mp4");
    QString err;
    QVERIFY2(runFfmpeg({"-y", "-f", "lavfi", "-i", "testsrc2=s=1920x1080:r=30:d=15", "-c:v", "libx264", "-preset",
                        "ultrafast", "-pix_fmt", "yuv420p", longClip},
                       &err),
             qPrintable(err));
    const MediaInfo info = MediaInfo::probe(AppSettings::ffprobePath(), longClip);
    QueueModel model;
    EncoderEngine engine(&model);
    const QString sid = model.addSource({longClip});
    model.setSourceInfo(sid, info, 0);
    ExportSettings s;
    s.setFormat("hevc");
    s.video.codec = "libx265";
    s.applyVideoCodecDefaults();
    s.video.speedPreset = "medium";
    const QString oid = model.addOutput(sid, s, {}, m_dir.filePath("pause"));

    QSignalSpy progress(&engine, &EncoderEngine::taskProgress);
    QSignalSpy finished(&engine, &EncoderEngine::queueFinished);
    engine.start();
    QVERIFY(progress.wait(60000));
    engine.pause();
    QCOMPARE(model.output(oid)->status, OutputStatus::Paused);
    QTest::qWait(500); // drain progress already in flight
    const int before = progress.count();
    const double pausedAt = model.output(oid)->progress;
    QTest::qWait(2000);
    QCOMPARE(progress.count(), before);
    QCOMPARE(model.output(oid)->progress, pausedAt);
    QVERIFY(pausedAt < 1.0);
    engine.resume();
    QCOMPARE(model.output(oid)->status, OutputStatus::Encoding);
    QVERIFY(finished.wait(900000));
    QCOMPARE(model.output(oid)->status, OutputStatus::Done);

    // Paused before any process runs (an analysis step is next): nothing runs until resumed.
    QueueModel model2;
    EncoderEngine engine2(&model2);
    const QString sid2 = model2.addSource({m_clip});
    model2.setSourceInfo(sid2, m_info, 0);
    ExportSettings cut;
    cut.setFormat("h264");
    cut.effects.removeSilence = true;
    const QString oid2 = model2.addOutput(sid2, cut, {}, m_dir.filePath("engine"));
    QSignalSpy finished2(&engine2, &EncoderEngine::queueFinished);
    connect(&engine2, &EncoderEngine::taskStarted, &engine2, &EncoderEngine::pause);
    engine2.start();
    QVERIFY(!finished2.wait(4000)); // a 2 s clip would be long done if it weren't paused
    QCOMPARE(model2.output(oid2)->status, OutputStatus::Paused);
    disconnect(&engine2, &EncoderEngine::taskStarted, &engine2, &EncoderEngine::pause);
    engine2.resume();
    QVERIFY(finished2.wait(300000));
    QCOMPARE(model2.output(oid2)->status, OutputStatus::Done);
}

void CoreTests::watchFolderPicksUpFiles()
{
    const QString watchDir = m_dir.filePath("watch");
    QDir().mkpath(watchDir);
    WatchFolderManager mgr;
    ExportSettings s;
    s.setFormat("h264");
    mgr.addFolder(watchDir, WatchOutput{s, {}, {}});
    QSignalSpy ready(&mgr, &WatchFolderManager::filesReady);
    mgr.setActive(true);
    QVERIFY(QFile::copy(m_clip2, QDir(watchDir).filePath("incoming.mov")));
    QFile junk(QDir(watchDir).filePath("notes.txt"));
    QVERIFY(junk.open(QIODevice::WriteOnly));
    junk.write("ignore me");
    junk.close();
    mgr.scanNow(); // first sighting: size recorded
    QCOMPARE(ready.count(), 0);
    mgr.scanNow(); // size unchanged: ready
    QCOMPARE(ready.count(), 1);
    const QStringList files = ready.first().at(1).toStringList();
    QCOMPARE(files.size(), 1);
    QVERIFY(files.first().endsWith("incoming.mov"));
    mgr.scanNow(); // already handled: not reported again
    QCOMPARE(ready.count(), 1);

    const QString fid = mgr.folders().first().id;
    QCOMPARE(mgr.folder(fid)->outputDirFor(mgr.folder(fid)->outputs.first()), QDir(watchDir).filePath("Output"));
    mgr.archiveSource(fid, files.first());
    QVERIFY(QFileInfo::exists(QDir(watchDir).filePath("Source/incoming.mov")));
    QVERIFY(!QFileInfo::exists(files.first()));
    mgr.removeFolder(fid);
}

void CoreTests::mediaFileModelListsAndSorts()
{
    const QString dir = m_dir.filePath("browse");
    QDir().mkpath(dir + "/Sub Folder");
    auto touch = [&](const QString &name, int bytes) {
        QFile f(QDir(dir).filePath(name));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QByteArray(bytes, 'x'));
    };
    touch("b_clip.mp4", 300);
    touch("a_song.wav", 100);
    touch("c_clip.mp4", 200);
    touch("d_still.png", 50);
    touch("notes.txt", 10);

    MediaFileModel model;
    MediaFileSortProxy proxy;
    proxy.setSourceModel(&model);
    model.setDirectory(dir);
    QCOMPARE(model.rowCount(), 5); // 4 media files + 1 folder; .txt hidden
    model.setShowAllFiles(true);
    QCOMPARE(model.rowCount(), 6);
    model.setShowAllFiles(false);

    auto namesInOrder = [&] {
        QStringList out;
        for (int r = 0; r < proxy.rowCount(); ++r)
            out << proxy.index(r, MediaFileModel::Name).data().toString();
        return out;
    };

    // Sort by type: folder first, then MP4s together (alphabetical within), PNG, WAV.
    proxy.sort(MediaFileModel::Type, Qt::AscendingOrder);
    QCOMPARE(namesInOrder(), QStringList({"Sub Folder", "b_clip.mp4", "c_clip.mp4", "d_still.png", "a_song.wav"}));
    proxy.sort(MediaFileModel::Type, Qt::DescendingOrder);
    QCOMPARE(namesInOrder(), QStringList({"Sub Folder", "a_song.wav", "d_still.png", "b_clip.mp4", "c_clip.mp4"}));

    // Sort by size (numeric, not text).
    proxy.sort(MediaFileModel::Size, Qt::DescendingOrder);
    QCOMPARE(namesInOrder(), QStringList({"Sub Folder", "b_clip.mp4", "c_clip.mp4", "a_song.wav", "d_still.png"}));

    QCOMPARE(proxy.index(1, MediaFileModel::Type).data().toString(), QString("MP4"));
    QCOMPARE(proxy.index(1, MediaFileModel::Kind).data().toString(), QString("Video"));

    // Filters: name text and kind.
    proxy.sort(MediaFileModel::Name, Qt::AscendingOrder);
    proxy.setNameFilterText("clip");
    QCOMPARE(namesInOrder(), QStringList({"b_clip.mp4", "c_clip.mp4"}));
    proxy.setNameFilterText(QString());
    proxy.setKindFilter(MediaFileSortProxy::AudioOnly);
    QCOMPARE(namesInOrder(), QStringList({"Sub Folder", "a_song.wav"}));
    proxy.setKindFilter(MediaFileSortProxy::AllMedia);

    // Drag data carries file URLs.
    QScopedPointer<QMimeData> mime(model.mimeData({model.index(0, 0)}));
    QCOMPARE(mime->urls().size(), 1);

    // New files show up after a refresh without resetting existing rows.
    touch("e_new.mov", 10);
    model.refresh();
    QCOMPARE(model.rowCount(), 6);
}

void CoreTests::mediaFileModelMetadataColumns()
{
    const QString dir = m_dir.filePath("meta");
    QDir().mkpath(dir);
    QVERIFY(QFile::copy(m_clip, QDir(dir).filePath("clip.mov")));
    QVERIFY(QFile::copy(m_still, QDir(dir).filePath("still.png")));

    MediaFileModel model;
    model.setShowFolders(false);
    model.setDirectory(dir);
    QCOMPARE(model.rowCount(), 2);
    const int clipRow = model.index(0, 0).data().toString() == "clip.mov" ? 0 : 1;

    // Disabled: no probing, no values.
    QVERIFY(!model.index(clipRow, MediaFileModel::Resolution).data().isValid());

    QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
    model.setMetadataEnabled(true);
    model.index(clipRow, MediaFileModel::Resolution).data(); // triggers the background probe
    QTRY_COMPARE_WITH_TIMEOUT(model.index(clipRow, MediaFileModel::Resolution).data().toString(), QString("1280 × 720"),
                              20000);
    QCOMPARE(model.index(clipRow, MediaFileModel::FrameRate).data().toString(), QString("29.970"));
    QCOMPARE(model.index(clipRow, MediaFileModel::VideoCodec).data().toString(), QString("H.264"));
    QVERIFY(model.index(clipRow, MediaFileModel::Audio).data().toString().contains("48 kHz"));
    QVERIFY(model.index(clipRow, MediaFileModel::Duration).data(MediaFileModel::SortRole).toDouble() > 1.5);
    QVERIFY(changed.count() > 0);

    // The cache answers without probing again.
    QVERIFY(MediaInfoCache::instance().lookup(QDir(dir).filePath("clip.mov"), false).has_value());
}

void CoreTests::queueReorder()
{
    QueueModel model;
    const QString a = model.addSource({m_dir.filePath("a.mov")});
    const QString b = model.addSource({m_dir.filePath("b.mov")});
    const QString c = model.addSource({m_dir.filePath("c.mov")});
    ExportSettings s;
    s.setFormat("h264");
    const QString outC = model.addOutput(c, s, {});
    QPersistentModelIndex persistentOut = model.indexForOutput(outC);
    QVERIFY(persistentOut.isValid());

    QVERIFY(model.moveSource(c, 0));
    QCOMPARE(model.sources().at(0).id, c);
    QCOMPARE(model.sources().at(1).id, a);
    QCOMPARE(model.sources().at(2).id, b);
    // Persistent indexes to outputs follow their source.
    QVERIFY(persistentOut.isValid());
    QCOMPARE(persistentOut.data(QueueModel::IdRole).toString(), outC);

    QVERIFY(model.moveSource(c, 99)); // clamps to the end
    QCOMPARE(model.sources().at(2).id, c);
    QCOMPARE(persistentOut.data(QueueModel::IdRole).toString(), outC);
    QVERIFY(!model.moveSource(c, 2)); // no-op
}

void CoreTests::imageSequenceDetection()
{
    const QString dir = m_dir.filePath("seqdetect");
    QDir().mkpath(dir);
    auto touch = [&](const QString &name) {
        QFile f(QDir(dir).filePath(name));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("x");
    };
    for (int i = 1; i <= 12; ++i)
        touch(QStringLiteral("shot_%1.png").arg(i, 4, 10, QChar('0')));
    // A gap: 14..16 is a separate run.
    for (int i = 14; i <= 16; ++i)
        touch(QStringLiteral("shot_%1.png").arg(i, 4, 10, QChar('0')));
    touch("shot_0003.jpg"); // different extension, ignored
    for (int i = 8; i <= 11; ++i)
        touch(QStringLiteral("frame%1.tif").arg(i)); // unpadded, crosses 9 -> 10

    ImageSequence a = ImageSequence::detect(QDir(dir).filePath("shot_0005.png"));
    QVERIFY(a.valid);
    QCOMPARE(a.start, 1);
    QCOMPARE(a.count, 12);
    QVERIFY(a.pattern.endsWith("shot_%04d.png"));
    QCOMPARE(a.baseName(), QString("shot"));
    QCOMPARE(a.displayName(), QString("shot_[0001-0012].png"));

    ImageSequence b = ImageSequence::detect(QDir(dir).filePath("shot_0015.png"));
    QVERIFY(b.valid);
    QCOMPARE(b.start, 14);
    QCOMPARE(b.count, 3);

    ImageSequence c = ImageSequence::detect(QDir(dir).filePath("frame9.tif"));
    QVERIFY(c.valid);
    QVERIFY(c.pattern.endsWith("frame%d.tif"));
    QCOMPARE(c.start, 8);
    QCOMPARE(c.count, 4);

    QVERIFY(!ImageSequence::detect(QDir(dir).filePath("shot_0003.jpg")).valid); // single frame
    QVERIFY(
        ImageSequence::filesFormSequence({QDir(dir).filePath("shot_0002.png"), QDir(dir).filePath("shot_0001.png")}));
    QVERIFY(
        !ImageSequence::filesFormSequence({QDir(dir).filePath("shot_0002.png"), QDir(dir).filePath("shot_0015.png")}));
    QVERIFY(!ImageSequence::filesFormSequence({m_clip, m_clip2}));
}

void CoreTests::imageSequenceEncode()
{
    const QString dir = m_dir.filePath("seqencode");
    QDir().mkpath(dir);
    QString err;
    // 30 RGBA frames numbered from 101.
    QVERIFY2(runFfmpeg({"-y", "-f", "lavfi", "-i", "testsrc2=s=640x360:r=30:d=1,format=rgba", "-frames:v", "30",
                        "-start_number", "101", QDir(dir).filePath("render_%04d.png")},
                       &err),
             qPrintable(err));
    ImageSequence seq = ImageSequence::detect(QDir(dir).filePath("render_0110.png"));
    QVERIFY(seq.valid);
    QCOMPARE(seq.start, 101);
    QCOMPARE(seq.count, 30);
    seq.fps = 24;

    const MediaInfo info = MediaInfo::probe(AppSettings::ffprobePath(), seq.pattern, 20000, seq.inputArgs());
    QVERIFY2(info.valid, qPrintable(info.error));
    QVERIFY(!info.isStill);
    QVERIFY(info.hasAlpha());
    QVERIFY2(qAbs(info.duration - 1.25) < 0.05, qPrintable(QString::number(info.duration)));

    QueueModel model;
    const QString sid = model.addImageSequence(seq);
    model.setSourceInfo(sid, info, 0);
    const SourceItem *src = model.source(sid);
    QVERIFY(src->displayName().contains("render_[0101-0130].png"));
    ExportSettings h264;
    h264.setFormat("h264");
    const QString oid = model.addOutput(sid, h264, {}, m_dir.filePath("seqout"));
    QCOMPARE(QFileInfo(model.output(oid)->outputPath).fileName(), QString("render.mp4"));

    // Encode to H.264 and to ProRes 4444 (alpha must survive).
    for (const QString &fmt : {QString("h264"), QString("quicktime")}) {
        ExportSettings s;
        s.setFormat(fmt);
        if (fmt == "quicktime") {
            s.video.profile = "4";
            s.audio.enabled = false;
        }
        BuildRequest req;
        req.inputs = src->files;
        req.inputArgs = src->inputArgs();
        req.source = info;
        req.settings = s;
        req.outputPath = m_dir.filePath(QStringLiteral("seqout/render_%1.%2").arg(fmt, s.extension()));
        req.workDir = m_dir.filePath("work");
        QDir().mkpath(m_dir.filePath("seqout"));
        const BuildResult r = CommandBuilder::build(req);
        QVERIFY2(r.ok(), qPrintable(r.error));
        QVERIFY2(runFfmpeg(r.args, &err), qPrintable(r.args.join(' ') + "\n" + err));
        const MediaInfo o = MediaInfo::probe(AppSettings::ffprobePath(), req.outputPath);
        QVERIFY2(qAbs(o.duration - 1.25) < 0.1, qPrintable(QString::number(o.duration)));
        QVERIFY(qAbs(o.fps - 24.0) < 0.01);
        if (fmt == "quicktime")
            QVERIFY2(o.pixFmt.startsWith("yuva444p"), qPrintable(o.pixFmt)); // alpha kept
    }

    // Preview frame grab from the middle of the sequence.
    BuildRequest preq;
    preq.inputs = src->files;
    preq.inputArgs = src->inputArgs();
    preq.source = info;
    preq.settings = h264;
    preq.workDir = m_dir.filePath("work");
    const QString bmp = m_dir.filePath("seqpreview.bmp");
    const BuildResult pr = CommandBuilder::buildPreview(preq, 0.6, bmp, true);
    QVERIFY2(runFfmpeg(pr.args, &err), qPrintable(pr.args.join(' ') + "\n" + err));
    QCOMPARE(QImage(bmp).width(), 640);

    // Queue persistence keeps the sequence.
    const QString qfile = m_dir.filePath("seqqueue.json");
    QVERIFY(model.save(qfile));
    QueueModel other;
    QVERIFY(other.load(qfile));
    QVERIFY(other.sources().first().sequence.valid);
    QCOMPARE(other.sources().first().sequence.start, 101);
    QCOMPARE(other.sources().first().inputArgs(), seq.inputArgs());
}

void CoreTests::presetReplacesAutoOutput()
{
    QueueModel model;
    const QString sid = model.addSource({m_clip});
    const Preset def = PresetManager::instance().defaultPreset();
    const QString auto1 = model.addOutput(sid, def.settings, def.id);
    model.setAutoAdded(auto1, true);

    // First preset replaces the placeholder output...
    const QString yt = "sys/h264/YouTube 720p HD";
    QVERIFY(PresetManager::instance().find(yt));
    QCOMPARE(model.applyPresetToSource(sid, yt), auto1);
    QCOMPARE(model.source(sid)->outputs.size(), 1);
    QCOMPARE(model.output(auto1)->settings.video.width, 1280);
    QVERIFY(!model.output(auto1)->autoAdded);
    // ...later ones add outputs.
    const QString second = model.applyPresetToSource(sid, "sys/h264/TikTok 1080x1920");
    QVERIFY(second != auto1);
    QCOMPARE(model.source(sid)->outputs.size(), 2);

    // Dropping a preset onto a source row does the same.
    const QString sid2 = model.addSource({m_clip2});
    const QString auto2 = model.addOutput(sid2, def.settings, def.id);
    model.setAutoAdded(auto2, true);
    QMimeData mime;
    mime.setData(PresetManager::MimeType, QByteArray("sys/h264/Vimeo 1080p HD"));
    QVERIFY(model.dropMimeData(&mime, Qt::CopyAction, -1, -1, model.indexForSource(sid2)));
    QCOMPARE(model.source(sid2)->outputs.size(), 1);
    QCOMPARE(model.output(auto2)->settings.presetName, QString("Vimeo 1080p HD"));
}

void CoreTests::frameRateLimitAndBitrateScaling()
{
    QString err;
    const QString fast = m_dir.filePath("fast120.mp4");
    const QString sixty = m_dir.filePath("sixty.mp4");
    QVERIFY2(runFfmpeg({"-y", "-f", "lavfi", "-i", "testsrc2=s=640x360:r=120:d=0.5", "-c:v", "libx264", "-preset",
                        "ultrafast", "-pix_fmt", "yuv420p", fast},
                       &err),
             qPrintable(err));
    QVERIFY2(runFfmpeg({"-y", "-f", "lavfi", "-i", "testsrc2=s=640x360:r=60:d=0.5", "-c:v", "libx264", "-preset",
                        "ultrafast", "-pix_fmt", "yuv420p", sixty},
                       &err),
             qPrintable(err));
    const ExportSettings yt = PresetManager::instance().find("sys/h264/YouTube 1080p HD")->settings;
    QCOMPARE(yt.group, QString("YouTube"));
    QCOMPARE(yt.video.frameRateLimit, 60.0);

    auto argsFor = [&](const QString &file, MediaInfo *infoOut) {
        BuildRequest req;
        req.inputs = {file};
        req.source = MediaInfo::probe(AppSettings::ffprobePath(), file);
        req.settings = yt;
        req.outputPath = m_dir.filePath("limit_" + QFileInfo(file).completeBaseName() + ".mp4");
        req.workDir = m_dir.filePath("work");
        *infoOut = req.source;
        return CommandBuilder::build(req);
    };
    MediaInfo info;
    // 29.97 fps source: no conversion, base bitrate.
    BuildResult r = argsFor(m_clip, &info);
    QVERIFY(!r.args.at(r.args.indexOf("-filter_complex") + 1).contains("fps="));
    QCOMPARE(r.args.at(r.args.indexOf("-b:v") + 1), QString("16000k"));
    // 60 fps source: kept at 60, bitrate raised 1.5x.
    r = argsFor(sixty, &info);
    QVERIFY(!r.args.at(r.args.indexOf("-filter_complex") + 1).contains("fps="));
    QCOMPARE(r.args.at(r.args.indexOf("-b:v") + 1), QString("24000k"));
    // 120 fps source: converted down to 60.
    r = argsFor(fast, &info);
    QVERIFY(r.args.at(r.args.indexOf("-filter_complex") + 1).contains("fps=60"));
    QVERIFY2(runFfmpeg(r.args, &err), qPrintable(err));
    const MediaInfo out = MediaInfo::probe(AppSettings::ffprobePath(), m_dir.filePath("limit_fast120.mp4"));
    QVERIFY(qAbs(out.fps - 60.0) < 0.01);
    QCOMPARE(out.width, 1920);
}

void CoreTests::platformPresetsPresent()
{
    const QStringList expected = {"sys/h264/YouTube 2160p 4K",
                                  "sys/h264/YouTube 1440p 2K",
                                  "sys/h264/YouTube 1080p HD",
                                  "sys/h264/YouTube 720p HD",
                                  "sys/h264/YouTube Shorts 1080x1920",
                                  "sys/h264/Vimeo 2160p 4K",
                                  "sys/h264/TikTok 1080x1920",
                                  "sys/h264/Instagram Reels 1080x1920",
                                  "sys/h264/Instagram Feed Square 1080x1080",
                                  "sys/h264/Facebook 1080p HD",
                                  "sys/h264/Twitter 1080p HD",
                                  "sys/h264/LinkedIn 1080p HD",
                                  "sys/h264/Snapchat Spotlight & Stories 1080x1920",
                                  "sys/h264/Pinterest Standard 1000x1500"};
    for (const QString &id : expected) {
        const Preset *p = PresetManager::instance().find(id);
        QVERIFY2(p, qPrintable(id));
        QVERIFY2(!p->settings.group.isEmpty(), qPrintable(id));
    }
    const ExportSettings tiktok = PresetManager::instance().find("sys/h264/TikTok 1080x1920")->settings;
    QCOMPARE(tiktok.video.scaleMode, QString("fill"));
    QCOMPARE(tiktok.video.height, 1920);
}

QProcess *CoreTests::startLiveSource(const QStringList &outputArgs, const QStringList &inputs)
{
    auto *p = new QProcess(this);
    p->setWorkingDirectory(m_dir.path());
    QStringList args{"-hide_banner", "-loglevel", "error", "-re"};
    args << (inputs.isEmpty() ? QStringList{"-f", "lavfi", "-i", "testsrc2=size=320x240:rate=25", "-f", "lavfi", "-i",
                                            "sine=frequency=440"}
                              : inputs);
    args << QStringList{"-c:v", "libx264", "-preset", "ultrafast", "-g", "25", "-c:a", "aac", "-t", "40"};
    p->start(AppSettings::ffmpegPath(), args + outputArgs);
    p->waitForStarted();
    return p;
}

void CoreTests::streamLinkHelpers()
{
    QVERIFY(StreamLink::isStreamUrl("https://example.com/live/index.m3u8"));
    QVERIFY(StreamLink::isStreamUrl("  rtmp://live.example.com/app/key  "));
    QVERIFY(StreamLink::isStreamUrl("srt://10.0.0.5:9000"));
    QVERIFY(StreamLink::isStreamUrl("udp://:1234"));
    QVERIFY(StreamLink::isStreamUrl("https://www.youtube.com/watch?v=abc123"));
    QVERIFY(!StreamLink::isStreamUrl("C:/Videos/clip.mp4"));
    QVERIFY(!StreamLink::isStreamUrl("not a link"));
    QVERIFY(!StreamLink::isStreamUrl("ftp://example.com/file.ts"));

    QVERIFY(StreamLink::isDirectMedia("https://cdn.example.com/hls/master.m3u8?token=abc"));
    QVERIFY(StreamLink::isDirectMedia("http://radio.example.com:8000/stream.mp3"));
    QVERIFY(StreamLink::isDirectMedia("rtsp://192.168.1.20/cam1"));
    QVERIFY(!StreamLink::isDirectMedia("https://www.youtube.com/watch?v=abc123"));
    QVERIFY(!StreamLink::isDirectMedia("https://www.twitch.tv/somechannel"));

    QStringList a = StreamLink::inputArgs("https://cdn.example.com/live.m3u8",
                                          {{"User-Agent", "TestAgent/1.0"}, {"Referer", "https://example.com/"}});
    QVERIFY(a.contains("-reconnect_streamed"));
    QCOMPARE(a.at(a.indexOf("-user_agent") + 1), QString("TestAgent/1.0"));
    QCOMPARE(a.at(a.indexOf("-headers") + 1), QString("Referer: https://example.com/\r\n"));
    a = StreamLink::inputArgs("rtsp://cam/stream");
    QCOMPARE(a.at(a.indexOf("-rtsp_transport") + 1), QString("tcp"));
    QVERIFY(!a.contains("-reconnect"));
    QVERIFY(StreamLink::inputArgs("C:/temp/live.m3u8").isEmpty());

    const QDateTime when(QDate(2026, 10, 16), QTime(20, 5, 9));
    QCOMPARE(StreamLink::suggestedName("https://www.twitch.tv/x", QString(), when),
             QString("twitch.tv 2026-10-16 20-05-09"));
    QCOMPARE(StreamLink::suggestedName("https://x.com/a", "Live: Q&A / \"Launch\"?", when),
             QString("Live_ Q&A _ _Launch__ 2026-10-16 20-05-09"));
    QCOMPARE(StreamLink::sanitizeFileName("  trailing dots... "), QString("trailing dots"));
}

void CoreTests::ytDlpJsonParsing()
{
    // One stream with picture and sound (Twitch, most sites).
    ResolvedStream rs = StreamLink::parseYtDlpJson(
        R"({"title":"My Stream 2026-10-16 14:12","fulltitle":"My Stream","is_live":true,"url":"https://host/x.m3u8",
            "http_headers":{"User-Agent":"UA"}})");
    QVERIFY(rs.ok());
    QVERIFY(rs.isLive);
    QCOMPARE(rs.title, QString("My Stream"));
    QCOMPARE(rs.url, QString("https://host/x.m3u8"));
    QVERIFY(rs.audioUrl.isEmpty());
    QCOMPARE(rs.headers.size(), 1);

    // YouTube live: picture and sound are separate HLS streams.
    rs = StreamLink::parseYtDlpJson(
        R"({"title":"News","live_status":"is_live","requested_formats":[
              {"url":"https://g/v.m3u8","vcodec":"avc1.64002A","acodec":"none","http_headers":{"User-Agent":"UA"}},
              {"url":"https://g/a.m3u8","vcodec":"none","acodec":"mp4a.40.2"}]})");
    QVERIFY(rs.ok());
    QVERIFY(rs.isLive);
    QCOMPARE(rs.url, QString("https://g/v.m3u8"));
    QCOMPARE(rs.audioUrl, QString("https://g/a.m3u8"));
    QCOMPARE(rs.headers.value(0).second, QString("UA"));

    // The video already carries sound: the extra audio stream isn't needed.
    rs = StreamLink::parseYtDlpJson(
        R"({"title":"T","requested_formats":[{"url":"https://t/v.m3u8","vcodec":"avc1","acodec":"mp4a"},
                                              {"url":"https://t/a.m3u8","vcodec":"none","acodec":"mp4a"}]})");
    QCOMPARE(rs.url, QString("https://t/v.m3u8"));
    QVERIFY(rs.audioUrl.isEmpty());

    QVERIFY(!StreamLink::parseYtDlpJson(R"({"title":"Nothing"})").ok());
    QVERIFY(!StreamLink::parseYtDlpJson("garbage").ok());
}

void CoreTests::recordLiveHttpStreamToMp4()
{
    const int port = 41000 + int(QCoreApplication::applicationPid() % 1000);
    const QString url = QStringLiteral("http://127.0.0.1:%1/live.ts").arg(port);
    QProcess *server = startLiveSource({"-f", "mpegts", "-listen", "1", url});
    QTest::qWait(1500);

    const QString dir = m_dir.filePath("recordings");
    StreamRecorder recorder;
    QSignalSpy active(&recorder, &StreamRecorder::activeCountChanged);
    RecordingOptions opt;
    opt.url = url;
    opt.outputDir = dir;
    opt.container = "mp4";
    StreamRecording *rec = recorder.record(opt);
    QCOMPARE(recorder.activeCount(), 1);
    QTRY_VERIFY_WITH_TIMEOUT(rec->recordedSeconds() >= 4.0, 20000);
    QCOMPARE(rec->state(), StreamRecording::State::Recording);
    QVERIFY(rec->bytes() > 0);
    QCOMPARE(rec->files().size(), 1);
    QVERIFY(rec->files().first().endsWith(".mkv")); // recorded as MKV first
    rec->stop();
    QVERIFY(recorder.waitForAll(30000));
    QCOMPARE(rec->state(), StreamRecording::State::Done);
    QVERIFY2(rec->note().isEmpty(), qPrintable(rec->note()));
    QCOMPARE(rec->files().size(), 1);
    const QString file = rec->files().first();
    QVERIFY2(file.endsWith(".mp4"), qPrintable(file));
    QVERIFY(QFileInfo(file).fileName().startsWith("127.0.0.1 "));
    QCOMPARE(QDir(dir).entryList({"*.mkv"}, QDir::Files).size(), 0); // converted, not left behind
    const MediaInfo info = MediaInfo::probe(AppSettings::ffprobePath(), file);
    QVERIFY2(info.valid, qPrintable(info.error));
    QVERIFY(info.hasVideo);
    QVERIFY(info.hasAudio);
    QVERIFY2(info.duration >= 3.5, qPrintable(QString::number(info.duration)));
    QCOMPARE(active.last().first().toInt(), 0);
    server->kill();
    server->waitForFinished();
}

void CoreTests::recordHlsWithTimeLimit()
{
    const QString hls = m_dir.filePath("hls");
    QDir().mkpath(hls);
    QProcess *server = startLiveSource(
        {"-f", "hls", "-hls_time", "1", "-hls_list_size", "4", "-hls_flags", "delete_segments", hls + "/live.m3u8"});
    QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(hls + "/live.m3u8"), 15000);
    QTest::qWait(1000);

    TinyHttpServer http(hls);
    QVERIFY(http.isListening());
    StreamRecorder recorder;
    RecordingOptions opt;
    opt.url = http.url("live.m3u8");
    opt.outputDir = m_dir.filePath("recordings");
    opt.name = "Timed: HLS";
    opt.container = "ts";
    opt.maxSeconds = 3;
    StreamRecording *rec = recorder.record(opt);
    QVERIFY(recorder.waitForAll(30000));
    QCOMPARE(rec->state(), StreamRecording::State::Done);
    QVERIFY2(rec->stateText().contains("time limit"), qPrintable(rec->stateText()));
    const QString file = rec->files().value(0);
    QCOMPARE(QFileInfo(file).fileName(), QString("Timed_ HLS.ts"));
    const MediaInfo info = MediaInfo::probe(AppSettings::ffprobePath(), file);
    QVERIFY2(info.valid, qPrintable(info.error));
    QVERIFY2(qAbs(info.duration - 3.0) < 1.0, qPrintable(QString::number(info.duration)));

    // A second recording with the same name doesn't overwrite the first.
    StreamRecording *again = recorder.record(opt);
    QVERIFY(recorder.waitForAll(30000));
    QCOMPARE(QFileInfo(again->files().value(0)).fileName(), QString("Timed_ HLS (2).ts"));
    server->kill();
    server->waitForFinished();
}

void CoreTests::recordSplitVideoAndAudio()
{
    // Like YouTube live: picture and sound arrive as two separate live HLS streams.
    const QString hls = m_dir.filePath("split");
    QDir().mkpath(hls);
    auto *video = startLiveSource({"-an", "-f", "hls", "-hls_time", "1", "-hls_list_size", "5", hls + "/video.m3u8"});
    auto *audio = new QProcess(this);
    audio->start(AppSettings::ffmpegPath(),
                 {"-hide_banner", "-loglevel", "error", "-re", "-f", "lavfi", "-i", "sine=frequency=440", "-c:a", "aac",
                  "-t", "40", "-f", "hls", "-hls_time", "1", "-hls_list_size", "5", hls + "/audio.m3u8"});
    QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(hls + "/video.m3u8") && QFileInfo::exists(hls + "/audio.m3u8"), 15000);
    QTest::qWait(1500);

    TinyHttpServer http(hls);
    QVERIFY(http.isListening());
    // Feed the recorder a resolved page through a stand-in for yt-dlp.
#ifdef Q_OS_WIN
    const QString fakeYtDlp = m_dir.filePath("fake-yt-dlp.cmd");
#else
    const QString fakeYtDlp = m_dir.filePath("fake-yt-dlp.sh");
#endif
    {
        QFile f(fakeYtDlp);
        QVERIFY(f.open(QIODevice::WriteOnly));
        const QString json = QStringLiteral(R"({"fulltitle":"Split Live","is_live":true,"requested_formats":[)"
                                            R"({"url":"%1","vcodec":"h264","acodec":"none"},)"
                                            R"({"url":"%2","vcodec":"none","acodec":"aac"}]})")
                                 .arg(http.url("video.m3u8"), http.url("audio.m3u8"));
#ifdef Q_OS_WIN
        f.write("@echo off\r\necho " + json.toUtf8() + "\r\n");
#else
        f.write("#!/bin/sh\ncat <<'JSON'\n" + json.toUtf8() + "\nJSON\n");
        f.close();
        f.setPermissions(f.permissions() | QFile::ExeOwner | QFile::ExeUser);
#endif
    }
    RecordingOptions opt;
    opt.url = "https://www.example.com/watch?v=live";
    opt.outputDir = m_dir.filePath("recordings");
    opt.maxSeconds = 4;
    StreamRecording rec("split", opt, AppSettings::ffmpegPath(), fakeYtDlp);
    QSignalSpy done(&rec, &StreamRecording::finished);
    rec.start();
    QVERIFY(done.wait(40000));
    QVERIFY2(rec.state() == StreamRecording::State::Done, qPrintable(rec.error() + "\n" + rec.logTail()));
    const QString file = rec.files().value(0);
    QVERIFY2(QFileInfo(file).fileName().startsWith("Split Live 20"), qPrintable(file));
    const MediaInfo info = MediaInfo::probe(AppSettings::ffprobePath(), file);
    QVERIFY2(info.valid, qPrintable(info.error));
    QVERIFY(info.hasVideo);
    QVERIFY(info.hasAudio);
    QVERIFY2(info.duration >= 3.5 && info.duration < 12, qPrintable(QString::number(info.duration)));
    for (QProcess *p : {video, audio}) {
        p->kill();
        p->waitForFinished();
    }
}

void CoreTests::recordDeadLinkFails()
{
    StreamRecorder recorder;
    RecordingOptions opt;
    opt.url = QStringLiteral("http://127.0.0.1:%1/nothing.m3u8").arg(43000 + QCoreApplication::applicationPid() % 1000);
    opt.outputDir = m_dir.filePath("dead");
    QElapsedTimer t;
    t.start();
    StreamRecording *rec = recorder.record(opt);
    QVERIFY(recorder.waitForAll(60000));
    QCOMPARE(rec->state(), StreamRecording::State::Failed);
    QVERIFY(!rec->error().isEmpty());
    QVERIFY(rec->files().isEmpty());
    QVERIFY(QDir(opt.outputDir).entryList(QDir::Files).isEmpty());
    qInfo() << "dead link failed after" << t.elapsed() << "ms:" << rec->error();
}

bool CoreTests::runQueue(QueueModel &model, int timeoutMs)
{
    EncoderEngine engine(&model);
    QSignalSpy finished(&engine, &EncoderEngine::queueFinished);
    engine.start();
    return finished.wait(timeoutMs);
}

QString CoreTests::ffprobe(const QStringList &args)
{
    QProcess p;
    p.start(AppSettings::ffprobePath(), QStringList{"-v", "error"} + args);
    p.waitForFinished(60000);
    return QString::fromUtf8(p.readAllStandardOutput()).trimmed();
}

void CoreTests::targetFileSize()
{
    ExportSettings s;
    s.setFormat("h264");
    s.video.rateControl = "size";
    s.video.targetSizeMB = 0.35; // 2 s: about 1.4 Mbps in total
    s.video.maxBitrate = 100;
    s.audio.bitrate = 128;
    QVERIFY(CommandBuilder::usesTwoPass(s));

    BuildRequest req = requestFor(s, m_dir.filePath("size.mp4"));
    req.outPoint = -1;
    QString err;
    const double mbps = CommandBuilder::targetSizeBitrate(req, &err);
    QVERIFY2(mbps > 0.85 && mbps < 1.1, qPrintable(QString::number(mbps)));
    const BuildResult r = CommandBuilder::build(req);
    QVERIFY2(r.ok(), qPrintable(r.error));
    QVERIFY(r.args.contains("-b:v"));

    // Too small to be possible: a clear error instead of a broken encode.
    BuildRequest tiny = req;
    tiny.settings.video.targetSizeMB = 0.01;
    QCOMPARE(CommandBuilder::targetSizeBitrate(tiny, &err), 0.0);
    QVERIFY(err.contains("too small"));
    QVERIFY(!CommandBuilder::build(tiny).ok());
    // Uncompressed audio has no fixed size.
    BuildRequest pcm = req;
    pcm.settings.setFormat("quicktime");
    pcm.settings.video.codec = "libx264";
    pcm.settings.video.rateControl = "size";
    pcm.settings.audio.codec = "pcm_s16le";
    QVERIFY(!CommandBuilder::build(pcm).ok());

    QueueModel model;
    const QString sid = model.addSource({m_clip});
    model.setSourceInfo(sid, m_info, 0);
    QDir().mkpath(m_dir.filePath("size"));
    const QString oid = model.addOutput(sid, s, {}, m_dir.filePath("size"));
    QVERIFY(runQueue(model, 300000));
    const OutputItem *o = model.output(oid);
    QVERIFY2(o->status == OutputStatus::Done, qPrintable(o->message));
    const qint64 size = QFileInfo(o->finalPath).size();
    QVERIFY2(size <= 350000 && size > 200000, qPrintable(QString::number(size)));
    QVERIFY(o->message.contains("Target file size"));
}

void CoreTests::targetQualityVmaf()
{
    if (!QualityProbe::vmafAvailable())
        SKIP_OR_FAIL("This FFmpeg build has no libvmaf");
    // Sample placement: one sample for short clips, several spread over longer ones.
    BuildRequest r = requestFor(ExportSettings(), {});
    r.outPoint = -1;
    QCOMPARE(QualityProbe::sampleRanges(r, 3, 4.0).size(), 1);
    MediaInfo longInfo = m_info;
    longInfo.duration = 600;
    r.source = longInfo;
    const TimeRanges samples = QualityProbe::sampleRanges(r, 5, 4.0);
    QCOMPARE(samples.size(), 5);
    QVERIFY(samples.first().first > 50 && samples.last().second < 560);
    QCOMPARE(QualityProbe::parseVmafScore("[Parsed_libvmaf_4 @ 0x1] VMAF score: 95.123400\n"), 95.1234);

    ExportSettings s;
    s.setFormat("h264");
    s.video.rateControl = "vmaf";
    s.video.targetVmaf = 92;
    s.video.speedPreset = "veryfast";
    QueueModel model;
    const QString sid = model.addSource({m_clip});
    model.setSourceInfo(sid, m_info, 0);
    QDir().mkpath(m_dir.filePath("vmaf"));
    const QString oid = model.addOutput(sid, s, {}, m_dir.filePath("vmaf"));
    QVERIFY(runQueue(model, 600000));
    const OutputItem *o = model.output(oid);
    QVERIFY2(o->status == OutputStatus::Done, qPrintable(o->message));
    QVERIFY2(o->message.contains("VMAF") && o->message.contains("CRF"), qPrintable(o->message));
    QVERIFY(QFileInfo(o->finalPath).size() > 0);
}

void CoreTests::blurredBackground()
{
    ExportSettings s;
    s.setFormat("h264");
    s.video.width = 1080;
    s.video.height = 1920;
    s.video.scaleMode = "blur";
    const QString out = m_dir.filePath("vertical blur.mp4");
    BuildRequest req = requestFor(s, out);
    const BuildResult r = CommandBuilder::build(req);
    QVERIFY2(r.ok(), qPrintable(r.error));
    QVERIFY(r.args.at(r.args.indexOf("-filter_complex") + 1).contains("gblur"));
    QString err;
    QVERIFY2(runFfmpeg(r.args, &err), qPrintable(r.args.join(' ') + "\n" + err));
    const MediaInfo info = MediaInfo::probe(AppSettings::ffprobePath(), out);
    QCOMPARE(info.width, 1080);
    QCOMPARE(info.height, 1920);

    // Above the picture is the blurred copy, not black bars.
    const QString png = m_dir.filePath("blur_frame.png");
    QVERIFY2(runFfmpeg({"-y", "-i", out, "-frames:v", "1", png}, &err), qPrintable(err));
    const QImage frame(png);
    QCOMPARE(frame.size(), QSize(1080, 1920));
    const QColor top = frame.pixelColor(540, 200);
    QVERIFY2(top.red() + top.green() + top.blue() > 60, qPrintable(top.name()));

    // The preview shows the same thing.
    const BuildResult p = CommandBuilder::buildPreview(req, 0.5, m_dir.filePath("blur_preview.bmp"), true);
    QVERIFY2(p.ok(), qPrintable(p.error));
    QVERIFY2(runFfmpeg(p.args, &err), qPrintable(err));
    QCOMPARE(QImage(m_dir.filePath("blur_preview.bmp")).size(), QSize(1080, 1920));
}

void CoreTests::removeSilence()
{
    // Unit: cut list from detected silences.
    const TimeRanges keep = CommandBuilder::keepRangesFromSilences({{0.0, 0.5}, {2.0, 4.0}, {9.0, 10.0}}, 10.0, 0.2);
    QCOMPARE(keep.size(), 2);
    QCOMPARE(keep[0].first, 0.3);
    QCOMPARE(keep[0].second, 2.2);
    QCOMPARE(keep[1].first, 3.8);
    QCOMPARE(keep[1].second, 9.2); // a little of the pause after the speech stays

    // 4 s clip: tone, 2 s of silence, tone.
    const QString clip = m_dir.filePath("pauses.mp4");
    QString err;
    QVERIFY2(runFfmpeg({"-y", "-f", "lavfi", "-i", "testsrc2=s=640x360:r=25:d=4", "-f", "lavfi", "-i",
                        "aevalsrc=if(lt(t\\,1)+gte(t\\,3)\\,0.5*sin(2*PI*440*t)\\,0):s=48000:d=4", "-c:v", "libx264",
                        "-pix_fmt", "yuv420p", "-c:a", "aac", "-shortest", clip},
                       &err),
             qPrintable(err));
    const MediaInfo info = MediaInfo::probe(AppSettings::ffprobePath(), clip);
    QVERIFY(info.valid);

    ExportSettings s;
    s.setFormat("h264");
    s.video.speedPreset = "veryfast";
    s.effects.removeSilence = true;
    s.effects.silenceThreshold = -40;
    s.effects.silenceMinDuration = 0.8;
    s.effects.silencePadding = 0.2;
    QueueModel model;
    const QString sid = model.addSource({clip});
    model.setSourceInfo(sid, info, 0);
    QDir().mkpath(m_dir.filePath("silence"));
    const QString whole = model.addOutput(sid, s, {}, m_dir.filePath("silence"));
    const QString ranged = model.addOutput(sid, s, {}, m_dir.filePath("silence"));
    model.setOutputRange(ranged, 0.5, -1); // the cut list is relative to the in point
    s.setFormat("mp3");
    s.effects.removeSilence = true;
    const QString audioOnly = model.addOutput(sid, s, {}, m_dir.filePath("silence"));
    QVERIFY(runQueue(model, 300000));

    auto check = [&](const QString &oid, double expected) {
        const OutputItem *o = model.output(oid);
        QVERIFY2(o->status == OutputStatus::Done, qPrintable(o->message));
        QVERIFY2(o->message.contains("Remove silence"), qPrintable(o->message));
        const double v =
            ffprobe({"-select_streams", "v:0", "-show_entries", "stream=duration", "-of", "csv=p=0", o->finalPath})
                .toDouble();
        const double a =
            ffprobe({"-select_streams", "a:0", "-show_entries", "stream=duration", "-of", "csv=p=0", o->finalPath})
                .toDouble();
        QVERIFY2(qAbs(a - expected) < 0.15, qPrintable(QString("audio %1, expected %2").arg(a).arg(expected)));
        if (v > 0)
            QVERIFY2(qAbs(v - a) < 0.1, qPrintable(QString("video %1 vs audio %2").arg(v).arg(a)));
    };
    check(whole, 2.4);  // 1 + 0.2 + 0.2 + 1
    check(ranged, 1.9); // 0.5 + 0.2 + 0.2 + 1
    check(audioOnly, 2.4);

    // Hundreds of cuts: the filters go through a file (command lines are limited to 32k on Windows),
    // and video and audio still end up the same length.
    const QString longClip = m_dir.filePath("many cuts source.mp4");
    QVERIFY2(runFfmpeg({"-y", "-f", "lavfi", "-i", "testsrc2=s=320x180:r=25:d=20", "-f", "lavfi", "-i",
                        "sine=frequency=330:sample_rate=48000:d=20", "-c:v", "libx264", "-preset", "ultrafast",
                        "-pix_fmt", "yuv420p", "-c:a", "aac", "-shortest", longClip},
                       &err),
             qPrintable(err));
    BuildRequest req = requestFor(PresetManager::instance().find("sys/h264/Match Source - Medium bitrate")->settings,
                                  m_dir.filePath("many cuts.mp4"));
    req.inputs = {longClip};
    req.source = MediaInfo::probe(AppSettings::ffprobePath(), longClip);
    req.outPoint = -1;
    req.settings.video.speedPreset = "ultrafast";
    for (int i = 0; i < 200; ++i)
        req.keepRanges << qMakePair(i * 0.08, i * 0.08 + 0.04);
    QVERIFY(qAbs(CommandBuilder::outputDuration(req) - 8.0) < 0.01);
    const BuildResult r = CommandBuilder::build(req);
    QVERIFY2(r.ok(), qPrintable(r.error));
    QVERIFY(r.args.contains("-/filter_complex"));
    QVERIFY(r.args.contains("-/af"));
    QVERIFY(r.args.join(' ').size() < 8000);
    QVERIFY2(runFfmpeg(r.args, &err), qPrintable(r.args.join(' ') + "\n" + err));
    for (const QString &f : r.tempFiles)
        QFile::remove(f);
    const double v =
        ffprobe({"-select_streams", "v:0", "-show_entries", "stream=duration", "-of", "csv=p=0", req.outputPath})
            .toDouble();
    const double a =
        ffprobe({"-select_streams", "a:0", "-show_entries", "stream=duration", "-of", "csv=p=0", req.outputPath})
            .toDouble();
    QVERIFY2(qAbs(v - 8.0) < 0.1 && qAbs(a - 8.0) < 0.1, qPrintable(QString("video %1, audio %2").arg(v).arg(a)));
}

void CoreTests::captionHelpers()
{
    const QString srt = m_dir.filePath("raw.srt");
    {
        QFile f(srt);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("1\n00:00:00,000 --> 00:00:01,250\n [BLANK_AUDIO]\n\n2\n00:00:01,250 --> 00:00:04,000\n"
                " Hello {there}, world\n\n3\n00:01:02,500 --> 00:01:03,000\n Second line\n\n");
    }
    const QList<Whisper::Cue> cues = Whisper::cleanCues(Whisper::parseSrt(srt));
    QCOMPARE(cues.size(), 2);
    QCOMPARE(cues[0].start, 1.25);
    QCOMPARE(cues[0].text, QString("Hello {there}, world"));
    QCOMPARE(cues[1].start, 62.5);

    const QString clean = m_dir.filePath("clean.srt");
    QVERIFY(Whisper::writeSrt(cues, clean));
    QCOMPARE(Whisper::parseSrt(clean).size(), 2);

    const QString ass = m_dir.filePath("captions.ass");
    QVERIFY(Whisper::writeAss(cues, ass, QSize(1080, 1920), 56));
    QFile f(ass);
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QString text = QString::fromUtf8(f.readAll());
    QVERIFY(text.contains("PlayResX: 1080") && text.contains("PlayResY: 1920"));
    QVERIFY(text.contains("Dialogue: 0,0:00:01.25,0:00:04.00,Default,,0,0,0,,Hello (there), world"));

    // Burning the ASS file and embedding the SRT both produce valid commands and files.
    ExportSettings s;
    s.setFormat("h264");
    s.effects.autoCaptions = true;
    s.effects.captionBurn = true;
    s.effects.captionEmbed = true;
    BuildRequest req = requestFor(s, m_dir.filePath("caption tracks.mp4"));
    req.outPoint = 2.0;
    req.captionsAss = ass;
    req.captionsSrt = clean;
    req.captionsLanguage = "en";
    const BuildResult r = CommandBuilder::build(req);
    QVERIFY2(r.ok(), qPrintable(r.error));
    QVERIFY(r.args.contains("mov_text"));
    QVERIFY(r.args.contains("language=eng"));
    QString err;
    QVERIFY2(runFfmpeg(r.args, &err), qPrintable(r.args.join(' ') + "\n" + err));
    QCOMPARE(ffprobe({"-select_streams", "s", "-show_entries", "stream=codec_name", "-of", "csv=p=0", req.outputPath}),
             QString("mov_text"));
    QVERIFY(Formats::videoCodec("libx264")->rateControls.contains("size"));
    QVERIFY(Formats::videoCodec("libx264")->rateControls.contains("vmaf"));
    QVERIFY(CommandBuilder::subtitleCodecFor(s) == "mov_text");
    s.setFormat("mxf");
    QVERIFY(CommandBuilder::subtitleCodecFor(s).isEmpty());
}

void CoreTests::automaticCaptions()
{
    // Needs whisper-cli (bundled by CMake) and a model plus speech sample in third_party/whisper-models
    // (CI downloads ggml-tiny-q5_1.bin and whisper.cpp's jfk.wav there).
    const QDir data(QStringLiteral(OME_TEST_DATA_DIR));
    const QString model = data.filePath("ggml-tiny-q5_1.bin");
    const QString speech = data.filePath("jfk.wav");
    if (AppSettings::whisperCliPath().isEmpty())
        SKIP_OR_FAIL("whisper-cli not bundled (run scripts/fetch-whisper)");
    if (!QFileInfo::exists(model) || !QFileInfo::exists(speech))
        SKIP_OR_FAIL("No test speech model in third_party/whisper-models");
    qputenv("OME_WHISPER_MODEL", QFile::encodeName(model));

    const QString clip = m_dir.filePath("speech clip.mp4");
    QString err;
    QVERIFY2(runFfmpeg({"-y", "-f", "lavfi", "-i", "testsrc2=s=1280x720:r=25:d=11", "-i", speech, "-c:v", "libx264",
                        "-pix_fmt", "yuv420p", "-c:a", "aac", "-shortest", clip},
                       &err),
             qPrintable(err));
    const MediaInfo info = MediaInfo::probe(AppSettings::ffprobePath(), clip);

    ExportSettings s;
    s.setFormat("h264");
    s.video.width = 1080;
    s.video.height = 1920;
    s.video.scaleMode = "blur";
    s.video.speedPreset = "veryfast";
    s.effects.autoCaptions = true;
    s.effects.captionSaveSrt = true;
    s.effects.captionEmbed = true;
    s.effects.captionBurn = true;
    QueueModel model2;
    const QString sid = model2.addSource({clip});
    model2.setSourceInfo(sid, info, 0);
    QDir().mkpath(m_dir.filePath("captions"));
    const QString oid = model2.addOutput(sid, s, {}, m_dir.filePath("captions"));
    QVERIFY(runQueue(model2, 600000));
    qunsetenv("OME_WHISPER_MODEL");
    const OutputItem *o = model2.output(oid);
    QVERIFY2(o->status == OutputStatus::Done, qPrintable(o->message));

    const QFileInfo out(o->finalPath);
    const QString srt = out.dir().filePath(out.completeBaseName() + ".srt");
    const QList<Whisper::Cue> cues = Whisper::parseSrt(srt);
    QVERIFY2(!cues.isEmpty(), qPrintable(srt));
    QString all;
    for (const auto &c : cues)
        all += c.text + ' ';
    QVERIFY2(all.contains("country", Qt::CaseInsensitive), qPrintable(all));
    QCOMPARE(ffprobe({"-select_streams", "s", "-show_entries", "stream=codec_name", "-of", "csv=p=0", o->finalPath}),
             QString("mov_text"));
    QVERIFY2(o->message.contains("Captions"), qPrintable(o->message));
}

void CoreTests::fillerWordHelpers()
{
    QCOMPARE(Whisper::dtwPreset("C:/m/ggml-small-q8_0.bin"), QString("small"));
    QCOMPARE(Whisper::dtwPreset("/m/ggml-large-v3-turbo-q5_0.bin"), QString("large.v3.turbo"));
    QCOMPARE(Whisper::dtwPreset("/m/ggml-base.en.bin"), QString("base.en"));
    QCOMPARE(Whisper::dtwPreset("/m/custom.bin"), QString());
    QCOMPARE(Whisper::fillerWords("um, Uh;  ERM hmm"), QStringList({"um", "uh", "erm", "hmm"}));

    // whisper-cli -ojf output: special tokens skipped, punctuation dropped, sub-word tokens joined.
    const QString json = m_dir.filePath("words.json");
    {
        QFile f(json);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(R"({"transcription": [{"tokens": [
            {"text": "[_BEG_]", "offsets": {"from": 0, "to": 0}, "t_dtw": -1},
            {"text": " Hm", "offsets": {"from": 12680, "to": 13910}, "t_dtw": 1306},
            {"text": ",", "offsets": {"from": 14860, "to": 16010}, "t_dtw": 1358},
            {"text": " let", "offsets": {"from": 16010, "to": 18560}, "t_dtw": 1380},
            {"text": " encod", "offsets": {"from": 18600, "to": 18700}, "t_dtw": 1400},
            {"text": "ing", "offsets": {"from": 18700, "to": 18800}, "t_dtw": 1420},
            {"text": " Uh", "offsets": {"from": 19000, "to": 19500}, "t_dtw": -1}]}]})");
    }
    const QList<Whisper::Word> words = Whisper::parseWords(json);
    QCOMPARE(words.size(), 4);
    QCOMPARE(words[0].text, QString("Hm"));
    QCOMPARE(words[0].time, 13.06);
    QCOMPARE(words[2].text, QString("encoding"));
    QCOMPARE(words[2].time, 14.2);
    QCOMPARE(words[3].time, 19.5); // no DTW timestamp: the token's end

    // Sounds between gaps: "welcome" 0-1.4 | "um" 2.3-2.5 | "today we" 3.0-4.2 | "is uh" 4.6-4.8 | "a file" 5.2-6.0
    const TimeRanges gaps = {{1.4, 2.3}, {2.5, 3.0}, {4.2, 4.6}, {4.8, 5.2}};
    const QList<Whisper::Word> spoken = {{"Welcome", 1.2}, {"Um", 2.6},  {"today", 3.3}, {"we", 4.3},
                                         {"is", 4.65},     {"uh", 4.85}, {"a", 5.4},     {"file", 6.0}};
    int count = 0;
    const TimeRanges cuts =
        Whisper::fillerCuts(spoken, gaps, 6.0, 0.2, Whisper::fillerWords(Whisper::defaultFillerWords()), &count);
    // Only the "um" standing on its own is cut; "uh" shares its sound with "is" and stays.
    QCOMPARE(count, 1);
    QCOMPARE(cuts.size(), 1);
    QVERIFY(qAbs(cuts[0].first - 1.6) < 1e-6);  // 0.2 s of the pause before it is kept
    QVERIFY(qAbs(cuts[0].second - 2.8) < 1e-6); // and 0.2 s of the pause after it
    // A long sound with only a filler in it means words were missed: never cut.
    QCOMPARE(Whisper::fillerCuts({{"um", 3.0}}, {{0.0, 1.0}}, 4.0, 0.2, {"um"}).size(), 0);

    const TimeRanges kept = Whisper::subtractRanges({{0.0, 6.0}}, cuts);
    QCOMPARE(kept.size(), 2);
    QVERIFY(qAbs(kept[0].second - 1.6) < 1e-6 && qAbs(kept[1].first - 2.8) < 1e-6);
}

void CoreTests::removeFillerWords()
{
    const QDir data(QStringLiteral(OME_TEST_DATA_DIR));
    const QString model = data.filePath("ggml-tiny-q5_1.bin");
    if (AppSettings::whisperCliPath().isEmpty())
        SKIP_OR_FAIL("whisper-cli not bundled (run scripts/fetch-whisper)");
    if (!QFileInfo::exists(model))
        SKIP_OR_FAIL("No test speech model in third_party/whisper-models");
    qputenv("OME_WHISPER_MODEL", QFile::encodeName(model));

    // Narration with "um", "uh", "hm" and "er" said on their own.
    const QString speech = QStringLiteral(OME_TESTS_DIR) + QStringLiteral("/filler-narration.opus");
    const QString clip = m_dir.filePath("narration.mp4");
    QString err;
    QVERIFY2(runFfmpeg({"-y", "-f", "lavfi", "-i", "testsrc2=s=640x360:r=25:d=19", "-i", speech, "-c:v", "libx264",
                        "-preset", "ultrafast", "-pix_fmt", "yuv420p", "-c:a", "aac", "-shortest", clip},
                       &err),
             qPrintable(err));
    const MediaInfo info = MediaInfo::probe(AppSettings::ffprobePath(), clip);

    ExportSettings s;
    s.setFormat("h264");
    s.video.speedPreset = "ultrafast";
    s.effects.removeSilence = true;
    s.effects.silenceMinDuration = 0.5;
    QueueModel model2;
    const QString sid = model2.addSource({clip});
    model2.setSourceInfo(sid, info, 0);
    QDir().mkpath(m_dir.filePath("fillers"));
    const QString pausesOnly = model2.addOutput(sid, s, {}, m_dir.filePath("fillers"));
    s.effects.removeFillers = true;
    const QString withFillers = model2.addOutput(sid, s, {}, m_dir.filePath("fillers"));
    QVERIFY(runQueue(model2, 600000));
    qunsetenv("OME_WHISPER_MODEL");

    const OutputItem *a = model2.output(pausesOnly);
    const OutputItem *b = model2.output(withFillers);
    QVERIFY2(a->status == OutputStatus::Done, qPrintable(a->message));
    QVERIFY2(b->status == OutputStatus::Done, qPrintable(b->message));
    static const QRegularExpression countRe(QStringLiteral("and ([0-9]+) filler word"));
    const auto m = countRe.match(b->message);
    QVERIFY2(m.hasMatch() && m.captured(1).toInt() >= 3, qPrintable(b->message));
    const double da = MediaInfo::probe(AppSettings::ffprobePath(), a->finalPath).duration;
    const double db = MediaInfo::probe(AppSettings::ffprobePath(), b->finalPath).duration;
    QVERIFY2(db < da - 0.5, qPrintable(QString("pauses only %1 s, with fillers %2 s").arg(da).arg(db)));
}

void CoreTests::testEncodeCompare()
{
    ExportSettings s = PresetManager::instance().find("sys/h264/Match Source - Medium bitrate")->settings;
    BuildRequest full = requestFor(s, {});
    full.outPoint = -1;
    TestEncode test(full, 0.5, 1.0, AppSettings::ffmpegPath());
    QSignalSpy done(&test, &TestEncode::finished);
    test.start();
    QVERIFY(done.count() || done.wait(180000));
    QVERIFY2(done.first().at(0).toBool(), qPrintable(done.first().at(1).toString()));
    const TestEncodeResult &res = test.result();
    QVERIFY(res.bytes > 0);
    QVERIFY(res.mbps > 0);
    QVERIFY(res.projected > res.bytes);
    QVERIFY(QFileInfo::exists(res.file));
    if (QualityProbe::vmafAvailable())
        QVERIFY2(res.vmaf > 50 && res.vmaf <= 100.0001, qPrintable(QString::number(res.vmaf)));
}

void CoreTests::hardwareEncoderCommands()
{
    // VA-API (Linux) and VideoToolbox (macOS) can't run on every CI machine, so check the commands.
    EncoderCaps::instance().setAvailable({"h264_vaapi", "hevc_vaapi", "h264_videotoolbox", "hevc_videotoolbox", "aac"},
                                         {"scale", "format", "hwupload", "fps", "setsar"});
    auto argsFor = [&](const QString &format, const QString &codec, const QString &rc, const QString &profile) {
        ExportSettings s;
        s.setFormat(format);
        s.video.codec = codec;
        s.applyVideoCodecDefaults();
        s.video.rateControl = rc;
        if (!profile.isEmpty())
            s.video.profile = profile;
        s.audio.codec = "aac";
        const BuildResult r = CommandBuilder::build(requestFor(s, m_dir.filePath("hw.mp4")));
        return r.ok() ? r.args : QStringList{"ERROR: " + r.error};
    };

    QStringList a = argsFor("h264", "h264_vaapi", "vbr1", {});
    QVERIFY2(a.contains("-vaapi_device"), qPrintable(a.join(' ')));
    QVERIFY(a.indexOf("-vaapi_device") < a.indexOf("-i")); // a global option, before the input
    QVERIFY2(a.at(a.indexOf("-filter_complex") + 1).endsWith("format=nv12,hwupload[vout]"), qPrintable(a.join(' ')));
    QVERIFY(!a.contains("-pix_fmt"));
    QCOMPARE(a.at(a.indexOf("-rc_mode") + 1), QString("VBR"));
    QVERIFY(a.contains("-maxrate"));

    a = argsFor("hevc", "hevc_vaapi", "quality", "main10");
    QVERIFY2(a.at(a.indexOf("-filter_complex") + 1).endsWith("format=p010le,hwupload[vout]"), qPrintable(a.join(' ')));
    QCOMPARE(a.at(a.indexOf("-rc_mode") + 1), QString("CQP"));
    QCOMPARE(a.at(a.indexOf("-profile:v") + 1), QString("main10"));

    a = argsFor("hevc", "hevc_videotoolbox", "quality", {});
    QVERIFY(!a.contains("-vaapi_device"));
    QCOMPARE(a.at(a.indexOf("-pix_fmt") + 1), QString("nv12"));
    QCOMPARE(a.at(a.indexOf("-q:v") + 1), QString("60"));
    QCOMPARE(a.at(a.indexOf("-tag:v") + 1), QString("hvc1"));

    a = argsFor("h264", "h264_videotoolbox", "cbr", {});
    QCOMPARE(a.at(a.indexOf("-b:v") + 1), a.at(a.indexOf("-maxrate") + 1));
    QCOMPARE(a.at(a.indexOf("-profile:v") + 1), QString("high"));

    // With GPU presets on, software H.264/HEVC presets move to a working GPU encoder.
    EncoderCaps::instance().setAvailable({"libx264", "libx265", "h264_nvenc", "hevc_nvenc", "aac"}, {"scale"});
    ExportSettings sw;
    sw.setFormat("h264");
    sw.video.codec = "libx264";
    sw.applyVideoCodecDefaults();
    sw.video.rateControl = "vbr2";
    ExportSettings hw = PresetManager::onGpu(sw);
    QCOMPARE(hw.video.codec, QString("h264_nvenc"));
    QCOMPARE(hw.video.rateControl, QString("vbr2")); // NVENC has two-pass (multipass) too
    QCOMPARE(hw.video.speedPreset, Formats::videoCodec("h264_nvenc")->defaultSpeed);
    sw.video.profile = "high10"; // 10-bit H.264: no GPU encoder takes it
    QCOMPARE(PresetManager::onGpu(sw).video.codec, QString("libx264"));
    ExportSettings sw10;
    sw10.setFormat("hevc");
    sw10.video.codec = "libx265";
    sw10.applyVideoCodecDefaults();
    sw10.video.profile = "main10";
    hw = PresetManager::onGpu(sw10);
    QCOMPARE(hw.video.codec, QString("hevc_nvenc"));
    QCOMPARE(hw.video.profile, QString("main10"));
    QCOMPARE(hw.video.pixelFormat, QString("p010le"));
    PresetManager::instance().setPreferHardware(true);
    PresetManager::instance().load();
    QCOMPARE(PresetManager::instance().find("sys/h264/YouTube 1080p HD")->settings.video.codec, QString("h264_nvenc"));
    PresetManager::instance().setPreferHardware(false);

    // Put the real encoder list back.
    EncoderCaps::instance().probe(AppSettings::ffmpegPath());
    EncoderCaps::instance().validateHardware(AppSettings::ffmpegPath());
    PresetManager::instance().load();
    QVERIFY(EncoderCaps::instance().hasEncoder("libx264"));
}

QTEST_GUILESS_MAIN(CoreTests)

#include "tst_core.moc"
