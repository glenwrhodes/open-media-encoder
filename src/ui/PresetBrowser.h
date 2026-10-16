#pragma once

#include <QTreeWidget>
#include <QWidget>

class QLineEdit;

class PresetTree : public QTreeWidget
{
    Q_OBJECT
public:
    using QTreeWidget::QTreeWidget;

protected:
    QStringList mimeTypes() const override;
    QMimeData *mimeData(const QList<QTreeWidgetItem *> &items) const override;
};

// Browsable library of system and user presets; presets can be dragged onto queue items.
class PresetBrowser : public QWidget
{
    Q_OBJECT
public:
    explicit PresetBrowser(QWidget *parent = nullptr);
    QString selectedPresetId() const;

signals:
    void applyPresetRequested(const QString &presetId);
    void newPresetRequested();
    void editPresetRequested(const QString &presetId);

public slots:
    void reload();

private:
    void applyFilter();
    void deleteSelected();
    void importPreset();
    void exportSelected();

    PresetTree *m_tree;
    QLineEdit *m_search;
};
