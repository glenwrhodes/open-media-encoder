#pragma once

#include "ExportSettings.h"

#include <QHash>
#include <QList>
#include <QObject>
#include <QSet>
#include <QTimer>

struct WatchOutput {
    ExportSettings settings;
    QString presetId;
    QString outputDir; // empty = <watch folder>/Output
};

struct WatchFolder {
    QString id;
    QString path;
    QList<WatchOutput> outputs;

    QString outputDirFor(const WatchOutput &o) const;
    QString sourceArchiveDir() const;
};

// Polls watch folders and reports newly arrived media files once their size has settled.
class WatchFolderManager : public QObject
{
    Q_OBJECT
public:
    explicit WatchFolderManager(QObject *parent = nullptr);

    const QList<WatchFolder> &folders() const { return m_folders; }
    const WatchFolder *folder(const QString &id) const;
    QString addFolder(const QString &path, const WatchOutput &firstOutput);
    void removeFolder(const QString &id);
    void addOutput(const QString &folderId, const WatchOutput &output);
    void removeOutput(const QString &folderId, int index);
    void setOutput(const QString &folderId, int index, const WatchOutput &output);

    bool isActive() const { return m_active; }
    void setActive(bool active);
    void scanNow();
    // Called after a file was queued so it isn't picked up again; also archives completed sources.
    void markHandled(const QString &path);
    void archiveSource(const QString &folderId, const QString &file);

    void load();
    void save() const;

signals:
    void filesReady(const QString &folderId, const QStringList &files);
    void foldersChanged();

private:
    void scan();
    QList<WatchFolder> m_folders;
    QTimer m_timer;
    bool m_active = false;
    QHash<QString, qint64> m_pendingSizes; // file -> size at last scan
    QSet<QString> m_handled;
};
