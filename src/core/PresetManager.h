#pragma once

#include "ExportSettings.h"

#include <QList>
#include <QObject>

struct Preset {
    QString id; // "sys/<format>/<name>" or "user/<file name>"
    ExportSettings settings;
    bool system = true;
    QString filePath; // user presets only

    QString name() const { return settings.presetName; }
};

class PresetManager : public QObject
{
    Q_OBJECT
public:
    static PresetManager &instance();
    static constexpr const char *MimeType = "application/x-prem-preset";

    void load();
    const QList<Preset> &presets() const { return m_presets; }
    QList<Preset> presetsForFormat(const QString &formatId) const;
    const Preset *find(const QString &id) const;
    const Preset *findByName(const QString &formatId, const QString &name) const;
    // Settings for the configured default preset (or the first available one).
    Preset defaultPreset() const;
    Preset firstPresetForFormat(const QString &formatId) const;

    Preset saveUserPreset(const ExportSettings &settings, QString *error = nullptr);
    bool removeUserPreset(const QString &id);
    bool importPreset(const QString &path, QString *error = nullptr);
    bool exportPreset(const QString &id, const QString &path, QString *error = nullptr) const;

    // H.264, HEVC and AV1 system presets use a GPU encoder that works on this machine (the
    // Preferences setting unless set here, e.g. by tests).
    void setPreferHardware(bool prefer);
    // The settings moved to this machine's best GPU encoder, or unchanged when there is none.
    static ExportSettings onGpu(const ExportSettings &s);

signals:
    void presetsChanged();

private:
    PresetManager() = default;
    void buildSystemPresets();
    void loadUserPresets();
    QList<Preset> m_presets;
    bool m_preferHardware = false;
    bool m_preferHardwareSet = false;
};
