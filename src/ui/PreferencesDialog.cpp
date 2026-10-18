#include "PreferencesDialog.h"

#include "SpeechModelPrompt.h"
#include "core/AppSettings.h"
#include "core/EncoderCaps.h"
#include "core/Formats.h"
#include "core/MediaInfo.h"
#include "core/PresetManager.h"
#include "core/Whisper.h"
#include "core/YtDlp.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QtConcurrent>

PreferencesDialog::PreferencesDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Preferences"));
    resize(640, 480);
    auto *tabs = new QTabWidget;

    // ---- General
    auto *general = new QWidget;
    auto *gv = new QVBoxLayout(general);
    auto *outGroup = new QGroupBox(QStringLiteral("Output"));
    auto *og = new QVBoxLayout(outGroup);
    m_outSource = new QRadioButton(QStringLiteral("Save output files next to the source file"));
    m_outCustom = new QRadioButton(QStringLiteral("Save output files to:"));
    m_customDir = new QLineEdit(QDir::toNativeSeparators(AppSettings::customOutputDir()));
    auto *browse = new QPushButton(QStringLiteral("Browse…"));
    connect(browse, &QPushButton::clicked, this, [this] {
        const QString d = QFileDialog::getExistingDirectory(this, QStringLiteral("Output Folder"), m_customDir->text());
        if (!d.isEmpty()) {
            m_customDir->setText(QDir::toNativeSeparators(d));
            m_outCustom->setChecked(true);
        }
    });
    (AppSettings::outputLocationMode() == "custom" ? m_outCustom : m_outSource)->setChecked(true);
    auto *cr = new QHBoxLayout;
    cr->addWidget(m_outCustom);
    cr->addWidget(m_customDir, 1);
    cr->addWidget(browse);
    og->addWidget(m_outSource);
    og->addLayout(cr);
    m_increment = new QCheckBox(QStringLiteral("Increment output file name if a file with the same name exists"));
    m_increment->setChecked(AppSettings::incrementOutputNames());
    og->addWidget(m_increment);
    m_deletePartial =
        new QCheckBox(QStringLiteral("Delete partially encoded files when an encode fails or is stopped"));
    m_deletePartial->setChecked(AppSettings::deletePartialOnFailure());
    og->addWidget(m_deletePartial);
    gv->addWidget(outGroup);

    auto *queueGroup = new QGroupBox(QStringLiteral("Queue"));
    auto *qf = new QFormLayout(queueGroup);
    m_autoStart = new QSpinBox;
    m_autoStart->setRange(0, 240);
    m_autoStart->setSpecialValueText(QStringLiteral("Never"));
    m_autoStart->setSuffix(QStringLiteral(" min"));
    m_autoStart->setValue(AppSettings::autoStartIdleMinutes());
    qf->addRow(QStringLiteral("Start queue automatically when idle for:"), m_autoStart);
    m_notify = new QCheckBox(QStringLiteral("Show a notification when the queue finishes"));
    m_notify->setChecked(AppSettings::notifyOnFinish());
    qf->addRow(QString(), m_notify);
    m_defaultPreset = new QComboBox;
    for (const Preset &p : PresetManager::instance().presets()) {
        const FormatDef *f = Formats::format(p.settings.format);
        m_defaultPreset->addItem(QStringLiteral("%1 — %2").arg(f ? f->displayName : p.settings.format, p.name()), p.id);
    }
    m_defaultPreset->setCurrentIndex(qMax(0, m_defaultPreset->findData(AppSettings::defaultPresetId())));
    qf->addRow(QStringLiteral("Default preset for new sources:"), m_defaultPreset);
    gv->addWidget(queueGroup);
    gv->addStretch();
    tabs->addTab(general, QStringLiteral("General"));

    // ---- Encoding
    auto *encoding = new QWidget;
    auto *ef = new QFormLayout(encoding);
    m_parallel = new QSpinBox;
    m_parallel->setRange(1, 8);
    m_parallel->setValue(AppSettings::parallelEncodes());
    ef->addRow(QStringLiteral("Parallel encodes:"), m_parallel);
    m_hwDecode = new QCheckBox(QStringLiteral("Use GPU accelerated decoding when available"));
    m_hwDecode->setChecked(AppSettings::hardwareDecode());
    ef->addRow(QString(), m_hwDecode);
    m_preferGpu = new QCheckBox(QStringLiteral("Use the GPU for H.264, HEVC and AV1 presets when available"));
    m_preferGpu->setToolTip(QStringLiteral("Built-in presets encode with NVIDIA NVENC, Intel Quick Sync, AMD AMF, "
                                           "Apple VideoToolbox or VA-API when that works on this computer: many "
                                           "times faster than software encoding, with slightly larger files at "
                                           "the same quality. Outputs already in the queue keep their settings."));
    m_preferGpu->setChecked(AppSettings::preferGpuEncoding());
    ef->addRow(QString(), m_preferGpu);
    m_preventSleep = new QCheckBox(QStringLiteral("Keep the computer awake while encoding or recording"));
    m_preventSleep->setChecked(AppSettings::preventSleepWhileEncoding());
    ef->addRow(QString(), m_preventSleep);
    QStringList hw;
    for (const VideoCodecDef &c : Formats::videoCodecs())
        if (c.hardware)
            hw << QStringLiteral("%1: %2").arg(c.displayName, EncoderCaps::instance().hasEncoder(c.encoder)
                                                                  ? QStringLiteral("available")
                                                                  : QStringLiteral("not available"));
    auto *hwLabel = new QLabel(hw.join('\n'));
    hwLabel->setObjectName("muted");
    ef->addRow(QStringLiteral("Hardware encoders:"), hwLabel);
    tabs->addTab(encoding, QStringLiteral("Encoding"));

    // ---- FFmpeg
    auto *tools = new QWidget;
    auto *tf = new QFormLayout(tools);
    m_ffmpeg = new QLineEdit(QDir::toNativeSeparators(AppSettings::ffmpegPath()));
    auto *ffBrowse = new QPushButton(QStringLiteral("Browse…"));
    connect(ffBrowse, &QPushButton::clicked, this, [this] {
        const QString f = QFileDialog::getOpenFileName(this, QStringLiteral("Locate ffmpeg"), m_ffmpeg->text(),
                                                       QStringLiteral("ffmpeg (ffmpeg*)"));
        if (!f.isEmpty())
            m_ffmpeg->setText(QDir::toNativeSeparators(f));
    });
    auto *fr = new QHBoxLayout;
    fr->addWidget(m_ffmpeg, 1);
    fr->addWidget(ffBrowse);
    auto *useBundled = new QPushButton(QStringLiteral("Use Bundled"));
    useBundled->setEnabled(!AppSettings::bundledFfmpegPath().isEmpty());
    connect(useBundled, &QPushButton::clicked, this,
            [this] { m_ffmpeg->setText(QDir::toNativeSeparators(AppSettings::bundledFfmpegPath())); });
    fr->addWidget(useBundled);
    tf->addRow(QStringLiteral("ffmpeg:"), fr);
    const bool bundled =
        !AppSettings::bundledFfmpegPath().isEmpty() && AppSettings::ffmpegPath() == AppSettings::bundledFfmpegPath();
    m_ffmpegInfo =
        new QLabel(QStringLiteral("%1%2\nffprobe: %3")
                       .arg(bundled ? QStringLiteral("Using the FFmpeg bundled with Open Media Encoder.\n") : QString(),
                            EncoderCaps::instance().version(), QDir::toNativeSeparators(AppSettings::ffprobePath())));
    m_ffmpegInfo->setObjectName("muted");
    m_ffmpegInfo->setWordWrap(true);
    tf->addRow(QString(), m_ffmpegInfo);
    tabs->addTab(tools, QStringLiteral("FFmpeg"));

    // ---- Recording
    auto *recording = new QWidget;
    auto *rf = new QFormLayout(recording);
    m_reconnect = new QCheckBox(QStringLiteral("If a stream drops, keep trying and record into a new part"));
    m_reconnect->setChecked(AppSettings::reconnectRecordings());
    rf->addRow(QString(), m_reconnect);
    const QString configuredYtDlp = AppSettings::value("tools/ytdlp").toString();
    m_ytDlp = new QLineEdit(QDir::toNativeSeparators(configuredYtDlp));
    m_ytDlp->setPlaceholderText(QStringLiteral("Automatic"));
    auto *ytBrowse = new QPushButton(QStringLiteral("Browse…"));
    connect(ytBrowse, &QPushButton::clicked, this, [this] {
        const QString f = QFileDialog::getOpenFileName(this, QStringLiteral("Locate yt-dlp"), m_ytDlp->text(),
                                                       QStringLiteral("yt-dlp (yt-dlp*)"));
        if (!f.isEmpty())
            m_ytDlp->setText(QDir::toNativeSeparators(f));
    });
    auto *ytDownload = new QPushButton(QStringLiteral("Download Latest"));
    ytDownload->setToolTip(QStringLiteral("Download the latest yt-dlp from the official project on GitHub"));
    connect(ytDownload, &QPushButton::clicked, this, [this, ytDownload] {
        ytDownload->setEnabled(false);
        m_ytDlpInfo->setText(QStringLiteral("Downloading…"));
        auto *installer = new YtDlpInstaller(this);
        connect(installer, &YtDlpInstaller::progress, this, [this](qint64 got, qint64 total) {
            if (total > 0)
                m_ytDlpInfo->setText(QStringLiteral("Downloading… %1%").arg(int(got * 100 / total)));
        });
        connect(installer, &YtDlpInstaller::finished, this,
                [this, installer, ytDownload](bool ok, const QString &error) {
                    installer->deleteLater();
                    ytDownload->setEnabled(true);
                    if (ok) {
                        m_ytDlp->clear(); // use the managed copy
                        refreshYtDlpInfo();
                    } else {
                        m_ytDlpInfo->setText(error);
                    }
                });
        installer->start();
    });
    auto *yr = new QHBoxLayout;
    yr->addWidget(m_ytDlp, 1);
    yr->addWidget(ytBrowse);
    yr->addWidget(ytDownload);
    rf->addRow(QStringLiteral("yt-dlp:"), yr);
    m_ytDlpInfo = new QLabel;
    m_ytDlpInfo->setObjectName("muted");
    m_ytDlpInfo->setWordWrap(true);
    rf->addRow(QString(), m_ytDlpInfo);
    auto *ytAbout = new QLabel(
        QStringLiteral("yt-dlp finds the video stream on web pages such as YouTube and Twitch. It's downloaded the "
                       "first time you record from a web page and checks for updates once a day. Direct stream "
                       "links (.m3u8, RTMP, RTSP, SRT) don't need it."));
    ytAbout->setObjectName("muted");
    ytAbout->setWordWrap(true);
    rf->addRow(QString(), ytAbout);
    tabs->addTab(recording, QStringLiteral("Recording"));
    refreshYtDlpInfo();

    // ---- Captions (speech to text)
    auto *captions = new QWidget;
    auto *cf = new QFormLayout(captions);
    auto *capAbout = new QLabel(
        QStringLiteral("Automatic captions turn speech into text on this computer with whisper.cpp; nothing is "
                       "uploaded. Turn them on per output in Export Settings > Effects. The speech model is "
                       "downloaded once, from the official whisper.cpp models on Hugging Face."));
    capAbout->setObjectName("muted");
    capAbout->setWordWrap(true);
    cf->addRow(capAbout);
    m_speechModel = new QComboBox;
    for (const Whisper::Model &m : Whisper::models())
        m_speechModel->addItem(m.label, m.id);
    m_speechModel->setCurrentIndex(qMax(0, m_speechModel->findData(Whisper::selectedModelId())));
    auto *getModel = new QPushButton(QStringLiteral("Download"));
    connect(getModel, &QPushButton::clicked, this, [this] {
        SpeechModelPrompt::download(this, m_speechModel->currentData().toString());
        refreshSpeechInfo();
    });
    connect(m_speechModel, &QComboBox::currentIndexChanged, this, [this, getModel] {
        refreshSpeechInfo();
        const Whisper::Model *m = Whisper::model(m_speechModel->currentData().toString());
        getModel->setEnabled(m && !Whisper::isInstalled(*m));
    });
    auto *mr = new QHBoxLayout;
    mr->addWidget(m_speechModel, 1);
    mr->addWidget(getModel);
    cf->addRow(QStringLiteral("Speech model:"), mr);
    m_speechInfo = new QLabel;
    m_speechInfo->setObjectName("muted");
    m_speechInfo->setWordWrap(true);
    cf->addRow(QString(), m_speechInfo);
    if (WhisperGpu::supported()) {
        m_speechGpu = new QCheckBox(QStringLiteral("Use the NVIDIA GPU for speech recognition"));
        m_speechGpu->setChecked(WhisperGpu::enabled());
        m_gpuButton = new QPushButton;
        connect(m_gpuButton, &QPushButton::clicked, this, [this] {
            if (WhisperGpu::installed()) {
                if (QMessageBox::question(this, QStringLiteral("NVIDIA GPU Support"),
                                          QStringLiteral("Remove the downloaded NVIDIA GPU support? Speech recognition "
                                                         "will run on the processor.")) == QMessageBox::Yes)
                    WhisperGpu::uninstall();
            } else {
                SpeechModelPrompt::downloadGpu(this);
            }
            refreshSpeechInfo();
        });
        auto *gr = new QHBoxLayout;
        gr->addWidget(m_speechGpu, 1);
        gr->addWidget(m_gpuButton);
        cf->addRow(QStringLiteral("Acceleration:"), gr);
        m_gpuInfo = new QLabel;
        m_gpuInfo->setObjectName("muted");
        m_gpuInfo->setWordWrap(true);
        cf->addRow(QString(), m_gpuInfo);
    }
    tabs->addTab(captions, QStringLiteral("Captions"));
    refreshSpeechInfo();
    if (const Whisper::Model *m = Whisper::model(m_speechModel->currentData().toString()))
        getModel->setEnabled(!Whisper::isInstalled(*m));

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &PreferencesDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &PreferencesDialog::reject);
    auto *v = new QVBoxLayout(this);
    v->addWidget(tabs, 1);
    v->addWidget(buttons);
}

void PreferencesDialog::accept()
{
    AppSettings::setValue("output/mode", m_outCustom->isChecked() ? "custom" : "source");
    AppSettings::setValue("output/customDir", QDir::fromNativeSeparators(m_customDir->text()));
    AppSettings::setValue("output/increment", m_increment->isChecked());
    AppSettings::setValue("encode/deletePartial", m_deletePartial->isChecked());
    AppSettings::setValue("queue/autoStartIdleMinutes", m_autoStart->value());
    AppSettings::setValue("queue/notify", m_notify->isChecked());
    AppSettings::setValue("presets/default", m_defaultPreset->currentData().toString());
    AppSettings::setValue("encode/parallel", m_parallel->value());
    AppSettings::setValue("encode/hwDecode", m_hwDecode->isChecked());
    if (m_preferGpu->isChecked() != AppSettings::preferGpuEncoding()) {
        AppSettings::setValue("encode/preferGpu", m_preferGpu->isChecked());
        PresetManager::instance().load();
    }
    AppSettings::setValue("encode/preventSleep", m_preventSleep->isChecked());
    AppSettings::setValue("recorder/reconnect", m_reconnect->isChecked());
    Whisper::setSelectedModelId(m_speechModel->currentData().toString());
    if (m_speechGpu)
        WhisperGpu::setEnabled(m_speechGpu->isChecked());
    AppSettings::setYtDlpPath(QDir::fromNativeSeparators(m_ytDlp->text().trimmed()));
    const QString ff = QDir::fromNativeSeparators(m_ffmpeg->text().trimmed());
    if (ff != AppSettings::ffmpegPath()) {
        AppSettings::setFfmpegPath(ff);
        m_ffmpegChanged = true;
    }
    QDialog::accept();
}

void PreferencesDialog::refreshSpeechInfo()
{
    const Whisper::Model *m = Whisper::model(m_speechModel->currentData().toString());
    if (!m)
        return;
    QStringList lines{m->description};
    lines << (Whisper::isInstalled(*m) ? QStringLiteral("Downloaded.")
                                       : QStringLiteral("Not downloaded yet (%1). It downloads the first time an "
                                                        "output uses automatic captions.")
                                             .arg(formatBytes(m->size)));
    if (AppSettings::whisperCliPath().isEmpty())
        lines << QStringLiteral("whisper-cli wasn't found next to the app, so captions can't be made.");
    lines << QStringLiteral("Models folder: %1").arg(QDir::toNativeSeparators(Whisper::modelsDir()));
    m_speechInfo->setText(lines.join('\n'));
    if (m_gpuButton) {
        const bool have = WhisperGpu::installed();
        m_gpuButton->setText(have ? QStringLiteral("Remove")
                                  : QStringLiteral("Download (%1)").arg(formatBytes(WhisperGpu::downloadSize())));
        m_gpuButton->setEnabled(have || WhisperGpu::nvidiaPresent());
        m_speechGpu->setEnabled(have);
        m_gpuInfo->setText(
            have ? QStringLiteral("NVIDIA GPU support is installed: captions and filler-word removal run on the GPU, "
                                  "several times faster than on the processor.")
            : WhisperGpu::nvidiaPresent()
                ? QStringLiteral("This computer has an NVIDIA GPU. Download GPU support (whisper.cpp's CUDA build) "
                                 "to make captions and filler-word removal several times faster.")
                : QStringLiteral("GPU speech recognition needs an NVIDIA graphics card; none was found, so it runs "
                                 "on the processor."));
    }
}

void PreferencesDialog::refreshYtDlpInfo()
{
    const QString typed = QDir::fromNativeSeparators(m_ytDlp->text().trimmed());
    const QString path = typed.isEmpty() ? AppSettings::ytDlpPath() : typed;
    if (path.isEmpty()) {
        m_ytDlpInfo->setText(QStringLiteral("Not installed yet."));
        return;
    }
    m_ytDlpInfo->setText(QStringLiteral("Checking…"));
    auto *watcher = new QFutureWatcher<QString>(this);
    connect(watcher, &QFutureWatcher<QString>::finished, this, [this, watcher, path] {
        const QString v = watcher->result();
        watcher->deleteLater();
        m_ytDlpInfo->setText(v.isEmpty()
                                 ? QStringLiteral("%1 couldn't be run.").arg(QDir::toNativeSeparators(path))
                                 : QStringLiteral("Version %1%2\n%3")
                                       .arg(v, YtDlp::isManaged(path) ? QStringLiteral(", kept up to date") : QString(),
                                            QDir::toNativeSeparators(path)));
    });
    watcher->setFuture(QtConcurrent::run([path] { return YtDlp::version(path); }));
}
