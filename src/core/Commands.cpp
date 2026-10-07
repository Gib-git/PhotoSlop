#include "core/Commands.h"

#include <QUndoStack>
#include <algorithm>
#include <iterator>

// ---------------- SnapshotCommand ----------------

SnapshotCommand::SnapshotCommand(Document* doc, const QString& text, DocState before,
                                 DocState after, int mergeId)
    : QUndoCommand(text)
    , m_doc(doc)
    , m_before(std::move(before))
    , m_after(std::move(after))
    , m_mergeId(mergeId)
{
}

void SnapshotCommand::undo() { m_doc->restoreState(m_before); }

void SnapshotCommand::redo()
{
    if (m_first) {
        m_first = false;
        return;
    }
    m_doc->restoreState(m_after);
}

bool SnapshotCommand::mergeWith(const QUndoCommand* other)
{
    if (other->id() != id() || m_mergeId < 0) return false;
    auto* o = static_cast<const SnapshotCommand*>(other);
    // Only merge consecutive edits to the same active layer.
    if (o->m_before.active != m_after.active) return false;
    m_after = o->m_after;
    return true;
}

// ---------------- PixelCommand ----------------

PixelCommand::PixelCommand(Document* doc, const QString& text, quint64 layerId,
                           const QRect& oldRect, const QRect& newRect, const QRect& region,
                           QImage before, QImage after)
    : QUndoCommand(text)
    , m_doc(doc)
    , m_layerId(layerId)
    , m_oldRect(oldRect)
    , m_newRect(newRect)
    , m_region(region)
    , m_before(std::move(before))
    , m_after(std::move(after))
{
}

void PixelCommand::apply(const QImage& pixels, const QRect& geometryBefore,
                         const QRect& geometryAfter)
{
    int idx = m_doc->indexOfId(m_layerId);
    if (idx == -1) return; // may be Document::kQuickMaskIndex
    Layer& l = m_doc->layerRef(idx);
    // Write pixels while the buffer has the geometry they were captured in.
    l.setGeometry(geometryBefore);
    if (!m_region.isEmpty() && !pixels.isNull()) {
        QPoint local = m_region.topLeft() - l.offset;
        const int bytes = m_region.width() * 4;
        for (int y = 0; y < m_region.height(); ++y)
            memcpy(l.image.scanLine(local.y() + y) + local.x() * 4, pixels.constScanLine(y), bytes);
    }
    l.setGeometry(geometryAfter);
    // Geometry changes only add or remove transparent pixels, so only the region repaints.
    m_doc->notifyLayerPixels(idx, m_region);
}

void PixelCommand::undo() { apply(m_before, m_newRect, m_oldRect); }

void PixelCommand::redo()
{
    if (m_first) {
        m_first = false;
        return;
    }
    apply(m_after, m_newRect, m_newRect);
}

// ---------------- PixelEdit ----------------

PixelEdit::PixelEdit(Document* doc, int layerIndex, const QRect& coverRect)
    : m_doc(doc)
{
    Layer& l = doc->layerRef(layerIndex);
    m_layerId = l.id;
    m_oldRect = l.rect();
    l.ensureCovers(coverRect);
    m_newRect = l.rect();
    m_original = l.image; // shared until the layer is written to
}

PixelEdit::~PixelEdit()
{
    if (m_active) cancel();
}

Layer& PixelEdit::layer()
{
    int idx = m_doc->indexOfId(m_layerId);
    Q_ASSERT(idx != -1);
    return m_doc->layerRef(idx);
}

void PixelEdit::markDirty(const QRect& canvasRect)
{
    QRect r = canvasRect & m_newRect;
    if (r.isEmpty()) return;
    m_dirty |= r;
    m_doc->notifyLayerPixels(m_doc->indexOfId(m_layerId), r);
}

void PixelEdit::commit(const QString& text)
{
    if (!m_active) return;
    m_active = false;
    Layer& l = layer();
    if (m_dirty.isEmpty()) {
        l.setGeometry(m_oldRect);
        return;
    }
    QRect local = m_dirty.translated(-m_newRect.topLeft());
    QImage before = m_original.copy(local);
    QImage after = l.image.copy(local);
    m_original = QImage();
    m_doc->undoStack()->push(new PixelCommand(m_doc, text, m_layerId, m_oldRect, m_newRect,
                                              m_dirty, before, after));
}

void PixelEdit::cancel()
{
    if (!m_active) return;
    m_active = false;
    Layer& l = layer();
    l.image = m_original;
    l.offset = m_newRect.topLeft();
    l.setGeometry(m_oldRect);
    if (!m_dirty.isEmpty()) m_doc->notifyLayerPixels(m_doc->indexOfId(m_layerId), m_dirty);
}
