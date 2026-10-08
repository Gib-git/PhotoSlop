#include "core/Document.h"

#include "core/Commands.h"
#include "core/Compositor.h"
#include "core/Selection.h"

#include <QMetaObject>
#include <QSignalBlocker>
#include <QUndoStack>
#include <cstring>
#include <algorithm>
#include <iterator>

Document::Document(const QSize& size, QObject* parent)
    : QObject(parent)
    , m_size(size)
    , m_undo(new QUndoStack(this))
{
    m_undo->setUndoLimit(50); // Photoshop's default number of history states
    connect(m_undo, &QUndoStack::cleanChanged, this, &Document::modifiedChanged);
    reallocate();
}

Document::~Document() = default;

void Document::setTitle(const QString& title)
{
    if (title == m_title) return;
    m_title = title;
    emit titleChanged();
}

bool Document::isModified() const { return !m_undo->isClean(); }

void Document::setClean() { m_undo->setClean(); }

void Document::setActiveIndex(int index)
{
    if (m_layers.isEmpty()) return;
    index = std::clamp(index, 0, int(m_layers.size()) - 1);
    if (index == m_active) return;
    m_active = index;
    // A newly selected layer is targeted by its pixels, as in Photoshop's default.
    m_maskTarget = false;
    emit activeLayerChanged();
    emit editTargetChanged();
}

Layer* Document::activeLayer()
{
    if (m_layers.isEmpty()) return nullptr;
    return &m_layers[std::clamp(m_active, 0, int(m_layers.size()) - 1)];
}

const Layer& Document::layerAt(int index) const
{
    if (index == kQuickMaskIndex) return m_qmLayer;
    if (isMaskIndex(index)) return *m_layers[maskOwner(index)].mask;
    return m_layers[index];
}

Layer& Document::layerRef(int index)
{
    if (index == kQuickMaskIndex) return m_qmLayer;
    if (isMaskIndex(index)) return *m_layers[maskOwner(index)].mask;
    return m_layers[index];
}

int Document::editIndex() const
{
    if (m_quickMask) return kQuickMaskIndex;
    if (m_layers.isEmpty()) return m_active;
    const int a = std::clamp(m_active, 0, int(m_layers.size()) - 1);
    const Layer& l = m_layers[a];
    if (l.mask && (m_maskTarget || l.kind == LayerKind::Adjustment)) return maskIndex(a);
    return a;
}

Layer* Document::editLayer()
{
    if (m_layers.isEmpty() && !m_quickMask) return nullptr;
    return &layerRef(editIndex());
}

void Document::setMaskTargeted(bool on)
{
    if (on == m_maskTarget) return;
    m_maskTarget = on;
    emit editTargetChanged();
}

int Document::indexOfId(quint64 id) const
{
    if (m_quickMask && id == m_qmLayer.id) return kQuickMaskIndex;
    for (int i = 0; i < m_layers.size(); ++i) {
        if (m_layers[i].id == id) return i;
        if (m_layers[i].mask && m_layers[i].mask->id == id) return maskIndex(i);
    }
    return -1;
}

QString Document::nextName(const QString& prefix) const
{
    for (int n = 1;; ++n) {
        const QString name = QStringLiteral("%1 %2").arg(prefix).arg(n);
        bool taken = false;
        for (const Layer& l : m_layers)
            if (l.name == name) taken = true;
        if (!taken) return name;
    }
}

DocState Document::stateAt(int undoIndex)
{
    const int current = m_undo->index();
    undoIndex = std::clamp(undoIndex, 0, m_undo->count());
    if (undoIndex == current) return state();
    // Step the history there and back without telling anyone.
    const QSignalBlocker blockDoc(this);
    const QSignalBlocker blockStack(m_undo);
    m_undo->setIndex(undoIndex);
    DocState s = state();
    m_undo->setIndex(current);
    invalidate();
    return s;
}

QString Document::nextLayerName()
{
    // Photoshop numbers new layers sequentially per document.
    for (;;) {
        QString name = QStringLiteral("Layer %1").arg(++m_layerCounter);
        bool taken = false;
        for (const Layer& l : m_layers)
            if (l.name == name) taken = true;
        if (!taken) return name;
    }
}

// ---------------- Selection ----------------

QRect Document::selectionBounds() const
{
    selectionEdges();
    return m_selBounds;
}

const QVector<QLine>& Document::selectionEdges() const
{
    if (!m_selCacheValid) {
        m_selEdges = Sel::edges(m_selection);
        m_selBounds = Sel::bounds(m_selection);
        m_selCacheValid = true;
    }
    return m_selEdges;
}

void Document::setSelectionRaw(const QImage& mask)
{
    if (mask.cacheKey() == m_selection.cacheKey() && mask.isNull() == m_selection.isNull()) return;
    if (!m_selection.isNull()) m_lastSelection = m_selection;
    m_selection = mask;
    m_selCacheValid = false;
    emit selectionChanged();
    emit guidesChanged();
    emit quickMaskChanged();
}

void Document::changeSelection(const QImage& mask, const QString& undoText)
{
    if (mask.isNull() && m_selection.isNull()) return;
    DocState before = state();
    setSelectionRaw(mask);
    pushSnapshot(undoText, before);
}

// ---------------- Quick Mask ----------------

void Document::setQuickMaskRaw(bool on, const Layer& maskLayer)
{
    m_quickMask = on;
    m_qmLayer = on ? maskLayer : Layer();
    m_qmOverlay = QImage();
    if (on) updateQuickMaskOverlay(bounds());
    emit quickMaskChanged();
    emit imageChanged(bounds());
}

const QImage& Document::quickMaskOverlay()
{
    if (!m_dirty.isEmpty()) flush();
    return m_qmOverlay;
}

void Document::updateQuickMaskOverlay(const QRect& rect)
{
    if (!m_quickMask) return;
    if (m_qmOverlay.size() != m_size) {
        m_qmOverlay = QImage(m_size, QImage::Format_ARGB32_Premultiplied);
        m_qmOverlay.fill(Qt::transparent);
    }
    // Masked (dark) areas get Photoshop's default 50% red tint.
    const QRect r = rect & bounds();
    for (int y = r.top(); y <= r.bottom(); ++y) {
        auto* d = reinterpret_cast<QRgb*>(m_qmOverlay.scanLine(y));
        for (int x = r.left(); x <= r.right(); ++x) {
            const int a = (255 - qGray(m_qmLayer.pixelAt(QPoint(x, y)))) / 2;
            d[x] = qRgba(a, 0, 0, a);
        }
    }
}

// ---------------- Guides ----------------

void Document::setGuidesRaw(const QList<Guide>& guides)
{
    if (guides == m_guides) return;
    m_guides = guides;
    emit guidesChanged();
}

void Document::changeGuides(const QList<Guide>& guides, const QString& undoText)
{
    if (guides == m_guides) return;
    DocState before = state();
    setGuidesRaw(guides);
    pushSnapshot(undoText, before);
}

// ---------------- Compositing ----------------

void Document::reallocate()
{
    m_composite = QImage(m_size, QImage::Format_ARGB32_Premultiplied);
    m_composite.fill(Qt::transparent);
    m_pyramid.clear();
    QSize s = m_size;
    while (s.width() > 64 || s.height() > 64) {
        s = QSize((s.width() + 1) / 2, (s.height() + 1) / 2);
        QImage level(s, QImage::Format_ARGB32_Premultiplied);
        level.fill(Qt::transparent);
        m_pyramid.append(level);
    }
    m_dirty = bounds();
}

void Document::invalidate(const QRect& canvasRect)
{
    QRect r = canvasRect.isNull() ? bounds() : (canvasRect & bounds());
    if (r.isEmpty()) return;
    m_dirty |= r;
    if (!m_flushScheduled) {
        m_flushScheduled = true;
        QMetaObject::invokeMethod(this, &Document::flush, Qt::QueuedConnection);
    }
}

void Document::flush()
{
    m_flushScheduled = false;
    if (m_dirty.isEmpty()) return;
    QRect r = m_dirty;
    m_dirty = QRect();
    recomposite(r);
    updatePyramid(r);
    updateQuickMaskOverlay(r);
    emit imageChanged(r);
}

const QImage& Document::composite()
{
    if (!m_dirty.isEmpty()) flush();
    return m_composite;
}

const QImage& Document::pyramidLevel(int level)
{
    if (!m_dirty.isEmpty()) flush();
    if (level <= 0 || m_pyramid.isEmpty()) return m_composite;
    return m_pyramid[std::min(level, int(m_pyramid.size())) - 1];
}

QRgb Document::compositePixel(const QPoint& pt)
{
    if (!bounds().contains(pt)) return 0;
    return composite().pixel(pt);
}

void Document::recomposite(const QRect& r)
{
    Compositor::renderParallel(m_layers, m_composite, r);
}

void Document::updatePyramid(const QRect& r)
{
    const QImage* prev = &m_composite;
    QRect pr = r;
    for (QImage& level : m_pyramid) {
        QRect lr(QPoint(pr.left() / 2, pr.top() / 2), QPoint(pr.right() / 2, pr.bottom() / 2));
        lr &= level.rect();
        const int pw = prev->width(), ph = prev->height();
        for (int y = lr.top(); y <= lr.bottom(); ++y) {
            const int y0 = 2 * y, y1 = std::min(2 * y + 1, ph - 1);
            auto* row0 = reinterpret_cast<const uint32_t*>(prev->constScanLine(y0));
            auto* row1 = reinterpret_cast<const uint32_t*>(prev->constScanLine(y1));
            auto* dst = reinterpret_cast<uint32_t*>(level.scanLine(y));
            for (int x = lr.left(); x <= lr.right(); ++x) {
                const int x0 = 2 * x, x1 = std::min(2 * x + 1, pw - 1);
                uint32_t p[4] = {row0[x0], row0[x1], row1[x0], row1[x1]};
                uint32_t out = 0;
                for (int shift = 0; shift < 32; shift += 8) {
                    uint32_t sum = ((p[0] >> shift) & 0xff) + ((p[1] >> shift) & 0xff)
                        + ((p[2] >> shift) & 0xff) + ((p[3] >> shift) & 0xff);
                    out |= ((sum + 2) / 4) << shift;
                }
                dst[x] = out;
            }
        }
        prev = &level;
        pr = lr;
    }
}

// ---------------- Undo ----------------

DocState Document::state() const
{
    DocState s;
    s.size = m_size;
    s.dpi = m_dpi;
    s.layers = m_layers;
    s.active = m_active;
    s.selection = m_selection;
    s.guides = m_guides;
    s.quickMask = m_quickMask;
    s.quickMaskLayer = m_qmLayer;
    return s;
}

void Document::restoreState(const DocState& s)
{
    const bool sizeChange = s.size != m_size;
    bool layersDiffer = s.layers.size() != m_layers.size();
    QRect dirty;
    if (!layersDiffer) {
        for (int i = 0; i < m_layers.size(); ++i) {
            if (m_layers[i].id != s.layers[i].id) {
                layersDiffer = true;
                dirty = bounds();
                break;
            }
            if (!sameLayer(m_layers[i], s.layers[i])) {
                layersDiffer = true;
                dirty |= Compositor::extent(m_layers, i, bounds()) | Compositor::extent(s.layers, i, bounds());
            }
        }
    } else {
        dirty = bounds();
    }
    const bool activeChange = s.active != m_active;
    const bool selChange = s.selection.cacheKey() != m_selection.cacheKey()
        || s.selection.isNull() != m_selection.isNull();

    m_size = s.size;
    m_dpi = s.dpi;
    m_layers = s.layers;
    m_active = std::clamp(s.active, 0, std::max(0, int(m_layers.size()) - 1));
    if (m_maskTarget && (m_layers.isEmpty() || !m_layers[m_active].mask)) {
        m_maskTarget = false;
        emit editTargetChanged();
    }

    if (sizeChange) {
        reallocate();
        invalidate();
        emit sizeChanged();
    } else if (!dirty.isEmpty()) {
        invalidate(dirty);
    }
    if (selChange) {
        if (!m_selection.isNull()) m_lastSelection = m_selection;
        m_selection = s.selection;
        m_selCacheValid = false;
        emit selectionChanged();
    }
    if (s.guides != m_guides) {
        m_guides = s.guides;
        emit guidesChanged();
    }
    restoreQuickMask(s.quickMask, s.quickMaskLayer);
    if (layersDiffer || sizeChange) emit layersChanged();
    if (activeChange || layersDiffer) emit activeLayerChanged();
}

void Document::restoreQuickMask(bool on, const Layer& maskLayer)
{
    if (on == m_quickMask && (!on || sameLayer(maskLayer, m_qmLayer))) return;
    if (on && m_quickMask) {
        m_qmLayer = maskLayer;
        invalidate();
        return;
    }
    setQuickMaskRaw(on, maskLayer);
}

void Document::pushSnapshot(const QString& text, const DocState& before, int mergeId)
{
    m_undo->push(new SnapshotCommand(this, text, before, state(), mergeId));
    // Callers mutate the document directly before pushing, so announce the change here.
    QRect dirty;
    if (before.size != m_size) {
        reallocate();
        invalidate();
        emit sizeChanged();
        emit layersChanged();
        emit activeLayerChanged();
    } else {
        bool structure = before.layers.size() != m_layers.size();
        for (int i = 0; !structure && i < m_layers.size(); ++i) {
            if (before.layers[i].id != m_layers[i].id) structure = true;
            else if (!sameLayer(before.layers[i], m_layers[i]))
                dirty |= Compositor::extent(before.layers, i, bounds()) | Compositor::extent(m_layers, i, bounds());
        }
        if (structure) dirty = bounds();
        if (!dirty.isEmpty()) invalidate(dirty);
        emit layersChanged();
        if (before.active != m_active || structure) emit activeLayerChanged();
    }
    if (m_maskTarget && (m_layers.isEmpty() || !m_layers[std::clamp(m_active, 0, int(m_layers.size()) - 1)].mask)) {
        m_maskTarget = false;
        emit editTargetChanged();
    }
    if (before.selection.cacheKey() != m_selection.cacheKey()
        || before.selection.isNull() != m_selection.isNull()) {
        m_selCacheValid = false;
        emit selectionChanged();
    }
}

void Document::notifyLayersChanged()
{
    emit layersChanged();
}

void Document::notifyLayerPixels(int index, const QRect& canvasRect)
{
    invalidate(canvasRect);
    if (index >= 0) emit layerPixelsChanged(index);
}

void Document::initialize(const DocState& s)
{
    m_size = s.size;
    m_dpi = s.dpi;
    m_layers = s.layers;
    m_active = std::clamp(s.active, 0, std::max(0, int(m_layers.size()) - 1));
    m_selection = s.selection;
    m_selCacheValid = false;
    m_guides = s.guides;
    m_quickMask = s.quickMask;
    m_qmLayer = s.quickMaskLayer;
    m_qmOverlay = QImage();
    m_maskTarget = false;
    m_historySource = s;
    reallocate();
    invalidate();
    m_undo->clear();
    emit sizeChanged();
    emit layersChanged();
    emit activeLayerChanged();
    emit selectionChanged();
    emit guidesChanged();
    emit quickMaskChanged();
}
