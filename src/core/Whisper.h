#pragma once

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QPair>
#include <QSize>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;
class QSaveFile;
class QCryptographicHash;

// Speech to text with whisper.cpp. The whisper-cli program ships with the app; the speech models are
// large, so the one the user picks is downloaded on first use and checked against a pinned SHA-256.
namespace Whisper {

struct Model {
    QString id;    // fast, balanced, best
    QString label; // shown in the UI
    QString file;  // file name on Hugging Face
    qint64 size = 0;
    QByteArray sha256;
    QString description;
};

const QList<Model> &models();
const Model *model(const QString &id);
QString selectedModelId(); // the model chosen in Preferences (default "balanced")
void setSelectedModelId(const QString &id);
QString modelsDir(); // <data dir>/Tools/whisper-models
QString modelPath(const Model &m);
bool isInstalled(const Model &m);
// Model file to transcribe with: OME_WHISPER_MODEL when set, else the selected model if downloaded.
QString activeModelPath();
QString modelUrl(const Model &m);

// Spoken languages offered in the UI: (code, name), "auto" first.
const QList<QPair<QString, QString>> &languages();

struct Cue {
    double start = 0.0; // seconds
    double end = 0.0;
    QString text;
};
QList<Cue> parseSrt(const QString &path);
bool writeSrt(const QList<Cue> &cues, const QString &path);
// Writes the captions as an ASS file laid out for a frame of the given size, so burned-in captions
// have the same proportions in landscape, vertical and square video. size is in pixels at 1080 lines.
bool writeAss(const QList<Cue> &cues, const QString &path, const QSize &frame, int size);
// Drops whisper's non-speech markers such as [BLANK_AUDIO] and tidies the text.
QList<Cue> cleanCues(const QList<Cue> &cues);

} // namespace Whisper

// Downloads a speech model into Whisper::modelsDir(), verifying size and SHA-256 as it streams.
class WhisperModelDownloader : public QObject
{
    Q_OBJECT
public:
    explicit WhisperModelDownloader(QObject *parent = nullptr);
    ~WhisperModelDownloader() override;

    void start(const QString &modelId);
    void cancel();

signals:
    void progress(qint64 received, qint64 total);
    void finished(bool ok, const QString &error);

private:
    void onReadyRead();
    void onFinished();
    void done(bool ok, const QString &error);

    QNetworkAccessManager *m_net;
    QNetworkReply *m_reply = nullptr;
    QSaveFile *m_file = nullptr;
    QCryptographicHash *m_hash = nullptr;
    Whisper::Model m_model;
    qint64 m_written = 0;
    bool m_done = false;
};
