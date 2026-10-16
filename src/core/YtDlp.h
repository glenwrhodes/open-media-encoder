#pragma once

#include <QObject>
#include <QStringList>

class QNetworkAccessManager;
class QNetworkReply;

// yt-dlp finds the actual stream behind web pages such as YouTube and Twitch. It isn't shipped
// with the app: a copy is downloaded on first use into the data folder and kept up to date there.
namespace YtDlp {
QString managedPath();      // <data dir>/Tools/yt-dlp(.exe)
QString releaseAssetName(); // file name of the build for this platform on GitHub
bool isManaged(const QString &path);
bool updateDue(const QString &path); // the managed copy hasn't checked for updates today
void markUpdateChecked();
QString version(const QString &path); // empty when it can't be run
// Extra yt-dlp arguments for this machine (enables Node.js for YouTube when Deno isn't installed).
QStringList extraArgs();
} // namespace YtDlp

// Downloads the latest yt-dlp release and verifies it against the release's SHA-256 list.
class YtDlpInstaller : public QObject
{
    Q_OBJECT
public:
    explicit YtDlpInstaller(QObject *parent = nullptr);

    void start();
    void cancel();

signals:
    void progress(qint64 received, qint64 total);
    void finished(bool ok, const QString &error);

private:
    void onChecksums();
    void onBinary();
    void done(bool ok, const QString &error);

    QNetworkAccessManager *m_net;
    QNetworkReply *m_reply = nullptr;
    QByteArray m_expectedHash;
    bool m_done = false;
};
