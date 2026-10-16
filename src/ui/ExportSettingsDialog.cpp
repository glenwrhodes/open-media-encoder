#include "ExportSettingsDialog.h"

#include "FrameGrabber.h"
#include "FramePreview.h"
#include "Theme.h"
#include "TimelineSlider.h"
#include "core/AppSettings.h"
#include "core/CommandBuilder.h"
#include "core/EncoderCaps.h"
#include "core/Formats.h"
#include "core/PresetManager.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QShortcut>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <cmath>
#include <functional>

namespace {

const QList<QPair<QString, QSize>> kFrameSizes = {
    {"Match Source", {0, 0}},
    {"7680 x 4320 (8K UHD)", {7680, 4320}},
    {"4096 x 2160 (DCI 4K)", {4096, 2160}},
    {"3840 x 2160 (4K UHD)", {3840, 2160}},
    {"2560 x 1440 (QHD)", {2560, 1440}},
    {"1920 x 1080 (Full HD)", {1920, 1080}},
    {"1280 x 720 (HD)", {1280, 720}},
    {"1080 x 1920 (Vertical 9:16)", {1080, 1920}},
    {"1080 x 1350 (Portrait 4:5)", {1080, 1350}},
    {"1080 x 1080 (Square)", {1080, 1080}},
    {"854 x 480 (SD Wide)", {854, 480}},
    {"720 x 480 (NTSC)", {720, 480}},
    {"720 x 576 (PAL)", {720, 576}},
    {"640 x 360", {640, 360}},
};

const QList<double> kFrameRates = {10, 12, 12.5, 15, 23.976, 24, 25, 29.97, 30, 48, 50, 59.94, 60, 120};

const QStringList kPositions = {"top-left",     "top-center",  "top-right",     "center-left", "center",
                                "center-right", "bottom-left", "bottom-center", "bottom-right"};

QString positionLabel(const QString &p)
{
    QString s = p;
    s.replace('-', ' ');
    if (!s.isEmpty())
        s[0] = s[0].toUpper();
    return s;
}

QComboBox *positionCombo()
{
    auto *c = new QComboBox;
    for (const QString &p : kPositions)
        c->addItem(positionLabel(p), p);
    return c;
}

void selectData(QComboBox *c, const QVariant &v)
{
    const int i = c->findData(v);
    if (i >= 0)
        c->setCurrentIndex(i);
}

QString fpsText(double fps) { return QString::number(fps, 'f', 3).remove(QRegularExpression("\\.?0+$")); }

QWidget *fileRow(QLineEdit *edit, const std::function<void()> &browse)
{
    auto *w = new QWidget;
    auto *l = new QHBoxLayout(w);
    l->setContentsMargins(0, 0, 0, 0);
    l->addWidget(edit, 1);
    auto *b = new QPushButton(QStringLiteral("Browse…"));
    QObject::connect(b, &QPushButton::clicked, browse);
    l->addWidget(b);
    return w;
}

QLabel *muted(const QString &text)
{
    auto *l = new QLabel(text);
    l->setObjectName("muted");
    l->setWordWrap(true);
    return l;
}

} // namespace

ExportSettingsDialog::ExportSettingsDialog(Mode mode, const ExportSettings &settings, const QString &presetId,
                                           QWidget *parent)
    : QDialog(parent)
    , m_mode(mode)
    , m_settings(settings)
    , m_presetId(presetId)
{
    setWindowTitle(mode == Mode::Preset ? QStringLiteral("Preset Settings") : QStringLiteral("Export Settings"));
    resize(1320, 860);

    m_grabber = new FrameGrabber(this);
    m_previewTimer = new QTimer(this);
    m_previewTimer->setSingleShot(true);
    m_previewTimer->setInterval(250);
    connect(m_previewTimer, &QTimer::timeout, this, &ExportSettingsDialog::renderPreview);
    connect(m_grabber, &FrameGrabber::frameReady, this, [this](const QImage &img) {
        m_preview->setBusy(false);
        m_preview->setImage(img);
    });
    connect(m_grabber, &FrameGrabber::failed, this, [this](const QString &err) {
        m_preview->setBusy(false);
        m_preview->setImage({});
        m_preview->setMessage(QStringLiteral("Preview unavailable:\n%1").arg(err));
    });

    auto *split = new QSplitter(Qt::Horizontal);
    split->addWidget(buildPreviewSide());
    split->addWidget(buildSettingsSide());
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 2);
    split->setSizes({760, 560});

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setObjectName("primary");
    connect(buttons, &QDialogButtonBox::accepted, this, &ExportSettingsDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &ExportSettingsDialog::reject);

    m_estimate = new QLabel;
    m_estimate->setObjectName("muted");

    auto *bottom = new QHBoxLayout;
    bottom->addWidget(m_estimate, 1);
    bottom->addWidget(buttons);

    auto *root = new QVBoxLayout(this);
    root->addWidget(split, 1);
    root->addLayout(bottom);

    loadToUi();
    if (m_mode == Mode::Preset)
        m_preview->setMessage(
            QStringLiteral("Presets are edited without a source.\nAdd media to the queue to preview."));
}

// ------------------------------------------------------------------ layout

QWidget *ExportSettingsDialog::buildPreviewSide()
{
    auto *w = new QWidget;
    auto *v = new QVBoxLayout(w);
    v->setContentsMargins(0, 0, 6, 0);

    m_viewTabs = new QTabBar;
    m_viewTabs->addTab(QStringLiteral("Source"));
    m_viewTabs->addTab(QStringLiteral("Output"));
    m_viewTabs->setCurrentIndex(1);
    m_viewTabs->setTabToolTip(0, QStringLiteral("The full source clip. Set In and Out points here."));
    m_viewTabs->setTabToolTip(1, QStringLiteral("What will be encoded: only the In-Out range, with effects applied."));
    connect(m_viewTabs, &QTabBar::currentChanged, this, [this] {
        syncTimeline();
        updateDerived();
    });
    v->addWidget(m_viewTabs);

    m_preview = new FramePreview;
    v->addWidget(m_preview, 1);

    auto *timeRow = new QHBoxLayout;
    m_timeline = new TimelineSlider;
    connect(m_timeline, &TimelineSlider::positionChanged, this, [this] {
        updateTimeLabel();
        schedulePreview();
    });
    m_timeLabel = new QLabel(QStringLiteral("00:00:00:00"));
    m_timeLabel->setMinimumWidth(170);
    timeRow->addWidget(m_timeLabel);
    timeRow->addWidget(m_timeline, 1);
    v->addLayout(timeRow);

    auto *rangeRow = new QHBoxLayout;
    m_setIn = new QPushButton(QStringLiteral("Set In"));
    m_setOut = new QPushButton(QStringLiteral("Set Out"));
    m_clearRange = new QPushButton(QStringLiteral("Entire Clip"));
    m_rangeLabel = new QLabel;
    m_rangeLabel->setObjectName("muted");
    m_setIn->setToolTip(QStringLiteral("Set the In point at the playhead (I)"));
    m_setOut->setToolTip(QStringLiteral("Set the Out point at the playhead (O)"));
    connect(m_setIn, &QPushButton::clicked, this, &ExportSettingsDialog::setInPoint);
    connect(m_setOut, &QPushButton::clicked, this, &ExportSettingsDialog::setOutPoint);
    connect(m_clearRange, &QPushButton::clicked, this, [this] {
        m_in = m_out = -1;
        updateRangeLabel();
        syncTimeline();
        updateDerived();
    });
    auto *inKey = new QShortcut(QKeySequence(Qt::Key_I), w, nullptr, nullptr, Qt::WidgetWithChildrenShortcut);
    connect(inKey, &QShortcut::activated, this, &ExportSettingsDialog::setInPoint);
    auto *outKey = new QShortcut(QKeySequence(Qt::Key_O), w, nullptr, nullptr, Qt::WidgetWithChildrenShortcut);
    connect(outKey, &QShortcut::activated, this, &ExportSettingsDialog::setOutPoint);
    m_rangeTitle = new QLabel(QStringLiteral("Source Range:"));
    rangeRow->addWidget(m_rangeTitle);
    rangeRow->addWidget(m_setIn);
    rangeRow->addWidget(m_setOut);
    rangeRow->addWidget(m_clearRange);
    rangeRow->addWidget(m_rangeLabel, 1);
    v->addLayout(rangeRow);

    m_cropGroup = new QGroupBox(QStringLiteral("Crop Source"));
    m_cropGroup->setCheckable(true);
    auto *cl = new QHBoxLayout(m_cropGroup);
    auto makeCrop = [&](const QString &label) {
        auto *s = new QSpinBox;
        s->setRange(0, 8192);
        s->setSuffix(QStringLiteral(" px"));
        cl->addWidget(new QLabel(label));
        cl->addWidget(s);
        connect(s, &QSpinBox::valueChanged, this, &ExportSettingsDialog::onFieldChanged);
        return s;
    };
    m_cropL = makeCrop(QStringLiteral("Left"));
    m_cropT = makeCrop(QStringLiteral("Top"));
    m_cropR = makeCrop(QStringLiteral("Right"));
    m_cropB = makeCrop(QStringLiteral("Bottom"));
    cl->addStretch();
    connect(m_cropGroup, &QGroupBox::toggled, this, &ExportSettingsDialog::onFieldChanged);
    v->addWidget(m_cropGroup);
    syncTimeline();
    return w;
}

QWidget *ExportSettingsDialog::buildSettingsSide()
{
    auto *w = new QWidget;
    auto *v = new QVBoxLayout(w);
    v->setContentsMargins(6, 0, 0, 0);

    auto *title = new QLabel(QStringLiteral("Export Settings"));
    title->setObjectName("sectionTitle");
    v->addWidget(title);

    auto *form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignRight);
    m_format = new QComboBox;
    for (const FormatDef &f : Formats::all()) {
        const bool usable = (!f.hasVideo() || !EncoderCaps::instance().availableVideoCodecs(f).isEmpty()) &&
                            (f.hasVideo() || !EncoderCaps::instance().availableAudioCodecs(f).isEmpty());
        if (usable)
            m_format->addItem(f.displayName, f.id);
    }
    connect(m_format, &QComboBox::activated, this, &ExportSettingsDialog::onFormatChanged);
    form->addRow(QStringLiteral("Format:"), m_format);

    auto *presetRow = new QWidget;
    auto *pr = new QHBoxLayout(presetRow);
    pr->setContentsMargins(0, 0, 0, 0);
    m_preset = new QComboBox;
    m_preset->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    connect(m_preset, &QComboBox::activated, this, &ExportSettingsDialog::onPresetChanged);
    m_savePreset = new QToolButton;
    m_savePreset->setIcon(Theme::icon("export"));
    m_savePreset->setToolTip(QStringLiteral("Save Preset"));
    m_importPreset = new QToolButton;
    m_importPreset->setIcon(Theme::icon("import"));
    m_importPreset->setToolTip(QStringLiteral("Import Preset"));
    m_deletePreset = new QToolButton;
    m_deletePreset->setIcon(Theme::icon("trash"));
    m_deletePreset->setToolTip(QStringLiteral("Delete Preset"));
    connect(m_savePreset, &QToolButton::clicked, this, &ExportSettingsDialog::onSavePreset);
    connect(m_importPreset, &QToolButton::clicked, this, &ExportSettingsDialog::onImportPreset);
    connect(m_deletePreset, &QToolButton::clicked, this, &ExportSettingsDialog::onDeletePreset);
    pr->addWidget(m_preset, 1);
    pr->addWidget(m_savePreset);
    pr->addWidget(m_importPreset);
    pr->addWidget(m_deletePreset);
    form->addRow(QStringLiteral("Preset:"), presetRow);

    m_comment = new QLineEdit;
    connect(m_comment, &QLineEdit::textEdited, this, &ExportSettingsDialog::onFieldChanged);
    form->addRow(QStringLiteral("Comments:"), m_comment);

    m_outputEdit = new QLineEdit;
    auto *outRow = fileRow(m_outputEdit, [this] { onBrowseOutput(); });
    form->addRow(QStringLiteral("Output Name:"), outRow);
    if (m_mode == Mode::Preset) {
        outRow->setVisible(false);
        form->labelForField(outRow)->setVisible(false);
    }

    auto *exportRow = new QHBoxLayout;
    m_exportVideo = new QCheckBox(QStringLiteral("Export Video"));
    m_exportAudio = new QCheckBox(QStringLiteral("Export Audio"));
    connect(m_exportVideo, &QCheckBox::toggled, this, &ExportSettingsDialog::onFieldChanged);
    connect(m_exportAudio, &QCheckBox::toggled, this, &ExportSettingsDialog::onFieldChanged);
    exportRow->addWidget(m_exportVideo);
    exportRow->addWidget(m_exportAudio);
    exportRow->addStretch();
    form->addRow(QString(), exportRow);
    v->addLayout(form);

    auto *summaryCard = new QFrame;
    summaryCard->setObjectName("card");
    auto *sl = new QVBoxLayout(summaryCard);
    m_summary = new QLabel;
    m_summary->setWordWrap(true);
    m_summary->setTextInteractionFlags(Qt::TextSelectableByMouse);
    sl->addWidget(m_summary);
    v->addWidget(summaryCard);

    auto *tabs = new QTabWidget;
    tabs->addTab(buildEffectsTab(), QStringLiteral("Effects"));
    m_videoTab = buildVideoTab();
    tabs->addTab(m_videoTab, QStringLiteral("Video"));
    m_audioTab = buildAudioTab();
    tabs->addTab(m_audioTab, QStringLiteral("Audio"));
    tabs->addTab(buildAdvancedTab(), QStringLiteral("Advanced"));
    tabs->setCurrentIndex(1);
    v->addWidget(tabs, 1);
    return w;
}

QWidget *ExportSettingsDialog::buildVideoTab()
{
    auto *inner = new QWidget;
    auto *v = new QVBoxLayout(inner);

    auto *codecForm = new QFormLayout;
    m_vCodec = new QComboBox;
    connect(m_vCodec, &QComboBox::activated, this, &ExportSettingsDialog::onVideoCodecChanged);
    codecForm->addRow(QStringLiteral("Video Codec:"), m_vCodec);
    v->addLayout(codecForm);

    auto *basic = new QGroupBox(QStringLiteral("Basic Video Settings"));
    auto *bf = new QFormLayout(basic);
    m_sizePreset = new QComboBox;
    m_sizePreset->addItem(QStringLiteral("Custom"), QSize(-1, -1));
    for (const auto &fs : kFrameSizes)
        m_sizePreset->addItem(fs.first, fs.second);
    connect(m_sizePreset, &QComboBox::activated, this, [this] {
        const QSize s = m_sizePreset->currentData().toSize();
        if (s.width() < 0)
            return;
        {
            QSignalBlocker b1(m_width), b2(m_height);
            m_width->setValue(s.width());
            m_height->setValue(s.height());
        }
        onFieldChanged();
    });
    bf->addRow(QStringLiteral("Frame Size:"), m_sizePreset);

    auto *wh = new QHBoxLayout;
    m_width = new QSpinBox;
    m_width->setRange(0, 16384);
    m_width->setSpecialValueText(QStringLiteral("Match"));
    m_height = new QSpinBox;
    m_height->setRange(0, 16384);
    m_height->setSpecialValueText(QStringLiteral("Match"));
    connect(m_width, &QSpinBox::valueChanged, this, &ExportSettingsDialog::onFieldChanged);
    connect(m_height, &QSpinBox::valueChanged, this, &ExportSettingsDialog::onFieldChanged);
    wh->addWidget(new QLabel(QStringLiteral("Width")));
    wh->addWidget(m_width);
    wh->addWidget(new QLabel(QStringLiteral("Height")));
    wh->addWidget(m_height);
    bf->addRow(QString(), wh);

    m_scaleMode = new QComboBox;
    m_scaleMode->addItem(QStringLiteral("Scale to Fit (letterbox)"), "fit");
    m_scaleMode->addItem(QStringLiteral("Scale to Fill (crop)"), "fill");
    m_scaleMode->addItem(QStringLiteral("Stretch to Fill"), "stretch");
    connect(m_scaleMode, &QComboBox::activated, this, &ExportSettingsDialog::onFieldChanged);
    bf->addRow(QStringLiteral("Source Scaling:"), m_scaleMode);

    m_fps = new QComboBox;
    m_fps->setEditable(true);
    m_fps->addItem(QStringLiteral("Match Source"), 0.0);
    for (double r : kFrameRates)
        m_fps->addItem(fpsText(r), r);
    connect(m_fps, &QComboBox::currentTextChanged, this, &ExportSettingsDialog::onFieldChanged);
    bf->addRow(QStringLiteral("Frame Rate:"), m_fps);

    m_fpsMode = new QComboBox;
    m_fpsMode->addItem(QStringLiteral("Frame Sampling"), "frame");
    m_fpsMode->addItem(QStringLiteral("Frame Blending"), "blend");
    m_fpsMode->addItem(QStringLiteral("Optical Flow"), "optical");
    connect(m_fpsMode, &QComboBox::activated, this, &ExportSettingsDialog::onFieldChanged);
    bf->addRow(QStringLiteral("Time Interpolation:"), m_fpsMode);

    m_fieldOrder = new QComboBox;
    m_fieldOrder->addItem(QStringLiteral("Progressive"), "progressive");
    m_fieldOrder->addItem(QStringLiteral("Upper First"), "tff");
    m_fieldOrder->addItem(QStringLiteral("Lower First"), "bff");
    connect(m_fieldOrder, &QComboBox::activated, this, &ExportSettingsDialog::onFieldChanged);
    bf->addRow(QStringLiteral("Field Order:"), m_fieldOrder);

    m_deinterlace = new QCheckBox(QStringLiteral("Always deinterlace source"));
    connect(m_deinterlace, &QCheckBox::toggled, this, &ExportSettingsDialog::onFieldChanged);
    bf->addRow(QString(), m_deinterlace);
    v->addWidget(basic);

    auto *enc = new QGroupBox(QStringLiteral("Encoding Settings"));
    auto *ef = new QFormLayout(enc);
    m_speedLabel = new QLabel(QStringLiteral("Encoding Speed:"));
    m_speed = new QComboBox;
    connect(m_speed, &QComboBox::activated, this, &ExportSettingsDialog::onFieldChanged);
    ef->addRow(m_speedLabel, m_speed);
    m_profile = new QComboBox;
    connect(m_profile, &QComboBox::activated, this, &ExportSettingsDialog::onFieldChanged);
    ef->addRow(QStringLiteral("Profile:"), m_profile);
    m_level = new QComboBox;
    connect(m_level, &QComboBox::activated, this, &ExportSettingsDialog::onFieldChanged);
    ef->addRow(QStringLiteral("Level:"), m_level);
    m_pixFmt = new QComboBox;
    connect(m_pixFmt, &QComboBox::activated, this, &ExportSettingsDialog::onFieldChanged);
    ef->addRow(QStringLiteral("Pixel Format:"), m_pixFmt);
    v->addWidget(enc);

    auto *br = new QGroupBox(QStringLiteral("Bitrate Settings"));
    auto *rf = new QFormLayout(br);
    m_rc = new QComboBox;
    connect(m_rc, &QComboBox::activated, this, &ExportSettingsDialog::onFieldChanged);
    rf->addRow(QStringLiteral("Bitrate Encoding:"), m_rc);
    m_target = new QDoubleSpinBox;
    m_target->setRange(0.05, 1000);
    m_target->setDecimals(2);
    m_target->setSuffix(QStringLiteral(" Mbps"));
    connect(m_target, &QDoubleSpinBox::valueChanged, this, &ExportSettingsDialog::onFieldChanged);
    rf->addRow(QStringLiteral("Target Bitrate:"), m_target);
    m_max = new QDoubleSpinBox;
    m_max->setRange(0.05, 1000);
    m_max->setDecimals(2);
    m_max->setSuffix(QStringLiteral(" Mbps"));
    connect(m_max, &QDoubleSpinBox::valueChanged, this, &ExportSettingsDialog::onFieldChanged);
    rf->addRow(QStringLiteral("Maximum Bitrate:"), m_max);
    auto *qrow = new QHBoxLayout;
    m_qualitySlider = new QSlider(Qt::Horizontal);
    m_quality = new QSpinBox;
    connect(m_qualitySlider, &QSlider::valueChanged, m_quality, &QSpinBox::setValue);
    connect(m_quality, &QSpinBox::valueChanged, m_qualitySlider, &QSlider::setValue);
    connect(m_quality, &QSpinBox::valueChanged, this, &ExportSettingsDialog::onFieldChanged);
    qrow->addWidget(m_qualitySlider, 1);
    qrow->addWidget(m_quality);
    rf->addRow(QStringLiteral("Quality (lower = better):"), qrow);
    v->addWidget(br);

    auto *adv = new QGroupBox(QStringLiteral("Advanced Settings"));
    auto *af = new QFormLayout(adv);
    m_gop = new QSpinBox;
    m_gop->setRange(0, 1000);
    m_gop->setSpecialValueText(QStringLiteral("Automatic"));
    m_gop->setSuffix(QStringLiteral(" frames"));
    connect(m_gop, &QSpinBox::valueChanged, this, &ExportSettingsDialog::onFieldChanged);
    af->addRow(QStringLiteral("Key Frame Distance:"), m_gop);
    m_imageSeq = new QCheckBox(QStringLiteral("Export as Sequence"));
    connect(m_imageSeq, &QCheckBox::toggled, this, &ExportSettingsDialog::onFieldChanged);
    af->addRow(QString(), m_imageSeq);
    m_stillDuration = new QDoubleSpinBox;
    m_stillDuration->setRange(0.1, 3600);
    m_stillDuration->setSuffix(QStringLiteral(" s"));
    connect(m_stillDuration, &QDoubleSpinBox::valueChanged, this, &ExportSettingsDialog::onFieldChanged);
    af->addRow(QStringLiteral("Still Image Duration:"), m_stillDuration);
    m_maxQuality = new QCheckBox(QStringLiteral("Use Maximum Render Quality (Lanczos scaling)"));
    connect(m_maxQuality, &QCheckBox::toggled, this, &ExportSettingsDialog::onFieldChanged);
    af->addRow(QString(), m_maxQuality);
    m_fpsLimit = new QComboBox;
    m_fpsLimit->addItem(QStringLiteral("No Limit"), 0.0);
    m_fpsLimit->addItem(QStringLiteral("30 fps"), 30.0);
    m_fpsLimit->addItem(QStringLiteral("60 fps"), 60.0);
    m_fpsLimit->setToolTip(QStringLiteral("When the frame rate matches the source, sources faster than this are "
                                          "converted down to it (e.g. 120 fps phone footage for a 60 fps platform)."));
    connect(m_fpsLimit, &QComboBox::activated, this, &ExportSettingsDialog::onFieldChanged);
    af->addRow(QStringLiteral("Maximum Frame Rate:"), m_fpsLimit);
    m_scaleBitrate = new QCheckBox(QStringLiteral("Raise bitrate 1.5x for frame rates above 30 fps"));
    connect(m_scaleBitrate, &QCheckBox::toggled, this, &ExportSettingsDialog::onFieldChanged);
    af->addRow(QString(), m_scaleBitrate);
    v->addWidget(adv);
    v->addStretch();

    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(inner);
    return scroll;
}

QWidget *ExportSettingsDialog::buildAudioTab()
{
    auto *w = new QWidget;
    auto *v = new QVBoxLayout(w);
    auto *g = new QGroupBox(QStringLiteral("Basic Audio Settings"));
    auto *f = new QFormLayout(g);
    m_aCodec = new QComboBox;
    connect(m_aCodec, &QComboBox::activated, this, &ExportSettingsDialog::onAudioCodecChanged);
    f->addRow(QStringLiteral("Audio Codec:"), m_aCodec);
    m_aRate = new QComboBox;
    connect(m_aRate, &QComboBox::activated, this, &ExportSettingsDialog::onFieldChanged);
    f->addRow(QStringLiteral("Sample Rate:"), m_aRate);
    m_aChannels = new QComboBox;
    m_aChannels->addItem(QStringLiteral("Match Source"), 0);
    m_aChannels->addItem(QStringLiteral("Mono"), 1);
    m_aChannels->addItem(QStringLiteral("Stereo"), 2);
    m_aChannels->addItem(QStringLiteral("5.1"), 6);
    connect(m_aChannels, &QComboBox::activated, this, &ExportSettingsDialog::onFieldChanged);
    f->addRow(QStringLiteral("Channels:"), m_aChannels);
    m_aBitrate = new QComboBox;
    connect(m_aBitrate, &QComboBox::activated, this, &ExportSettingsDialog::onFieldChanged);
    f->addRow(QStringLiteral("Bitrate:"), m_aBitrate);
    v->addWidget(g);
    v->addStretch();
    return w;
}

QWidget *ExportSettingsDialog::buildEffectsTab()
{
    auto *inner = new QWidget;
    auto *v = new QVBoxLayout(inner);
    auto checkGroup = [&](const QString &title) {
        auto *g = new QGroupBox(title);
        g->setCheckable(true);
        connect(g, &QGroupBox::toggled, this, &ExportSettingsDialog::onFieldChanged);
        v->addWidget(g);
        return g;
    };
    auto spin = [&](int lo, int hi, const QString &suffix = {}) {
        auto *s = new QSpinBox;
        s->setRange(lo, hi);
        s->setSuffix(suffix);
        connect(s, &QSpinBox::valueChanged, this, &ExportSettingsDialog::onFieldChanged);
        return s;
    };

    // LUT
    m_lutGroup = checkGroup(QStringLiteral("Color LUT (Look)"));
    {
        auto *f = new QFormLayout(m_lutGroup);
        m_lutPath = new QLineEdit;
        m_lutPath->setPlaceholderText(QStringLiteral(".cube / .3dl LUT file"));
        connect(m_lutPath, &QLineEdit::editingFinished, this, &ExportSettingsDialog::onFieldChanged);
        f->addRow(QStringLiteral("LUT:"), fileRow(m_lutPath, [this] {
                      const QString p =
                          QFileDialog::getOpenFileName(this, QStringLiteral("Choose LUT"), m_lutPath->text(),
                                                       QStringLiteral("LUT files (*.cube *.3dl *.dat *.m3d *.csp)"));
                      if (!p.isEmpty()) {
                          m_lutPath->setText(p);
                          onFieldChanged();
                      }
                  }));
    }

    // Image overlay
    m_imgGroup = checkGroup(QStringLiteral("Image Overlay"));
    {
        auto *f = new QFormLayout(m_imgGroup);
        m_imgPath = new QLineEdit;
        connect(m_imgPath, &QLineEdit::editingFinished, this, &ExportSettingsDialog::onFieldChanged);
        f->addRow(QStringLiteral("Image:"), fileRow(m_imgPath, [this] {
                      const QString p = QFileDialog::getOpenFileName(
                          this, QStringLiteral("Choose Overlay Image"), m_imgPath->text(),
                          QStringLiteral("Images (*.png *.jpg *.jpeg *.tif *.tiff *.bmp *.webp)"));
                      if (!p.isEmpty()) {
                          m_imgPath->setText(p);
                          onFieldChanged();
                      }
                  }));
        m_imgPos = positionCombo();
        connect(m_imgPos, &QComboBox::activated, this, &ExportSettingsDialog::onFieldChanged);
        f->addRow(QStringLiteral("Position:"), m_imgPos);
        m_imgOffset = spin(0, 2000, QStringLiteral(" px"));
        f->addRow(QStringLiteral("Offset:"), m_imgOffset);
        m_imgScale = spin(1, 1000, QStringLiteral(" %"));
        f->addRow(QStringLiteral("Size:"), m_imgScale);
        m_imgOpacity = spin(0, 100, QStringLiteral(" %"));
        f->addRow(QStringLiteral("Opacity:"), m_imgOpacity);
    }

    // Text overlay
    m_textGroup = checkGroup(QStringLiteral("Text Overlay"));
    {
        auto *f = new QFormLayout(m_textGroup);
        m_text = new QLineEdit;
        m_text->setPlaceholderText(QStringLiteral("e.g. CONFIDENTIAL — For Review Only"));
        connect(m_text, &QLineEdit::textEdited, this, &ExportSettingsDialog::onFieldChanged);
        f->addRow(QStringLiteral("Text:"), m_text);
        m_textPos = positionCombo();
        connect(m_textPos, &QComboBox::activated, this, &ExportSettingsDialog::onFieldChanged);
        f->addRow(QStringLiteral("Position:"), m_textPos);
        m_textSize = spin(6, 600, QStringLiteral(" px"));
        f->addRow(QStringLiteral("Font Size:"), m_textSize);
        m_textColor = new QPushButton;
        connect(m_textColor, &QPushButton::clicked, this, [this] {
            const QColor c = QColorDialog::getColor(QColor(m_textColor->property("color").toString()), this);
            if (c.isValid()) {
                m_textColor->setProperty("color", c.name());
                m_textColor->setText(c.name());
                m_textColor->setStyleSheet(
                    QStringLiteral("background:%1; color:%2;").arg(c.name(), c.lightness() > 128 ? "black" : "white"));
                onFieldChanged();
            }
        });
        f->addRow(QStringLiteral("Color:"), m_textColor);
        m_textOpacity = spin(0, 100, QStringLiteral(" %"));
        f->addRow(QStringLiteral("Opacity:"), m_textOpacity);
        m_textBox = new QCheckBox(QStringLiteral("Background box"));
        connect(m_textBox, &QCheckBox::toggled, this, &ExportSettingsDialog::onFieldChanged);
        f->addRow(QString(), m_textBox);
    }

    // Timecode overlay
    m_tcGroup = checkGroup(QStringLiteral("Timecode Overlay"));
    {
        auto *f = new QFormLayout(m_tcGroup);
        m_tcPos = positionCombo();
        connect(m_tcPos, &QComboBox::activated, this, &ExportSettingsDialog::onFieldChanged);
        f->addRow(QStringLiteral("Position:"), m_tcPos);
        m_tcSize = spin(8, 400, QStringLiteral(" px"));
        f->addRow(QStringLiteral("Size:"), m_tcSize);
        m_tcFromSource = new QCheckBox(QStringLiteral("Use source timecode when available"));
        connect(m_tcFromSource, &QCheckBox::toggled, this, &ExportSettingsDialog::onFieldChanged);
        f->addRow(QString(), m_tcFromSource);
        m_tcStart = new QLineEdit;
        m_tcStart->setInputMask(QStringLiteral("99:99:99:99"));
        connect(m_tcStart, &QLineEdit::editingFinished, this, &ExportSettingsDialog::onFieldChanged);
        f->addRow(QStringLiteral("Start Timecode:"), m_tcStart);
    }

    // Limiter
    m_limiterGroup = checkGroup(QStringLiteral("Video Limiter"));
    {
        auto *l = new QVBoxLayout(m_limiterGroup);
        l->addWidget(muted(
            QStringLiteral("Clamps luma and chroma to broadcast-legal range (16–235 for 8-bit, 64–940 for 10-bit).")));
    }

    // Loudness
    m_loudGroup = checkGroup(QStringLiteral("Loudness Normalization (ITU BS.1770)"));
    {
        auto *f = new QFormLayout(m_loudGroup);
        m_loudTarget = new QDoubleSpinBox;
        m_loudTarget->setRange(-70, -5);
        m_loudTarget->setSuffix(QStringLiteral(" LUFS"));
        m_loudTarget->setDecimals(1);
        connect(m_loudTarget, &QDoubleSpinBox::valueChanged, this, &ExportSettingsDialog::onFieldChanged);
        auto *standards = new QComboBox;
        standards->addItem(QStringLiteral("Custom"), 0.0);
        standards->addItem(QStringLiteral("EBU R128 (-23 LUFS)"), -23.0);
        standards->addItem(QStringLiteral("ATSC A/85 (-24 LKFS)"), -24.0);
        standards->addItem(QStringLiteral("Streaming (-14 LUFS)"), -14.0);
        standards->addItem(QStringLiteral("Podcast (-16 LUFS)"), -16.0);
        connect(standards, &QComboBox::activated, this, [this, standards] {
            const double v = standards->currentData().toDouble();
            if (v != 0.0)
                m_loudTarget->setValue(v);
        });
        f->addRow(QStringLiteral("Standard:"), standards);
        f->addRow(QStringLiteral("Target Loudness:"), m_loudTarget);
        m_truePeak = new QDoubleSpinBox;
        m_truePeak->setRange(-9, 0);
        m_truePeak->setDecimals(1);
        m_truePeak->setSuffix(QStringLiteral(" dBTP"));
        connect(m_truePeak, &QDoubleSpinBox::valueChanged, this, &ExportSettingsDialog::onFieldChanged);
        f->addRow(QStringLiteral("Max True Peak:"), m_truePeak);
        m_lra = new QDoubleSpinBox;
        m_lra->setRange(1, 50);
        m_lra->setDecimals(1);
        m_lra->setSuffix(QStringLiteral(" LU"));
        connect(m_lra, &QDoubleSpinBox::valueChanged, this, &ExportSettingsDialog::onFieldChanged);
        f->addRow(QStringLiteral("Loudness Range:"), m_lra);
    }

    // Captions
    m_capGroup = checkGroup(QStringLiteral("Burn-in Captions"));
    {
        auto *f = new QFormLayout(m_capGroup);
        m_capPath = new QLineEdit;
        m_capPath->setPlaceholderText(QStringLiteral(".srt / .ass / .vtt file"));
        connect(m_capPath, &QLineEdit::editingFinished, this, &ExportSettingsDialog::onFieldChanged);
        f->addRow(QStringLiteral("Captions:"), fileRow(m_capPath, [this] {
                      const QString p =
                          QFileDialog::getOpenFileName(this, QStringLiteral("Choose Captions"), m_capPath->text(),
                                                       QStringLiteral("Subtitles (*.srt *.ass *.ssa *.vtt)"));
                      if (!p.isEmpty()) {
                          m_capPath->setText(p);
                          onFieldChanged();
                      }
                  }));
        m_capSize = spin(8, 200, QStringLiteral(" pt"));
        f->addRow(QStringLiteral("Font Size:"), m_capSize);
    }
    v->addStretch();

    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(inner);
    return scroll;
}

QWidget *ExportSettingsDialog::buildAdvancedTab()
{
    auto *w = new QWidget;
    auto *v = new QVBoxLayout(w);
    auto *f = new QFormLayout;
    m_vExtra = new QLineEdit;
    m_vExtra->setPlaceholderText(QStringLiteral("Extra ffmpeg video options, e.g. -tune film"));
    connect(m_vExtra, &QLineEdit::editingFinished, this, &ExportSettingsDialog::onFieldChanged);
    f->addRow(QStringLiteral("Video Options:"), m_vExtra);
    m_aExtra = new QLineEdit;
    m_aExtra->setPlaceholderText(QStringLiteral("Extra ffmpeg audio options"));
    connect(m_aExtra, &QLineEdit::editingFinished, this, &ExportSettingsDialog::onFieldChanged);
    f->addRow(QStringLiteral("Audio Options:"), m_aExtra);
    v->addLayout(f);
    v->addWidget(new QLabel(QStringLiteral("Encoder command:")));
    m_cmdPreview = new QPlainTextEdit;
    m_cmdPreview->setReadOnly(true);
    QFont mono(QStringLiteral("Consolas"));
    mono.setStyleHint(QFont::Monospace);
    mono.setPointSizeF(8.5);
    m_cmdPreview->setFont(mono);
    v->addWidget(m_cmdPreview, 1);
    return w;
}

// ------------------------------------------------------------------ source

void ExportSettingsDialog::setSource(const QStringList &files, const MediaInfo &info, double totalDuration)
{
    m_files = files;
    m_info = info;
    m_totalDuration = totalDuration;
    const bool hasVideo = info.valid && info.hasVideo;
    m_timeline->setEnabled(hasVideo && !info.isStill);
    m_timeline->setFrameRate(info.fps);
    m_timeline->setSpan(0, qMax(0.001, previewDuration()));
    m_timeline->setPosition(previewDuration() * 0.1);
    m_cropL->setMaximum(qMax(0, info.displayWidth() - 16));
    m_cropR->setMaximum(qMax(0, info.displayWidth() - 16));
    m_cropT->setMaximum(qMax(0, info.displayHeight() - 16));
    m_cropB->setMaximum(qMax(0, info.displayHeight() - 16));
    if (!info.valid)
        m_preview->setMessage(QStringLiteral("Source could not be read:\n%1").arg(info.error));
    else if (!hasVideo)
        m_preview->setMessage(QStringLiteral("Audio-only source"));
    updateRangeLabel();
    syncTimeline();
    updateDerived();
}

void ExportSettingsDialog::setOutputPath(const QString &path) { m_outputEdit->setText(QDir::toNativeSeparators(path)); }

QString ExportSettingsDialog::outputPath() const { return QDir::fromNativeSeparators(m_outputEdit->text().trimmed()); }

void ExportSettingsDialog::setRange(double in, double out)
{
    m_in = in;
    m_out = out;
    if (in > 0)
        m_timeline->setPosition(in);
    updateRangeLabel();
    syncTimeline();
    updateDerived();
}

double ExportSettingsDialog::sourceDuration() const { return m_totalDuration > 0 ? m_totalDuration : m_info.duration; }

double ExportSettingsDialog::currentTime() const { return m_timeline->position(); }

double ExportSettingsDialog::previewDuration() const { return m_files.size() > 1 ? m_info.duration : sourceDuration(); }

double ExportSettingsDialog::rangeIn() const { return qBound(0.0, m_in, previewDuration()); }

double ExportSettingsDialog::rangeOut() const
{
    const double end = previewDuration();
    return m_out > 0 ? qMin(m_out, end) : end;
}

// Source view scrubs the whole clip and shows In/Out ticks; Output view scrubs only In..Out,
// which is exactly what gets encoded.
void ExportSettingsDialog::syncTimeline()
{
    const bool output = m_viewTabs->currentIndex() == 1;
    // In/Out and crop edit the source, so they're only offered on the Source tab.
    for (QWidget *w : std::initializer_list<QWidget *>{m_setIn, m_setOut, m_clearRange, m_cropGroup})
        w->setVisible(!output);
    m_rangeTitle->setText(output ? QStringLiteral("Encoding:") : QStringLiteral("Source Range:"));
    updateRangeLabel();
    m_timeline->setMarks(m_in, m_out);
    if (output) {
        m_timeline->setSpan(rangeIn(), qMax(rangeOut(), rangeIn() + 0.001));
        m_timeline->setShowMarks(false);
    } else {
        m_timeline->setSpan(0, qMax(0.001, previewDuration()));
        m_timeline->setShowMarks(true);
    }
    updateTimeLabel();
    schedulePreview();
}

void ExportSettingsDialog::updateTimeLabel()
{
    const double fps = m_info.fps;
    const double t = currentTime();
    if (m_viewTabs->currentIndex() == 1)
        m_timeLabel->setText(QStringLiteral("%1 / %2").arg(formatDuration(t - rangeIn(), fps),
                                                           formatDuration(rangeOut() - rangeIn(), fps)));
    else
        m_timeLabel->setText(
            QStringLiteral("%1 / %2").arg(formatDuration(t, fps), formatDuration(previewDuration(), fps)));
}

void ExportSettingsDialog::setInPoint()
{
    if (!m_timeline->isEnabled() || m_viewTabs->currentIndex() != 0)
        return;
    m_in = currentTime();
    if (m_out >= 0 && m_out <= m_in)
        m_out = -1;
    updateRangeLabel();
    syncTimeline();
    updateDerived();
}

void ExportSettingsDialog::setOutPoint()
{
    if (!m_timeline->isEnabled() || m_viewTabs->currentIndex() != 0)
        return;
    const double t = currentTime();
    if (t > qMax(0.0, m_in))
        m_out = t;
    updateRangeLabel();
    syncTimeline();
    updateDerived();
}

void ExportSettingsDialog::updateRangeLabel()
{
    const double fps = m_info.fps;
    const bool output = m_viewTabs && m_viewTabs->currentIndex() == 1;
    if (m_in < 0 && m_out < 0) {
        m_rangeLabel->setText(output ? QStringLiteral("the entire clip (%1). Set In/Out on the Source tab to trim.")
                                           .arg(formatDuration(sourceDuration(), fps))
                                     : QStringLiteral("Entire clip (%1)").arg(formatDuration(sourceDuration(), fps)));
        return;
    }
    const double in = qMax(0.0, m_in);
    const double out = m_out > 0 ? m_out : sourceDuration();
    if (output)
        m_rangeLabel->setText(
            QStringLiteral("%1 → %2 of the source (%3). Change it on the Source tab.")
                .arg(formatDuration(in, fps), formatDuration(out, fps), formatDuration(out - in, fps)));
    else
        m_rangeLabel->setText(
            QStringLiteral("In %1  ·  Out %2  ·  Duration %3")
                .arg(formatDuration(in, fps), formatDuration(out, fps), formatDuration(out - in, fps)));
}

// ------------------------------------------------------------------ ui <-> settings

void ExportSettingsDialog::loadToUi()
{
    m_updating = true;
    const ExportSettings &s = m_settings;
    const FormatDef *f = Formats::format(s.format);

    selectData(m_format, s.format);
    refreshPresetCombo();
    m_comment->setText(s.comment);
    m_exportVideo->setChecked(s.video.enabled);
    m_exportAudio->setChecked(s.audio.enabled);
    m_exportVideo->setEnabled(f && f->hasVideo());
    m_exportAudio->setEnabled(f && f->hasAudio());

    // Video codec list
    m_vCodec->clear();
    if (f) {
        for (const QString &c : EncoderCaps::instance().availableVideoCodecs(*f)) {
            const VideoCodecDef *d = Formats::videoCodec(c);
            m_vCodec->addItem(d ? d->displayName : c, c);
        }
    }
    selectData(m_vCodec, s.video.codec);
    m_width->setValue(s.video.width);
    m_height->setValue(s.video.height);
    {
        const int i = m_sizePreset->findData(QSize(s.video.width, s.video.height));
        m_sizePreset->setCurrentIndex(i >= 0 ? i : 0);
    }
    selectData(m_scaleMode, s.video.scaleMode);
    if (s.video.frameRate <= 0)
        m_fps->setCurrentIndex(0);
    else {
        const int i = m_fps->findData(s.video.frameRate);
        if (i >= 0)
            m_fps->setCurrentIndex(i);
        else
            m_fps->setEditText(fpsText(s.video.frameRate));
    }
    selectData(m_fpsMode, s.video.frameRateMode);
    selectData(m_fieldOrder, s.video.fieldOrder);
    m_deinterlace->setChecked(s.video.deinterlace);
    m_gop->setValue(s.video.keyframeInterval);
    m_imageSeq->setChecked(s.video.imageSequence);
    m_stillDuration->setValue(s.video.stillDuration);
    m_maxQuality->setChecked(s.maxRenderQuality);
    {
        int i = m_fpsLimit->findData(s.video.frameRateLimit);
        if (i < 0) {
            m_fpsLimit->addItem(QStringLiteral("%1 fps").arg(s.video.frameRateLimit), s.video.frameRateLimit);
            i = m_fpsLimit->count() - 1;
        }
        m_fpsLimit->setCurrentIndex(i);
    }
    m_scaleBitrate->setChecked(s.video.scaleBitrateWithFps);
    refreshCodecControls();

    // Audio
    m_aCodec->clear();
    if (f) {
        for (const QString &c : EncoderCaps::instance().availableAudioCodecs(*f)) {
            const AudioCodecDef *d = Formats::audioCodec(c);
            m_aCodec->addItem(d ? d->displayName : c, c);
        }
    }
    selectData(m_aCodec, s.audio.codec);
    refreshAudioControls();

    // Effects
    const EffectsSettings &e = s.effects;
    m_cropGroup->setChecked(e.cropEnabled);
    m_cropL->setValue(e.cropLeft);
    m_cropT->setValue(e.cropTop);
    m_cropR->setValue(e.cropRight);
    m_cropB->setValue(e.cropBottom);
    m_lutGroup->setChecked(e.lutEnabled);
    m_lutPath->setText(e.lutPath);
    m_imgGroup->setChecked(e.imageOverlayEnabled);
    m_imgPath->setText(e.imagePath);
    selectData(m_imgPos, e.imagePosition);
    m_imgOffset->setValue(e.imageOffset);
    m_imgScale->setValue(e.imageScale);
    m_imgOpacity->setValue(e.imageOpacity);
    m_textGroup->setChecked(e.textOverlayEnabled);
    m_text->setText(e.text);
    selectData(m_textPos, e.textPosition);
    m_textSize->setValue(e.textSize);
    const QColor tc(e.textColor);
    m_textColor->setProperty("color", tc.name());
    m_textColor->setText(tc.name());
    m_textColor->setStyleSheet(
        QStringLiteral("background:%1; color:%2;").arg(tc.name(), tc.lightness() > 128 ? "black" : "white"));
    m_textOpacity->setValue(e.textOpacity);
    m_textBox->setChecked(e.textBox);
    m_tcGroup->setChecked(e.timecodeEnabled);
    selectData(m_tcPos, e.timecodePosition);
    m_tcSize->setValue(e.timecodeSize);
    m_tcFromSource->setChecked(e.timecodeFromSource);
    m_tcStart->setText(e.timecodeStart);
    m_limiterGroup->setChecked(e.limiterEnabled);
    m_loudGroup->setChecked(e.loudnessEnabled);
    m_loudTarget->setValue(e.loudnessTarget);
    m_truePeak->setValue(e.truePeak);
    m_lra->setValue(e.loudnessRange);
    m_capGroup->setChecked(e.captionsEnabled);
    m_capPath->setText(e.captionFile);
    m_capSize->setValue(e.captionSize);

    m_vExtra->setText(s.video.extraArgs);
    m_aExtra->setText(s.audio.extraArgs);
    m_updating = false;
    updateDerived();
}

void ExportSettingsDialog::refreshCodecControls()
{
    const bool wasUpdating = m_updating;
    m_updating = true;
    const VideoCodecDef *c = Formats::videoCodec(m_settings.video.codec);
    VideoSettings &v = m_settings.video;

    m_speed->clear();
    if (c) {
        for (const QString &sp : c->speedPresets)
            m_speed->addItem(sp, sp);
        m_speedLabel->setText(c->speedLabel + ":");
    }
    selectData(m_speed, v.speedPreset);
    m_speed->setEnabled(c && !c->speedPresets.isEmpty());

    m_profile->clear();
    if (c)
        for (int i = 0; i < c->profiles.size(); ++i)
            m_profile->addItem(c->profileLabels.value(i, c->profiles[i]), c->profiles[i]);
    selectData(m_profile, v.profile);
    m_profile->setEnabled(c && !c->profiles.isEmpty());

    m_level->clear();
    if (c)
        for (const QString &l : c->levels)
            m_level->addItem(l == "auto" ? QStringLiteral("Auto") : l, l);
    selectData(m_level, v.level);
    m_level->setEnabled(c && !c->levels.isEmpty());

    m_pixFmt->clear();
    m_pixFmt->addItem(QStringLiteral("Auto (%1)")
                          .arg(CommandBuilder::resolvePixelFormat(
                              [&] {
                                  ExportSettings t = m_settings;
                                  t.video.pixelFormat.clear();
                                  return t;
                              }(),
                              m_info)),
                      QString());
    if (c)
        for (const QString &p : c->pixelFormats)
            m_pixFmt->addItem(p, p);
    selectData(m_pixFmt, v.pixelFormat);
    m_pixFmt->setEnabled(c && !c->pixelFormats.isEmpty());

    m_rc->clear();
    if (c)
        for (const QString &rc : c->rateControls)
            m_rc->addItem(Formats::rateControlLabel(rc), rc);
    selectData(m_rc, v.rateControl);
    m_target->setValue(v.targetBitrate);
    m_max->setValue(v.maxBitrate);
    if (c) {
        m_qualitySlider->setRange(c->qualityMin, c->qualityMax);
        m_quality->setRange(c->qualityMin, c->qualityMax);
    }
    m_quality->setValue(v.quality);
    m_updating = wasUpdating;
}

void ExportSettingsDialog::refreshAudioControls()
{
    const bool wasUpdating = m_updating;
    m_updating = true;
    const AudioCodecDef *c = Formats::audioCodec(m_settings.audio.codec);
    const AudioSettings &a = m_settings.audio;
    m_aRate->clear();
    m_aRate->addItem(QStringLiteral("Match Source"), 0);
    if (c)
        for (int r : c->sampleRates)
            m_aRate->addItem(QStringLiteral("%1 Hz").arg(r), r);
    selectData(m_aRate, a.sampleRate);
    selectData(m_aChannels, a.channels);
    m_aBitrate->clear();
    if (c && !c->lossless)
        for (int b : c->bitrates)
            m_aBitrate->addItem(QStringLiteral("%1 kbps").arg(b), b);
    selectData(m_aBitrate, a.bitrate);
    m_aBitrate->setEnabled(c && !c->lossless);
    m_updating = wasUpdating;
}

void ExportSettingsDialog::readFromUi()
{
    ExportSettings &s = m_settings;
    s.comment = m_comment->text();
    s.video.enabled = m_exportVideo->isChecked();
    s.audio.enabled = m_exportAudio->isChecked();

    s.video.codec = m_vCodec->currentData().toString();
    s.video.width = m_width->value();
    s.video.height = m_height->value();
    s.video.scaleMode = m_scaleMode->currentData().toString();
    const QString fpsTxt = m_fps->currentText().trimmed();
    s.video.frameRate = (fpsTxt.isEmpty() || fpsTxt.startsWith("Match")) ? 0.0 : qMax(0.0, fpsTxt.toDouble());
    s.video.frameRateMode = m_fpsMode->currentData().toString();
    s.video.fieldOrder = m_fieldOrder->currentData().toString();
    s.video.deinterlace = m_deinterlace->isChecked();
    if (m_speed->count())
        s.video.speedPreset = m_speed->currentData().toString();
    if (m_profile->count())
        s.video.profile = m_profile->currentData().toString();
    if (m_level->count())
        s.video.level = m_level->currentData().toString();
    s.video.pixelFormat = m_pixFmt->currentData().toString();
    if (m_rc->count())
        s.video.rateControl = m_rc->currentData().toString();
    s.video.targetBitrate = m_target->value();
    s.video.maxBitrate = qMax(m_max->value(), m_target->value());
    s.video.quality = m_quality->value();
    s.video.keyframeInterval = m_gop->value();
    s.video.imageSequence = m_imageSeq->isChecked();
    s.video.stillDuration = m_stillDuration->value();
    s.maxRenderQuality = m_maxQuality->isChecked();
    s.video.frameRateLimit = m_fpsLimit->currentData().toDouble();
    s.video.scaleBitrateWithFps = m_scaleBitrate->isChecked();
    s.video.extraArgs = m_vExtra->text();

    s.audio.codec = m_aCodec->currentData().toString();
    s.audio.sampleRate = m_aRate->currentData().toInt();
    s.audio.channels = m_aChannels->currentData().toInt();
    if (m_aBitrate->count())
        s.audio.bitrate = m_aBitrate->currentData().toInt();
    s.audio.extraArgs = m_aExtra->text();

    EffectsSettings &e = s.effects;
    e.cropEnabled = m_cropGroup->isChecked();
    e.cropLeft = m_cropL->value();
    e.cropTop = m_cropT->value();
    e.cropRight = m_cropR->value();
    e.cropBottom = m_cropB->value();
    e.lutEnabled = m_lutGroup->isChecked();
    e.lutPath = m_lutPath->text().trimmed();
    e.imageOverlayEnabled = m_imgGroup->isChecked();
    e.imagePath = m_imgPath->text().trimmed();
    e.imagePosition = m_imgPos->currentData().toString();
    e.imageOffset = m_imgOffset->value();
    e.imageScale = m_imgScale->value();
    e.imageOpacity = m_imgOpacity->value();
    e.textOverlayEnabled = m_textGroup->isChecked();
    e.text = m_text->text();
    e.textPosition = m_textPos->currentData().toString();
    e.textSize = m_textSize->value();
    e.textColor = m_textColor->property("color").toString();
    e.textOpacity = m_textOpacity->value();
    e.textBox = m_textBox->isChecked();
    e.timecodeEnabled = m_tcGroup->isChecked();
    e.timecodePosition = m_tcPos->currentData().toString();
    e.timecodeSize = m_tcSize->value();
    e.timecodeFromSource = m_tcFromSource->isChecked();
    e.timecodeStart = m_tcStart->text();
    e.limiterEnabled = m_limiterGroup->isChecked();
    e.loudnessEnabled = m_loudGroup->isChecked();
    e.loudnessTarget = m_loudTarget->value();
    e.truePeak = m_truePeak->value();
    e.loudnessRange = m_lra->value();
    e.captionsEnabled = m_capGroup->isChecked();
    e.captionFile = m_capPath->text().trimmed();
    e.captionSize = m_capSize->value();
}

void ExportSettingsDialog::refreshPresetCombo()
{
    const bool wasUpdating = m_updating;
    m_updating = true;
    m_preset->clear();
    if (m_presetId.isEmpty())
        m_preset->addItem(QStringLiteral("Custom"), QString());
    for (const Preset &p : PresetManager::instance().presetsForFormat(m_settings.format))
        m_preset->addItem(p.system ? p.name() : p.name() + QStringLiteral("  (user)"), p.id);
    selectData(m_preset, m_presetId);
    const Preset *cur = PresetManager::instance().find(m_presetId);
    m_deletePreset->setEnabled(cur && !cur->system);
    m_updating = wasUpdating;
}

void ExportSettingsDialog::markCustom()
{
    if (m_presetId.isEmpty())
        return;
    m_presetId.clear();
    m_settings.presetName = QStringLiteral("Custom");
    refreshPresetCombo();
}

// ------------------------------------------------------------------ handlers

void ExportSettingsDialog::onFieldChanged()
{
    if (m_updating)
        return;
    readFromUi();
    // Keep frame size preset combo in sync with spin boxes.
    {
        QSignalBlocker b(m_sizePreset);
        const int i = m_sizePreset->findData(QSize(m_width->value(), m_height->value()));
        m_sizePreset->setCurrentIndex(i >= 0 ? i : 0);
    }
    markCustom();
    updateDerived();
}

void ExportSettingsDialog::onFormatChanged()
{
    const QString fmt = m_format->currentData().toString();
    if (fmt == m_settings.format)
        return;
    const QString oldExt = m_settings.extension();
    const bool oldSeq = m_settings.isImageSequence();
    const EffectsSettings keepFx = m_settings.effects;
    const Preset p = PresetManager::instance().firstPresetForFormat(fmt);
    m_settings = p.settings;
    m_settings.effects = keepFx;
    m_presetId = p.id;
    // Follow the new extension in the output name.
    if (m_mode == Mode::Output && !outputPath().isEmpty() &&
        (oldExt != m_settings.extension() || oldSeq != m_settings.isImageSequence())) {
        QFileInfo fi(outputPath());
        QString base = fi.completeBaseName();
        if (oldSeq)
            base.remove(QStringLiteral("_%05d"));
        setOutputPath(fi.dir().filePath(m_settings.isImageSequence()
                                            ? QStringLiteral("%1_%05d.%2").arg(base, m_settings.extension())
                                            : QStringLiteral("%1.%2").arg(base, m_settings.extension())));
    }
    loadToUi();
}

void ExportSettingsDialog::onPresetChanged()
{
    if (m_updating)
        return;
    const QString id = m_preset->currentData().toString();
    const Preset *p = PresetManager::instance().find(id);
    if (!p)
        return;
    const EffectsSettings fx = m_settings.effects;
    m_settings = p->settings;
    // Crop belongs to the source rather than the preset.
    m_settings.effects.cropEnabled = fx.cropEnabled;
    m_settings.effects.cropLeft = fx.cropLeft;
    m_settings.effects.cropTop = fx.cropTop;
    m_settings.effects.cropRight = fx.cropRight;
    m_settings.effects.cropBottom = fx.cropBottom;
    m_presetId = id;
    loadToUi();
}

void ExportSettingsDialog::onVideoCodecChanged()
{
    if (m_updating)
        return;
    readFromUi();
    m_settings.applyVideoCodecDefaults();
    markCustom();
    loadToUi();
}

void ExportSettingsDialog::onAudioCodecChanged()
{
    if (m_updating)
        return;
    readFromUi();
    m_settings.applyAudioCodecDefaults();
    markCustom();
    loadToUi();
}

void ExportSettingsDialog::onSavePreset()
{
    readFromUi();
    bool ok = false;
    const QString suggested = m_settings.presetName == "Custom" ? QString() : m_settings.presetName;
    const QString name = QInputDialog::getText(this, QStringLiteral("Save Preset"), QStringLiteral("Preset name:"),
                                               QLineEdit::Normal, suggested, &ok)
                             .trimmed();
    if (!ok || name.isEmpty())
        return;
    ExportSettings s = m_settings;
    s.presetName = name;
    QString err;
    const Preset p = PresetManager::instance().saveUserPreset(s, &err);
    if (p.id.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Save Preset"), err);
        return;
    }
    m_settings.presetName = name;
    m_presetId = p.id;
    refreshPresetCombo();
}

void ExportSettingsDialog::onImportPreset()
{
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Import Preset"), QString(),
                                                      QStringLiteral("Presets (*.json)"));
    if (path.isEmpty())
        return;
    QString err;
    if (!PresetManager::instance().importPreset(path, &err))
        QMessageBox::warning(this, QStringLiteral("Import Preset"), err);
    refreshPresetCombo();
}

void ExportSettingsDialog::onDeletePreset()
{
    const Preset *p = PresetManager::instance().find(m_presetId);
    if (!p || p->system)
        return;
    if (QMessageBox::question(this, QStringLiteral("Delete Preset"),
                              QStringLiteral("Delete the preset \"%1\"?").arg(p->name())) != QMessageBox::Yes)
        return;
    PresetManager::instance().removeUserPreset(m_presetId);
    m_presetId.clear();
    refreshPresetCombo();
}

void ExportSettingsDialog::onBrowseOutput()
{
    const QString ext = m_settings.extension();
    QString current = outputPath();
    const bool seq = m_settings.isImageSequence();
    if (seq)
        current.remove(QStringLiteral("_%05d"));
    QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("Output File"), current,
        QStringLiteral("%1 (*.%2)").arg(Formats::format(m_settings.format)->displayName, ext));
    if (path.isEmpty())
        return;
    QFileInfo fi(path);
    const QString base = fi.suffix().compare(ext, Qt::CaseInsensitive) == 0 ? fi.completeBaseName() : fi.fileName();
    path =
        fi.dir().filePath(seq ? QStringLiteral("%1_%05d.%2").arg(base, ext) : QStringLiteral("%1.%2").arg(base, ext));
    setOutputPath(path);
    updateDerived();
}

// ------------------------------------------------------------------ derived state

void ExportSettingsDialog::updateDerived()
{
    const FormatDef *f = Formats::format(m_settings.format);
    const VideoCodecDef *vc = Formats::videoCodec(m_settings.video.codec);
    const bool videoOn = f && f->hasVideo() && m_settings.video.enabled;
    const bool audioOn = f && f->hasAudio() && m_settings.audio.enabled;
    m_videoTab->setEnabled(videoOn);
    m_audioTab->setEnabled(audioOn);

    const QString rc = m_settings.video.rateControl;
    const bool bitrateMode = rc == "cbr" || rc == "vbr1" || rc == "vbr2";
    m_target->setEnabled(bitrateMode);
    m_max->setEnabled(rc == "vbr1" || rc == "vbr2");
    m_quality->setEnabled(rc == "quality");
    m_qualitySlider->setEnabled(rc == "quality");
    m_rc->setEnabled(vc && vc->rateControls.size() > 1);
    m_imageSeq->setEnabled(f && f->imageFormat);
    m_stillDuration->setEnabled(!f || !f->imageFormat);
    m_fpsMode->setEnabled(m_settings.video.frameRate > 0 || m_settings.video.frameRateLimit > 0);
    m_fpsLimit->setEnabled(m_settings.video.frameRate <= 0);
    m_scaleMode->setEnabled(m_settings.video.width > 0 && m_settings.video.height > 0);

    // Summary
    const MediaInfo *src = m_info.valid ? &m_info : nullptr;
    QString text =
        QStringLiteral("<b>Output:</b> %1<br>%2")
            .arg(m_mode == Mode::Output ? QFileInfo(outputPath()).fileName().toHtmlEscaped() : QStringLiteral("—"),
                 m_settings.summary(src).toHtmlEscaped().replace('\n', "<br>"));
    if (src)
        text += QStringLiteral("<br><br><b>Source:</b> %1<br>%2")
                    .arg(QFileInfo(m_info.path).fileName().toHtmlEscaped(), m_info.shortDescription().toHtmlEscaped());
    m_summary->setText(text);

    // Estimated size
    double duration = sourceDuration();
    if (m_in >= 0 || m_out >= 0)
        duration = (m_out > 0 ? m_out : duration) - qMax(0.0, m_in);
    if (m_info.isStill)
        duration = m_settings.video.stillDuration;
    const qint64 est = m_settings.estimateSize(duration, src);
    m_estimate->setText(src ? QStringLiteral("Estimated File Size: %1    ·    Duration: %2")
                                  .arg(est >= 0 ? formatBytes(est) : QStringLiteral("varies with content"),
                                       formatDuration(duration, m_info.fps))
                            : QString());

    // Command preview
    if (src) {
        BuildRequest req;
        req.inputs = m_files.isEmpty() ? QStringList{m_info.path} : m_files;
        req.source = m_info;
        req.inputArgs = m_inputArgs;
        req.inputArgs = m_inputArgs;
        req.totalDuration = m_totalDuration;
        req.settings = m_settings;
        req.outputPath = outputPath();
        req.inPoint = m_in;
        req.outPoint = m_out;
        req.pass = CommandBuilder::usesTwoPass(m_settings) ? 2 : 0;
        req.passLogFile = QStringLiteral("passlog");
        req.workDir = AppSettings::tempDir();
        const BuildResult r = CommandBuilder::build(req);
        for (const QString &t : r.tempFiles)
            QFile::remove(t);
        if (r.ok()) {
            QStringList quoted;
            for (const QString &a : r.args)
                quoted << (a.contains(' ') || a.contains(';') ? QStringLiteral("\"%1\"").arg(a) : a);
            m_cmdPreview->setPlainText(
                QStringLiteral("ffmpeg ") + quoted.join(' ') +
                (req.pass ? QStringLiteral("\n\n(two-pass: a first analysis pass runs before this)") : QString()));
        } else {
            m_cmdPreview->setPlainText(QStringLiteral("Cannot encode with these settings:\n") + r.error);
        }
    } else {
        m_cmdPreview->setPlainText(QStringLiteral("Command preview is available when editing a queued output."));
    }

    const bool sourceView = m_viewTabs->currentIndex() == 0;
    m_preview->setCropOverlay(sourceView && m_settings.effects.cropEnabled, m_settings.effects.cropLeft,
                              m_settings.effects.cropTop, m_settings.effects.cropRight, m_settings.effects.cropBottom);
    schedulePreview();
}

void ExportSettingsDialog::schedulePreview()
{
    if (m_info.valid && m_info.hasVideo)
        m_previewTimer->start();
}

void ExportSettingsDialog::renderPreview()
{
    if (!m_info.valid || !m_info.hasVideo)
        return;
    BuildRequest req;
    req.inputs = {m_files.isEmpty() ? m_info.path : m_files.first()};
    req.source = m_info;
    req.settings = m_settings;
    req.inPoint = currentTime();
    req.workDir = AppSettings::tempDir();
    const bool effects = m_viewTabs->currentIndex() == 1;
    if (!effects)
        req.settings.effects = EffectsSettings();
    m_preview->setBusy(true);
    m_grabber->request(req, currentTime(), effects);
}

void ExportSettingsDialog::accept()
{
    readFromUi();
    if (m_mode == Mode::Output && outputPath().isEmpty()) {
        QMessageBox::warning(this, windowTitle(), QStringLiteral("Please choose an output file."));
        return;
    }
    if (m_presetId.isEmpty())
        m_settings.presetName = QStringLiteral("Custom");
    QDialog::accept();
}
