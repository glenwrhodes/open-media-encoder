#pragma once

#include "core/CommandBuilder.h"

#include <QImage>
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <optional>

// Grabs single frames with ffmpeg asynchronously. A new request supersedes any queued one,
// so dragging a time slider stays responsive.
class FrameGrabber : public QObject
{
    Q_OBJECT
public:
    explicit FrameGrabber(QObject *parent = nullptr);
    ~FrameGrabber() override;

    void request(const BuildRequest &req, double time, bool applyEffects, int tag = 0);
    bool busy() const { return m_proc != nullptr; }

signals:
    void frameReady(const QImage &image, int tag);
    void failed(const QString &error, int tag);

private:
    struct Job {
        BuildRequest req;
        double time;
        bool effects;
        int tag;
    };
    void run(const Job &job);
    void onFinished();

    QProcess *m_proc = nullptr;
    std::optional<Job> m_pending;
    Job m_current;
    QString m_outFile;
    QStringList m_tempFiles;
};
