#include "MediaInfoCache.h"

#include "AppSettings.h"

#include <QFileInfo>
#include <QMutexLocker>

namespace {
constexpr int MaxEntries = 20000;
}

MediaInfoCache &MediaInfoCache::instance()
{
    static MediaInfoCache cache;
    return cache;
}

MediaInfoCache::MediaInfoCache(QObject *parent)
    : QObject(parent)
{
    // ffprobe is mostly I/O bound; a few at a time keeps folders responsive without thrashing disks.
    m_pool.setMaxThreadCount(4);
}

std::optional<MediaInfo> MediaInfoCache::lookup(const QString &path, bool probeIfMissing)
{
    const QFileInfo fi(path);
    if (!fi.exists() || fi.isDir())
        return std::nullopt;
    const qint64 size = fi.size();
    const QDateTime mtime = fi.lastModified();
    {
        QMutexLocker lock(&m_mutex);
        const auto it = m_entries.constFind(path);
        if (it != m_entries.constEnd() && it->size == size && it->modified == mtime)
            return it->info;
        if (!probeIfMissing || m_pending.contains(path))
            return std::nullopt;
        m_pending.insert(path);
    }
    const QString probe = AppSettings::ffprobePath();
    m_pool.start([this, path, size, mtime, probe] {
        const MediaInfo info = MediaInfo::probe(probe, path, 15000);
        QMetaObject::invokeMethod(
            this, [this, path, size, mtime, info] { store(path, size, mtime, info); }, Qt::QueuedConnection);
    });
    return std::nullopt;
}

void MediaInfoCache::store(const QString &path, qint64 size, const QDateTime &mtime, const MediaInfo &info)
{
    {
        QMutexLocker lock(&m_mutex);
        m_pending.remove(path);
        if (m_entries.size() >= MaxEntries)
            m_entries.clear();
        m_entries.insert(path, Entry{size, mtime, info});
    }
    emit infoReady(path);
}

void MediaInfoCache::clear()
{
    QMutexLocker lock(&m_mutex);
    m_entries.clear();
}

int MediaInfoCache::pendingCount() const
{
    QMutexLocker lock(&m_mutex);
    return m_pending.size();
}
