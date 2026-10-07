#pragma once

#include "core/Document.h"

#include <QUndoCommand>

// Restores a whole-document snapshot. Used for structural operations
// (layer order, crop, resize, selection changes...).
class SnapshotCommand : public QUndoCommand {
public:
    SnapshotCommand(Document* doc, const QString& text, DocState before, DocState after,
                    int mergeId = -1);
    void undo() override;
    void redo() override;
    int id() const override { return m_mergeId; }
    bool mergeWith(const QUndoCommand* other) override;

private:
    Document* m_doc;
    DocState m_before;
    DocState m_after;
    int m_mergeId;
    bool m_first = true;
};

// Stores only the changed pixels of one layer (paint strokes, fills).
class PixelCommand : public QUndoCommand {
public:
    PixelCommand(Document* doc, const QString& text, quint64 layerId, const QRect& oldRect,
                 const QRect& newRect, const QRect& region, QImage before, QImage after);
    void undo() override;
    void redo() override;

private:
    void apply(const QImage& pixels, const QRect& geometryBefore, const QRect& geometryAfter);

    Document* m_doc;
    quint64 m_layerId;
    QRect m_oldRect;
    QRect m_newRect;
    QRect m_region; // canvas coordinates
    QImage m_before;
    QImage m_after;
    bool m_first = true;
};

// Helper for tools that paint into a layer: remembers the original pixels,
// tracks the dirty area and pushes a PixelCommand on commit.
class PixelEdit {
public:
    PixelEdit(Document* doc, int layerIndex, const QRect& coverRect);
    ~PixelEdit();

    Document* document() const { return m_doc; }
    Layer& layer();
    QImage& image() { return layer().image; }
    QPoint offset() { return layer().offset; }
    const QImage& original() const { return m_original; }
    QRect layerRect() const { return m_newRect; }

    void markDirty(const QRect& canvasRect);
    QRect dirty() const { return m_dirty; }
    void commit(const QString& text);
    void cancel();
    bool isActive() const { return m_active; }

private:
    Document* m_doc;
    quint64 m_layerId;
    QRect m_oldRect;
    QRect m_newRect;
    QImage m_original;
    QRect m_dirty;
    bool m_active = true;
};
