#pragma once

#include <QTreeWidget>
#include <QWidget>

class WatchFolderManager;

class WatchFolderTree : public QTreeWidget
{
    Q_OBJECT
public:
    using QTreeWidget::QTreeWidget;

signals:
    void presetDropped(QTreeWidgetItem *target, const QString &presetId);
    void foldersDropped(const QStringList &paths);

protected:
    void dragEnterEvent(QDragEnterEvent *e) override;
    void dragMoveEvent(QDragMoveEvent *e) override;
    void dropEvent(QDropEvent *e) override;
};

class WatchFolderPanel : public QWidget
{
    Q_OBJECT
public:
    explicit WatchFolderPanel(WatchFolderManager *manager, QWidget *parent = nullptr);

signals:
    void editOutputRequested(const QString &folderId, int outputIndex);

private:
    void reload();
    void addFolder(const QString &path = {});
    void addOutput();
    void removeSelected();
    void chooseOutputDir(const QString &folderId, int index);

    WatchFolderManager *m_manager;
    WatchFolderTree *m_tree;
};
