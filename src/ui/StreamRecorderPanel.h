#pragma once

#include <QHash>
#include <QWidget>
#include <functional>

class QComboBox;
class QLabel;
class QLineEdit;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;
class StreamRecorder;
class StreamRecording;
struct RecordingOptions;

// Paste a stream or web page link and record it to disk while it plays.
class StreamRecorderPanel : public QWidget
{
    Q_OBJECT
public:
    explicit StreamRecorderPanel(StreamRecorder *recorder, QWidget *parent = nullptr);

    // Selects the link box, pasting a link from the clipboard when the box is empty.
    void focusUrl();

signals:
    void addToQueueRequested(const QStringList &files);

private:
    void startRecording();
    void record(const RecordingOptions &options);
    // Downloads yt-dlp (after asking) and then calls `then`.
    void installYtDlp(const std::function<void()> &then);
    void addItem(StreamRecording *r);
    void updateItem(StreamRecording *r);
    void updateFolderButton();
    void updateButtons();
    void showContextMenu(const QPoint &pos);
    void playRecording(StreamRecording *r);
    void revealRecording(StreamRecording *r);
    void chooseStopAfter();
    int stopAfterSeconds() const;
    StreamRecording *selected() const;
    QList<StreamRecording *> selectedRecordings() const;

    StreamRecorder *m_recorder;
    QLineEdit *m_url;
    QToolButton *m_record;
    QLabel *m_message;
    QComboBox *m_format;
    QComboBox *m_stopAfter;
    QToolButton *m_folder;
    QToolButton *m_stop;
    QToolButton *m_remove;
    QTreeWidget *m_list;
    QHash<QString, QTreeWidgetItem *> m_items;
    bool m_installing = false;
};
