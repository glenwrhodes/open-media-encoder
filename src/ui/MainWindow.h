#pragma once

#include <QElapsedTimer>
#include <QHash>
#include <QMainWindow>

class EncoderEngine;
class EncodingPanel;
class MediaBrowser;
class PresetBrowser;
class QueueModel;
class QueuePanel;
class StreamRecorder;
class StreamRecorderPanel;
class QSystemTrayIcon;
class QLabel;
class QTimer;
class TaskbarProgress;
class WatchFolderManager;
class WatchFolderPanel;
struct Preset;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    void addFiles(const QStringList &files);
    void addFiles(const QStringList &files, const QString &presetId);
    void stitchFiles(QStringList files);
    // Adds a numbered image sequence (any frame of it); asks for the frame rate.
    bool addImageSequence(const QString &anyFrame, bool askFrameRate = true);

protected:
    void closeEvent(QCloseEvent *e) override;

private:
    void startQueue();
    void buildDocks();
    void applyDefaultDockSizes();
    void buildMenus();
    void wireEngine();

    void probeSource(const QString &sourceId);
    void addSourceDialog();
    void addFolderDialog();
    void stitchDialog();
    void imageSequenceDialog();
    void addOutputToSelection();
    void duplicateSelection();
    void removeSelection();
    void resetSelection();
    void skipSelection();
    void revealSelection();
    void editSelection();
    void editOutput(const QString &outputId);
    void chooseOutputPath(const QString &outputId);
    void applyPreset(const QString &presetId);
    void newPreset();
    void editPreset(const QString &presetId);
    void editWatchOutput(const QString &folderId, int index);
    void showQueueContextMenu(const QPoint &globalPos);
    void showMediaInfo(const QString &sourceId);
    void showLog(bool errors);
    void showPreferences();
    void showCapabilities();
    void showAbout();
    void saveQueueAs();
    void openQueue();
    void onWatchFilesReady(const QString &folderId, const QStringList &files);
    void onQueueFinished(int done, int failed);
    void scheduleSave();
    void stopQueue();
    void moveSelectedSource(int delta);
    void playSelectedOutput();
    void updateQueueProgress();
    void runAfterQueueAction();
    void restartIdleTimer();
    void updateStatus();
    void updatePreventSleep();
    void showRecorder();

    QueueModel *m_model;
    EncoderEngine *m_engine;
    WatchFolderManager *m_watch;
    QueuePanel *m_queue;
    PresetBrowser *m_presets;
    MediaBrowser *m_media;
    WatchFolderPanel *m_watchPanel;
    EncodingPanel *m_encoding;
    StreamRecorder *m_recorder;
    StreamRecorderPanel *m_recorderPanel;
    QSystemTrayIcon *m_tray;
    QTimer *m_saveTimer;
    QTimer *m_idleTimer;
    QList<QDockWidget *> m_docks;
    QByteArray m_defaultState;

    enum class AfterQueue { Nothing, Sleep, ShutDown };
    AfterQueue m_afterQueue = AfterQueue::Nothing;
    QList<QAction *> m_afterQueueActions;
    bool m_userStopped = false;
    bool m_afterQueueWaiting = false; // sleep/shut down once the running recordings finish
    TaskbarProgress *m_taskbar = nullptr;
    QLabel *m_progressLabel = nullptr;
    QElapsedTimer m_runTimer;
    int m_runFinished = 0;
    QHash<QString, double> m_activeProgress;
};
