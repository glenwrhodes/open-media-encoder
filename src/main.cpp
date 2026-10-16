#include "core/AppSettings.h"
#include "core/EncoderCaps.h"
#include "core/EncoderEngine.h"
#include "core/Formats.h"
#include "core/ImageSequence.h"
#include "core/MediaInfo.h"
#include "core/PresetManager.h"
#include "core/QueueModel.h"
#include "core/StreamRecorder.h"
#include "core/YtDlp.h"
#include "ui/MainWindow.h"
#include "ui/Theme.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QPainter>
#include <QPainterPath>
#include <QTextStream>
#include <QTimer>
#include <QtConcurrent>
#include <atomic>
#include <cstdio>

#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <csignal>
#endif

namespace {

QIcon appIcon()
{
    QIcon icon;
    for (int s : {16, 24, 32, 48, 64, 128, 256}) {
        QPixmap pm(s, s);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        QLinearGradient g(0, 0, s, s);
        g.setColorAt(0, QColor(0x1f, 0x3a, 0x6b));
        g.setColorAt(1, QColor(0x0b, 0x16, 0x2e));
        p.setBrush(g);
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(QRectF(0, 0, s, s), s * 0.2, s * 0.2);
        QPainterPath tri;
        tri.moveTo(s * 0.36, s * 0.26);
        tri.lineTo(s * 0.76, s * 0.5);
        tri.lineTo(s * 0.36, s * 0.74);
        tri.closeSubpath();
        p.setBrush(Theme::accent().lighter(130));
        p.drawPath(tri);
        p.setPen(QPen(QColor(0x9f, 0xd0, 0xff), qMax(1.0, s * 0.05)));
        p.drawLine(QPointF(s * 0.2, s * 0.3), QPointF(s * 0.2, s * 0.7));
        p.end();
        icon.addPixmap(pm);
    }
    return icon;
}

void attachConsole()
{
#ifdef Q_OS_WIN
    // Output already redirected to a file/pipe: keep the inherited handles.
    const HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (h != nullptr && h != INVALID_HANDLE_VALUE && GetFileType(h) != FILE_TYPE_UNKNOWN)
        return;
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        FILE *f = nullptr;
        freopen_s(&f, "CONOUT$", "w", stdout);
        freopen_s(&f, "CONOUT$", "w", stderr);
    }
#endif
}

const Preset *findPreset(const QString &key)
{
    PresetManager &pm = PresetManager::instance();
    if (const Preset *p = pm.find(key))
        return p;
    // "Format/Preset Name" or just "Preset Name".
    const QString fmt = key.section('/', 0, 0);
    const QString name = key.section('/', 1);
    for (const Preset &p : pm.presets()) {
        const FormatDef *f = Formats::format(p.settings.format);
        if (!name.isEmpty() &&
            (p.settings.format.compare(fmt, Qt::CaseInsensitive) == 0 ||
             (f && f->displayName.compare(fmt, Qt::CaseInsensitive) == 0)) &&
            p.name().compare(name, Qt::CaseInsensitive) == 0)
            return &p;
    }
    for (const Preset &p : pm.presets())
        if (p.name().compare(key, Qt::CaseInsensitive) == 0)
            return &p;
    return nullptr;
}

int runCli(QApplication &app, const QCommandLineParser &parser)
{
    attachConsole();
    QTextStream out(stdout);
    QTextStream err(stderr);

    if (parser.isSet("list-presets")) {
        for (const Preset &p : PresetManager::instance().presets())
            out << p.id << "\n";
        out.flush();
        return 0;
    }

    const QStringList inputs = parser.positionalArguments();
    if (inputs.isEmpty()) {
        err << "No input files given.\n";
        return 2;
    }
    const Preset *preset = findPreset(parser.value("preset"));
    if (!preset) {
        err << "Unknown preset: " << parser.value("preset") << " (use --list-presets)\n";
        return 2;
    }

    QueueModel model;
    EncoderEngine engine(&model);
    const QString outArg = parser.value("output");
    const bool stitch = parser.isSet("stitch");
    QList<QStringList> sources;
    if (stitch)
        sources << inputs;
    else
        for (const QString &i : inputs)
            sources << QStringList{QFileInfo(i).absoluteFilePath()};

    const bool asSequence = parser.isSet("sequence");
    const double sequenceFps = parser.isSet("fps") ? parser.value("fps").toDouble() : 24.0;
    for (const QStringList &files : sources) {
        ImageSequence seq;
        if (asSequence) {
            seq = ImageSequence::detect(files.first());
            if (!seq.valid) {
                err << files.first() << ": not part of a numbered image sequence\n";
                return 2;
            }
            seq.fps = sequenceFps > 0 ? sequenceFps : 24.0;
        }
        MediaInfo info = MediaInfo::probe(AppSettings::ffprobePath(), seq.valid ? seq.pattern : files.first(), 20000,
                                          seq.valid ? seq.inputArgs() : QStringList());
        if (!info.valid) {
            err << files.first() << ": " << info.error << "\n";
            return 1;
        }
        double total = 0;
        if (files.size() > 1) {
            total = info.duration;
            for (int i = 1; i < files.size(); ++i)
                total += MediaInfo::probe(AppSettings::ffprobePath(), files[i]).duration;
        }
        const QString sid = seq.valid ? model.addImageSequence(seq) : model.addSource(files);
        model.setSourceInfo(sid, info, total);
        QString dir;
        if (!outArg.isEmpty() && (sources.size() > 1 || QFileInfo(outArg).isDir()))
            dir = outArg;
        const QString oid = model.addOutput(sid, preset->settings, preset->id, dir);
        if (!outArg.isEmpty() && dir.isEmpty())
            model.setOutputPath(oid, QFileInfo(outArg).absoluteFilePath());
        const double in = parser.isSet("in") ? parser.value("in").toDouble() : -1;
        const double outPt = parser.isSet("out") ? parser.value("out").toDouble() : -1;
        model.setOutputRange(oid, in, outPt);
    }

    int lastPct = -1;
    QObject::connect(&engine, &EncoderEngine::taskStarted, [&](const QString &, const QString &oid) {
        out << "Encoding -> " << QDir::toNativeSeparators(model.output(oid)->finalPath) << "\n";
        out.flush();
        lastPct = -1;
    });
    QObject::connect(&engine, &EncoderEngine::taskProgress, [&](const QString &, const EncodeStats &st) {
        const int pct = int(st.progress * 100);
        if (pct != lastPct) {
            lastPct = pct;
            out << "\r  " << pct << "%  " << QString::number(st.speed, 'f', 2) << "x   ";
            out.flush();
        }
    });
    QObject::connect(
        &engine, &EncoderEngine::taskFinished, [&](const QString &, const QString &, bool ok, const QString &msg) {
            out << "\r  " << (ok ? QStringLiteral("done") : QStringLiteral("FAILED: ") + msg) << "            \n";
            out.flush();
        });
    int exitCode = 0;
    QObject::connect(&engine, &EncoderEngine::queueFinished, [&](int, int failed) {
        exitCode = failed ? 1 : 0;
        app.quit();
    });
    QMetaObject::invokeMethod(&engine, &EncoderEngine::start, Qt::QueuedConnection);
    app.exec();
    return exitCode;
}

std::atomic<StreamRecording *> g_cliRecording{nullptr};

#ifndef Q_OS_WIN
std::atomic<bool> g_stopSignal{false};
void onStopSignal(int) { g_stopSignal = true; }
#endif

#ifdef Q_OS_WIN
BOOL WINAPI onConsoleCtrl(DWORD type)
{
    if (type != CTRL_C_EVENT && type != CTRL_BREAK_EVENT && type != CTRL_CLOSE_EVENT)
        return FALSE;
    if (StreamRecording *r = g_cliRecording.load())
        QMetaObject::invokeMethod(r, &StreamRecording::stop, Qt::QueuedConnection);
    return TRUE;
}
#endif

int runRecordCli(QApplication &app, const QCommandLineParser &parser)
{
    attachConsole();
    QTextStream out(stdout);
    QTextStream err(stderr);

    RecordingOptions opt;
    opt.url = parser.value("record").trimmed();
    if (!StreamLink::isStreamUrl(opt.url)) {
        err << "Not a stream or web page link: " << opt.url << "\n";
        return 2;
    }
    opt.container = parser.isSet("format") ? parser.value("format").toLower() : QStringLiteral("mkv");
    if (opt.container == "mpegts")
        opt.container = "ts";
    opt.outputDir = AppSettings::recordingDir();
    const QString o = parser.value("output");
    if (!o.isEmpty()) {
        const QFileInfo fi(o);
        const QString ext = fi.suffix().toLower();
        if (fi.isDir() || ext.isEmpty()) {
            opt.outputDir = fi.absoluteFilePath();
        } else {
            opt.outputDir = fi.absolutePath();
            opt.name = fi.completeBaseName();
            if (!parser.isSet("format") && (ext == "mp4" || ext == "mkv" || ext == "ts"))
                opt.container = ext;
        }
    }
    if (opt.container != "mkv" && opt.container != "mp4" && opt.container != "ts") {
        err << "Unknown format: " << opt.container << " (use mkv, mp4 or ts)\n";
        return 2;
    }
    opt.maxSeconds = parser.isSet("duration") ? qMax(0, int(parser.value("duration").toDouble())) : 0;
    opt.reconnect = AppSettings::reconnectRecordings();

    if (!StreamLink::isDirectMedia(opt.url) && AppSettings::ytDlpPath().isEmpty()) {
        out << "Downloading yt-dlp to find the stream on this page...\n";
        out.flush();
        YtDlpInstaller installer;
        QEventLoop loop;
        bool ok = false;
        QString error;
        QObject::connect(&installer, &YtDlpInstaller::finished, &loop, [&](bool success, const QString &e) {
            ok = success;
            error = e;
            loop.quit();
        });
        installer.start();
        loop.exec();
        if (!ok) {
            err << "Couldn't set up yt-dlp: " << error << "\n";
            return 1;
        }
    }

    StreamRecorder recorder;
    QString lastState;
    StreamRecording *rec = nullptr;
    QObject::connect(&recorder, &StreamRecorder::recordingAdded, [&](StreamRecording *r) {
        rec = r;
        g_cliRecording = r;
    });
    QObject::connect(&recorder, &StreamRecorder::recordingChanged, [&](StreamRecording *r) {
        const QString state = r->stateText();
        if (r->state() == StreamRecording::State::Recording) {
            out << "\r  " << state << "  " << formatDuration(r->recordedSeconds()) << "  " << formatBytes(r->bytes())
                << "      ";
        } else if (state != lastState) {
            out << "\n" << state;
        }
        lastState = state;
        out.flush();
    });
    QObject::connect(&recorder, &StreamRecorder::recordingFinished, &app, [&](StreamRecording *) { app.quit(); });
#ifdef Q_OS_WIN
    SetConsoleCtrlHandler(onConsoleCtrl, TRUE);
#else
    // Signal handlers may only set a flag; the event loop picks it up.
    std::signal(SIGINT, onStopSignal);
    std::signal(SIGTERM, onStopSignal);
    std::signal(SIGHUP, onStopSignal);
    QTimer stopPoll;
    QObject::connect(&stopPoll, &QTimer::timeout, [&] {
        if (g_stopSignal.exchange(false) && rec)
            rec->stop();
    });
    stopPoll.start(200);
#endif
    out << "Recording " << opt.url << " (Ctrl+C to stop)\n";
    out.flush();
    recorder.record(opt);
    if (rec && rec->isActive())
        app.exec();
    g_cliRecording = nullptr;
    out << "\n";
    if (!rec || rec->state() != StreamRecording::State::Done) {
        err << "Recording failed: " << (rec ? rec->error() : QStringLiteral("unknown error")) << "\n";
        return 1;
    }
    out << "Saved " << formatDuration(rec->recordedSeconds()) << ":\n";
    for (const QString &f : rec->files())
        out << "  " << QDir::toNativeSeparators(f) << "\n";
    if (!rec->note().isEmpty())
        out << rec->note() << "\n";
    return 0;
}

} // namespace

int main(int argc, char *argv[])
{
#if !defined(Q_OS_WIN) && !defined(Q_OS_MACOS)
    // Command-line runs on a server without a display: use Qt's offscreen platform instead of failing.
    if (qEnvironmentVariableIsEmpty("DISPLAY") && qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY") &&
        qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        for (int i = 1; i < argc; ++i) {
            const QByteArray a(argv[i]);
            if (a.startsWith("--preset") || a == "-p" || a.startsWith("--record") || a == "--list-presets" ||
                a == "--help" || a == "-h" || a == "--version" || a == "-v") {
                qputenv("QT_QPA_PLATFORM", "offscreen");
                break;
            }
        }
    }
#endif
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("OpenMediaEncoder"));
    QApplication::setApplicationDisplayName(QStringLiteral("Open Media Encoder"));
    QApplication::setOrganizationName(QStringLiteral("OpenMediaEncoder"));
    QApplication::setApplicationVersion(QStringLiteral(OME_VERSION));
    app.setWindowIcon(appIcon());

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Open Media Encoder"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption(
        {{"p", "preset"}, QStringLiteral("Encode headless with this preset id or name, then exit."), "preset"});
    parser.addOption({{"o", "output"}, QStringLiteral("Output file (single input) or folder."), "path"});
    parser.addOption({"in", QStringLiteral("Source in point in seconds."), "seconds"});
    parser.addOption({"out", QStringLiteral("Source out point in seconds."), "seconds"});
    parser.addOption({"stitch", QStringLiteral("Stitch all inputs together into one output.")});
    parser.addOption({"sequence", QStringLiteral("Treat each input image as a frame of a numbered image sequence.")});
    parser.addOption({"fps", QStringLiteral("Frame rate for --sequence inputs (default 24)."), "rate"});
    parser.addOption({"list-presets", QStringLiteral("List available preset ids and exit.")});
    parser.addOption({"record",
                      QStringLiteral("Record a live stream or web page (YouTube, Twitch...) until it ends, the "
                                     "--duration passes or Ctrl+C is pressed. -o sets the file or folder."),
                      "link"});
    parser.addOption({"duration", QStringLiteral("Stop --record after this many seconds."), "seconds"});
    parser.addOption({"format", QStringLiteral("File format for --record: mkv (default), mp4 or ts."), "format"});
    parser.addPositionalArgument(QStringLiteral("files"), QStringLiteral("Media files to add to the queue."),
                                 "[files...]");
    parser.process(app);

    EncoderCaps::instance().probe(AppSettings::ffmpegPath());
    PresetManager::instance().load();

    if (parser.isSet("record"))
        return runRecordCli(app, parser);

    if (parser.isSet("preset") || parser.isSet("list-presets")) {
        EncoderCaps::instance().validateHardware(AppSettings::ffmpegPath());
        PresetManager::instance().load();
        return runCli(app, parser);
    }

    Theme::apply(app);
    MainWindow w;
    w.show();
    w.addFiles(parser.positionalArguments());

    // Verify hardware encoders in the background, then refresh presets that depend on them.
    auto *watcher = new QFutureWatcher<void>(&w);
    QObject::connect(watcher, &QFutureWatcher<void>::finished, &w, [watcher] {
        PresetManager::instance().load();
        watcher->deleteLater();
    });
    watcher->setFuture(QtConcurrent::run([] { EncoderCaps::instance().validateHardware(AppSettings::ffmpegPath()); }));

    return app.exec();
}
