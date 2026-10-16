#include "PreferencesDialog.h"

#include "core/AppSettings.h"
#include "core/EncoderCaps.h"
#include "core/Formats.h"
#include "core/PresetManager.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

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
    const QString ff = QDir::fromNativeSeparators(m_ffmpeg->text().trimmed());
    if (ff != AppSettings::ffmpegPath()) {
        AppSettings::setFfmpegPath(ff);
        m_ffmpegChanged = true;
    }
    QDialog::accept();
}
