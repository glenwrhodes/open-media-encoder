#pragma once

#include "ExportSettings.h"
#include "MediaInfo.h"

#include <QAbstractItemModel>
#include <QDateTime>
#include <QList>

enum class OutputStatus { Ready, Encoding, Paused, Done, Failed, Stopped, Skipped };

QString statusText(OutputStatus s);

struct OutputItem {
    QString id;
    ExportSettings settings;
    QString presetId; // empty when settings were customised
    QString outputPath;
    double inPoint = -1.0;
    double outPoint = -1.0;
    OutputStatus status = OutputStatus::Ready;
    double progress = 0.0; // 0..1
    QString message;
    QString finalPath; // actual file written (after collision handling)
    QDateTime started;
    QDateTime finished;

    QJsonObject toJson() const;
    static OutputItem fromJson(const QJsonObject &o);
};

struct SourceItem {
    QString id;
    QStringList files; // > 1 when clips are stitched together
    MediaInfo info;
    double totalDuration = 0.0;
    bool probed = false;
    QString watchFolderId;
    QList<OutputItem> outputs;

    QString displayName() const;
    double duration() const { return totalDuration > 0 ? totalDuration : info.duration; }
};

class QueueModel : public QAbstractItemModel
{
    Q_OBJECT
public:
    enum Column { ColFormat, ColPreset, ColOutput, ColStatus, ColumnCount };
    enum Roles {
        IdRole = Qt::UserRole + 1,
        IsSourceRole,
        StatusRole,
        ProgressRole,
        FormatIdRole,
        PresetIdRole,
    };

    explicit QueueModel(QObject *parent = nullptr);

    // QAbstractItemModel
    QModelIndex index(int row, int column, const QModelIndex &parent = {}) const override;
    QModelIndex parent(const QModelIndex &child) const override;
    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role) override;
    QVariant headerData(int section, Qt::Orientation o, int role) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    Qt::DropActions supportedDropActions() const override;
    QStringList mimeTypes() const override;
    bool canDropMimeData(const QMimeData *data, Qt::DropAction action, int row, int column,
                         const QModelIndex &parent) const override;
    bool dropMimeData(const QMimeData *data, Qt::DropAction action, int row, int column,
                      const QModelIndex &parent) override;

    // Queue API
    QString addSource(const QStringList &files, const QString &watchFolderId = {});
    QString addOutput(const QString &sourceId, const ExportSettings &settings, const QString &presetId,
                      const QString &outputDir = {});
    void setSourceInfo(const QString &sourceId, const MediaInfo &info, double totalDuration);
    void removeIndexes(const QModelIndexList &indexes);
    void removeSource(const QString &sourceId);
    QString duplicateOutput(const QString &outputId);
    void applyPreset(const QString &outputId, const QString &presetId);
    void setOutputSettings(const QString &outputId, const ExportSettings &settings, const QString &presetId);
    void setOutputPath(const QString &outputId, const QString &path);
    void setOutputRange(const QString &outputId, double in, double out);
    void setStatus(const QString &outputId, OutputStatus status, const QString &message = {});
    void setProgress(const QString &outputId, double progress);
    void setFinalPath(const QString &outputId, const QString &path);
    void resetStatus(const QStringList &outputIds);
    void skip(const QStringList &outputIds);
    void clearDone();
    void clear();

    SourceItem *source(const QString &sourceId);
    const SourceItem *source(const QString &sourceId) const;
    OutputItem *output(const QString &outputId, SourceItem **owner = nullptr);
    const QList<SourceItem> &sources() const { return m_sources; }
    QModelIndex indexForSource(const QString &sourceId, int column = 0) const;
    QModelIndex indexForOutput(const QString &outputId, int column = 0) const;
    // Output ids referenced by a selection (sources expand to all their outputs).
    QStringList outputIdsFor(const QModelIndexList &indexes) const;
    QString sourceIdFor(const QModelIndex &index) const;

    bool nextReady(QString *sourceId, QString *outputId) const;
    int countWithStatus(OutputStatus s) const;
    int outputCount() const;

    bool save(const QString &path) const;
    bool load(const QString &path);

signals:
    void filesDropped(const QStringList &files);
    void sourceAdded(const QString &sourceId);
    void queueChanged();

private:
    void emitOutputChanged(const QString &outputId);
    QList<SourceItem> m_sources;
};
