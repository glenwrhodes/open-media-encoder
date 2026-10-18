#include "SpeechModelPrompt.h"

#include "core/AppSettings.h"
#include "core/MediaInfo.h"
#include "core/Whisper.h"

#include <QEventLoop>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>

namespace SpeechModelPrompt {

bool download(QWidget *parent, const QString &modelId)
{
    const Whisper::Model *m = Whisper::model(modelId);
    if (!m)
        return false;
    if (Whisper::isInstalled(*m))
        return true;
    QProgressDialog progress(QStringLiteral("Downloading the %1 speech model…").arg(m->label), QStringLiteral("Cancel"),
                             0, 1000, parent);
    progress.setWindowTitle(QStringLiteral("Automatic Captions"));
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    progress.setValue(0);

    WhisperModelDownloader downloader;
    QEventLoop loop;
    bool ok = false;
    bool finished = false;
    QString error;
    QObject::connect(&downloader, &WhisperModelDownloader::progress, &progress, [&](qint64 got, qint64 total) {
        if (total > 0) {
            progress.setValue(int(got * 1000 / total));
            progress.setLabelText(QStringLiteral("Downloading the %1 speech model…\n%2 of %3")
                                      .arg(m->label, formatBytes(got), formatBytes(total)));
        }
    });
    QObject::connect(&downloader, &WhisperModelDownloader::finished, &loop, [&](bool success, const QString &e) {
        ok = success;
        error = e;
        finished = true;
        loop.quit();
    });
    QObject::connect(&progress, &QProgressDialog::canceled, &downloader, &WhisperModelDownloader::cancel);
    downloader.start(modelId);
    if (!finished) // start() can fail straight away
        loop.exec();
    progress.reset();
    if (!ok && !error.contains(QStringLiteral("cancelled")))
        QMessageBox::warning(parent, QStringLiteral("Automatic Captions"),
                             QStringLiteral("Couldn't download the speech model: %1").arg(error));
    return ok;
}

bool downloadGpu(QWidget *parent)
{
    if (WhisperGpu::installed())
        return true;
    QProgressDialog progress(QStringLiteral("Downloading NVIDIA GPU support…"), QStringLiteral("Cancel"), 0, 1000,
                             parent);
    progress.setWindowTitle(QStringLiteral("Speech Recognition"));
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    progress.setValue(0);
    WhisperGpuInstaller installer;
    QEventLoop loop;
    bool ok = false;
    bool finished = false;
    QString error;
    QObject::connect(&installer, &WhisperGpuInstaller::progress, &progress, [&](qint64 got, qint64 total) {
        if (total > 0) {
            progress.setValue(int(got * 1000 / total));
            progress.setLabelText(
                QStringLiteral("Downloading NVIDIA GPU support…\n%1 of %2").arg(formatBytes(got), formatBytes(total)));
        }
    });
    QObject::connect(&installer, &WhisperGpuInstaller::status, &progress, [&](const QString &text) {
        progress.setLabelText(text);
        if (text.startsWith(QStringLiteral("Unpacking"))) {
            progress.setRange(0, 0); // busy
            progress.setCancelButton(nullptr);
        }
    });
    QObject::connect(&installer, &WhisperGpuInstaller::finished, &loop, [&](bool success, const QString &e) {
        ok = success;
        error = e;
        finished = true;
        loop.quit();
    });
    QObject::connect(&progress, &QProgressDialog::canceled, &installer, &WhisperGpuInstaller::cancel);
    installer.start();
    if (!finished)
        loop.exec();
    progress.reset();
    if (!ok && !error.contains(QStringLiteral("cancelled")))
        QMessageBox::warning(parent, QStringLiteral("Speech Recognition"),
                             QStringLiteral("Couldn't install NVIDIA GPU support: %1").arg(error));
    return ok;
}

// Offers NVIDIA GPU support once on machines that have an NVIDIA GPU.
static void offerGpu(QWidget *parent)
{
    if (!WhisperGpu::supported() || WhisperGpu::installed() || !WhisperGpu::nvidiaPresent() ||
        AppSettings::value("captions/gpuOffered").toBool())
        return;
    AppSettings::setValue("captions/gpuOffered", true);
    QMessageBox box(QMessageBox::Question, QStringLiteral("Speech Recognition"),
                    QStringLiteral("This computer has an NVIDIA GPU. Speech recognition runs several times faster "
                                   "on it with GPU support (whisper.cpp's CUDA build, %1, downloaded once).\n\n"
                                   "Download it now? You can also do this later in Preferences > Captions.")
                        .arg(formatBytes(WhisperGpu::downloadSize())),
                    QMessageBox::NoButton, parent);
    QPushButton *get = box.addButton(QStringLiteral("Download"), QMessageBox::AcceptRole);
    box.addButton(QStringLiteral("Not Now"), QMessageBox::RejectRole);
    box.setDefaultButton(get);
    box.exec();
    if (box.clickedButton() == get)
        downloadGpu(parent);
}

bool ensureInstalled(QWidget *parent)
{
    if (AppSettings::whisperCliPath().isEmpty()) {
        QMessageBox::warning(
            parent, QStringLiteral("Speech Recognition"),
            QStringLiteral("Captions and filler-word removal need whisper-cli, which should come with the app "
                           "but wasn't found. Reinstalling the app will restore it."));
        return false;
    }
    if (!Whisper::activeModelPath().isEmpty()) {
        offerGpu(parent);
        return true;
    }
    const Whisper::Model *m = Whisper::model(Whisper::selectedModelId());
    QMessageBox box(QMessageBox::Question, QStringLiteral("Speech Recognition"),
                    QStringLiteral("Automatic captions and filler-word removal recognize the speech in your videos, "
                                   "entirely on this computer. They need a speech model, downloaded once:\n\n%1: %2\n\n"
                                   "Download it now? You can pick a faster or more accurate model in "
                                   "Preferences > Captions.")
                        .arg(m->label, m->description),
                    QMessageBox::Cancel, parent);
    QPushButton *get = box.addButton(QStringLiteral("Download"), QMessageBox::AcceptRole);
    box.setDefaultButton(get);
    box.exec();
    if (box.clickedButton() != get)
        return false;
    if (!download(parent, m->id))
        return false;
    offerGpu(parent);
    return true;
}

} // namespace SpeechModelPrompt
