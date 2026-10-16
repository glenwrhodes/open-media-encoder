#pragma once

#include "core/MediaInfo.h"

#include <QFutureWatcher>
#include <QWidget>

class FrameGrabber;
class QFileSystemModel;
class QLabel;
class QLineEdit;
class QTreeView;

// File system browser filtered to media, with metadata and thumbnail of the selected file.
class MediaBrowser : public QWidget
{
    Q_OBJECT
public:
    explicit MediaBrowser(QWidget *parent = nullptr);
    void setRootPath(const QString &path);

signals:
    void addToQueueRequested(const QStringList &files);

private:
    void showInfoFor(const QString &path);
    QStringList selectedFiles() const;

    QFileSystemModel *m_dirModel;
    QFileSystemModel *m_fileModel;
    QTreeView *m_dirView;
    QTreeView *m_fileView;
    QLineEdit *m_path;
    QLabel *m_thumb;
    QLabel *m_info;
    FrameGrabber *m_grabber;
    QFutureWatcher<MediaInfo> m_probe;
    QString m_current;
    QString m_targetDir;
};
