#pragma once

#include "core/ExportSettings.h"
#include "core/MediaInfo.h"

#include <QDialog>

class FrameGrabber;
class FramePreview;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSlider;
class QSpinBox;
class QTabBar;
class QTimer;
class QToolButton;
class TimelineSlider;

class ExportSettingsDialog : public QDialog
{
    Q_OBJECT
public:
    enum class Mode { Output, Preset };

    ExportSettingsDialog(Mode mode, const ExportSettings &settings, const QString &presetId, QWidget *parent = nullptr);

    void setSource(const QStringList &files, const MediaInfo &info, double totalDuration);
    void setOutputPath(const QString &path);
    void setRange(double in, double out);
    // Demuxer options for the source (image sequences need their frame rate and start number).
    void setInputArgs(const QStringList &args) { m_inputArgs = args; }

    ExportSettings settings() const { return m_settings; }
    QString presetId() const { return m_presetId; }
    QString outputPath() const;
    double inPoint() const { return m_in; }
    double outPoint() const { return m_out; }

protected:
    void accept() override;

private:
    QWidget *buildPreviewSide();
    QWidget *buildSettingsSide();
    QWidget *buildVideoTab();
    QWidget *buildAudioTab();
    QWidget *buildEffectsTab();
    QWidget *buildAdvancedTab();

    void loadToUi();
    void readFromUi();
    void refreshPresetCombo();
    void refreshCodecControls();
    void refreshAudioControls();
    void updateDerived();
    void markCustom();
    void schedulePreview();
    void renderPreview();

    void onFieldChanged();
    void onFormatChanged();
    void onPresetChanged();
    void onVideoCodecChanged();
    void onAudioCodecChanged();
    void onSavePreset();
    void onImportPreset();
    void onDeletePreset();
    void onBrowseOutput();
    void updateRangeLabel();
    double currentTime() const;
    double sourceDuration() const;
    double previewDuration() const; // scrubbable span (first clip when stitched)
    double rangeIn() const;
    double rangeOut() const;
    void syncTimeline();
    void updateTimeLabel();
    void setInPoint();
    void setOutPoint();

    Mode m_mode;
    ExportSettings m_settings;
    QString m_presetId;
    QStringList m_files;
    MediaInfo m_info;
    double m_totalDuration = 0.0;
    double m_in = -1.0;
    double m_out = -1.0;
    QStringList m_inputArgs;
    bool m_updating = false;

    FrameGrabber *m_grabber = nullptr;
    QTimer *m_previewTimer = nullptr;

    // preview side
    QTabBar *m_viewTabs = nullptr;
    FramePreview *m_preview = nullptr;
    TimelineSlider *m_timeline = nullptr;
    QLabel *m_timeLabel = nullptr;
    QLabel *m_rangeLabel = nullptr;
    QLabel *m_rangeTitle = nullptr;
    QPushButton *m_setIn = nullptr;
    QPushButton *m_setOut = nullptr;
    QPushButton *m_clearRange = nullptr;
    QGroupBox *m_cropGroup = nullptr;
    QSpinBox *m_cropL = nullptr, *m_cropT = nullptr, *m_cropR = nullptr, *m_cropB = nullptr;

    // general
    QComboBox *m_format = nullptr;
    QComboBox *m_preset = nullptr;
    QToolButton *m_savePreset = nullptr;
    QToolButton *m_importPreset = nullptr;
    QToolButton *m_deletePreset = nullptr;
    QLineEdit *m_comment = nullptr;
    QLineEdit *m_outputEdit = nullptr;
    QCheckBox *m_exportVideo = nullptr;
    QCheckBox *m_exportAudio = nullptr;
    QLabel *m_summary = nullptr;
    QLabel *m_estimate = nullptr;

    // video
    QWidget *m_videoTab = nullptr;
    QComboBox *m_vCodec = nullptr;
    QComboBox *m_sizePreset = nullptr;
    QSpinBox *m_width = nullptr;
    QSpinBox *m_height = nullptr;
    QComboBox *m_scaleMode = nullptr;
    QComboBox *m_fps = nullptr;
    QComboBox *m_fpsMode = nullptr;
    QComboBox *m_fieldOrder = nullptr;
    QCheckBox *m_deinterlace = nullptr;
    QLabel *m_speedLabel = nullptr;
    QComboBox *m_speed = nullptr;
    QComboBox *m_profile = nullptr;
    QComboBox *m_level = nullptr;
    QComboBox *m_pixFmt = nullptr;
    QComboBox *m_rc = nullptr;
    QDoubleSpinBox *m_target = nullptr;
    QDoubleSpinBox *m_max = nullptr;
    QSlider *m_qualitySlider = nullptr;
    QSpinBox *m_quality = nullptr;
    QSpinBox *m_gop = nullptr;
    QCheckBox *m_imageSeq = nullptr;
    QDoubleSpinBox *m_stillDuration = nullptr;
    QCheckBox *m_maxQuality = nullptr;
    QComboBox *m_fpsLimit = nullptr;
    QCheckBox *m_scaleBitrate = nullptr;

    // audio
    QWidget *m_audioTab = nullptr;
    QComboBox *m_aCodec = nullptr;
    QComboBox *m_aRate = nullptr;
    QComboBox *m_aChannels = nullptr;
    QComboBox *m_aBitrate = nullptr;

    // effects
    QGroupBox *m_lutGroup = nullptr;
    QLineEdit *m_lutPath = nullptr;
    QGroupBox *m_imgGroup = nullptr;
    QLineEdit *m_imgPath = nullptr;
    QComboBox *m_imgPos = nullptr;
    QSpinBox *m_imgOffset = nullptr;
    QSpinBox *m_imgScale = nullptr;
    QSpinBox *m_imgOpacity = nullptr;
    QGroupBox *m_textGroup = nullptr;
    QLineEdit *m_text = nullptr;
    QComboBox *m_textPos = nullptr;
    QSpinBox *m_textSize = nullptr;
    QPushButton *m_textColor = nullptr;
    QSpinBox *m_textOpacity = nullptr;
    QCheckBox *m_textBox = nullptr;
    QGroupBox *m_tcGroup = nullptr;
    QComboBox *m_tcPos = nullptr;
    QSpinBox *m_tcSize = nullptr;
    QCheckBox *m_tcFromSource = nullptr;
    QLineEdit *m_tcStart = nullptr;
    QGroupBox *m_limiterGroup = nullptr;
    QGroupBox *m_loudGroup = nullptr;
    QDoubleSpinBox *m_loudTarget = nullptr;
    QDoubleSpinBox *m_truePeak = nullptr;
    QDoubleSpinBox *m_lra = nullptr;
    QGroupBox *m_capGroup = nullptr;
    QLineEdit *m_capPath = nullptr;
    QSpinBox *m_capSize = nullptr;

    // advanced
    QLineEdit *m_vExtra = nullptr;
    QLineEdit *m_aExtra = nullptr;
    QPlainTextEdit *m_cmdPreview = nullptr;
};
