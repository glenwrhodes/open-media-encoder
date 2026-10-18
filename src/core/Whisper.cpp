#include "Whisper.h"

#include "AppSettings.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTextStream>
#include <cmath>

namespace {

// Pinned revision of github.com/ggerganov/whisper.cpp models on Hugging Face (MIT licensed weights).
const QString ModelBase =
    QStringLiteral("https://huggingface.co/ggerganov/whisper.cpp/resolve/5359861c739e955e79d9a303bcbc70fb988958b1/");

QString srtTime(double t)
{
    const qint64 ms = qMax<qint64>(0, qint64(std::llround(t * 1000.0)));
    return QStringLiteral("%1:%2:%3,%4")
        .arg(ms / 3600000, 2, 10, QChar('0'))
        .arg((ms / 60000) % 60, 2, 10, QChar('0'))
        .arg((ms / 1000) % 60, 2, 10, QChar('0'))
        .arg(ms % 1000, 3, 10, QChar('0'));
}

QString assTime(double t)
{
    const qint64 cs = qMax<qint64>(0, qint64(std::llround(t * 100.0)));
    return QStringLiteral("%1:%2:%3.%4")
        .arg(cs / 360000)
        .arg((cs / 6000) % 60, 2, 10, QChar('0'))
        .arg((cs / 100) % 60, 2, 10, QChar('0'))
        .arg(cs % 100, 2, 10, QChar('0'));
}

double parseSrtTime(const QString &s)
{
    static const QRegularExpression re(QStringLiteral("(\\d+):(\\d+):(\\d+)[,.](\\d+)"));
    const auto m = re.match(s);
    if (!m.hasMatch())
        return -1.0;
    return m.captured(1).toInt() * 3600.0 + m.captured(2).toInt() * 60.0 + m.captured(3).toInt() +
           m.captured(4).toInt() / std::pow(10.0, m.captured(4).size());
}

} // namespace

namespace Whisper {

const QList<Model> &models()
{
    static const QList<Model> list = {
        {"fast", "Fast (82 MB)", "ggml-base-q8_0.bin", 81768585,
         "c577b9a86e7e048a0b7eada054f4dd79a56bbfa911fbdacf900ac5b567cbb7d9",
         "Whisper base. Quick, fine for clear speech in common languages."},
        {"balanced", "Balanced (264 MB)", "ggml-small-q8_0.bin", 264464607,
         "49c8fb02b65e6049d5fa6c04f81f53b867b5ec9540406812c643f177317f779f",
         "Whisper small. Accurate for most recordings; the recommended choice."},
        {"best", "Best (574 MB)", "ggml-large-v3-turbo-q5_0.bin", 574041195,
         "394221709cd5ad1f40c46e6031ca61bce88931e6e088c188294c6d5a55ffa7e2",
         "Whisper large v3 turbo. Most accurate, especially for accents and noisy audio; slower."},
    };
    return list;
}

const Model *model(const QString &id)
{
    for (const Model &m : models())
        if (m.id == id)
            return &m;
    return nullptr;
}

QString selectedModelId()
{
    const QString id = AppSettings::value("captions/model", "balanced").toString();
    return model(id) ? id : QStringLiteral("balanced");
}

void setSelectedModelId(const QString &id) { AppSettings::setValue("captions/model", id); }

QString modelsDir() { return QDir(AppSettings::dataDir()).filePath(QStringLiteral("Tools/whisper-models")); }

QString modelPath(const Model &m) { return QDir(modelsDir()).filePath(m.file); }

bool isInstalled(const Model &m) { return QFileInfo(modelPath(m)).size() == m.size; }

QString activeModelPath()
{
    const QString env = qEnvironmentVariable("OME_WHISPER_MODEL");
    if (!env.isEmpty() && QFileInfo::exists(env))
        return env;
    const Model *m = model(selectedModelId());
    return m && isInstalled(*m) ? modelPath(*m) : QString();
}

QString modelUrl(const Model &m) { return ModelBase + m.file; }

const QList<QPair<QString, QString>> &languages()
{
    static const QList<QPair<QString, QString>> list = {
        {"auto", "Detect automatically"},
        {"en", "English"},
        {"es", "Spanish"},
        {"fr", "French"},
        {"de", "German"},
        {"it", "Italian"},
        {"pt", "Portuguese"},
        {"nl", "Dutch"},
        {"sv", "Swedish"},
        {"no", "Norwegian"},
        {"da", "Danish"},
        {"fi", "Finnish"},
        {"pl", "Polish"},
        {"cs", "Czech"},
        {"hu", "Hungarian"},
        {"ro", "Romanian"},
        {"el", "Greek"},
        {"ru", "Russian"},
        {"uk", "Ukrainian"},
        {"tr", "Turkish"},
        {"ar", "Arabic"},
        {"he", "Hebrew"},
        {"hi", "Hindi"},
        {"id", "Indonesian"},
        {"vi", "Vietnamese"},
        {"th", "Thai"},
        {"ja", "Japanese"},
        {"ko", "Korean"},
        {"zh", "Chinese"},
    };
    return list;
}

QList<Cue> parseSrt(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    const QString text = QString::fromUtf8(f.readAll());
    QList<Cue> cues;
    static const QRegularExpression blank(QStringLiteral("\\n\\s*\\n"));
    for (const QString &block : text.split(blank, Qt::SkipEmptyParts)) {
        const QStringList lines = block.trimmed().split('\n');
        int i = 0;
        while (i < lines.size() && !lines[i].contains(QStringLiteral("-->")))
            ++i;
        if (i >= lines.size())
            continue;
        const QStringList times = lines[i].split(QStringLiteral("-->"));
        Cue c;
        c.start = parseSrtTime(times.value(0));
        c.end = parseSrtTime(times.value(1));
        QStringList body;
        for (int j = i + 1; j < lines.size(); ++j)
            body << lines[j].trimmed();
        c.text = body.join('\n').trimmed();
        if (c.start >= 0 && c.end > c.start)
            cues << c;
    }
    return cues;
}

QList<Cue> cleanCues(const QList<Cue> &cues)
{
    static const QRegularExpression marker(QStringLiteral("^\\[[^\\]]*\\]$"));
    QList<Cue> out;
    for (Cue c : cues) {
        c.text = c.text.simplified();
        if (c.text.isEmpty() || marker.match(c.text).hasMatch())
            continue;
        out << c;
    }
    return out;
}

bool writeSrt(const QList<Cue> &cues, const QString &path)
{
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    QByteArray data;
    int n = 1;
    for (const Cue &c : cues)
        data += QStringLiteral("%1\n%2 --> %3\n%4\n\n").arg(n++).arg(srtTime(c.start), srtTime(c.end), c.text).toUtf8();
    f.write(data);
    return f.commit();
}

bool writeAss(const QList<Cue> &cues, const QString &path, const QSize &frame, int size)
{
    const int w = qMax(16, frame.width());
    const int h = qMax(16, frame.height());
    const double scale = qMin(w, h) / 1080.0;
    const int fontSize = qMax(8, int(std::lround(size * scale)));
    const int outline = qMax(2, int(std::lround(fontSize / 14.0)));
    // Vertical video keeps captions clear of the buttons and titles that phone apps draw at the bottom.
    const int marginV = int(std::lround(h * (h > w ? 0.16 : 0.07)));
    const int marginH = int(std::lround(w * 0.06));

    QString out;
    QTextStream s(&out);
    s << "[Script Info]\nScriptType: v4.00+\nPlayResX: " << w << "\nPlayResY: " << h
      << "\nWrapStyle: 0\nScaledBorderAndShadow: yes\n\n"
      << "[V4+ Styles]\nFormat: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, "
         "Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, "
         "Alignment, MarginL, MarginR, MarginV, Encoding\n"
      << "Style: Default,Arial," << fontSize << ",&H00FFFFFF,&H000000FF,&H00000000,&H64000000,-1,0,0,0,100,100,0,0,1,"
      << outline << ",0,2," << marginH << "," << marginH << "," << marginV << ",1\n\n"
      << "[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n";
    for (const Cue &c : cues) {
        QString text = c.text;
        text.replace('{', '(').replace('}', ')').replace('\n', QStringLiteral("\\N"));
        s << "Dialogue: 0," << assTime(c.start) << "," << assTime(c.end) << ",Default,,0,0,0,," << text << "\n";
    }
    s.flush();
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(out.toUtf8());
    return f.commit();
}

} // namespace Whisper

// ================================================================= downloader

WhisperModelDownloader::WhisperModelDownloader(QObject *parent)
    : QObject(parent)
    , m_net(new QNetworkAccessManager(this))
{}

WhisperModelDownloader::~WhisperModelDownloader()
{
    if (m_reply)
        m_reply->abort();
    delete m_file;
    delete m_hash;
}

void WhisperModelDownloader::start(const QString &modelId)
{
    m_done = false;
    const Whisper::Model *m = Whisper::model(modelId);
    if (!m) {
        done(false, QStringLiteral("Unknown speech model: %1").arg(modelId));
        return;
    }
    m_model = *m;
    QDir().mkpath(Whisper::modelsDir());
    delete m_file;
    m_file = new QSaveFile(Whisper::modelPath(m_model));
    if (!m_file->open(QIODevice::WriteOnly)) {
        done(false, QStringLiteral("Couldn't write to %1").arg(QDir::toNativeSeparators(Whisper::modelsDir())));
        return;
    }
    delete m_hash;
    m_hash = new QCryptographicHash(QCryptographicHash::Sha256);
    m_written = 0;
    QNetworkRequest req{QUrl(Whisper::modelUrl(m_model))};
    req.setTransferTimeout(60000);
    m_reply = m_net->get(req);
    connect(m_reply, &QNetworkReply::readyRead, this, &WhisperModelDownloader::onReadyRead);
    connect(m_reply, &QNetworkReply::downloadProgress, this,
            [this](qint64 received, qint64 total) { emit progress(received, total > 0 ? total : m_model.size); });
    connect(m_reply, &QNetworkReply::finished, this, &WhisperModelDownloader::onFinished);
}

void WhisperModelDownloader::cancel()
{
    if (m_reply)
        m_reply->abort();
}

void WhisperModelDownloader::onReadyRead()
{
    const QByteArray chunk = m_reply->readAll();
    m_hash->addData(chunk);
    m_written += chunk.size();
    if (m_file->write(chunk) != chunk.size())
        m_reply->abort();
}

void WhisperModelDownloader::onFinished()
{
    QNetworkReply *reply = m_reply;
    m_reply = nullptr;
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) {
        m_file->cancelWriting();
        done(false, reply->error() == QNetworkReply::OperationCanceledError
                        ? QStringLiteral("Download cancelled")
                        : QStringLiteral("Download failed: %1").arg(reply->errorString()));
        return;
    }
    const QByteArray rest = reply->readAll();
    m_hash->addData(rest);
    m_written += rest.size();
    m_file->write(rest);
    if (m_written != m_model.size || m_hash->result().toHex() != m_model.sha256) {
        m_file->cancelWriting();
        done(false, QStringLiteral("The downloaded model didn't match its published checksum, so it wasn't used"));
        return;
    }
    if (!m_file->commit()) {
        done(false,
             QStringLiteral("Couldn't save the model to %1").arg(QDir::toNativeSeparators(Whisper::modelsDir())));
        return;
    }
    done(true, {});
}

void WhisperModelDownloader::done(bool ok, const QString &error)
{
    if (m_done)
        return;
    m_done = true;
    emit finished(ok, error);
}
