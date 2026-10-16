#include "core/AppSettings.h"
#include "core/CommandBuilder.h"
#include "core/EncoderCaps.h"
#include "core/EncoderEngine.h"
#include "core/Formats.h"
#include "core/MediaInfo.h"
#include "core/PresetManager.h"
#include "core/QueueModel.h"
#include "core/WatchFolderManager.h"

#include <QColor>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QPainter>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

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

private:
    bool runFfmpeg(const QStringList &args, QString *err = nullptr, int timeoutMs = 180000);
    BuildRequest requestFor(const ExportSettings &s, const QString &out);

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

QTEST_GUILESS_MAIN(CoreTests)
#include "tst_core.moc"
