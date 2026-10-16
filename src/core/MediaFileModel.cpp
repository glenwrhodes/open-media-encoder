#include "MediaFileModel.h"

#include "MediaInfo.h"
#include "MediaInfoCache.h"

#include <QAbstractFileIconProvider>
#include <QDir>
#include <QFileInfo>
#include <QLocale>
#include <QMimeData>
#include <QUrl>
#include <cmath>

namespace {

QString codecLabel(const QString &c)
{
    static const QHash<QString, QString> names = {
        {"h264", "H.264"},       {"hevc", "HEVC"},        {"av1", "AV1"},          {"vp9", "VP9"},
        {"vp8", "VP8"},          {"prores", "ProRes"},    {"dnxhd", "DNxHD/HR"},   {"mpeg2video", "MPEG-2"},
        {"mjpeg", "MJPEG"},      {"cfhd", "CineForm"},    {"aac", "AAC"},          {"mp3", "MP3"},
        {"pcm_s16le", "PCM 16"}, {"pcm_s24le", "PCM 24"}, {"pcm_s32le", "PCM 32"}, {"pcm_f32le", "PCM float"},
        {"flac", "FLAC"},        {"opus", "Opus"},        {"ac3", "AC-3"},         {"png", "PNG"},
        {"gif", "GIF"},          {"qtrle", "Animation"},  {"mpeg4", "MPEG-4"}};
    return names.value(c, c.toUpper());
}

QString channelLabel(int ch)
{
    switch (ch) {
    case 1:
        return QStringLiteral("mono");
    case 2:
        return QStringLiteral("stereo");
    case 6:
        return QStringLiteral("5.1");
    case 8:
        return QStringLiteral("7.1");
    default:
        return QStringLiteral("%1 ch").arg(ch);
    }
}

} // namespace

// ================================================================= MediaFileModel

MediaFileModel::MediaFileModel(QObject *parent)
    : QAbstractTableModel(parent)
{
    m_refreshTimer.setSingleShot(true);
    m_refreshTimer.setInterval(400);
    connect(&m_refreshTimer, &QTimer::timeout, this, &MediaFileModel::refresh);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, &m_refreshTimer, qOverload<>(&QTimer::start));
    connect(&MediaInfoCache::instance(), &MediaInfoCache::infoReady, this, &MediaFileModel::onInfoReady);
}

bool MediaFileModel::isMetadataColumn(int column)
{
    return column == Duration || column == Resolution || column == FrameRate || column == VideoCodec ||
           column == Audio || column == Bitrate;
}

QString MediaFileModel::columnTitle(int column)
{
    switch (column) {
    case Name:
        return QStringLiteral("Name");
    case Type:
        return QStringLiteral("Type");
    case Size:
        return QStringLiteral("Size");
    case Modified:
        return QStringLiteral("Date Modified");
    case Kind:
        return QStringLiteral("Kind");
    case Duration:
        return QStringLiteral("Duration");
    case Resolution:
        return QStringLiteral("Resolution");
    case FrameRate:
        return QStringLiteral("Frame Rate");
    case VideoCodec:
        return QStringLiteral("Codec");
    case Audio:
        return QStringLiteral("Audio");
    case Bitrate:
        return QStringLiteral("Bitrate");
    case Created:
        return QStringLiteral("Date Created");
    }
    return {};
}

void MediaFileModel::setDirectory(const QString &path)
{
    const QString dir = QDir(path).absolutePath();
    if (!m_dir.isEmpty())
        m_watcher.removePath(m_dir);
    m_dir = dir;
    if (QDir(dir).exists())
        m_watcher.addPath(dir);
    beginResetModel();
    m_entries = scan();
    m_rowByPath.clear();
    for (int i = 0; i < m_entries.size(); ++i)
        m_rowByPath.insert(m_entries[i].path, i);
    endResetModel();
}

void MediaFileModel::setShowFolders(bool on)
{
    if (on == m_showFolders)
        return;
    m_showFolders = on;
    setDirectory(m_dir);
}

void MediaFileModel::setShowAllFiles(bool on)
{
    if (on == m_showAll)
        return;
    m_showAll = on;
    setDirectory(m_dir);
}

void MediaFileModel::setMetadataEnabled(bool on)
{
    if (on == m_metadata)
        return;
    m_metadata = on;
    if (!m_entries.isEmpty())
        emit dataChanged(index(0, Duration), index(m_entries.size() - 1, Bitrate));
}

QList<MediaFileModel::Entry> MediaFileModel::scan() const
{
    QList<Entry> out;
    QDir d(m_dir);
    if (m_dir.isEmpty() || !d.exists())
        return out;
    QDir::Filters filters = QDir::Files | QDir::NoDotAndDotDot;
    if (m_showFolders)
        filters |= QDir::Dirs;
    const auto infos = d.entryInfoList(filters, QDir::Name | QDir::DirsFirst | QDir::IgnoreCase);
    for (const QFileInfo &fi : infos) {
        Entry e;
        e.dir = fi.isDir();
        e.path = fi.absoluteFilePath();
        if (!e.dir && !m_showAll && !Formats::isMediaFile(e.path))
            continue;
        e.name = fi.fileName();
        e.size = e.dir ? 0 : fi.size();
        e.modified = fi.lastModified();
        e.created = fi.birthTime().isValid() ? fi.birthTime() : fi.metadataChangeTime();
        e.kind = e.dir ? MediaKind::Other : Formats::mediaKind(e.path);
        out << e;
    }
    return out;
}

void MediaFileModel::refresh()
{
    if (!QDir(m_dir).exists()) {
        setDirectory(m_dir);
        return;
    }
    // Apply the difference so selections survive files appearing and growing (e.g. renders in progress).
    const QList<Entry> fresh = scan();
    QHash<QString, const Entry *> freshByPath;
    for (const Entry &e : fresh)
        freshByPath.insert(e.path, &e);

    for (int i = m_entries.size() - 1; i >= 0; --i) {
        if (!freshByPath.contains(m_entries[i].path)) {
            beginRemoveRows({}, i, i);
            m_entries.removeAt(i);
            endRemoveRows();
        }
    }
    QSet<QString> existing;
    for (int i = 0; i < m_entries.size(); ++i) {
        Entry &cur = m_entries[i];
        existing.insert(cur.path);
        const Entry *f = freshByPath.value(cur.path);
        if (f->size != cur.size || f->modified != cur.modified) {
            cur = *f;
            emit dataChanged(index(i, 0), index(i, ColumnCount - 1));
        }
    }
    QList<Entry> added;
    for (const Entry &e : fresh)
        if (!existing.contains(e.path))
            added << e;
    if (!added.isEmpty()) {
        beginInsertRows({}, m_entries.size(), m_entries.size() + added.size() - 1);
        m_entries << added;
        endInsertRows();
    }
    m_rowByPath.clear();
    for (int i = 0; i < m_entries.size(); ++i)
        m_rowByPath.insert(m_entries[i].path, i);
}

void MediaFileModel::onInfoReady(const QString &path)
{
    const int row = m_rowByPath.value(path, -1);
    if (row >= 0 && m_metadata)
        emit dataChanged(index(row, Duration), index(row, Bitrate));
}

QString MediaFileModel::filePath(const QModelIndex &index) const
{
    return index.isValid() && index.row() < m_entries.size() ? m_entries[index.row()].path : QString();
}

bool MediaFileModel::isDir(const QModelIndex &index) const
{
    return index.isValid() && index.row() < m_entries.size() && m_entries[index.row()].dir;
}

int MediaFileModel::rowCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : m_entries.size(); }

int MediaFileModel::columnCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : ColumnCount; }

QVariant MediaFileModel::metadataValue(const Entry &e, int column, bool sortValue) const
{
    if (e.dir || !m_metadata)
        return sortValue ? QVariant(-1.0) : QVariant();
    const std::optional<MediaInfo> info = MediaInfoCache::instance().lookup(e.path, true);
    if (!info)
        return sortValue ? QVariant(-1.0) : QVariant(QStringLiteral("…"));
    if (!info->valid)
        return sortValue ? QVariant(-2.0) : QVariant(QStringLiteral("—"));
    const MediaInfo &m = *info;
    switch (column) {
    case Duration:
        if (m.isStill || m.duration <= 0)
            return sortValue ? QVariant(0.0) : QVariant(QStringLiteral("—"));
        if (sortValue)
            return m.duration;
        {
            // Compact, consistent for audio and video: 0:24, 12:05, 1:02:03.
            const qint64 t = qint64(std::llround(m.duration));
            const qint64 h = t / 3600, mi = (t % 3600) / 60, se = t % 60;
            return h > 0 ? QStringLiteral("%1:%2:%3").arg(h).arg(mi, 2, 10, QChar('0')).arg(se, 2, 10, QChar('0'))
                         : QStringLiteral("%1:%2").arg(mi).arg(se, 2, 10, QChar('0'));
        }
    case Resolution:
        if (!m.hasVideo)
            return sortValue ? QVariant(0.0) : QVariant(QStringLiteral("—"));
        return sortValue ? QVariant(double(m.displayWidth()) * m.displayHeight())
                         : QVariant(QStringLiteral("%1 × %2").arg(m.displayWidth()).arg(m.displayHeight()));
    case FrameRate:
        if (!m.hasVideo || m.isStill)
            return sortValue ? QVariant(0.0) : QVariant(QStringLiteral("—"));
        return sortValue ? QVariant(m.fps)
                         : QVariant(QString::number(m.fps, 'f', std::abs(m.fps - std::round(m.fps)) < 0.001 ? 0 : 3));
    case VideoCodec:
        if (!m.hasVideo)
            return sortValue ? QVariant(QString()) : QVariant(QStringLiteral("—"));
        return sortValue ? QVariant(m.videoCodec) : QVariant(codecLabel(m.videoCodec));
    case Audio:
        if (!m.hasAudio)
            return sortValue ? QVariant(QString()) : QVariant(QStringLiteral("—"));
        return sortValue ? QVariant(m.audioCodec + QString::number(m.sampleRate))
                         : QVariant(QStringLiteral("%1 · %2 kHz · %3")
                                        .arg(codecLabel(m.audioCodec))
                                        .arg(m.sampleRate / 1000.0, 0, 'g', 3)
                                        .arg(channelLabel(m.channels)));
    case Bitrate:
        if (m.bitrate <= 0)
            return sortValue ? QVariant(0.0) : QVariant(QStringLiteral("—"));
        return sortValue ? QVariant(double(m.bitrate))
                         : QVariant(m.bitrate >= 1000000 ? QStringLiteral("%1 Mbps").arg(m.bitrate / 1e6, 0, 'f', 1)
                                                         : QStringLiteral("%1 kbps").arg(m.bitrate / 1000));
    }
    return {};
}

QVariant MediaFileModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_entries.size())
        return {};
    const Entry &e = m_entries[index.row()];
    const int col = index.column();

    if (role == PathRole)
        return e.path;
    if (role == IsDirRole)
        return e.dir;
    if (role == KindRole)
        return e.dir ? -1 : int(e.kind);
    if (role == Qt::DecorationRole && col == Name && m_icons)
        return m_icons->icon(QFileInfo(e.path));
    if (role == Qt::ToolTipRole && col == Name)
        return QDir::toNativeSeparators(e.path);
    if (role == Qt::TextAlignmentRole && (col == Size || col == Bitrate || col == FrameRate || col == Duration))
        return int(Qt::AlignRight | Qt::AlignVCenter);

    if (role == SortRole) {
        switch (col) {
        case Name:
            return e.name.toLower();
        case Type:
            return e.dir ? QString() : QFileInfo(e.name).suffix().toLower();
        case Size:
            return e.size;
        case Modified:
            return e.modified;
        case Created:
            return e.created;
        case Kind:
            return e.dir ? -1 : int(e.kind);
        default:
            return metadataValue(e, col, true);
        }
    }
    if (role != Qt::DisplayRole)
        return {};
    switch (col) {
    case Name:
        return e.name;
    case Type:
        return e.dir ? QStringLiteral("Folder") : QFileInfo(e.name).suffix().toUpper();
    case Size:
        return e.dir ? QString() : formatBytes(e.size);
    case Modified:
        return QLocale().toString(e.modified, QLocale::ShortFormat);
    case Created:
        return QLocale().toString(e.created, QLocale::ShortFormat);
    case Kind:
        return e.dir ? QStringLiteral("Folder") : Formats::mediaKindName(e.kind);
    default:
        return metadataValue(e, col, false);
    }
}

QVariant MediaFileModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole)
        return columnTitle(section);
    return {};
}

Qt::ItemFlags MediaFileModel::flags(const QModelIndex &index) const
{
    if (!index.isValid())
        return Qt::NoItemFlags;
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled;
}

QStringList MediaFileModel::mimeTypes() const { return {QStringLiteral("text/uri-list")}; }

QMimeData *MediaFileModel::mimeData(const QModelIndexList &indexes) const
{
    QList<QUrl> urls;
    QSet<int> rows;
    for (const QModelIndex &i : indexes) {
        if (!i.isValid() || rows.contains(i.row()))
            continue;
        rows.insert(i.row());
        urls << QUrl::fromLocalFile(m_entries[i.row()].path);
    }
    auto *m = new QMimeData;
    m->setUrls(urls);
    return m;
}

// ================================================================= MediaFileSortProxy

MediaFileSortProxy::MediaFileSortProxy(QObject *parent)
    : QSortFilterProxyModel(parent)
{
    setSortRole(MediaFileModel::SortRole);
    setDynamicSortFilter(true);
    setSortCaseSensitivity(Qt::CaseInsensitive);
}

void MediaFileSortProxy::setNameFilterText(const QString &text)
{
    applyFilterChange([&] { m_text = text.trimmed(); });
}

void MediaFileSortProxy::setKindFilter(KindFilter kind)
{
    applyFilterChange([&] { m_kind = kind; });
}

bool MediaFileSortProxy::lessThan(const QModelIndex &left, const QModelIndex &right) const
{
    const bool ld = left.data(MediaFileModel::IsDirRole).toBool();
    const bool rd = right.data(MediaFileModel::IsDirRole).toBool();
    if (ld != rd) // folders stay on top in either sort order
        return sortOrder() == Qt::AscendingOrder ? ld : rd;

    const QVariant a = left.data(MediaFileModel::SortRole);
    const QVariant b = right.data(MediaFileModel::SortRole);
    const int cmp = QVariant::compare(a, b) == QPartialOrdering::Less      ? -1
                    : QVariant::compare(a, b) == QPartialOrdering::Greater ? 1
                                                                           : 0;
    if (cmp != 0)
        return cmp < 0;
    // Stable tie-break on the name, so e.g. all MP4s are alphabetical within their group.
    const QString ln = left.sibling(left.row(), MediaFileModel::Name).data(MediaFileModel::SortRole).toString();
    const QString rn = right.sibling(right.row(), MediaFileModel::Name).data(MediaFileModel::SortRole).toString();
    return sortOrder() == Qt::AscendingOrder ? ln < rn : ln > rn;
}

bool MediaFileSortProxy::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
    const QModelIndex idx = sourceModel()->index(sourceRow, MediaFileModel::Name, sourceParent);
    const bool dir = idx.data(MediaFileModel::IsDirRole).toBool();
    if (!m_text.isEmpty() && !idx.data(Qt::DisplayRole).toString().contains(m_text, Qt::CaseInsensitive))
        return false;
    if (dir || m_kind == AllMedia)
        return true;
    const auto kind = MediaKind(idx.data(MediaFileModel::KindRole).toInt());
    return (m_kind == VideoOnly && kind == MediaKind::Video) || (m_kind == AudioOnly && kind == MediaKind::Audio) ||
           (m_kind == ImagesOnly && kind == MediaKind::Image);
}
