#include "StreamRecorder.h"

#include "AppSettings.h"
#include "CommandBuilder.h"
#include "EncoderEngine.h"
#include "MediaInfo.h"
#include "YtDlp.h"

#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QUrl>

#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace {

constexpr int MaxReconnectAttempts = 5;

const QStringList &directSchemes()
{
    static const QStringList s{"rtmp", "rtmps", "rtmpt", "rtmpe", "rtsp", "rtsps", "srt",    "udp",
                               "rtp",  "tcp",   "rist",  "mms",   "mmsh", "mmst",  "icecast"};
    return s;
}

const QStringList &mediaSuffixes()
{
    static const QStringList s{"m3u8", "m3u", "mpd", "ts",  "mts", "m2ts", "flv",  "mp4", "m4v", "mkv", "webm",
                               "mov",  "aac", "mp3", "ogg", "oga", "opus", "flac", "wav", "m4a", "f4m"};
    return s;
}

bool isHttp(const QString &scheme) { return scheme == "http" || scheme == "https"; }

QString quoteArg(const QString &a)
{
    if (a.isEmpty())
        return QStringLiteral("\"\"");
    if (a.contains(' ') || a.contains('&') || a.contains('?') || a.contains('"') || a.contains('\r'))
        return QStringLiteral("\"%1\"").arg(QString(a).replace('"', QStringLiteral("\\\"")).replace("\r\n", "\\r\\n"));
    return a;
}

} // namespace

// ================================================================= StreamLink

namespace StreamLink {

bool isStreamUrl(const QString &text)
{
    const QUrl u(text.trimmed(), QUrl::StrictMode);
    if (!u.isValid())
        return false;
    const QString scheme = u.scheme().toLower();
    if (!isHttp(scheme) && !directSchemes().contains(scheme))
        return false;
    return !u.host().isEmpty() || u.port() > 0;
}

bool isDirectMedia(const QString &url)
{
    const QUrl u(url.trimmed());
    const QString scheme = u.scheme().toLower();
    if (!isHttp(scheme))
        return true;
    const QString path = u.path().toLower();
    if (path.contains(QStringLiteral(".m3u8")) || path.contains(QStringLiteral(".mpd")))
        return true;
    return mediaSuffixes().contains(QFileInfo(path).suffix());
}

QStringList inputArgs(const QString &url, const HttpHeaders &headers)
{
    const QString scheme = QUrl(url.trimmed()).scheme().toLower();
    QStringList a;
    if (isHttp(scheme)) {
        // Ride out short network hiccups inside ffmpeg before the recording has to start a new part.
        // (Not -reconnect_on_network_error: it also retries the first connection, so a dead link takes a
        // minute to fail.)
        a << "-reconnect" << "1" << "-reconnect_streamed" << "1" << "-reconnect_delay_max" << "30" << "-rw_timeout"
          << "30000000";
        QString extra;
        for (const auto &h : headers) {
            if (h.first.compare(QStringLiteral("User-Agent"), Qt::CaseInsensitive) == 0)
                a << "-user_agent" << h.second;
            else
                extra += h.first + QStringLiteral(": ") + h.second + QStringLiteral("\r\n");
        }
        if (!extra.isEmpty())
            a << "-headers" << extra;
    } else if (scheme == "rtsp" || scheme == "rtsps") {
        a << "-rtsp_transport" << "tcp" << "-timeout" << "30000000";
    } else if (scheme.startsWith(QStringLiteral("rtmp")) || scheme == "tcp") {
        a << "-rw_timeout" << "30000000";
    }
    return a;
}

QString sanitizeFileName(const QString &name)
{
    QString s = name;
    static const QRegularExpression bad(QStringLiteral(R"([<>:"/\\|?*\x00-\x1f])"));
    s.replace(bad, QStringLiteral("_"));
    s = s.simplified();
    while (s.endsWith('.') || s.endsWith(' '))
        s.chop(1);
    if (s.size() > 120)
        s = s.left(120).trimmed();
    return s;
}

QString suggestedName(const QString &url, const QString &title, const QDateTime &when)
{
    QString stem = title.trimmed();
    if (stem.size() > 80)
        stem = stem.left(80).trimmed();
    if (stem.isEmpty()) {
        QString host = QUrl(url.trimmed()).host();
        if (host.startsWith(QStringLiteral("www.")))
            host = host.mid(4);
        stem = host.isEmpty() ? QStringLiteral("Stream") : host;
    }
    QString name = sanitizeFileName(stem + ' ' + when.toString(QStringLiteral("yyyy-MM-dd HH-mm-ss")));
    return name.isEmpty() ? QStringLiteral("Stream") : name;
}

ResolvedStream parseYtDlpJson(const QByteArray &json)
{
    ResolvedStream r;
    // yt-dlp prints one JSON object per line; the first one is the stream asked for.
    QJsonDocument doc = QJsonDocument::fromJson(json);
    if (!doc.isObject())
        doc = QJsonDocument::fromJson(json.trimmed().split('\n').value(0));
    if (!doc.isObject()) {
        r.error = QStringLiteral("yt-dlp did not return any stream information");
        return r;
    }
    const QJsonObject o = doc.object();
    // "title" has the date appended for live streams; "fulltitle" doesn't.
    r.title = o.value("fulltitle").toString();
    if (r.title.isEmpty())
        r.title = o.value("title").toString();
    r.isLive = o.value("is_live").toBool() || o.value("live_status").toString() == QLatin1String("is_live");
    QJsonObject stream = o;
    const QJsonArray parts = o.value("requested_formats").toArray();
    if (!parts.isEmpty()) {
        QJsonObject video, audio;
        for (const QJsonValue &v : parts) {
            const QJsonObject f = v.toObject();
            (f.value("vcodec").toString() == QLatin1String("none") ? audio : video) = f;
        }
        stream = video.isEmpty() ? audio : video;
        if (!video.isEmpty() && !audio.isEmpty() && video.value("acodec").toString() == QLatin1String("none"))
            r.audioUrl = audio.value("url").toString();
    }
    r.url = stream.value("url").toString();
    QJsonObject headers = stream.value("http_headers").toObject();
    if (headers.isEmpty())
        headers = o.value("http_headers").toObject();
    for (auto it = headers.begin(); it != headers.end(); ++it)
        r.headers.append({it.key(), it.value().toString()});
    if (r.url.isEmpty())
        r.error = QStringLiteral("yt-dlp did not find a recordable stream on this page");
    return r;
}

QString friendlyError(const QString &raw)
{
    struct Rule {
        QStringList needles;
        const char *text;
    };
    static const Rule rules[] = {
        {{"404", "Not Found"}, "The stream wasn't found. Check the link; the broadcast may also have ended"},
        {{"403", "Forbidden"}, "The server refused access. The link may have expired, so copy a fresh one"},
        {{"401", "Unauthorized"}, "The server wants a login for this stream"},
        {{"-138", "-10060", "-10061", "timed out", "Connection refused", "Connection timed out"},
         "Couldn't connect. Check the link and your internet connection"},
        {{"-11001", "resolve", "getaddrinfo"}, "Couldn't find that server. Check the link"},
        {{"Invalid data found"}, "That link didn't return a stream that can be recorded"},
    };
    for (const Rule &r : rules)
        for (const QString &n : r.needles)
            if (raw.contains(n, Qt::CaseInsensitive))
                return QStringLiteral("%1 (%2)").arg(QString::fromUtf8(r.text), raw);
    return raw;
}

} // namespace StreamLink

// ================================================================= StreamRecording

StreamRecording::StreamRecording(const QString &id, const RecordingOptions &options, const QString &ffmpeg,
                                 const QString &ytDlp, QObject *parent)
    : QObject(parent)
    , m_id(id)
    , m_options(options)
    , m_ffmpeg(ffmpeg)
    , m_ytDlp(ytDlp)
{
    m_options.url = m_options.url.trimmed();
    if (m_options.container != "mp4" && m_options.container != "ts")
        m_options.container = "mkv";
    m_title = QUrl(m_options.url).host();
    m_retryTimer.setSingleShot(true);
    connect(&m_retryTimer, &QTimer::timeout, this, [this] {
        if (m_viaYtDlp)
            resolve(); // stream links from web pages expire, so look the stream up again
        else
            startPart();
    });
    m_killTimer.setSingleShot(true);
    connect(&m_killTimer, &QTimer::timeout, this, [this] {
        if (m_proc && m_proc->state() != QProcess::NotRunning)
            m_proc->kill();
    });
}

StreamRecording::~StreamRecording()
{
    m_retryTimer.stop();
    if (m_resolver) {
        m_resolver->disconnect(this);
        m_resolver->kill();
        m_resolver->waitForFinished(2000);
    }
    if (m_proc && m_proc->state() != QProcess::NotRunning) {
        m_proc->disconnect(this);
        if (m_state == State::Converting) {
            // The MKV is only deleted after a successful conversion, so nothing is lost.
            m_proc->kill();
            m_proc->waitForFinished(2000);
            QFile::remove(m_proc->arguments().constLast());
        } else {
            m_proc->write("q");
            if (!m_proc->waitForFinished(5000)) {
                m_proc->kill();
                m_proc->waitForFinished(2000);
            }
        }
    }
}

void StreamRecording::prepare(QProcess *p)
{
#ifdef Q_OS_WIN
    // Keep console Ctrl+C away from ffmpeg; recordings are always ended through stop().
    p->setCreateProcessArgumentsModifier(
        [](QProcess::CreateProcessArguments *args) { args->flags |= CREATE_NEW_PROCESS_GROUP; });
#else
    // Its own process group, so Ctrl+C in a terminal reaches only us.
    p->setChildProcessModifier([] { ::setpgid(0, 0); });
#endif
}

void StreamRecording::start()
{
    m_startedAt = QDateTime::currentDateTime();
    if (!StreamLink::isDirectMedia(m_options.url) && !m_ytDlp.isEmpty()) {
        m_viaYtDlp = true;
        resolve();
    } else {
        m_streamUrl = m_options.url;
        startPart();
    }
}

void StreamRecording::resolve()
{
    setState(m_part > 0 ? State::Reconnecting : State::Resolving);
    // Sites change often, so the managed copy of yt-dlp updates itself once a day before use.
    m_updating = YtDlp::updateDue(m_ytDlp);
    m_resolver = new QProcess(this);
    prepare(m_resolver);
    connect(m_resolver, &QProcess::finished, this, [this](int code) {
        if (m_updating)
            onUpdated();
        else
            onResolved(code);
    });
    connect(m_resolver, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) {
            m_resolver->deleteLater();
            m_resolver = nullptr;
            m_error = QStringLiteral("Could not start yt-dlp (%1)").arg(QDir::toNativeSeparators(m_ytDlp));
            finish();
        }
    });
    QProcess *r = m_resolver;
    QTimer::singleShot(m_updating ? 120000 : 90000, r, [r] { r->kill(); });
    if (m_updating) {
        emit changed();
        m_resolver->start(m_ytDlp, {"-U"});
        return;
    }
    m_resolver->start(m_ytDlp, QStringList{"-j", "--no-playlist", "--no-warnings", "-f", "bv*+ba/b"} +
                                   YtDlp::extraArgs() + QStringList{m_options.url});
}

void StreamRecording::onUpdated()
{
    QProcess *r = m_resolver;
    m_resolver = nullptr;
    r->deleteLater();
    m_updating = false;
    YtDlp::markUpdateChecked(); // even if it failed: try again tomorrow rather than before every recording
    if (m_stopRequested) {
        finish();
        return;
    }
    resolve();
}

void StreamRecording::onResolved(int exitCode)
{
    QProcess *r = m_resolver;
    m_resolver = nullptr;
    r->deleteLater();
    if (m_stopRequested) {
        m_endReason = QStringLiteral("stopped");
        finish();
        return;
    }
    const ResolvedStream rs = StreamLink::parseYtDlpJson(r->readAllStandardOutput());
    if (!rs.ok()) {
        QString err = rs.error;
        const QStringList lines = QString::fromUtf8(r->readAllStandardError()).split('\n', Qt::SkipEmptyParts);
        for (auto it = lines.crbegin(); it != lines.crend(); ++it) {
            if (it->startsWith(QStringLiteral("ERROR:"))) {
                err = it->mid(6).trimmed();
                break;
            }
        }
        if (exitCode != 0 && err.isEmpty())
            err = QStringLiteral("yt-dlp exited with code %1").arg(exitCode);
        if (m_part > 0)
            retryOrFinish(err);
        else
            fail(err);
        return;
    }
    m_streamUrl = rs.url;
    m_audioUrl = rs.audioUrl;
    m_headers = rs.headers;
    if (!rs.title.isEmpty())
        m_title = rs.title;
    startPart();
}

QString StreamRecording::recordExtension() const
{
    return m_options.container == "ts" ? QStringLiteral("ts") : QStringLiteral("mkv");
}

QString StreamRecording::partPath(int part) const
{
    const QString path = part <= 1 ? m_base + '.' + recordExtension()
                                   : QStringLiteral("%1 (part %2).%3").arg(m_base).arg(part).arg(recordExtension());
    return CommandBuilder::uniqueOutputPath(path);
}

void StreamRecording::startPart()
{
    if (m_base.isEmpty()) {
        QDir().mkpath(m_options.outputDir);
        const QString name =
            m_options.name.isEmpty()
                ? StreamLink::suggestedName(m_options.url, m_viaYtDlp ? m_title : QString(), m_startedAt)
                : StreamLink::sanitizeFileName(m_options.name);
        const QDir dir(m_options.outputDir);
        m_base = dir.filePath(name);
        for (int n = 2; QFileInfo::exists(m_base + ".mkv") || QFileInfo::exists(m_base + ".mp4") ||
                        QFileInfo::exists(m_base + ".ts");
             ++n)
            m_base = dir.filePath(QStringLiteral("%1 (%2)").arg(name).arg(n));
    }
    ++m_part;
    m_partFile = partPath(m_part);
    m_partSeconds = 0;
    m_partBytes = 0;
    m_bitrate = 0;
    m_stdoutBuf.clear();

    m_limitStopSent = false;

    const bool split = !m_audioUrl.isEmpty();
    QStringList args{"-hide_banner", "-nostats", "-loglevel", "warning", "-y"};
    // Separate picture and sound streams share one clock; keep their original timestamps so they stay in sync.
    if (split)
        args << "-copyts";
    // Live HLS normally starts three segments back and reads that backlog ahead, which with two inputs
    // overshoots time limits by a segment or more; start at the newest segment instead.
    auto liveStart = [split](const QString &url) {
        return split && url.contains(QStringLiteral(".m3u8")) ? QStringList{"-live_start_index", "-1"} : QStringList{};
    };
    args << StreamLink::inputArgs(m_streamUrl, m_headers) << liveStart(m_streamUrl) << "-i" << m_streamUrl;
    if (split)
        args << StreamLink::inputArgs(m_audioUrl, m_headers) << liveStart(m_audioUrl) << "-i" << m_audioUrl << "-map"
             << "0:v:0" << "-map" << "1:a:0" << "-avoid_negative_ts" << "make_zero";
    args << "-c" << "copy";
    // -t doesn't work with -copyts, so split streams are stopped from onReadyStdout() instead.
    if (m_options.maxSeconds > 0 && !split)
        args << "-t" << QString::number(qMax(1.0, m_options.maxSeconds - m_prevSeconds), 'f', 3);
    if (recordExtension() == "ts")
        args << "-f" << "mpegts";
    args << "-progress" << "pipe:1" << m_partFile;
    m_command = quoteArg(m_ffmpeg);
    for (const QString &a : std::as_const(args))
        m_command += ' ' + quoteArg(a);

    m_proc = new QProcess(this);
    prepare(m_proc);
    connect(m_proc, &QProcess::readyReadStandardOutput, this, &StreamRecording::onReadyStdout);
    connect(m_proc, &QProcess::readyReadStandardError, this, &StreamRecording::onReadyStderr);
    connect(m_proc, &QProcess::finished, this, &StreamRecording::onPartFinished);
    connect(m_proc, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) {
            m_proc->deleteLater();
            m_proc = nullptr;
            m_partFile.clear();
            m_error = QStringLiteral("Could not start ffmpeg (%1)").arg(QDir::toNativeSeparators(m_ffmpeg));
            finish();
        }
    });
    setState(State::Connecting);
    m_proc->start(m_ffmpeg, args);
}

void StreamRecording::stop()
{
    if (!isActive() || m_stopRequested)
        return;
    m_stopRequested = true;
    m_endReason = QStringLiteral("stopped");
    switch (m_state) {
    case State::Resolving:
        if (m_resolver)
            m_resolver->kill(); // onResolved() finishes up
        break;
    case State::Reconnecting:
        if (m_resolver) {
            m_resolver->kill();
        } else {
            m_retryTimer.stop();
            finish();
        }
        break;
    case State::Connecting:
        // Nothing written yet, and ffmpeg doesn't read its input until it has connected.
        if (m_proc && m_proc->state() != QProcess::NotRunning)
            m_proc->kill();
        break;
    case State::Recording:
        if (m_proc && m_proc->state() != QProcess::NotRunning) {
            m_proc->write("q"); // lets ffmpeg close the file properly
            m_killTimer.start(10000);
        }
        break;
    default:
        break; // converting: let it complete
    }
    emit changed();
}

void StreamRecording::onReadyStdout()
{
    if (!m_proc)
        return;
    m_stdoutBuf += m_proc->readAllStandardOutput();
    bool tick = false;
    int nl;
    while ((nl = m_stdoutBuf.indexOf('\n')) >= 0) {
        const QByteArray line = m_stdoutBuf.left(nl).trimmed();
        m_stdoutBuf.remove(0, nl + 1);
        const int eq = line.indexOf('=');
        if (eq <= 0)
            continue;
        const QByteArray key = line.left(eq);
        const QByteArray val = line.mid(eq + 1).trimmed();
        bool ok = false;
        if (key == "out_time_us") {
            const qint64 us = val.toLongLong(&ok);
            if (ok && us >= 0)
                m_partSeconds = double(us) / 1e6;
        } else if (key == "total_size") {
            const qint64 b = val.toLongLong(&ok);
            if (ok && b >= 0)
                m_partBytes = b;
        } else if (key == "bitrate") {
            QByteArray v = val;
            v.replace("kbits/s", "");
            const double kbps = v.toDouble(&ok);
            if (ok)
                m_bitrate = kbps;
        } else if (key == "progress") {
            tick = true;
        }
    }
    if (tick) {
        if (m_partSeconds > 0 && m_state == State::Connecting)
            setState(State::Recording);
        else
            emit changed();
        if (!m_audioUrl.isEmpty() && m_options.maxSeconds > 0 && recordedSeconds() >= m_options.maxSeconds &&
            !m_limitStopSent && m_proc) {
            m_limitStopSent = true;
            m_proc->write("q");
            m_killTimer.start(10000);
        }
    }
}

void StreamRecording::onReadyStderr()
{
    if (!m_proc)
        return;
    const QString text = QString::fromUtf8(m_proc->readAllStandardError());
    for (const QString &l : text.split('\n', Qt::SkipEmptyParts)) {
        const QString t = l.trimmed();
        if (t.isEmpty())
            continue;
        m_log << t;
        if (m_log.size() > 50)
            m_log.removeFirst();
    }
}

void StreamRecording::onPartFinished(int exitCode, QProcess::ExitStatus status)
{
    onReadyStdout();
    onReadyStderr();
    m_killTimer.stop();
    QProcess *p = m_proc;
    m_proc = nullptr;
    p->deleteLater();

    const qint64 size = QFileInfo(m_partFile).size();
    double partSeconds = m_partSeconds;
    if (size > 0 && partSeconds > 0.05) {
        // ffmpeg's progress lags what it flushes on exit (several seconds with separate sound), so measure the file.
        const MediaInfo info = MediaInfo::probe(AppSettings::ffprobePath(), m_partFile, 15000);
        if (info.valid && info.duration > 0)
            partSeconds = info.duration;
        m_files << m_partFile;
        m_fileSeconds << partSeconds;
        m_prevSeconds += partSeconds;
        m_prevBytes += size;
        if (partSeconds >= 10)
            m_failedAttempts = 0;
    } else {
        QFile::remove(m_partFile);
    }
    m_partFile.clear();
    m_partSeconds = 0;
    m_partBytes = 0;

    if (m_stopRequested) {
        finish();
        return;
    }
    if (m_options.maxSeconds > 0 && m_prevSeconds >= m_options.maxSeconds - 1.0) {
        m_endReason = QStringLiteral("time limit reached");
        finish();
        return;
    }
    if (status == QProcess::NormalExit && exitCode == 0) {
        m_endReason = QStringLiteral("stream ended");
        finish();
        return;
    }
    QString err;
    for (auto it = m_log.crbegin(); it != m_log.crend() && err.isEmpty(); ++it)
        err = *it;
    retryOrFinish(err.isEmpty() ? QStringLiteral("ffmpeg exited with code %1").arg(exitCode) : err);
}

void StreamRecording::retryOrFinish(const QString &error)
{
    m_error = error;
    // Only reconnect once something has been recorded; a link that never worked fails straight away.
    if (m_options.reconnect && !m_files.isEmpty() && m_failedAttempts < MaxReconnectAttempts) {
        ++m_failedAttempts;
        setState(State::Reconnecting);
        m_retryTimer.start(qMin(30, 5 * m_failedAttempts) * 1000);
        return;
    }
    if (!m_files.isEmpty())
        m_endReason = QStringLiteral("stream dropped");
    finish();
}

void StreamRecording::finish()
{
    m_retryTimer.stop();
    if (m_files.isEmpty()) {
        if (m_stopRequested && m_error.isEmpty())
            m_error = QStringLiteral("Stopped before anything was recorded");
        fail(m_error.isEmpty() ? QStringLiteral("Nothing was recorded") : m_error);
        return;
    }
    // Separate picture and sound can't use ffmpeg's own time limit and run a few seconds over, so
    // trim the last part; MP4 recordings are converted from MKV. Both are lossless stream copies.
    const bool mp4 = m_options.container == "mp4";
    const double over = m_options.maxSeconds > 0 ? m_prevSeconds - m_options.maxSeconds : 0.0;
    m_finishSteps.clear();
    for (int i = 0; i < m_files.size(); ++i) {
        FinishStep step{m_files[i], 0.0};
        if (i == m_files.size() - 1 && over > 1.0)
            step.keepSeconds = qMax(1.0, m_fileSeconds.value(i) - over);
        if (mp4 || step.keepSeconds > 0)
            m_finishSteps << step;
    }
    convertNext();
}

void StreamRecording::convertNext()
{
    if (m_finishSteps.isEmpty()) {
        complete();
        return;
    }
    const FinishStep step = m_finishSteps.takeFirst();
    const QString in = step.file;
    const QFileInfo fi(in);
    const bool toMp4 = m_options.container == "mp4";
    // Trimming without conversion writes a temporary file that then replaces the original.
    const QString out = toMp4 ? CommandBuilder::uniqueOutputPath(fi.dir().filePath(fi.completeBaseName() + ".mp4"))
                              : fi.dir().filePath(fi.completeBaseName() + ".trimmed." + fi.suffix());
    setState(State::Converting);
    m_proc = new QProcess(this);
    prepare(m_proc);
    connect(m_proc, &QProcess::finished, this, [this, in, out, toMp4](int code, QProcess::ExitStatus status) {
        QProcess *p = m_proc;
        m_proc = nullptr;
        p->deleteLater();
        const int index = m_files.indexOf(in);
        if (status == QProcess::NormalExit && code == 0 && QFileInfo(out).size() > 0) {
            QFile::remove(in);
            QString final = out;
            if (!toMp4 && QFile::rename(out, in))
                final = in;
            m_files.replace(index, final);
            // Keep the reported length and size in step with the files.
            const MediaInfo info = MediaInfo::probe(AppSettings::ffprobePath(), final, 15000);
            if (info.valid && info.duration > 0 && index < m_fileSeconds.size()) {
                m_prevSeconds += info.duration - m_fileSeconds[index];
                m_fileSeconds[index] = info.duration;
            }
            m_prevBytes = 0;
            for (const QString &f : std::as_const(m_files))
                m_prevBytes += QFileInfo(f).size();
        } else {
            QFile::remove(out);
            const QStringList lines = QString::fromUtf8(p->readAllStandardError()).split('\n', Qt::SkipEmptyParts);
            const QString why = lines.isEmpty() ? QString() : QStringLiteral(": ") + lines.constLast().trimmed();
            m_note = toMp4 ? QStringLiteral("Kept as MKV because it couldn't be converted to MP4") + why
                           : QStringLiteral("Couldn't trim to the time limit") + why;
        }
        convertNext();
    });
    QStringList args{"-hide_banner", "-loglevel", "error", "-y",   "-i", in,
                     "-map",         "0:v?",      "-map",  "0:a?", "-c", "copy"};
    if (step.keepSeconds > 0)
        args << "-t" << QString::number(step.keepSeconds, 'f', 3);
    if (toMp4)
        args << "-movflags" << "+faststart";
    else if (fi.suffix() == QLatin1String("ts"))
        args << "-f" << "mpegts";
    args << out;
    m_proc->start(m_ffmpeg, args);
}

void StreamRecording::complete()
{
    setState(State::Done);
    EncodingLog::append(QStringLiteral("%1  Recorded %2 -> %3\n  Length: %4 (%5)%6\n  Command: %7")
                            .arg(QDateTime::currentDateTime().toString(Qt::ISODate), m_options.url,
                                 QDir::toNativeSeparators(m_files.join(QStringLiteral(", "))),
                                 formatDuration(recordedSeconds()), m_endReason,
                                 m_note.isEmpty() ? QString() : QStringLiteral("\n  Note: ") + m_note, m_command));
    emit finished();
}

void StreamRecording::fail(const QString &error)
{
    m_retryTimer.stop();
    m_error = StreamLink::friendlyError(error);
    if (!m_viaYtDlp && m_ytDlp.isEmpty() && !StreamLink::isDirectMedia(m_options.url))
        m_error += QStringLiteral("\n\nIf this is a web page rather than a stream link, install yt-dlp so the stream "
                                  "can be found on the page (see Preferences > Recording).");
    setState(State::Failed);
    const QString entry =
        QStringLiteral("%1  FAILED recording %2\n  Error: %3\n  Command: %4\n  ffmpeg output:\n    %5")
            .arg(QDateTime::currentDateTime().toString(Qt::ISODate), m_options.url, error, m_command,
                 logTail().replace('\n', QStringLiteral("\n    ")));
    EncodingLog::append(entry);
    EncodingLog::appendError(entry);
    emit finished();
}

void StreamRecording::setState(State s)
{
    m_state = s;
    emit changed();
}

QStringList StreamRecording::files() const
{
    QStringList f = m_files;
    if (!m_partFile.isEmpty())
        f << m_partFile;
    return f;
}

QString StreamRecording::stateText() const
{
    switch (m_state) {
    case State::Resolving:
        return m_updating ? QStringLiteral("Updating yt-dlp…") : QStringLiteral("Finding stream…");
    case State::Connecting:
        return m_stopRequested ? QStringLiteral("Stopping…")
               : m_part > 1    ? QStringLiteral("Reconnecting…")
                               : QStringLiteral("Connecting…");
    case State::Recording:
        if (m_stopRequested)
            return QStringLiteral("Stopping…");
        return m_part > 1 ? QStringLiteral("Recording (part %1)").arg(m_part) : QStringLiteral("Recording");
    case State::Reconnecting:
        return QStringLiteral("Stream dropped, retrying (%1 of %2)…").arg(m_failedAttempts).arg(MaxReconnectAttempts);
    case State::Converting:
        return m_options.container == "mp4" ? QStringLiteral("Converting to MP4…") : QStringLiteral("Finishing…");
    case State::Done:
        return m_endReason.isEmpty() ? QStringLiteral("Done") : QStringLiteral("Done (%1)").arg(m_endReason);
    case State::Failed:
        return QStringLiteral("Failed");
    }
    return {};
}

// ================================================================= StreamRecorder

StreamRecorder::StreamRecorder(QObject *parent)
    : QObject(parent)
{}

StreamRecorder::~StreamRecorder() { qDeleteAll(m_recordings); }

StreamRecording *StreamRecorder::record(const RecordingOptions &options)
{
    auto *r = new StreamRecording(QString::number(m_nextId++), options, AppSettings::ffmpegPath(),
                                  AppSettings::ytDlpPath(), this);
    m_recordings << r;
    auto updateActive = [this] {
        const int n = activeCount();
        if (n != m_lastActive) {
            m_lastActive = n;
            emit activeCountChanged(n);
        }
    };
    connect(r, &StreamRecording::changed, this, [this, r, updateActive] {
        emit recordingChanged(r);
        updateActive();
    });
    connect(r, &StreamRecording::finished, this, [this, r, updateActive] {
        emit recordingFinished(r);
        updateActive();
    });
    emit recordingAdded(r);
    r->start();
    updateActive();
    return r;
}

StreamRecording *StreamRecorder::recording(const QString &id) const
{
    for (StreamRecording *r : m_recordings)
        if (r->id() == id)
            return r;
    return nullptr;
}

void StreamRecorder::remove(const QString &id)
{
    StreamRecording *r = recording(id);
    if (!r || r->isActive())
        return;
    m_recordings.removeOne(r);
    emit recordingRemoved(id);
    r->deleteLater();
}

void StreamRecorder::stopAll()
{
    for (StreamRecording *r : std::as_const(m_recordings))
        r->stop();
}

int StreamRecorder::activeCount() const
{
    int n = 0;
    for (const StreamRecording *r : m_recordings)
        n += r->isActive() ? 1 : 0;
    return n;
}

bool StreamRecorder::waitForAll(int msecs)
{
    if (activeCount() == 0)
        return true;
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    connect(this, &StreamRecorder::activeCountChanged, &loop, [&loop](int n) {
        if (n == 0)
            loop.quit();
    });
    timeout.start(msecs);
    loop.exec();
    return activeCount() == 0;
}
