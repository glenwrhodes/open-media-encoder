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

bool ensureInstalled(QWidget *parent)
{
    if (AppSettings::whisperCliPath().isEmpty()) {
        QMessageBox::warning(
            parent, QStringLiteral("Speech Recognition"),
            QStringLiteral("Captions and filler-word removal need whisper-cli, which should come with the app "
                           "but wasn't found. Reinstalling the app will restore it."));
        return false;
    }
    if (!Whisper::activeModelPath().isEmpty())
        return true;
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
    return download(parent, m->id);
}

} // namespace SpeechModelPrompt
