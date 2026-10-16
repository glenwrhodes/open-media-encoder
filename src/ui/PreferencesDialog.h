#pragma once

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QRadioButton;
class QSpinBox;

class PreferencesDialog : public QDialog
{
    Q_OBJECT
public:
    explicit PreferencesDialog(QWidget *parent = nullptr);

    bool ffmpegChanged() const { return m_ffmpegChanged; }

protected:
    void accept() override;

private:
    void refreshYtDlpInfo();

private:
    QRadioButton *m_outSource;
    QRadioButton *m_outCustom;
    QLineEdit *m_customDir;
    QCheckBox *m_increment;
    QCheckBox *m_notify;
    QCheckBox *m_deletePartial;
    QSpinBox *m_autoStart;
    QSpinBox *m_parallel;
    QCheckBox *m_hwDecode;
    QCheckBox *m_preventSleep;
    QComboBox *m_defaultPreset;
    QLineEdit *m_ffmpeg;
    QLabel *m_ffmpegInfo;
    QCheckBox *m_reconnect;
    QLineEdit *m_ytDlp;
    QLabel *m_ytDlpInfo;
    bool m_ffmpegChanged = false;
};
