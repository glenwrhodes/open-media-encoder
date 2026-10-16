#pragma once

#include <QStyledItemDelegate>
#include <QTreeView>
#include <QWidget>

class QueueModel;
class QCheckBox;
class QComboBox;
class QLabel;
class QToolButton;

class QueueView : public QTreeView
{
    Q_OBJECT
public:
    using QTreeView::QTreeView;

signals:
    void itemClicked(const QModelIndex &index, const QPoint &pos);

protected:
    void paintEvent(QPaintEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
};

class QueueItemDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    static constexpr int ArrowWidth = 18;

    void paint(QPainter *p, const QStyleOptionViewItem &opt, const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &opt, const QModelIndex &index) const override;
    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &opt, const QModelIndex &index) const override;
    void setEditorData(QWidget *editor, const QModelIndex &index) const override;
    void setModelData(QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const override;
};

class QueuePanel : public QWidget
{
    Q_OBJECT
public:
    explicit QueuePanel(QueueModel *model, QWidget *parent = nullptr);

    QueueView *view() const { return m_view; }
    QModelIndexList selectedRows() const;
    void setEngineState(bool running, bool paused);
    void setAutoEncode(bool on);
    void updateSummary();

signals:
    void addSourceClicked();
    void addOutputClicked();
    void duplicateClicked();
    void removeClicked();
    void startClicked();
    void pauseClicked();
    void stopClicked();
    void editOutputRequested(const QString &outputId);
    void chooseOutputPathRequested(const QString &outputId);
    void contextMenuRequested(const QPoint &globalPos);
    void autoEncodeToggled(bool on);

private:
    void spanSourceRows();

    QueueModel *m_model;
    QueueView *m_view;
    QToolButton *m_start;
    QToolButton *m_pause;
    QToolButton *m_stop;
    QCheckBox *m_autoEncode;
    QComboBox *m_renderer;
    QLabel *m_summary;
};
