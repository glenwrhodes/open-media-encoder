#include "FrameGrabber.h"

#include "core/AppSettings.h"

#include <QDir>
#include <QFile>
#include <QUuid>

FrameGrabber::FrameGrabber(QObject *parent)
    : QObject(parent)
{
    m_outFile = QDir(AppSettings::tempDir())
                    .filePath(QStringLiteral("preview_%1.bmp").arg(QUuid::createUuid().toString(QUuid::Id128)));
}

FrameGrabber::~FrameGrabber()
{
    if (m_proc) {
        m_proc->disconnect(this);
        m_proc->kill();
        m_proc->waitForFinished(1000);
        delete m_proc;
    }
    QFile::remove(m_outFile);
    for (const QString &f : std::as_const(m_tempFiles))
        QFile::remove(f);
}

void FrameGrabber::request(const BuildRequest &req, double time, bool applyEffects, int tag)
{
    Job job{req, time, applyEffects, tag};
    if (m_proc) {
        m_pending = job;
        return;
    }
    run(job);
}

void FrameGrabber::run(const Job &job)
{
    m_current = job;
    BuildRequest req = job.req;
    if (req.workDir.isEmpty())
        req.workDir = AppSettings::tempDir();
    const BuildResult r = CommandBuilder::buildPreview(req, job.time, m_outFile, job.effects);
    m_tempFiles << r.tempFiles;
    if (!r.ok()) {
        emit failed(r.error, job.tag);
        return;
    }
    QFile::remove(m_outFile);
    m_proc = new QProcess(this);
    connect(m_proc, &QProcess::finished, this, &FrameGrabber::onFinished);
    connect(m_proc, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart)
            onFinished();
    });
    m_proc->start(AppSettings::ffmpegPath(), r.args);
}

void FrameGrabber::onFinished()
{
    QProcess *p = m_proc;
    m_proc = nullptr;
    const QString err = QString::fromUtf8(p->readAllStandardError()).trimmed();
    const bool ok = p->exitStatus() == QProcess::NormalExit && p->exitCode() == 0;
    p->deleteLater();
    for (const QString &f : std::as_const(m_tempFiles))
        QFile::remove(f);
    m_tempFiles.clear();

    if (m_pending) {
        // A newer request arrived; skip this result and run the latest one.
        const Job next = *m_pending;
        m_pending.reset();
        run(next);
        return;
    }
    QImage img;
    if (ok)
        img.load(m_outFile);
    if (!img.isNull())
        emit frameReady(img, m_current.tag);
    else
        emit failed(err.isEmpty() ? QStringLiteral("Could not decode a frame") : err.section('\n', -1), m_current.tag);
}
