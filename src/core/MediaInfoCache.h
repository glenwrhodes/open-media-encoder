#pragma once

#include "MediaInfo.h"

#include <QDateTime>
#include <QHash>
#include <QMutex>
#include <QObject>
#include <QSet>
#include <QThreadPool>
#include <optional>

// Probes media files in the background and remembers the results, keyed by path and
// invalidated when a file's size or modification time changes.
class MediaInfoCache : public QObject
{
    Q_OBJECT
public:
    static MediaInfoCache &instance();

    // Returns the cached info when it is current. Otherwise schedules a probe (when
    // requested) and returns nothing; infoReady() fires once the probe finishes.
    std::optional<MediaInfo> lookup(const QString &path, bool probeIfMissing = true);
    void clear();
    int pendingCount() const;

    void setMaxThreads(int n) { m_pool.setMaxThreadCount(n); }

signals:
    void infoReady(const QString &path);

private:
    explicit MediaInfoCache(QObject *parent = nullptr);
    void store(const QString &path, qint64 size, const QDateTime &mtime, const MediaInfo &info);

    struct Entry {
        qint64 size = -1;
        QDateTime modified;
        MediaInfo info;
    };
    mutable QMutex m_mutex;
    QHash<QString, Entry> m_entries;
    QSet<QString> m_pending;
    QThreadPool m_pool;
};
