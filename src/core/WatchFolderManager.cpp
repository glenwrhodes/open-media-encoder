#include "WatchFolderManager.h"

#include "AppSettings.h"
#include "Formats.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QUuid>

QString WatchFolder::outputDirFor(const WatchOutput &o) const
{
    return o.outputDir.isEmpty() ? QDir(path).filePath(QStringLiteral("Output")) : o.outputDir;
}

QString WatchFolder::sourceArchiveDir() const { return QDir(path).filePath(QStringLiteral("Source")); }

WatchFolderManager::WatchFolderManager(QObject *parent)
    : QObject(parent)
{
    m_timer.setInterval(4000);
    connect(&m_timer, &QTimer::timeout, this, &WatchFolderManager::scan);
}

const WatchFolder *WatchFolderManager::folder(const QString &id) const
{
    for (const WatchFolder &f : m_folders)
        if (f.id == id)
            return &f;
    return nullptr;
}

QString WatchFolderManager::addFolder(const QString &path, const WatchOutput &firstOutput)
{
    for (const WatchFolder &f : m_folders)
        if (QDir(f.path) == QDir(path))
            return f.id;
    WatchFolder f;
    f.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    f.path = QDir(path).absolutePath();
    f.outputs << firstOutput;
    m_folders << f;
    save();
    emit foldersChanged();
    return f.id;
}

void WatchFolderManager::removeFolder(const QString &id)
{
    for (int i = 0; i < m_folders.size(); ++i) {
        if (m_folders[i].id == id) {
            m_folders.removeAt(i);
            save();
            emit foldersChanged();
            return;
        }
    }
}

void WatchFolderManager::addOutput(const QString &folderId, const WatchOutput &output)
{
    for (WatchFolder &f : m_folders) {
        if (f.id == folderId) {
            f.outputs << output;
            save();
            emit foldersChanged();
            return;
        }
    }
}

void WatchFolderManager::removeOutput(const QString &folderId, int index)
{
    for (WatchFolder &f : m_folders) {
        if (f.id == folderId && index >= 0 && index < f.outputs.size()) {
            f.outputs.removeAt(index);
            save();
            emit foldersChanged();
            return;
        }
    }
}

void WatchFolderManager::setOutput(const QString &folderId, int index, const WatchOutput &output)
{
    for (WatchFolder &f : m_folders) {
        if (f.id == folderId && index >= 0 && index < f.outputs.size()) {
            f.outputs[index] = output;
            save();
            emit foldersChanged();
            return;
        }
    }
}

void WatchFolderManager::setActive(bool active)
{
    m_active = active;
    if (active) {
        m_timer.start();
        scan();
    } else {
        m_timer.stop();
        m_pendingSizes.clear();
    }
}

void WatchFolderManager::scanNow() { scan(); }

void WatchFolderManager::markHandled(const QString &path) { m_handled.insert(QFileInfo(path).absoluteFilePath()); }

void WatchFolderManager::archiveSource(const QString &folderId, const QString &file)
{
    const WatchFolder *f = folder(folderId);
    if (!f || !QFileInfo::exists(file))
        return;
    QDir().mkpath(f->sourceArchiveDir());
    QString target = QDir(f->sourceArchiveDir()).filePath(QFileInfo(file).fileName());
    for (int i = 1; QFileInfo::exists(target); ++i)
        target = QDir(f->sourceArchiveDir())
                     .filePath(QStringLiteral("%1_%2.%3")
                                   .arg(QFileInfo(file).completeBaseName())
                                   .arg(i)
                                   .arg(QFileInfo(file).suffix()));
    QFile::rename(file, target);
}

void WatchFolderManager::scan()
{
    if (!m_active)
        return;
    for (const WatchFolder &f : std::as_const(m_folders)) {
        QDir dir(f.path);
        if (!dir.exists() || f.outputs.isEmpty())
            continue;
        QStringList ready;
        const auto entries = dir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot, QDir::Name);
        for (const QFileInfo &fi : entries) {
            const QString p = fi.absoluteFilePath();
            if (m_handled.contains(p) || !Formats::isMediaFile(p))
                continue;
            const qint64 size = fi.size();
            // Only accept a file once its size is unchanged across two scans (copy finished).
            if (m_pendingSizes.contains(p) && m_pendingSizes.value(p) == size && size > 0) {
                QFile probe(p);
                if (probe.open(QIODevice::ReadOnly)) {
                    probe.close();
                    ready << p;
                    m_pendingSizes.remove(p);
                    m_handled.insert(p);
                }
            } else {
                m_pendingSizes.insert(p, size);
            }
        }
        if (!ready.isEmpty())
            emit filesReady(f.id, ready);
    }
}

void WatchFolderManager::load()
{
    m_folders.clear();
    QFile file(QDir(AppSettings::dataDir()).filePath(QStringLiteral("watchfolders.json")));
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    for (const auto &v : doc.array()) {
        const QJsonObject o = v.toObject();
        WatchFolder f;
        f.id = o.value("id").toString(QUuid::createUuid().toString(QUuid::WithoutBraces));
        f.path = o.value("path").toString();
        for (const auto &ov : o.value("outputs").toArray()) {
            const QJsonObject oo = ov.toObject();
            WatchOutput w;
            w.settings = ExportSettings::fromJson(oo.value("settings").toObject());
            w.presetId = oo.value("presetId").toString();
            w.outputDir = oo.value("outputDir").toString();
            f.outputs << w;
        }
        if (!f.path.isEmpty())
            m_folders << f;
    }
    emit foldersChanged();
}

void WatchFolderManager::save() const
{
    QJsonArray arr;
    for (const WatchFolder &f : m_folders) {
        QJsonArray outs;
        for (const WatchOutput &w : f.outputs)
            outs << QJsonObject{
                {"settings", w.settings.toJson()}, {"presetId", w.presetId}, {"outputDir", w.outputDir}};
        arr << QJsonObject{{"id", f.id}, {"path", f.path}, {"outputs", outs}};
    }
    QFile file(QDir(AppSettings::dataDir()).filePath(QStringLiteral("watchfolders.json")));
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        file.write(QJsonDocument(arr).toJson(QJsonDocument::Indented));
}
