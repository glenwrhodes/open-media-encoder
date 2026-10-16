#pragma once

#include <QHash>
#include <QMutex>
#include <QSet>
#include <QString>
#include <QStringList>

struct FormatDef;

// Discovers which encoders/filters the installed ffmpeg supports and whether
// hardware encoders actually work on this machine.
class EncoderCaps
{
public:
    static EncoderCaps &instance();

    void probe(const QString &ffmpegPath);
    // Runs a tiny test encode for each hardware encoder; slow, call off the UI thread.
    void validateHardware(const QString &ffmpegPath);

    bool probed() const;
    bool hasEncoder(const QString &encoder) const;
    bool hasFilter(const QString &filter) const;
    bool hardwareValidated() const;
    QString version() const;

    QStringList availableVideoCodecs(const FormatDef &format) const;
    QStringList availableAudioCodecs(const FormatDef &format) const;

    // For tests / when ffmpeg is unavailable.
    void setAvailable(const QSet<QString> &encoders, const QSet<QString> &filters);

private:
    EncoderCaps() = default;
    mutable QMutex m_mutex;
    QSet<QString> m_encoders;
    QSet<QString> m_filters;
    QHash<QString, bool> m_hwWorks;
    QString m_version;
    bool m_probed = false;
    bool m_hwValidated = false;
};
