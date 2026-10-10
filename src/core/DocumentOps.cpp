#include "core/DocumentOps.h"

#include "core/Adjustments.h"
#include "core/Commands.h"
#include "core/Compositor.h"
#include "core/Document.h"
#include "core/ImageOps.h"
#include "core/LayerStyle.h"
#include "core/LayerTree.h"
#include "core/VectorLayers.h"

#include <QHash>
#include <QPainter>
#include <QTransform>
#include <cmath>
#include <algorithm>
#include <iterator>

namespace Ops {

namespace {

void setError(QString* error, const QString& text)
{
    if (error) *error = text;
}

bool isTransparent(const QImage& img)
{
    for (int y = 0; y < img.height(); ++y) {
        auto* row = reinterpret_cast<const QRgb*>(img.constScanLine(y));
        for (int x = 0; x < img.width(); ++x)
            if (qAlpha(row[x])) return false;
    }
    return true;
}

QRect mapRect(const QRect& r, Rotation how, const QSize& s)
{
    const int W = s.width(), H = s.height();
    switch (how) {
    case Rotation::Rotate90CW: return QRect(H - r.y() - r.height(), r.x(), r.height(), r.width());
    case Rotation::Rotate90CCW: return QRect(r.y(), W - r.x() - r.width(), r.height(), r.width());
    case Rotation::Rotate180: return QRect(W - r.x() - r.width(), H - r.y() - r.height(), r.width(), r.height());
    case Rotation::FlipHorizontal: return QRect(W - r.x() - r.width(), r.y(), r.width(), r.height());
    case Rotation::FlipVertical: return QRect(r.x(), H - r.y() - r.height(), r.width(), r.height());
    }
    return r;
}

// The canvas mapping of a rotation or flip, for vector data.
QTransform rotationTransform(Rotation how, const QSize& s)
{
    const double W = s.width(), H = s.height();
    switch (how) {
    case Rotation::Rotate90CW: return QTransform(0, 1, -1, 0, H, 0);
    case Rotation::Rotate90CCW: return QTransform(0, -1, 1, 0, 0, W);
    case Rotation::Rotate180: return QTransform(-1, 0, 0, -1, W, H);
    case Rotation::FlipHorizontal: return QTransform(-1, 0, 0, 1, W, 0);
    case Rotation::FlipVertical: return QTransform(1, 0, 0, -1, 0, H);
    }
    return QTransform();
}

QImage flip(const QImage& img, bool horizontal, bool vertical)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
    Qt::Orientations o;
    if (horizontal) o |= Qt::Horizontal;
    if (vertical) o |= Qt::Vertical;
    return img.flipped(o);
#else
    return img.mirrored(horizontal, vertical);
#endif
}

QImage transformImage(const QImage& img, Rotation how)
{
    switch (how) {
    case Rotation::Rotate90CW: return img.transformed(QTransform().rotate(90));
    case Rotation::Rotate90CCW: return img.transformed(QTransform().rotate(-90));
    case Rotation::Rotate180: return flip(img, true, true);
    case Rotation::FlipHorizontal: return flip(img, true, false);
    case Rotation::FlipVertical: return flip(img, false, true);
    }
    return img;
}

// Where a new layer goes: above the active layer in its group, or at the top of the active
// group. Sets the insertion index and the parent id.
void insertionPoint(Document* doc, int& at, quint64& parent)
{
    if (doc->layerCount() == 0) {
        at = 0;
        parent = 0;
        return;
    }
    const Layer* a = doc->activeLayer();
    if (a->isGroup()) {
        at = doc->activeIndex();
        parent = a->id;
    } else {
        at = doc->activeIndex() + 1;
        parent = a->parent;
    }
}

int insertLayer(Document* doc, Layer l)
{
    int at;
    quint64 parent;
    insertionPoint(doc, at, parent);
    l.parent = parent;
    doc->layersRef().insert(at, l);
    doc->setActiveIndex(at);
    return at;
}

// Bakes a layer's mask into its pixels and removes it.
void applyMaskToPixels(Layer& l)
{
    if (!l.mask) return;
    if (l.maskEnabled && !l.image.isNull()) {
        std::vector<uint8_t> m(size_t(l.image.width()));
        for (int y = 0; y < l.image.height(); ++y) {
            l.maskRow(y + l.offset.y(), l.offset.x(), l.image.width(), m.data());
            auto* row = reinterpret_cast<uint32_t*>(l.image.scanLine(y));
            for (int x = 0; x < l.image.width(); ++x)
                if (m[size_t(x)] != 255) row[x] = Blend::byteMul(row[x], m[size_t(x)]);
        }
    }
    l.mask.reset();
}

// A copy of `l` that blends plainly (for merging): no mask, style, clipping or parent.
Layer plainCopy(const Layer& l)
{
    Layer p = l;
    applyMaskToPixels(p);
    p.style.reset();
    p.mode = BlendMode::Normal;
    p.opacity = p.fill = 1.0f;
    p.parent = 0;
    p.clipped = false;
    p.visible = true;
    return p;
}

// Applies `fn` to a layer's pixels and to its mask.
template <typename F>
void forEachRaster(Layer& l, F&& fn)
{
    fn(l);
    if (l.mask) fn(*l.mask);
}

// Mask pixels for a selection: opaque grey where `reveal` matches the selection.
QImage maskFromSelection(const QImage& sel, bool reveal)
{
    QImage m(sel.size(), QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < sel.height(); ++y) {
        const uchar* s = sel.constScanLine(y);
        auto* d = reinterpret_cast<QRgb*>(m.scanLine(y));
        for (int x = 0; x < sel.width(); ++x) {
            const int v = reveal ? s[x] : 255 - s[x];
            d[x] = qRgb(v, v, v);
        }
    }
    return m;
}

QString kindPhrase(const Layer& l)
{
    switch (l.kind) {
    case LayerKind::Group: return QStringLiteral("the target layer is a group");
    case LayerKind::Adjustment: return QStringLiteral("the target layer is an adjustment layer");
    case LayerKind::Text: return QStringLiteral("the type layer must be rasterized first");
    case LayerKind::Shape: return QStringLiteral("the shape layer must be rasterized first");
    case LayerKind::Pixel: break;
    }
    return QString();
}

} // namespace

QString editTargetError(Document* doc, const QString& prefix)
{
    Layer* target = doc->editLayer();
    if (!target) return QStringLiteral("%1 because there is no layer to edit.").arg(prefix);
    const int idx = doc->editIndex();
    if (idx == Document::kQuickMaskIndex) return QString();
    const int owner = Document::isMaskIndex(idx) ? Document::maskOwner(idx) : idx;
    if (!Tree::effectivelyVisible(doc->layers(), owner))
        return QStringLiteral("%1 because the target layer is hidden.").arg(prefix);
    if (Document::isMaskIndex(idx)) return QString();
    if (!target->isPixel()) return QStringLiteral("%1 because %2.").arg(prefix, kindPhrase(*target));
    if (target->pixelsLocked()) return QStringLiteral("%1 because the layer is locked.").arg(prefix);
    return QString();
}

// ---------------- Layers ----------------

int insertLayerRaw(Document* doc, Layer layer) { return insertLayer(doc, std::move(layer)); }

void newLayer(Document* doc, const QString& name, BlendMode mode, float opacity)
{
    doc->modify(QStringLiteral("New Layer"), [&] {
        Layer l = Layer::create(name.isEmpty() ? doc->nextLayerName() : name);
        l.mode = mode;
        l.opacity = opacity;
        insertLayer(doc, l);
    });
}

bool duplicateLayer(Document* doc, const QString& name)
{
    Layer* src = doc->activeLayer();
    if (!src) return false;
    doc->modify(src->isGroup() ? QStringLiteral("Duplicate Group") : QStringLiteral("Duplicate Layer"), [&] {
        QList<Layer>& layers = doc->layersRef();
        const int top = doc->activeIndex();
        const int start = Tree::subtreeStart(layers, top);
        QList<Layer> block = layers.mid(start, top - start + 1);
        // Fresh ids, with the copies' parent links pointing at the copied groups.
        const quint64 parent = block.last().parent;
        Tree::renewIds(block);
        Layer& root = block.last();
        root.parent = parent;
        root.name = name.isEmpty() ? src->name + QStringLiteral(" copy") : name;
        root.isBackground = false;
        for (int i = 0; i < block.size(); ++i) layers.insert(top + 1 + i, block[i]);
        doc->setActiveIndex(top + int(block.size()));
    });
    return true;
}

bool deleteLayer(Document* doc, QString* error)
{
    const int idx = doc->activeIndex();
    const int start = Tree::subtreeStart(doc->layers(), idx);
    if (doc->layerCount() - (idx - start + 1) < 1) {
        setError(error, QStringLiteral("Could not delete the layer because a document must have at least one layer."));
        return false;
    }
    const bool group = doc->layerAt(idx).isGroup();
    doc->modify(group ? QStringLiteral("Delete Group") : QStringLiteral("Delete Layer"), [&] {
        doc->layersRef().remove(start, idx - start + 1);
        // The layer below takes the selection.
        doc->setActiveIndex(std::clamp(start - 1, 0, doc->layerCount() - 1));
    });
    return true;
}

namespace {

// Moves layer `index` (with its contents) to position `dest` of the list without it, inside
// `parent`. Rejects moves that would put a layer inside itself or below the Background.
bool placeSubtree(Document* doc, int index, int dest, quint64 parent, const QString& text)
{
    const QList<Layer>& layers = doc->layers();
    if (index < 0 || index >= layers.size()) return false;
    const Layer& l = layers[index];
    if (l.isBackground) return false;
    const int start = Tree::subtreeStart(layers, index);
    // A group cannot go inside itself.
    for (int i = start; i <= index; ++i)
        if (layers[i].id == parent) return false;
    // Nothing goes below the Background.
    if (doc->hasBackground() && parent == 0 && dest < 1) dest = 1;
    // No change?
    if (dest == start && l.parent == parent) return false;
    doc->modify(text, [&] {
        const int top = Tree::moveSubtree(doc->layersRef(), index, dest, parent);
        doc->setActiveIndex(top);
    });
    return true;
}

// Position (in the list without the moving subtree) that puts it directly above `target`.
int destAbove(const QList<Layer>& layers, int moving, int target)
{
    const int start = Tree::subtreeStart(layers, moving);
    const int size = moving - start + 1;
    const int pos = target + 1;
    return pos > start ? pos - size : pos;
}

// Position that puts it directly below `target` (and target's contents).
int destBelow(const QList<Layer>& layers, int moving, int target)
{
    const int start = Tree::subtreeStart(layers, moving);
    const int size = moving - start + 1;
    const int pos = Tree::subtreeStart(layers, target);
    return pos > start ? pos - size : pos;
}

} // namespace

bool moveLayerAbove(Document* doc, int index, int target)
{
    const QList<Layer>& layers = doc->layers();
    if (target < 0 || target >= layers.size() || target == index || Tree::isInside(layers, target, index)) return false;
    return placeSubtree(doc, index, destAbove(layers, index, target), layers[target].parent, QStringLiteral("Layer Order"));
}

bool moveLayerBelow(Document* doc, int index, int target)
{
    const QList<Layer>& layers = doc->layers();
    if (target < 0 || target >= layers.size() || target == index || Tree::isInside(layers, target, index)) return false;
    if (layers[target].isBackground) return false;
    return placeSubtree(doc, index, destBelow(layers, index, target), layers[target].parent, QStringLiteral("Layer Order"));
}

bool moveLayerInto(Document* doc, int index, int group)
{
    const QList<Layer>& layers = doc->layers();
    if (group < 0 || group >= layers.size() || !layers[group].isGroup() || group == index
        || Tree::isInside(layers, group, index))
        return false;
    // Top of the group: just below the group layer itself.
    const int start = Tree::subtreeStart(layers, index);
    const int size = index - start + 1;
    const int dest = group > start ? group - size : group;
    return placeSubtree(doc, index, dest, layers[group].id, QStringLiteral("Layer Order"));
}

bool moveLayer(Document* doc, int from, int to)
{
    const int n = doc->layerCount();
    if (from < 0 || from >= n || to < 0 || to >= n || from == to) return false;
    return to > from ? moveLayerAbove(doc, from, to) : moveLayerBelow(doc, from, to);
}

bool arrange(Document* doc, Arrange how)
{
    const QList<Layer>& layers = doc->layers();
    const int i = doc->activeIndex();
    if (i < 0 || i >= layers.size() || layers[i].isBackground) return false;
    const int parent = Tree::parentIndex(layers, i);
    const QList<int> sibs = Tree::children(layers, parent);
    const int pos = int(sibs.indexOf(i));
    switch (how) {
    case Arrange::BringToFront:
        return pos < sibs.size() - 1 && moveLayerAbove(doc, i, sibs.last());
    case Arrange::SendToBack: {
        int bottom = sibs.first();
        if (layers[bottom].isBackground) {
            if (sibs.size() < 2 || sibs[1] == i) return false;
            bottom = sibs[1];
        }
        return pos > 0 && bottom != i && moveLayerBelow(doc, i, bottom);
    }
    case Arrange::BringForward:
        if (pos < sibs.size() - 1) {
            const int above = sibs[pos + 1];
            // Photoshop steps into a group above as its bottom layer.
            if (layers[above].isGroup() && layers[above].expanded) {
                const QList<int> inner = Tree::children(layers, above);
                if (inner.isEmpty()) return moveLayerInto(doc, i, above);
                return moveLayerBelow(doc, i, inner.first());
            }
            return moveLayerAbove(doc, i, above);
        }
        // The top layer of a group steps out above it.
        return parent >= 0 && moveLayerAbove(doc, i, parent);
    case Arrange::SendBackward:
        if (pos > 0) {
            const int below = sibs[pos - 1];
            if (layers[below].isBackground) return false;
            if (layers[below].isGroup() && layers[below].expanded) return moveLayerInto(doc, i, below);
            return moveLayerBelow(doc, i, below);
        }
        return parent >= 0 && moveLayerBelow(doc, i, parent);
    }
    return false;
}

void setVisible(Document* doc, int index, bool visible)
{
    if (doc->layerAt(index).visible == visible) return;
    doc->modify(visible ? QStringLiteral("Show Layer") : QStringLiteral("Hide Layer"),
                [&] { doc->layerRef(index).visible = visible; });
}

void soloVisibility(Document* doc, int index)
{
    // Alt-click: show only this layer (with its groups and contents), or show everything again.
    const QList<Layer>& layers = doc->layers();
    auto related = [&](int i) {
        return i == index || Tree::isInside(layers, i, index) || Tree::isInside(layers, index, i);
    };
    bool othersHidden = true;
    for (int i = 0; i < doc->layerCount(); ++i)
        if (!related(i) && layers[i].visible) othersHidden = false;
    doc->modify(QStringLiteral("Show/Hide Layers"), [&] {
        for (int i = 0; i < doc->layerCount(); ++i)
            doc->layerRef(i).visible = othersHidden ? true : related(i);
    });
}

void setOpacity(Document* doc, int index, float opacity)
{
    if (qFuzzyCompare(doc->layerAt(index).opacity, opacity)) return;
    doc->modify(QStringLiteral("Opacity Change"), [&] { doc->layerRef(index).opacity = opacity; }, 1001);
}

void setFill(Document* doc, int index, float fill)
{
    if (qFuzzyCompare(doc->layerAt(index).fill, fill)) return;
    doc->modify(QStringLiteral("Fill Opacity Change"), [&] { doc->layerRef(index).fill = fill; }, 1002);
}

void setBlendMode(Document* doc, int index, BlendMode mode)
{
    const Layer& l = doc->layerAt(index);
    if (l.mode == mode || (mode == BlendMode::PassThrough && !l.isGroup())) return;
    doc->modify(QStringLiteral("Blending Change"), [&] { doc->layerRef(index).mode = mode; });
}

void rename(Document* doc, int index, const QString& name)
{
    if (name.isEmpty() || doc->layerAt(index).name == name) return;
    doc->modify(QStringLiteral("Rename Layer"), [&] { doc->layerRef(index).name = name; });
}

void setLocks(Document* doc, int index, bool transparency, bool pixels, bool position, bool all)
{
    doc->modify(QStringLiteral("Layer Properties"), [&] {
        Layer& l = doc->layerRef(index);
        l.lockTransparency = transparency;
        l.lockPixels = pixels;
        l.lockPosition = position;
        l.lockAll = all;
    });
}

bool layerFromBackground(Document* doc, int index, const QString& name)
{
    if (!doc->layerAt(index).isBackground) return false;
    doc->modify(QStringLiteral("Layer From Background"), [&] {
        Layer& l = doc->layerRef(index);
        l.isBackground = false;
        l.name = name;
    });
    return true;
}

void setExpanded(Document* doc, int index, bool expanded)
{
    Layer& l = doc->layerRef(index);
    if (!l.isGroup() || l.expanded == expanded) return;
    l.expanded = expanded;
    doc->notifyLayersChanged();
}

namespace {

// Replaces the subtree at `index` with a pixel layer of the same name rendered from it.
void mergeGroupAt(Document* doc, int index)
{
    QList<Layer>& layers = doc->layersRef();
    const int start = Tree::subtreeStart(layers, index);
    const QRect area = Compositor::extent(layers, index, doc->bounds());
    // Render the group as if it were visible and blended Normal at full opacity: the merged
    // layer keeps those settings itself.
    QList<Layer> tmp = layers;
    Layer& g = tmp[index];
    g.visible = true;
    const BlendMode mode = g.mode == BlendMode::PassThrough ? BlendMode::Normal : g.mode;
    const float opacity = g.opacity;
    g.mode = BlendMode::Normal;
    g.opacity = 1.0f;
    QImage img = area.isEmpty() ? QImage() : Compositor::renderSingle(tmp, index, area);
    Layer merged = Layer::create(layers[index].name);
    merged.image = img;
    merged.offset = area.topLeft();
    merged.trimToContent();
    merged.mode = mode;
    merged.opacity = opacity;
    merged.visible = layers[index].visible;
    merged.parent = layers[index].parent;
    merged.clipped = layers[index].clipped;
    layers.remove(start, index - start + 1);
    layers.insert(start, merged);
    doc->setActiveIndex(start);
}

} // namespace

bool mergeDown(Document* doc, QString* error)
{
    const int idx = doc->activeIndex();
    const QList<Layer>& layers = doc->layers();
    if (idx < 0 || idx >= layers.size()) return false;
    if (layers[idx].isGroup()) {
        doc->modify(QStringLiteral("Merge Group"), [&] { mergeGroupAt(doc, idx); });
        return true;
    }
    const QList<int> sibs = Tree::children(layers, Tree::parentIndex(layers, idx));
    const int pos = int(sibs.indexOf(idx));
    if (pos <= 0) {
        setError(error, QStringLiteral("Could not complete the Merge Down command because there is no layer below."));
        return false;
    }
    const int lowerIdx = sibs[pos - 1];
    const Layer& lowerL = layers[lowerIdx];
    if (lowerL.isGroup() || lowerL.kind == LayerKind::Adjustment) {
        setError(error, QStringLiteral("Could not complete the Merge Down command because the layer below is %1.")
                            .arg(lowerL.isGroup() ? QStringLiteral("a group") : QStringLiteral("an adjustment layer")));
        return false;
    }
    if (lowerL.pixelsLocked()) {
        setError(error, QStringLiteral("Could not complete the Merge Down command because the target layer is locked."));
        return false;
    }
    doc->modify(QStringLiteral("Merge Down"), [&] {
        QList<Layer>& ls = doc->layersRef();
        Layer lower = ls[lowerIdx];
        const Layer& upper = ls[idx];
        // The lower layer's pixels, then the upper layer blended on with everything it has
        // (an adjustment layer adjusts them, a clipped layer stays inside them).
        Layer base = plainCopy(lower);
        Vector::convertToPixels(base);
        Layer top = upper;
        top.parent = 0;
        QRect area = lower.isBackground ? doc->bounds() : base.rect();
        if (upper.visible) {
            QList<Layer> pair{base, top};
            if (!lower.isBackground) {
                area |= Compositor::extent(pair, 1, area);
                if (top.clipped) area = base.rect();
            }
            if (!area.isEmpty()) {
                base.image = Compositor::flatten(pair, area);
                base.offset = area.topLeft();
            }
        }
        Layer& result = ls[lowerIdx];
        result.image = base.image;
        result.offset = base.offset;
        result.mask.reset();
        Vector::convertToPixels(result);
        if (!result.isBackground) result.trimToContent();
        ls.removeAt(idx);
        doc->setActiveIndex(lowerIdx);
    });
    return true;
}

bool mergeVisible(Document* doc, QString* error)
{
    const QList<Layer>& layers = doc->layers();
    const QList<int> roots = Tree::children(layers, -1);
    QList<int> visibleRoots;
    for (int r : roots)
        if (layers[r].visible) visibleRoots.append(r);
    int pixelSources = 0;
    for (int i = 0; i < layers.size(); ++i)
        if (Tree::effectivelyVisible(layers, i) && !layers[i].isGroup()) ++pixelSources;
    if (pixelSources < 2) {
        setError(error, QStringLiteral("Could not complete the Merge Visible command because there are not enough visible layers."));
        return false;
    }
    doc->modify(QStringLiteral("Merge Visible"), [&] {
        QList<Layer>& ls = doc->layersRef();
        const int target = visibleRoots.first();
        const bool bg = ls[target].isBackground;
        QRect area = bg ? doc->bounds() : QRect();
        if (!bg)
            for (int r : visibleRoots) area |= Compositor::extent(ls, r, doc->bounds());
        Layer result = Layer::create(ls[target].name);
        result.isBackground = bg;
        if (!area.isEmpty()) {
            result.image = Compositor::flatten(ls, area);
            result.offset = area.topLeft();
        }
        if (bg) {
            // The Background stays opaque.
            QImage flat(doc->size(), QImage::Format_ARGB32_Premultiplied);
            flat.fill(Qt::white);
            QPainter p(&flat);
            p.drawImage(0, 0, result.image);
            p.end();
            result.image = flat;
            result.offset = QPoint();
        }
        // Keep hidden top-level layers (and their contents) in place.
        QList<Layer> next;
        int newActive = 0;
        for (int i = 0; i < ls.size(); ++i) {
            int root = i;
            for (int p = Tree::parentIndex(ls, root); p >= 0; p = Tree::parentIndex(ls, p)) root = p;
            if (!visibleRoots.contains(root)) {
                next.append(ls[i]);
            } else if (i == target) {
                newActive = int(next.size());
                next.append(result);
            }
        }
        ls = next;
        doc->setActiveIndex(newActive);
    });
    return true;
}

void flatten(Document* doc)
{
    doc->modify(QStringLiteral("Flatten Image"), [&] {
        QImage flat(doc->size(), QImage::Format_ARGB32_Premultiplied);
        flat.fill(Qt::white);
        QImage comp = Compositor::flatten(doc->layers(), doc->bounds());
        QPainter p(&flat);
        p.drawImage(0, 0, comp);
        p.end();
        Layer bg = Layer::create(QStringLiteral("Background"));
        bg.isBackground = true;
        bg.image = flat;
        doc->layersRef() = {bg};
        doc->setActiveIndex(0);
    });
}

bool layerViaCopy(Document* doc, bool cut, QString* error)
{
    Layer* src = doc->activeLayer();
    if (!src) return false;
    const QString command = cut ? QStringLiteral("New Layer via Cut") : QStringLiteral("New Layer via Copy");
    if (!src->isPixel() && !(src->isVector() && !cut)) {
        setError(error, QStringLiteral("Could not complete the %1 command because %2.").arg(command, kindPhrase(*src)));
        return false;
    }
    if (!doc->hasSelection()) {
        if (cut) {
            setError(error, QStringLiteral("Could not complete the New Layer via Cut command because the selected area is empty."));
            return false;
        }
        return duplicateLayer(doc, doc->nextLayerName());
    }
    QRect r = doc->selectionBounds();
    QImage pixels = ImageOps::maskedPixels(*src, r, doc->selection());
    if (isTransparent(pixels)) {
        setError(error, QStringLiteral("Could not complete the %1 command because the selected area is empty.").arg(command));
        return false;
    }
    doc->modify(cut ? QStringLiteral("Layer Via Cut") : QStringLiteral("Layer Via Copy"), [&] {
        int idx = doc->activeIndex();
        if (cut) {
            Layer& s = doc->layerRef(idx);
            if (s.isBackground)
                ImageOps::fillColor(s, r, ImageOps::premultiplied(Qt::white), BlendMode::Normal, 1.f,
                                    doc->selection(), false);
            else
                ImageOps::clearPixels(s, r, doc->selection());
        }
        Layer l = Layer::create(doc->nextLayerName());
        l.image = pixels;
        l.offset = r.topLeft();
        l.parent = doc->layerAt(idx).parent;
        doc->layersRef().insert(idx + 1, l);
        doc->setActiveIndex(idx + 1);
    });
    return true;
}

// ---------------- Groups ----------------

void newGroup(Document* doc, const QString& name)
{
    doc->modify(QStringLiteral("New Group"), [&] {
        Layer g = Layer::create(name.isEmpty() ? doc->nextName(QStringLiteral("Group")) : name);
        g.kind = LayerKind::Group;
        g.mode = BlendMode::PassThrough;
        insertLayer(doc, g);
    });
}

bool groupLayer(Document* doc, QString* error)
{
    const int idx = doc->activeIndex();
    if (idx < 0 || idx >= doc->layerCount()) return false;
    if (doc->layerAt(idx).isBackground) {
        setError(error, QStringLiteral("Could not complete the Group Layers command because the Background layer is locked."));
        return false;
    }
    doc->modify(QStringLiteral("Group Layers"), [&] {
        QList<Layer>& ls = doc->layersRef();
        Layer g = Layer::create(doc->nextName(QStringLiteral("Group")));
        g.kind = LayerKind::Group;
        g.mode = BlendMode::PassThrough;
        g.parent = ls[idx].parent;
        ls[idx].parent = g.id;
        ls[idx].clipped = false;
        ls.insert(idx + 1, g);
        doc->setActiveIndex(idx + 1);
    });
    return true;
}

bool ungroup(Document* doc, QString* error)
{
    const int idx = doc->activeIndex();
    if (idx < 0 || idx >= doc->layerCount() || !doc->layerAt(idx).isGroup()) {
        setError(error, QStringLiteral("Could not complete the Ungroup Layers command because the selected layer is not a group."));
        return false;
    }
    doc->modify(QStringLiteral("Ungroup Layers"), [&] {
        QList<Layer>& ls = doc->layersRef();
        const quint64 gid = ls[idx].id, parent = ls[idx].parent;
        for (Layer& l : ls)
            if (l.parent == gid) l.parent = parent;
        ls.removeAt(idx);
        doc->setActiveIndex(std::max(0, idx - 1));
    });
    return true;
}

// ---------------- Clipping masks ----------------

bool toggleClippingMask(Document* doc, QString* error)
{
    const int idx = doc->activeIndex();
    const QList<Layer>& ls = doc->layers();
    if (idx < 0 || idx >= ls.size()) return false;
    const Layer& l = ls[idx];
    if (l.clipped) {
        doc->modify(QStringLiteral("Release Clipping Mask"), [&] { doc->layerRef(idx).clipped = false; });
        return true;
    }
    const QList<int> sibs = Tree::children(ls, Tree::parentIndex(ls, idx));
    if (l.isBackground || sibs.indexOf(idx) <= 0) {
        setError(error, QStringLiteral("Could not complete the Create Clipping Mask command because there is no layer below to clip to."));
        return false;
    }
    doc->modify(QStringLiteral("Create Clipping Mask"), [&] { doc->layerRef(idx).clipped = true; });
    return true;
}

// ---------------- Layer masks ----------------

bool addMask(Document* doc, MaskFill fill, QString* error)
{
    Layer* l = doc->activeLayer();
    if (!l) return false;
    if (l->isBackground) {
        setError(error, QStringLiteral("Could not complete the Add Layer Mask command because the Background layer is locked."));
        return false;
    }
    if (l->mask) {
        setError(error, QStringLiteral("Could not complete the Add Layer Mask command because the layer already has a mask."));
        return false;
    }
    const bool fromSel = fill == MaskFill::RevealSelection || fill == MaskFill::HideSelection;
    if (fromSel && !doc->hasSelection()) fill = fill == MaskFill::RevealSelection ? MaskFill::RevealAll : MaskFill::HideAll;
    doc->modify(QStringLiteral("Add Layer Mask"), [&] {
        Layer& t = *doc->activeLayer();
        Layer m = Layer::create(QStringLiteral("Layer Mask"));
        switch (fill) {
        case MaskFill::RevealAll: t.maskDefault = 255; break;
        case MaskFill::HideAll: t.maskDefault = 0; break;
        case MaskFill::RevealSelection:
        case MaskFill::HideSelection:
            t.maskDefault = fill == MaskFill::RevealSelection ? 0 : 255;
            m.image = maskFromSelection(doc->selection(), fill == MaskFill::RevealSelection);
            doc->setSelectionRaw(QImage());
            break;
        }
        t.mask.set(m);
        t.maskEnabled = true;
        t.maskLinked = true;
    });
    doc->setMaskTargeted(true);
    return true;
}

bool deleteMask(Document* doc, bool apply, QString* error)
{
    Layer* l = doc->activeLayer();
    if (!l || !l->mask) return false;
    if (apply && !l->isPixel()) {
        setError(error, QStringLiteral("Could not apply the layer mask because %1.").arg(kindPhrase(*l)));
        return false;
    }
    doc->setMaskTargeted(false);
    doc->modify(apply ? QStringLiteral("Apply Layer Mask") : QStringLiteral("Delete Layer Mask"), [&] {
        Layer& t = *doc->activeLayer();
        if (apply) applyMaskToPixels(t);
        else t.mask.reset();
    });
    return true;
}

void setMaskEnabled(Document* doc, int index, bool enabled)
{
    const Layer& l = doc->layerAt(index);
    if (!l.mask || l.maskEnabled == enabled) return;
    doc->modify(enabled ? QStringLiteral("Enable Layer Mask") : QStringLiteral("Disable Layer Mask"),
                [&] { doc->layerRef(index).maskEnabled = enabled; });
}

void setMaskLinked(Document* doc, int index, bool linked)
{
    const Layer& l = doc->layerAt(index);
    if (!l.mask || l.maskLinked == linked) return;
    doc->modify(linked ? QStringLiteral("Link Layer Mask") : QStringLiteral("Unlink Layer Mask"),
                [&] { doc->layerRef(index).maskLinked = linked; });
}

void loadSelectionFromMask(Document* doc, int index, Sel::Op op)
{
    const Layer& l = doc->layerAt(index);
    if (!l.mask) return;
    QImage mask = Sel::empty(doc->size());
    std::vector<uint8_t> row(size_t(doc->width()));
    Layer probe = l;
    probe.maskEnabled = true;
    for (int y = 0; y < doc->height(); ++y) {
        probe.maskRow(y, 0, doc->width(), row.data());
        memcpy(mask.scanLine(y), row.data(), size_t(doc->width()));
    }
    const QImage result = Sel::combine(doc->selection(), mask, op);
    doc->changeSelection(Sel::isEmpty(result) ? QImage() : result, QStringLiteral("Load Selection"));
}

// ---------------- Adjustment layers ----------------

void newAdjustmentLayer(Document* doc, std::shared_ptr<const Adjust::LayerSettings> settings, const QString& name)
{
    if (!settings) return;
    doc->modify(QStringLiteral("New %1 Layer").arg(settings->name()), [&] {
        Layer l = Layer::create(name.isEmpty() ? doc->nextName(settings->name()) : name);
        l.kind = LayerKind::Adjustment;
        l.adjustment = settings;
        Layer m = Layer::create(QStringLiteral("Layer Mask"));
        if (doc->hasSelection()) {
            // Photoshop masks a new adjustment layer to the selection.
            m.image = maskFromSelection(doc->selection(), true);
            l.maskDefault = 0;
            doc->setSelectionRaw(QImage());
        }
        l.mask.set(m);
        insertLayer(doc, l);
    });
}

void setAdjustment(Document* doc, int index, std::shared_ptr<const Adjust::LayerSettings> settings)
{
    if (!settings || doc->layerAt(index).kind != LayerKind::Adjustment) return;
    doc->modify(QStringLiteral("Modify %1 Layer").arg(settings->name()),
                [&] { doc->layerRef(index).adjustment = settings; });
}

// ---------------- Layer styles ----------------

void setStyle(Document* doc, int index, std::shared_ptr<const LayerStyle> style, const QString& undoText)
{
    if (style && !style->anyEnabled()) style.reset();
    doc->modify(undoText, [&] { doc->layerRef(index).style = style; });
}

// ---------------- Text and shape layers ----------------

int newTextLayer(Document* doc, const TextData& text)
{
    int at = 0;
    doc->modify(QStringLiteral("Type Tool"), [&] {
        Layer l = Layer::create(text.text.isEmpty() ? QStringLiteral("Layer") : text.text.section(QLatin1Char('\n'), 0, 0).left(40));
        l.kind = LayerKind::Text;
        l.text = std::make_shared<const TextData>(text);
        Vector::rasterize(l);
        at = insertLayer(doc, l);
    });
    return at;
}

void setText(Document* doc, int index, const TextData& text, const QString& undoText, int mergeId)
{
    if (doc->layerAt(index).kind != LayerKind::Text) return;
    doc->modify(undoText, [&] {
        Layer& l = doc->layerRef(index);
        l.text = std::make_shared<const TextData>(text);
        if (!text.text.isEmpty()) l.name = text.text.section(QLatin1Char('\n'), 0, 0).left(40);
        Vector::rasterize(l);
    }, mergeId);
}

int newShapeLayer(Document* doc, const ShapeData& shape, const QString& baseName)
{
    int at = 0;
    doc->modify(QStringLiteral("New %1").arg(baseName), [&] {
        Layer l = Layer::create(doc->nextName(baseName));
        l.kind = LayerKind::Shape;
        l.shape = std::make_shared<const ShapeData>(shape);
        Vector::rasterize(l);
        at = insertLayer(doc, l);
    });
    return at;
}

void setShape(Document* doc, int index, const ShapeData& shape, const QString& undoText, int mergeId)
{
    if (doc->layerAt(index).kind != LayerKind::Shape) return;
    doc->modify(undoText, [&] {
        Layer& l = doc->layerRef(index);
        l.shape = std::make_shared<const ShapeData>(shape);
        Vector::rasterize(l);
    }, mergeId);
}

bool rasterizeLayer(Document* doc, int index)
{
    const Layer& l = doc->layerAt(index);
    if (!l.isVector()) return false;
    doc->modify(l.kind == LayerKind::Text ? QStringLiteral("Rasterize Type") : QStringLiteral("Rasterize Shape"),
                [&] { Vector::convertToPixels(doc->layerRef(index)); });
    return true;
}

bool rasterizeStyle(Document* doc, int index)
{
    const Layer& l = doc->layerAt(index);
    if (!l.hasStyle() || l.isGroup() || l.kind == LayerKind::Adjustment) return false;
    doc->modify(QStringLiteral("Rasterize Layer Style"), [&] {
        QList<Layer> one{doc->layerAt(index)};
        Layer& c = one.first();
        c.parent = 0;
        c.clipped = false;
        c.visible = true;
        // Effects bake at the layer's own opacity settings; the result keeps them.
        const float opacity = c.opacity;
        const BlendMode mode = c.mode;
        c.opacity = 1.0f;
        c.mode = BlendMode::Normal;
        const QRect area = Compositor::extent(one, 0, doc->bounds());
        Layer& t = doc->layerRef(index);
        Vector::convertToPixels(t);
        t.image = Compositor::renderSingle(one, 0, area);
        t.offset = area.topLeft();
        t.mask.reset();
        t.style.reset();
        t.opacity = opacity;
        t.fill = 1.0f;
        t.mode = mode;
        t.trimToContent();
    });
    return true;
}

// ---------------- Selection ----------------

void selectAll(Document* doc) { doc->changeSelection(Sel::full(doc->size()), QStringLiteral("Select All")); }

void deselect(Document* doc)
{
    if (doc->hasSelection()) doc->changeSelection(QImage(), QStringLiteral("Deselect"));
}

void reselect(Document* doc)
{
    const QImage& last = doc->lastSelection();
    if (last.isNull() || last.size() != doc->size()) return;
    doc->changeSelection(last, QStringLiteral("Reselect"));
}

void inverse(Document* doc)
{
    if (!doc->hasSelection()) return;
    doc->changeSelection(Sel::inverted(doc->selection(), doc->size()), QStringLiteral("Inverse"));
}

bool modifySelection(Document* doc, Modify how, int amount, bool atCanvasBounds)
{
    if (!doc->hasSelection() || amount <= 0) return false;
    const QImage& sel = doc->selection();
    QImage result;
    QString text;
    switch (how) {
    case Modify::Border:
        result = Sel::border(sel, amount);
        text = QStringLiteral("Border");
        break;
    case Modify::Smooth:
        result = Sel::smoothed(sel, amount, atCanvasBounds);
        text = QStringLiteral("Smooth");
        break;
    case Modify::Expand:
        result = Sel::expanded(sel, amount);
        text = QStringLiteral("Expand");
        break;
    case Modify::Contract:
        result = Sel::contracted(sel, amount, atCanvasBounds);
        text = QStringLiteral("Contract");
        break;
    }
    doc->changeSelection(Sel::isEmpty(result) ? QImage() : result, text);
    return true;
}

bool growSelection(Document* doc, int tolerance, bool contiguous)
{
    if (!doc->hasSelection()) return false;
    QImage result = Sel::grown(doc->selection(), doc->composite(), tolerance, contiguous);
    doc->changeSelection(result, contiguous ? QStringLiteral("Grow") : QStringLiteral("Similar"));
    return true;
}

void setQuickMask(Document* doc, bool on)
{
    if (on == doc->inQuickMask()) return;
    doc->modify(QStringLiteral("Quick Mask"), [&] {
        if (on) {
            // White is selected, black is masked; no selection means everything is selected.
            Layer mask = Layer::create(QStringLiteral("Quick Mask"));
            mask.isBackground = true;
            mask.image = QImage(doc->size(), QImage::Format_ARGB32_Premultiplied);
            const QImage& sel = doc->selection();
            for (int y = 0; y < doc->height(); ++y) {
                auto* d = reinterpret_cast<QRgb*>(mask.image.scanLine(y));
                const uchar* s = sel.isNull() ? nullptr : sel.constScanLine(y);
                for (int x = 0; x < doc->width(); ++x) {
                    const int v = s ? s[x] : 255;
                    d[x] = qRgb(v, v, v);
                }
            }
            doc->setSelectionRaw(QImage());
            doc->setQuickMaskRaw(true, mask);
        } else {
            const Layer& mask = doc->quickMaskLayer();
            QImage sel = Sel::empty(doc->size());
            bool all = true;
            for (int y = 0; y < doc->height(); ++y) {
                uchar* d = sel.scanLine(y);
                for (int x = 0; x < doc->width(); ++x) {
                    d[x] = uchar(qGray(mask.pixelAt(QPoint(x, y))));
                    all = all && d[x] == 255;
                }
            }
            doc->setQuickMaskRaw(false);
            doc->setSelectionRaw(all || Sel::isEmpty(sel) ? QImage() : sel);
        }
    });
}

void loadSelectionFromLayer(Document* doc, int index, Sel::Op op)
{
    const Layer& l = doc->layerAt(index);
    QImage mask;
    if (l.isPixel() || l.isVector()) {
        mask = Sel::fromLayerAlpha(l, doc->size());
    } else if (l.isGroup()) {
        // A group's transparency is that of its blended contents.
        Layer probe;
        probe.image = Compositor::renderSingle(doc->layers(), index, doc->bounds());
        mask = Sel::fromLayerAlpha(probe, doc->size());
    }
    if (mask.isNull() && op == Sel::Op::Replace) return;
    doc->changeSelection(Sel::combine(doc->selection(), mask.isNull() ? Sel::empty(doc->size()) : mask, op),
                         QStringLiteral("Load Selection"));
}

// ---------------- Pixels ----------------

bool fill(Document* doc, const QColor& color, BlendMode mode, float opacity,
          bool preserveTransparency, QString* error)
{
    Layer* l = doc->editLayer();
    if (!l) return false;
    const QString err = editTargetError(doc, QStringLiteral("Could not use the Fill command"));
    if (!err.isEmpty()) {
        setError(error, err);
        return false;
    }
    QRect r = doc->hasSelection() ? doc->selectionBounds() : doc->bounds();
    PixelEdit edit(doc, doc->editIndex(), preserveTransparency ? QRect() : r);
    ImageOps::fillColor(edit.layer(), r, ImageOps::premultiplied(color), mode, opacity,
                        doc->selection(), preserveTransparency || edit.layer().lockTransparency);
    edit.markDirty(r);
    edit.commit(QStringLiteral("Fill"));
    return true;
}

bool clear(Document* doc, const QColor& backgroundColor, QString* error)
{
    Layer* l = doc->editLayer();
    if (!l || !doc->hasSelection()) return false;
    const QString err = editTargetError(doc, QStringLiteral("Could not complete the Clear command"));
    if (!err.isEmpty()) {
        setError(error, err);
        return false;
    }
    if (l->lockTransparency && !l->isBackground) {
        setError(error, QStringLiteral("Could not complete the Clear command because the layer is locked."));
        return false;
    }
    QRect r = doc->selectionBounds();
    PixelEdit edit(doc, doc->editIndex(), QRect());
    if (edit.layer().isBackground)
        ImageOps::fillColor(edit.layer(), r, ImageOps::premultiplied(backgroundColor),
                            BlendMode::Normal, 1.f, doc->selection(), false);
    else
        ImageOps::clearPixels(edit.layer(), r, doc->selection());
    edit.markDirty(r);
    edit.commit(QStringLiteral("Clear"));
    return true;
}

QImage copy(Document* doc, bool merged, QPoint* topLeft, QString* error)
{
    QRect r = doc->hasSelection() ? doc->selectionBounds() : doc->bounds();
    QImage out;
    if (merged) {
        Layer tmp;
        tmp.image = doc->composite();
        out = ImageOps::maskedPixels(tmp, r, doc->selection());
    } else {
        Layer* l = doc->activeLayer();
        if (!l) return QImage();
        if (l->isGroup() || l->kind == LayerKind::Adjustment) {
            setError(error, QStringLiteral("Could not complete the Copy command because %1.").arg(kindPhrase(*l)));
            return QImage();
        }
        out = ImageOps::maskedPixels(*l, r, doc->selection());
    }
    if (isTransparent(out)) {
        setError(error, QStringLiteral("Could not complete the Copy command because the selected area is empty."));
        return QImage();
    }
    if (topLeft) *topLeft = r.topLeft();
    return out;
}

void paste(Document* doc, const QImage& image, const QPoint* preferredTopLeft)
{
    if (image.isNull()) return;
    QImage img = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    QPoint at = preferredTopLeft ? *preferredTopLeft
                                 : QPoint((doc->width() - img.width()) / 2, (doc->height() - img.height()) / 2);
    doc->modify(QStringLiteral("Paste"), [&] {
        Layer l = Layer::create(doc->nextLayerName());
        l.image = img;
        l.offset = at;
        insertLayer(doc, l);
        doc->setSelectionRaw(QImage());
    });
}

// ---------------- Canvas ----------------

void crop(Document* doc, const QRect& rectIn, bool deleteCroppedPixels)
{
    QRect rect = rectIn.normalized();
    if (rect.isEmpty()) return;
    doc->modify(QStringLiteral("Crop"), [&] {
        const QRect newBounds(QPoint(), rect.size());
        const QPoint shift = -rect.topLeft();
        for (Layer& l : doc->layersRef()) {
            if (l.isBackground) {
                QImage bg(rect.size(), QImage::Format_ARGB32_Premultiplied);
                bg.fill(Qt::white);
                QPainter p(&bg);
                p.drawImage(l.offset + shift, l.image);
                p.end();
                l.image = bg;
                l.offset = QPoint();
                continue;
            }
            if (l.isVector()) {
                Vector::transform(l, QTransform::fromTranslate(shift.x(), shift.y()));
                if (l.mask) l.mask->offset += shift;
                continue;
            }
            forEachRaster(l, [&](Layer& r) {
                if (r.image.isNull()) return;
                r.offset += shift;
                if (deleteCroppedPixels) r.setGeometry(r.rect() & newBounds);
            });
        }
        doc->setSizeRaw(rect.size());
        doc->setSelectionRaw(QImage());
    });
}

void cropToSelection(Document* doc)
{
    if (!doc->hasSelection()) return;
    crop(doc, doc->selectionBounds(), true);
}

void imageSize(Document* doc, const QSize& newSize, double dpi, Resample method)
{
    if (newSize.isEmpty()) return;
    if (newSize == doc->size()) {
        if (!qFuzzyCompare(dpi, doc->dpi()))
            doc->modify(QStringLiteral("Image Size"), [&] { doc->setDpi(dpi); });
        return;
    }
    const double sx = double(newSize.width()) / doc->width();
    const double sy = double(newSize.height()) / doc->height();
    const Qt::TransformationMode mode =
        method == Resample::NearestNeighbor ? Qt::FastTransformation : Qt::SmoothTransformation;
    auto scaleRaster = [&](Layer& l) {
        if (l.image.isNull()) return;
        if (l.isBackground) {
            l.image = l.image.scaled(newSize, Qt::IgnoreAspectRatio, mode);
            return;
        }
        QPoint o(int(std::lround(l.offset.x() * sx)), int(std::lround(l.offset.y() * sy)));
        QSize s(std::max(1, int(std::lround(l.image.width() * sx))),
                std::max(1, int(std::lround(l.image.height() * sy))));
        l.image = l.image.scaled(s, Qt::IgnoreAspectRatio, mode);
        l.offset = o;
    };
    doc->modify(QStringLiteral("Image Size"), [&] {
        for (Layer& l : doc->layersRef()) {
            if (l.isVector()) {
                // Vector layers re-render crisply at the new size.
                if (l.kind == LayerKind::Text && l.text) {
                    TextData t = *l.text;
                    if (std::fabs(sx - sy) < 1e-9 && t.transform.isIdentity()) {
                        t.size = std::max(1, int(std::lround(t.size * sx)));
                        t.position = QPointF(t.position.x() * sx, t.position.y() * sy);
                        l.text = std::make_shared<const TextData>(t);
                        Vector::rasterize(l);
                    } else {
                        Vector::transform(l, QTransform::fromScale(sx, sy));
                    }
                } else {
                    Vector::transform(l, QTransform::fromScale(sx, sy));
                }
                if (l.mask) scaleRaster(*l.mask);
                continue;
            }
            forEachRaster(l, scaleRaster);
        }
        if (doc->hasSelection())
            doc->setSelectionRaw(doc->selection()
                                     .scaled(newSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                                     .convertToFormat(QImage::Format_Grayscale8));
        doc->setSizeRaw(newSize);
        doc->setDpi(dpi);
    });
}

void canvasSize(Document* doc, const QSize& newSize, const QPoint& anchorOffset,
                const QColor& extensionColor)
{
    if (newSize.isEmpty() || (newSize == doc->size() && anchorOffset.isNull())) return;
    doc->modify(QStringLiteral("Canvas Size"), [&] {
        for (Layer& l : doc->layersRef()) {
            if (l.isBackground) {
                QImage bg(newSize, QImage::Format_ARGB32_Premultiplied);
                bg.fill(extensionColor);
                QPainter p(&bg);
                p.setCompositionMode(QPainter::CompositionMode_Source);
                p.drawImage(anchorOffset, l.image);
                p.end();
                l.image = bg;
                l.offset = QPoint();
            } else {
                // Moves pixels, mask and vector data alike.
                const bool linked = l.maskLinked;
                l.maskLinked = true;
                l.translate(anchorOffset);
                l.maskLinked = linked;
            }
        }
        if (doc->hasSelection()) doc->setSelectionRaw(Sel::remapped(doc->selection(), newSize, anchorOffset));
        doc->setSizeRaw(newSize);
    });
}

void rotate(Document* doc, Rotation how)
{
    static const char* names[] = {"Rotate Canvas", "Rotate Canvas", "Rotate Canvas",
                                  "Flip Canvas Horizontal", "Flip Canvas Vertical"};
    doc->modify(QString::fromLatin1(names[int(how)]), [&] {
        const QSize old = doc->size();
        auto rotateRaster = [&](Layer& l) {
            if (l.image.isNull()) return;
            QRect r = mapRect(l.rect(), how, old);
            l.image = transformImage(l.image, how);
            l.offset = r.topLeft();
        };
        for (Layer& l : doc->layersRef()) {
            if (l.isVector()) {
                Vector::transform(l, rotationTransform(how, old));
                if (l.mask) rotateRaster(*l.mask);
                continue;
            }
            forEachRaster(l, rotateRaster);
        }
        if (doc->hasSelection())
            doc->setSelectionRaw(transformImage(doc->selection(), how).convertToFormat(QImage::Format_Grayscale8));
        if (how == Rotation::Rotate90CW || how == Rotation::Rotate90CCW)
            doc->setSizeRaw(old.transposed());
    });
}

void setColorMode(Document* doc, ColorMode mode)
{
    if (doc->colorMode() == mode) return;
    doc->modify(ColorModes::menuName(mode), [&] {
        if (mode == ColorMode::Grayscale)
            for (Layer& l : doc->layersRef())
                if (!l.image.isNull()) ColorModes::grayscaleInPlace(l.image);
        doc->setColorModeRaw(mode);
    });
}

} // namespace Ops
