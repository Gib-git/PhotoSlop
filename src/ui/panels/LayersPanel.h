#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QPixmap>
#include <QPointer>
#include <QStyledItemDelegate>
#include <QWidget>
#include <functional>

class BlendModeCombo;
class Document;
class QListView;
class QTimer;
class QToolButton;
class ValueField;
class QAction;

// Rows are displayed top-most layer first.
class LayerModel : public QAbstractListModel {
    Q_OBJECT
public:
    explicit LayerModel(QObject* parent = nullptr);
    void setDocument(Document* doc);
    Document* document() const { return m_doc; }
    int docIndex(int row) const;
    int rowFor(int docIndex) const;
    QPixmap thumbnail(int docIndex);
    void invalidateThumbnails() { m_thumbs.clear(); }

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    bool setData(const QModelIndex& index, const QVariant& value, int role) override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    Qt::DropActions supportedDropActions() const override { return Qt::MoveAction; }
    QStringList mimeTypes() const override;
    QMimeData* mimeData(const QModelIndexList& indexes) const override;
    bool dropMimeData(const QMimeData* data, Qt::DropAction action, int row, int column,
                      const QModelIndex& parent) override;

    void reset();

private:
    QPointer<Document> m_doc;
    struct Thumb {
        qint64 key;
        QPoint offset;
        QSize canvas;
        QPixmap pixmap;
    };
    QHash<quint64, Thumb> m_thumbs;
};

class LayerDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    explicit LayerDelegate(LayerModel* model, QObject* parent = nullptr);
    void paint(QPainter* p, const QStyleOptionViewItem& opt, const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& opt, const QModelIndex& index) const override;
    bool editorEvent(QEvent* e, QAbstractItemModel* model, const QStyleOptionViewItem& opt,
                     const QModelIndex& index) override;
    void updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& opt,
                              const QModelIndex& index) const override;

private:
    LayerModel* m_model;
};

class LayersPanel : public QWidget {
    Q_OBJECT
public:
    explicit LayersPanel(QWidget* parent = nullptr);
    void setDocument(Document* doc);
    // Looks up application actions (new layer, delete, merge...) by id.
    void setActionLookup(std::function<QAction*(const QString&)> lookup) { m_lookup = std::move(lookup); }

protected:
    void contextMenuEvent(QContextMenuEvent* e) override;

private:
    void syncControls();
    void syncSelection();
    void trigger(const QString& id);

    QPointer<Document> m_doc;
    LayerModel* m_model;
    QListView* m_list;
    BlendModeCombo* m_mode;
    ValueField* m_opacity;
    ValueField* m_fill;
    QToolButton* m_lockTransparency;
    QToolButton* m_lockPixels;
    QToolButton* m_lockPosition;
    QToolButton* m_lockAll;
    QTimer* m_thumbTimer;
    std::function<QAction*(const QString&)> m_lookup;
    bool m_syncing = false;
};
