#pragma once

#include "Formats.h"

#include <QAbstractTableModel>
#include <QDateTime>
#include <QFileSystemWatcher>
#include <QSortFilterProxyModel>
#include <QTimer>

class QAbstractFileIconProvider;
struct MediaInfo;

// Lists the media files (and optionally sub-folders) of one directory with sortable columns,
// including media metadata columns that are filled in asynchronously by MediaInfoCache.
class MediaFileModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    enum Column {
        Name,
        Type,
        Size,
        Modified,
        Kind,
        Duration,
        Resolution,
        FrameRate,
        VideoCodec,
        Audio,
        Bitrate,
        Created,
        ColumnCount
    };
    enum Roles {
        SortRole = Qt::UserRole + 1, // raw value used for sorting (numbers, dates, lower-case text)
        PathRole,
        IsDirRole,
        KindRole, // int(MediaKind), -1 for folders
    };

    explicit MediaFileModel(QObject *parent = nullptr);

    void setDirectory(const QString &path);
    QString directory() const { return m_dir; }
    void setShowFolders(bool on);
    void setShowAllFiles(bool on); // include non-media files
    // Metadata columns trigger background probing only while enabled (i.e. while visible).
    void setMetadataEnabled(bool on);
    bool metadataEnabled() const { return m_metadata; }
    void setIconProvider(QAbstractFileIconProvider *provider) { m_icons = provider; }

    static bool isMetadataColumn(int column);
    static QString columnTitle(int column);
    QString filePath(const QModelIndex &index) const;
    bool isDir(const QModelIndex &index) const;

    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    QStringList mimeTypes() const override;
    QMimeData *mimeData(const QModelIndexList &indexes) const override;

public slots:
    void refresh();

private:
    struct Entry {
        QString path;
        QString name;
        bool dir = false;
        qint64 size = 0;
        QDateTime modified;
        QDateTime created;
        MediaKind kind = MediaKind::Other;
    };
    QList<Entry> scan() const;
    QVariant metadataValue(const Entry &e, int column, bool sortValue) const;
    void onInfoReady(const QString &path);

    QString m_dir;
    QList<Entry> m_entries;
    QHash<QString, int> m_rowByPath;
    bool m_showFolders = true;
    bool m_showAll = false;
    bool m_metadata = false;
    QAbstractFileIconProvider *m_icons = nullptr;
    QFileSystemWatcher m_watcher;
    QTimer m_refreshTimer;
};

// Sorting and filtering for MediaFileModel: folders always first, then by the chosen column;
// filters by name text and media kind.
class MediaFileSortProxy : public QSortFilterProxyModel
{
    Q_OBJECT
public:
    enum KindFilter { AllMedia, VideoOnly, AudioOnly, ImagesOnly };

    explicit MediaFileSortProxy(QObject *parent = nullptr);
    void setNameFilterText(const QString &text);
    void setKindFilter(KindFilter kind);

protected:
    bool lessThan(const QModelIndex &left, const QModelIndex &right) const override;
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;

private:
    template<typename Change>
    void applyFilterChange(Change change)
    {
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
        beginFilterChange();
        change();
        endFilterChange(Direction::Rows);
#else
        change();
        invalidateRowsFilter();
#endif
    }
    QString m_text;
    KindFilter m_kind = AllMedia;
};
