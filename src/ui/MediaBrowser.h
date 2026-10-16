#pragma once

#include <QWidget>

class FrameGrabber;
class MediaFileModel;
class MediaFileSortProxy;
class QComboBox;
class QFileIconProvider;
class QFileSystemModel;
class QLabel;
class QLineEdit;
class QMenu;
class QToolButton;
class QTimer;
class QTreeView;
struct MediaInfo;

// File browser filtered to media: folder tree, sortable file list with optional metadata columns,
// filtering, favorites, and a metadata/thumbnail pane for the selected file.
class MediaBrowser : public QWidget
{
    Q_OBJECT
public:
    explicit MediaBrowser(QWidget *parent = nullptr);
    ~MediaBrowser() override;

    void setRootPath(const QString &path);
    QString currentPath() const;

signals:
    void addToQueueRequested(const QStringList &files);
    void addToQueueWithPresetRequested(const QStringList &files, const QString &presetId);
    void stitchRequested(const QStringList &files);
    void watchFolderRequested(const QString &folder);

private:
    QWidget *buildToolbar();
    void showInfoFor(const QString &path);
    void showInfo(const MediaInfo &info);
    void activate(const QModelIndex &proxyIndex);
    void goUp();
    void showHeaderMenu(const QPoint &pos);
    void showFileMenu(const QPoint &pos);
    void rebuildFavoritesMenu();
    void updateMetadataEnabled();
    void saveHeaderState();
    void restoreHeaderState();
    QStringList selectedFiles() const;
    QStringList selectedPaths() const;

    QFileSystemModel *m_dirModel;
    QFileIconProvider *m_iconProvider;
    MediaFileModel *m_fileModel;
    MediaFileSortProxy *m_proxy;
    QTreeView *m_dirView;
    QTreeView *m_fileView;
    QLineEdit *m_path;
    QLineEdit *m_filter;
    QComboBox *m_kind;
    QToolButton *m_favorites;
    QMenu *m_favoritesMenu;
    QLabel *m_thumb;
    QLabel *m_info;
    QLabel *m_count;
    FrameGrabber *m_grabber;
    QString m_current;
    QString m_targetDir;
    bool m_restoringHeader = false;
    QTimer *m_headerSaveTimer;
};
