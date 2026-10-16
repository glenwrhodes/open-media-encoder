#pragma once

#include "core/EncoderEngine.h"

#include <QHash>
#include <QWidget>

class QueueModel;
class QLabel;
class QProgressBar;
class QVBoxLayout;
class FrameGrabber;

// Live view of the encodes in progress.
class EncodingPanel : public QWidget
{
    Q_OBJECT
public:
    explicit EncodingPanel(QueueModel *model, QWidget *parent = nullptr);

public slots:
    void onTaskStarted(const QString &sourceId, const QString &outputId);
    void onTaskProgress(const QString &outputId, const EncodeStats &stats);
    void onTaskFinished(const QString &sourceId, const QString &outputId, bool ok, const QString &message);

private:
    struct Row {
        QWidget *widget = nullptr;
        QLabel *thumb = nullptr;
        QLabel *title = nullptr;
        QLabel *detail = nullptr;
        QLabel *stats = nullptr;
        QProgressBar *bar = nullptr;
        FrameGrabber *grabber = nullptr;
    };
    void updateIdle();

    QueueModel *m_model;
    QVBoxLayout *m_list;
    QLabel *m_idle;
    QHash<QString, Row> m_rows;
};
