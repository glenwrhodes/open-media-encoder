#include "YtDlp.h"

#include "AppSettings.h"

#include <QCryptographicHash>
#include <QDate>
#include <QDir>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>
#include <QSysInfo>

namespace {
const QString ReleaseBase = QStringLiteral("https://github.com/yt-dlp/yt-dlp/releases/latest/download/");
}

namespace YtDlp {

QString releaseAssetName()
{
#if defined(Q_OS_WIN)
    return QStringLiteral("yt-dlp.exe");
#elif defined(Q_OS_MACOS)
    return QStringLiteral("yt-dlp_macos");
#else
    return QSysInfo::currentCpuArchitecture() == QLatin1String("arm64") ? QStringLiteral("yt-dlp_linux_aarch64")
                                                                        : QStringLiteral("yt-dlp_linux");
#endif
}

QString managedPath()
{
#ifdef Q_OS_WIN
    const QString name = QStringLiteral("yt-dlp.exe");
#else
    const QString name = QStringLiteral("yt-dlp");
#endif
    return QDir(AppSettings::dataDir()).filePath(QStringLiteral("Tools/") + name);
}

bool isManaged(const QString &path)
{
    return !path.isEmpty() && QFileInfo(path).absoluteFilePath() == QFileInfo(managedPath()).absoluteFilePath();
}

bool updateDue(const QString &path)
{
    return isManaged(path) && AppSettings::value("tools/ytdlpUpdateChecked").toDate() != QDate::currentDate();
}

void markUpdateChecked() { AppSettings::setValue("tools/ytdlpUpdateChecked", QDate::currentDate()); }

QString version(const QString &path)
{
    if (path.isEmpty())
        return {};
    QProcess p;
    p.start(path, {"--version"});
    if (!p.waitForFinished(30000)) {
        p.kill();
        return {};
    }
    return p.exitCode() == 0 ? QString::fromUtf8(p.readAllStandardOutput()).trimmed() : QString();
}

QStringList extraArgs()
{
    // Recent YouTube pages need a JavaScript runtime. yt-dlp uses Deno by default; Node.js has to be named.
    if (QStandardPaths::findExecutable(QStringLiteral("deno")).isEmpty() &&
        !QStandardPaths::findExecutable(QStringLiteral("node")).isEmpty())
        return {"--js-runtimes", "node"};
    return {};
}

} // namespace YtDlp

YtDlpInstaller::YtDlpInstaller(QObject *parent)
    : QObject(parent)
    , m_net(new QNetworkAccessManager(this))
{}

void YtDlpInstaller::start()
{
    m_done = false;
    QNetworkRequest req(QUrl(ReleaseBase + QStringLiteral("SHA2-256SUMS")));
    req.setTransferTimeout(30000);
    m_reply = m_net->get(req);
    connect(m_reply, &QNetworkReply::finished, this, &YtDlpInstaller::onChecksums);
}

void YtDlpInstaller::cancel()
{
    if (m_reply)
        m_reply->abort();
}

void YtDlpInstaller::onChecksums()
{
    QNetworkReply *reply = m_reply;
    m_reply = nullptr;
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) {
        done(false, QStringLiteral("Couldn't reach GitHub: %1").arg(reply->errorString()));
        return;
    }
    // Lines look like "<sha256>  <file name>".
    const QByteArray asset = YtDlp::releaseAssetName().toUtf8();
    for (const QByteArray &line : reply->readAll().split('\n')) {
        const QList<QByteArray> parts = line.simplified().split(' ');
        if (parts.size() == 2 && parts[1] == asset)
            m_expectedHash = parts[0].toLower();
    }
    if (m_expectedHash.size() != 64) {
        done(false, QStringLiteral("The yt-dlp release has no checksum for %1").arg(YtDlp::releaseAssetName()));
        return;
    }
    QNetworkRequest req(QUrl(ReleaseBase + YtDlp::releaseAssetName()));
    req.setTransferTimeout(60000);
    m_reply = m_net->get(req);
    connect(m_reply, &QNetworkReply::downloadProgress, this, &YtDlpInstaller::progress);
    connect(m_reply, &QNetworkReply::finished, this, &YtDlpInstaller::onBinary);
}

void YtDlpInstaller::onBinary()
{
    QNetworkReply *reply = m_reply;
    m_reply = nullptr;
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) {
        done(false, reply->error() == QNetworkReply::OperationCanceledError
                        ? QStringLiteral("Download cancelled")
                        : QStringLiteral("Download failed: %1").arg(reply->errorString()));
        return;
    }
    const QByteArray data = reply->readAll();
    if (QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex() != m_expectedHash) {
        done(false, QStringLiteral("The downloaded yt-dlp didn't match its published checksum, so it wasn't used"));
        return;
    }
    const QString path = YtDlp::managedPath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size() || !f.commit()) {
        done(false, QStringLiteral("Couldn't save yt-dlp to %1").arg(QDir::toNativeSeparators(path)));
        return;
    }
    QFile::setPermissions(path, QFile::permissions(path) | QFile::ExeOwner | QFile::ExeUser);
    YtDlp::markUpdateChecked();
    done(true, {});
}

void YtDlpInstaller::done(bool ok, const QString &error)
{
    if (m_done)
        return;
    m_done = true;
    emit finished(ok, error);
}
