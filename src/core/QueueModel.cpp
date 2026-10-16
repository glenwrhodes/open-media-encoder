#include "QueueModel.h"

#include "AppSettings.h"
#include "CommandBuilder.h"
#include "Formats.h"
#include "PresetManager.h"

#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMimeData>
#include <QUrl>
#include <QUuid>
#include <algorithm>

namespace {
QString newId() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
} // namespace

QString statusText(OutputStatus s)
{
    switch (s) {
    case OutputStatus::Ready:
        return QStringLiteral("Ready");
    case OutputStatus::Encoding:
        return QStringLiteral("Encoding");
    case OutputStatus::Paused:
        return QStringLiteral("Paused");
    case OutputStatus::Done:
        return QStringLiteral("Done");
    case OutputStatus::Failed:
        return QStringLiteral("Failed");
    case OutputStatus::Stopped:
        return QStringLiteral("Stopped");
    case OutputStatus::Skipped:
        return QStringLiteral("Skipped");
    }
    return {};
}

// ----------------------------------------------------------------- items

QJsonObject OutputItem::toJson() const
{
    return QJsonObject{
        {"id", id},
        {"settings", settings.toJson()},
        {"presetId", presetId},
        {"outputPath", outputPath},
        {"inPoint", inPoint},
        {"outPoint", outPoint},
        {"status", int(status)},
        {"message", message},
        {"finalPath", finalPath},
    };
}

OutputItem OutputItem::fromJson(const QJsonObject &o)
{
    OutputItem it;
    it.id = o.value("id").toString(newId());
    it.settings = ExportSettings::fromJson(o.value("settings").toObject());
    it.presetId = o.value("presetId").toString();
    it.outputPath = o.value("outputPath").toString();
    it.inPoint = o.value("inPoint").toDouble(-1);
    it.outPoint = o.value("outPoint").toDouble(-1);
    it.status = OutputStatus(o.value("status").toInt());
    it.message = o.value("message").toString();
    it.finalPath = o.value("finalPath").toString();
    if (it.status == OutputStatus::Encoding || it.status == OutputStatus::Paused)
        it.status = OutputStatus::Ready;
    if (it.status == OutputStatus::Done)
        it.progress = 1.0;
    return it;
}

QString SourceItem::displayName() const
{
    if (files.isEmpty())
        return {};
    if (files.size() == 1)
        return QFileInfo(files.first()).fileName();
    return QStringLiteral("%1 (+%2 stitched)").arg(QFileInfo(files.first()).fileName()).arg(files.size() - 1);
}

// ----------------------------------------------------------------- model basics

QueueModel::QueueModel(QObject *parent)
    : QAbstractItemModel(parent)
{}

// internalId: 0 for sources, (sourceRow + 1) for outputs.
QModelIndex QueueModel::index(int row, int column, const QModelIndex &parent) const
{
    if (column < 0 || column >= ColumnCount || row < 0)
        return {};
    if (!parent.isValid()) {
        if (row >= m_sources.size())
            return {};
        return createIndex(row, column, quintptr(0));
    }
    if (parent.internalId() != 0)
        return {};
    const int srow = parent.row();
    if (srow >= m_sources.size() || row >= m_sources[srow].outputs.size())
        return {};
    return createIndex(row, column, quintptr(srow + 1));
}

QModelIndex QueueModel::parent(const QModelIndex &child) const
{
    if (!child.isValid() || child.internalId() == 0)
        return {};
    return createIndex(int(child.internalId() - 1), 0, quintptr(0));
}

int QueueModel::rowCount(const QModelIndex &parent) const
{
    if (!parent.isValid())
        return m_sources.size();
    if (parent.internalId() == 0 && parent.column() == 0 && parent.row() < m_sources.size())
        return m_sources[parent.row()].outputs.size();
    return 0;
}

int QueueModel::columnCount(const QModelIndex &) const { return ColumnCount; }

QVariant QueueModel::headerData(int section, Qt::Orientation o, int role) const
{
    if (o != Qt::Horizontal || role != Qt::DisplayRole)
        return {};
    switch (section) {
    case ColFormat:
        return QStringLiteral("Format");
    case ColPreset:
        return QStringLiteral("Preset");
    case ColOutput:
        return QStringLiteral("Output File");
    case ColStatus:
        return QStringLiteral("Status");
    }
    return {};
}

QVariant QueueModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
        return {};
    if (index.internalId() == 0) {
        const SourceItem &s = m_sources.at(index.row());
        if (role == IdRole)
            return s.id;
        if (role == IsSourceRole)
            return true;
        if (role == Qt::DisplayRole && index.column() == ColFormat) {
            QString text = s.displayName();
            if (s.probed && s.info.valid)
                text += QStringLiteral("    —    %1").arg(s.info.shortDescription());
            else if (s.probed)
                text += QStringLiteral("    —    %1").arg(s.info.error);
            else
                text += QStringLiteral("    —    reading media…");
            return text;
        }
        if (role == Qt::ToolTipRole)
            return s.files.join('\n') + (s.probed ? "\n\n" + s.info.describe() : QString());
        if (role == Qt::ForegroundRole && s.probed && !s.info.valid)
            return QColor(230, 90, 90);
        return {};
    }

    const SourceItem &s = m_sources.at(int(index.internalId() - 1));
    const OutputItem &o = s.outputs.at(index.row());
    switch (role) {
    case IdRole:
        return o.id;
    case IsSourceRole:
        return false;
    case StatusRole:
        return int(o.status);
    case ProgressRole:
        return o.progress;
    case FormatIdRole:
        return o.settings.format;
    case PresetIdRole:
        return o.presetId;
    case Qt::DisplayRole:
    case Qt::EditRole:
        switch (index.column()) {
        case ColFormat: {
            const FormatDef *f = Formats::format(o.settings.format);
            return f ? f->displayName : o.settings.format;
        }
        case ColPreset:
            return o.presetId.isEmpty() ? QStringLiteral("Custom") : o.settings.presetName;
        case ColOutput:
            return o.finalPath.isEmpty() ? o.outputPath : o.finalPath;
        case ColStatus:
            if (o.status == OutputStatus::Encoding)
                return QStringLiteral("Encoding %1%").arg(int(o.progress * 100));
            if (o.status == OutputStatus::Failed && !o.message.isEmpty())
                return QStringLiteral("Failed: %1").arg(o.message.section('\n', 0, 0));
            return statusText(o.status);
        }
        break;
    case Qt::ToolTipRole:
        if (index.column() == ColStatus && !o.message.isEmpty())
            return o.message;
        return o.settings.summary(s.probed ? &s.info : nullptr);
    case Qt::ForegroundRole:
        if (index.column() == ColOutput || index.column() == ColPreset || index.column() == ColFormat)
            return QColor(0x4d, 0xa3, 0xff);
        if (o.status == OutputStatus::Failed)
            return QColor(230, 90, 90);
        if (o.status == OutputStatus::Done)
            return QColor(110, 200, 120);
        if (o.status == OutputStatus::Skipped || o.status == OutputStatus::Stopped)
            return QColor(150, 150, 150);
        break;
    }
    return {};
}

bool QueueModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (!index.isValid() || index.internalId() == 0 || role != Qt::EditRole)
        return false;
    SourceItem &s = m_sources[int(index.internalId() - 1)];
    OutputItem &o = s.outputs[index.row()];
    if (index.column() == ColFormat) {
        const QString fmt = value.toString();
        if (fmt == o.settings.format)
            return false;
        const Preset p = PresetManager::instance().firstPresetForFormat(fmt);
        applyPreset(o.id, p.id.isEmpty() ? QString() : p.id);
        if (p.id.isEmpty())
            setOutputSettings(o.id, p.settings, {});
        return true;
    }
    if (index.column() == ColPreset) {
        applyPreset(o.id, value.toString());
        return true;
    }
    if (index.column() == ColOutput) {
        setOutputPath(o.id, value.toString());
        return true;
    }
    return false;
}

Qt::ItemFlags QueueModel::flags(const QModelIndex &index) const
{
    Qt::ItemFlags f = Qt::ItemIsDropEnabled;
    if (!index.isValid())
        return f;
    f |= Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    if (index.internalId() != 0) {
        const OutputItem &o = m_sources[int(index.internalId() - 1)].outputs[index.row()];
        if (o.status != OutputStatus::Encoding && o.status != OutputStatus::Paused &&
            (index.column() == ColFormat || index.column() == ColPreset))
            f |= Qt::ItemIsEditable;
    }
    return f;
}

Qt::DropActions QueueModel::supportedDropActions() const { return Qt::CopyAction | Qt::MoveAction; }

QStringList QueueModel::mimeTypes() const
{
    return {QStringLiteral("text/uri-list"), QString::fromLatin1(PresetManager::MimeType)};
}

bool QueueModel::canDropMimeData(const QMimeData *data, Qt::DropAction, int, int, const QModelIndex &parent) const
{
    if (data->hasUrls())
        return true;
    if (data->hasFormat(PresetManager::MimeType))
        return parent.isValid();
    return false;
}

bool QueueModel::dropMimeData(const QMimeData *data, Qt::DropAction, int, int, const QModelIndex &parent)
{
    if (data->hasFormat(PresetManager::MimeType)) {
        const QString presetId = QString::fromUtf8(data->data(PresetManager::MimeType));
        const Preset *p = PresetManager::instance().find(presetId);
        if (!p || !parent.isValid())
            return false;
        if (parent.internalId() == 0) {
            const SourceItem &s = m_sources[parent.row()];
            addOutput(s.id, p->settings, p->id);
        } else {
            const OutputItem &o = m_sources[int(parent.internalId() - 1)].outputs[parent.row()];
            applyPreset(o.id, p->id);
        }
        return true;
    }
    if (data->hasUrls()) {
        QStringList files;
        for (const QUrl &u : data->urls())
            if (u.isLocalFile())
                files << u.toLocalFile();
        if (!files.isEmpty())
            emit filesDropped(files);
        return !files.isEmpty();
    }
    return false;
}

// ----------------------------------------------------------------- queue API

QString QueueModel::addSource(const QStringList &files, const QString &watchFolderId)
{
    SourceItem s;
    s.id = newId();
    s.files = files;
    s.info.path = files.value(0);
    s.watchFolderId = watchFolderId;
    beginInsertRows({}, m_sources.size(), m_sources.size());
    m_sources << s;
    endInsertRows();
    emit sourceAdded(s.id);
    emit queueChanged();
    return s.id;
}

QString QueueModel::addOutput(const QString &sourceId, const ExportSettings &settings, const QString &presetId,
                              const QString &outputDir)
{
    const QModelIndex sidx = indexForSource(sourceId);
    if (!sidx.isValid())
        return {};
    SourceItem &s = m_sources[sidx.row()];
    OutputItem o;
    o.id = newId();
    o.settings = settings;
    o.presetId = presetId;
    const QString dir = outputDir.isEmpty() ? AppSettings::defaultOutputDir(s.files.value(0)) : outputDir;
    o.outputPath = CommandBuilder::defaultOutputPath(s.files.value(0), settings, dir);
    beginInsertRows(sidx, s.outputs.size(), s.outputs.size());
    s.outputs << o;
    endInsertRows();
    emit queueChanged();
    return o.id;
}

void QueueModel::setSourceInfo(const QString &sourceId, const MediaInfo &info, double totalDuration)
{
    SourceItem *s = source(sourceId);
    if (!s)
        return;
    s->info = info;
    s->totalDuration = totalDuration;
    s->probed = true;
    const QModelIndex idx = indexForSource(sourceId);
    emit dataChanged(idx, idx.siblingAtColumn(ColumnCount - 1));
    for (const OutputItem &o : s->outputs)
        emitOutputChanged(o.id);
    emit queueChanged();
}

void QueueModel::removeIndexes(const QModelIndexList &indexes)
{
    QStringList sourceIds;
    QStringList outputIds;
    for (const QModelIndex &i : indexes) {
        if (!i.isValid())
            continue;
        if (i.internalId() == 0)
            sourceIds << m_sources[i.row()].id;
        else
            outputIds << m_sources[int(i.internalId() - 1)].outputs[i.row()].id;
    }
    for (const QString &id : outputIds) {
        SourceItem *owner = nullptr;
        OutputItem *o = output(id, &owner);
        if (!o || o->status == OutputStatus::Encoding || o->status == OutputStatus::Paused)
            continue;
        const QModelIndex sidx = indexForSource(owner->id);
        const int row = indexForOutput(id).row();
        beginRemoveRows(sidx, row, row);
        owner->outputs.removeAt(row);
        endRemoveRows();
        if (owner->outputs.isEmpty() && !sourceIds.contains(owner->id))
            sourceIds << owner->id;
    }
    sourceIds.removeDuplicates();
    for (const QString &id : sourceIds)
        removeSource(id);
    emit queueChanged();
}

void QueueModel::removeSource(const QString &sourceId)
{
    const QModelIndex idx = indexForSource(sourceId);
    if (!idx.isValid())
        return;
    const SourceItem &s = m_sources[idx.row()];
    for (const OutputItem &o : s.outputs)
        if (o.status == OutputStatus::Encoding || o.status == OutputStatus::Paused)
            return;
    beginRemoveRows({}, idx.row(), idx.row());
    m_sources.removeAt(idx.row());
    endRemoveRows();
    emit queueChanged();
}

bool QueueModel::moveSource(const QString &sourceId, int newRow)
{
    const QModelIndex idx = indexForSource(sourceId);
    if (!idx.isValid())
        return false;
    const int from = idx.row();
    const int to = qBound(0, newRow, int(m_sources.size()) - 1);
    if (from == to)
        return false;
    // Output indexes encode their source's row, so remap every persistent index explicitly
    // (keeps selection and expansion intact in views).
    emit layoutAboutToBeChanged();
    const QModelIndexList oldIndexes = persistentIndexList();
    struct Key {
        QString sourceId;
        int outputRow;
        int column;
    };
    QList<Key> keys;
    keys.reserve(oldIndexes.size());
    for (const QModelIndex &i : oldIndexes)
        keys << Key{sourceIdFor(i), i.internalId() == 0 ? -1 : i.row(), i.column()};
    m_sources.move(from, to);
    QModelIndexList newIndexes;
    newIndexes.reserve(keys.size());
    for (const Key &k : keys) {
        const QModelIndex src = indexForSource(k.sourceId);
        newIndexes << (k.outputRow < 0 ? src.siblingAtColumn(k.column) : index(k.outputRow, k.column, src));
    }
    changePersistentIndexList(oldIndexes, newIndexes);
    emit layoutChanged();
    emit queueChanged();
    return true;
}

QString QueueModel::duplicateOutput(const QString &outputId)
{
    SourceItem *owner = nullptr;
    OutputItem *o = output(outputId, &owner);
    if (!o)
        return {};
    OutputItem copy = *o;
    copy.id = newId();
    copy.status = OutputStatus::Ready;
    copy.progress = 0;
    copy.message.clear();
    copy.finalPath.clear();
    const QModelIndex sidx = indexForSource(owner->id);
    beginInsertRows(sidx, owner->outputs.size(), owner->outputs.size());
    owner->outputs << copy;
    endInsertRows();
    emit queueChanged();
    return copy.id;
}

void QueueModel::applyPreset(const QString &outputId, const QString &presetId)
{
    const Preset *p = PresetManager::instance().find(presetId);
    if (!p)
        return;
    setOutputSettings(outputId, p->settings, p->id);
}

void QueueModel::setOutputSettings(const QString &outputId, const ExportSettings &settings, const QString &presetId)
{
    SourceItem *owner = nullptr;
    OutputItem *o = output(outputId, &owner);
    if (!o)
        return;
    const QString oldExt = o->settings.extension();
    const bool oldSeq = o->settings.isImageSequence();
    o->settings = settings;
    o->presetId = presetId;
    // Keep the chosen folder and base name but follow the new extension / sequence pattern.
    if (oldExt != settings.extension() || oldSeq != settings.isImageSequence()) {
        QFileInfo fi(o->outputPath);
        QString base = fi.completeBaseName();
        if (oldSeq)
            base.remove(QStringLiteral("_%05d"));
        const QString name = settings.isImageSequence() ? QStringLiteral("%1_%05d.%2").arg(base, settings.extension())
                                                        : QStringLiteral("%1.%2").arg(base, settings.extension());
        o->outputPath = fi.dir().filePath(name);
    }
    if (o->status != OutputStatus::Encoding && o->status != OutputStatus::Paused) {
        o->status = OutputStatus::Ready;
        o->progress = 0;
        o->message.clear();
        o->finalPath.clear();
    }
    emitOutputChanged(outputId);
    emit queueChanged();
}

void QueueModel::setOutputPath(const QString &outputId, const QString &path)
{
    OutputItem *o = output(outputId);
    if (!o || path.isEmpty())
        return;
    o->outputPath = path;
    o->finalPath.clear();
    emitOutputChanged(outputId);
    emit queueChanged();
}

void QueueModel::setOutputRange(const QString &outputId, double in, double out)
{
    OutputItem *o = output(outputId);
    if (!o)
        return;
    o->inPoint = in;
    o->outPoint = out;
    emitOutputChanged(outputId);
    emit queueChanged();
}

void QueueModel::setStatus(const QString &outputId, OutputStatus status, const QString &message)
{
    OutputItem *o = output(outputId);
    if (!o)
        return;
    o->status = status;
    o->message = message;
    if (status == OutputStatus::Encoding && !o->started.isValid())
        o->started = QDateTime::currentDateTime();
    if (status == OutputStatus::Done) {
        o->progress = 1.0;
        o->finished = QDateTime::currentDateTime();
    }
    if (status == OutputStatus::Ready)
        o->progress = 0.0;
    emitOutputChanged(outputId);
    emit queueChanged();
}

void QueueModel::setProgress(const QString &outputId, double progress)
{
    OutputItem *o = output(outputId);
    if (!o)
        return;
    o->progress = qBound(0.0, progress, 1.0);
    const QModelIndex idx = indexForOutput(outputId, ColStatus);
    emit dataChanged(idx, idx);
}

void QueueModel::setFinalPath(const QString &outputId, const QString &path)
{
    OutputItem *o = output(outputId);
    if (!o)
        return;
    o->finalPath = path;
    emitOutputChanged(outputId);
}

void QueueModel::resetStatus(const QStringList &outputIds)
{
    for (const QString &id : outputIds) {
        OutputItem *o = output(id);
        if (!o || o->status == OutputStatus::Encoding || o->status == OutputStatus::Paused)
            continue;
        o->status = OutputStatus::Ready;
        o->progress = 0;
        o->message.clear();
        o->finalPath.clear();
        o->started = {};
        o->finished = {};
        emitOutputChanged(id);
    }
    emit queueChanged();
}

void QueueModel::skip(const QStringList &outputIds)
{
    for (const QString &id : outputIds) {
        OutputItem *o = output(id);
        if (o && o->status == OutputStatus::Ready) {
            o->status = OutputStatus::Skipped;
            emitOutputChanged(id);
        }
    }
    emit queueChanged();
}

void QueueModel::clearDone()
{
    QModelIndexList toRemove;
    for (int s = 0; s < m_sources.size(); ++s) {
        bool allDone = !m_sources[s].outputs.isEmpty();
        for (const OutputItem &o : m_sources[s].outputs)
            allDone = allDone && o.status == OutputStatus::Done;
        if (allDone)
            toRemove << index(s, 0);
    }
    removeIndexes(toRemove);
}

void QueueModel::clear()
{
    for (const SourceItem &s : m_sources)
        for (const OutputItem &o : s.outputs)
            if (o.status == OutputStatus::Encoding || o.status == OutputStatus::Paused)
                return;
    beginResetModel();
    m_sources.clear();
    endResetModel();
    emit queueChanged();
}

SourceItem *QueueModel::source(const QString &sourceId)
{
    for (SourceItem &s : m_sources)
        if (s.id == sourceId)
            return &s;
    return nullptr;
}

const SourceItem *QueueModel::source(const QString &sourceId) const
{
    for (const SourceItem &s : m_sources)
        if (s.id == sourceId)
            return &s;
    return nullptr;
}

OutputItem *QueueModel::output(const QString &outputId, SourceItem **owner)
{
    for (SourceItem &s : m_sources)
        for (OutputItem &o : s.outputs)
            if (o.id == outputId) {
                if (owner)
                    *owner = &s;
                return &o;
            }
    return nullptr;
}

QModelIndex QueueModel::indexForSource(const QString &sourceId, int column) const
{
    for (int i = 0; i < m_sources.size(); ++i)
        if (m_sources[i].id == sourceId)
            return index(i, column);
    return {};
}

QModelIndex QueueModel::indexForOutput(const QString &outputId, int column) const
{
    for (int i = 0; i < m_sources.size(); ++i)
        for (int j = 0; j < m_sources[i].outputs.size(); ++j)
            if (m_sources[i].outputs[j].id == outputId)
                return index(j, column, index(i, 0));
    return {};
}

QStringList QueueModel::outputIdsFor(const QModelIndexList &indexes) const
{
    QStringList ids;
    for (const QModelIndex &i : indexes) {
        if (!i.isValid())
            continue;
        if (i.internalId() == 0) {
            for (const OutputItem &o : m_sources[i.row()].outputs)
                ids << o.id;
        } else {
            ids << m_sources[int(i.internalId() - 1)].outputs[i.row()].id;
        }
    }
    ids.removeDuplicates();
    return ids;
}

QString QueueModel::sourceIdFor(const QModelIndex &index) const
{
    if (!index.isValid())
        return {};
    const int row = index.internalId() == 0 ? index.row() : int(index.internalId() - 1);
    return m_sources.value(row).id;
}

bool QueueModel::nextReady(QString *sourceId, QString *outputId) const
{
    for (const SourceItem &s : m_sources) {
        if (!s.probed || !s.info.valid)
            continue;
        for (const OutputItem &o : s.outputs) {
            if (o.status == OutputStatus::Ready) {
                *sourceId = s.id;
                *outputId = o.id;
                return true;
            }
        }
    }
    return false;
}

int QueueModel::countWithStatus(OutputStatus st) const
{
    int n = 0;
    for (const SourceItem &s : m_sources)
        for (const OutputItem &o : s.outputs)
            n += o.status == st ? 1 : 0;
    return n;
}

int QueueModel::outputCount() const
{
    int n = 0;
    for (const SourceItem &s : m_sources)
        n += s.outputs.size();
    return n;
}

void QueueModel::emitOutputChanged(const QString &outputId)
{
    const QModelIndex a = indexForOutput(outputId, 0);
    if (a.isValid())
        emit dataChanged(a, a.siblingAtColumn(ColumnCount - 1));
}

bool QueueModel::save(const QString &path) const
{
    QJsonArray arr;
    for (const SourceItem &s : m_sources) {
        QJsonArray outs;
        for (const OutputItem &o : s.outputs)
            outs << o.toJson();
        arr << QJsonObject{{"id", s.id},
                           {"files", QJsonArray::fromStringList(s.files)},
                           {"watchFolderId", s.watchFolderId},
                           {"outputs", outs}};
    }
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    f.write(QJsonDocument(QJsonObject{{"version", 1}, {"sources", arr}}).toJson(QJsonDocument::Indented));
    return true;
}

bool QueueModel::load(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject())
        return false;
    QList<SourceItem> loaded;
    for (const auto &sv : doc.object().value("sources").toArray()) {
        const QJsonObject so = sv.toObject();
        SourceItem s;
        s.id = so.value("id").toString(newId());
        for (const auto &fv : so.value("files").toArray())
            s.files << fv.toString();
        if (s.files.isEmpty())
            continue;
        s.info.path = s.files.first();
        s.watchFolderId = so.value("watchFolderId").toString();
        for (const auto &ov : so.value("outputs").toArray())
            s.outputs << OutputItem::fromJson(ov.toObject());
        loaded << s;
    }
    beginResetModel();
    m_sources = loaded;
    endResetModel();
    for (const SourceItem &s : m_sources)
        emit sourceAdded(s.id);
    emit queueChanged();
    return true;
}
