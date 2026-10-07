#include "core/DocumentOps.h"

#include "core/Commands.h"
#include "core/Document.h"
#include "core/ImageOps.h"

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

// Composites `layers` (visible only) into an image covering `rect` (canvas coords).
QImage compositeArea(const QList<Layer>& layers, const QRect& rect)
{
    QImage out(rect.size(), QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);
    for (const Layer& l : layers) {
        if (!l.visible || l.image.isNull()) continue;
        QRect r = l.rect() & rect;
        for (int y = r.top(); y <= r.bottom(); ++y) {
            auto* dst = reinterpret_cast<uint32_t*>(out.scanLine(y - rect.top())) + (r.left() - rect.left());
            auto* src = reinterpret_cast<const uint32_t*>(l.image.constScanLine(y - l.offset.y()))
                + (r.left() - l.offset.x());
            Blend::compositeRow(dst, src, r.width(), l.mode, l.opacity * l.fill, nullptr, r.left(), y);
        }
    }
    return out;
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

} // namespace

// ---------------- Layers ----------------

void newLayer(Document* doc, const QString& name, BlendMode mode, float opacity)
{
    doc->modify(QStringLiteral("New Layer"), [&] {
        Layer l = Layer::create(name.isEmpty() ? doc->nextLayerName() : name);
        l.mode = mode;
        l.opacity = opacity;
        int at = doc->layerCount() ? doc->activeIndex() + 1 : 0;
        doc->layersRef().insert(at, l);
        doc->setActiveIndex(at);
    });
}

bool duplicateLayer(Document* doc, const QString& name)
{
    Layer* src = doc->activeLayer();
    if (!src) return false;
    doc->modify(QStringLiteral("Duplicate Layer"), [&] {
        Layer l = *src;
        l.id = Layer::nextId();
        l.name = name.isEmpty() ? src->name + QStringLiteral(" copy") : name;
        l.isBackground = false;
        int at = doc->activeIndex() + 1;
        doc->layersRef().insert(at, l);
        doc->setActiveIndex(at);
    });
    return true;
}

bool deleteLayer(Document* doc, QString* error)
{
    if (doc->layerCount() <= 1) {
        setError(error, QStringLiteral("Could not delete the layer because a document must have at least one layer."));
        return false;
    }
    doc->modify(QStringLiteral("Delete Layer"), [&] {
        int idx = doc->activeIndex();
        doc->layersRef().removeAt(idx);
        doc->setActiveIndex(std::max(0, idx - 1));
    });
    return true;
}

bool moveLayer(Document* doc, int from, int to)
{
    const int n = doc->layerCount();
    if (from < 0 || from >= n) return false;
    to = std::clamp(to, 0, n - 1);
    if (doc->hasBackground()) {
        if (from == 0) return false; // the Background layer cannot move
        to = std::max(to, 1);
    }
    if (from == to) return false;
    doc->modify(QStringLiteral("Layer Order"), [&] {
        doc->layersRef().move(from, to);
        doc->setActiveIndex(to);
    });
    return true;
}

bool arrange(Document* doc, Arrange how)
{
    int i = doc->activeIndex();
    switch (how) {
    case Arrange::BringToFront: return moveLayer(doc, i, doc->layerCount() - 1);
    case Arrange::BringForward: return moveLayer(doc, i, i + 1);
    case Arrange::SendBackward: return moveLayer(doc, i, i - 1);
    case Arrange::SendToBack: return moveLayer(doc, i, 0);
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
    bool othersHidden = true;
    for (int i = 0; i < doc->layerCount(); ++i)
        if (i != index && doc->layerAt(i).visible) othersHidden = false;
    doc->modify(QStringLiteral("Show/Hide Layers"), [&] {
        for (int i = 0; i < doc->layerCount(); ++i)
            doc->layerRef(i).visible = othersHidden ? true : (i == index);
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
    if (doc->layerAt(index).mode == mode) return;
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

bool mergeDown(Document* doc, QString* error)
{
    const int idx = doc->activeIndex();
    if (idx <= 0) {
        setError(error, QStringLiteral("Could not complete the Merge Down command because there is no layer below."));
        return false;
    }
    if (doc->layerAt(idx - 1).pixelsLocked()) {
        setError(error, QStringLiteral("Could not complete the Merge Down command because the target layer is locked."));
        return false;
    }
    doc->modify(QStringLiteral("Merge Down"), [&] {
        Layer upper = doc->layerAt(idx);
        Layer& lower = doc->layerRef(idx - 1);
        if (upper.visible && !upper.image.isNull()) {
            QRect cover = lower.isBackground ? doc->bounds() : (upper.rect() | lower.rect());
            lower.ensureCovers(cover & (lower.isBackground ? doc->bounds() : cover));
            QRect r = upper.rect() & lower.rect();
            for (int y = r.top(); y <= r.bottom(); ++y) {
                auto* dst = reinterpret_cast<uint32_t*>(lower.image.scanLine(y - lower.offset.y()))
                    + (r.left() - lower.offset.x());
                auto* src = reinterpret_cast<const uint32_t*>(upper.image.constScanLine(y - upper.offset.y()))
                    + (r.left() - upper.offset.x());
                Blend::compositeRow(dst, src, r.width(), upper.mode, upper.opacity * upper.fill,
                                    nullptr, r.left(), y);
            }
        }
        doc->layersRef().removeAt(idx);
        doc->setActiveIndex(idx - 1);
    });
    return true;
}

bool mergeVisible(Document* doc, QString* error)
{
    int target = -1, visibleCount = 0;
    QRect area;
    for (int i = 0; i < doc->layerCount(); ++i) {
        const Layer& l = doc->layerAt(i);
        if (!l.visible) continue;
        if (target < 0) target = i;
        ++visibleCount;
        area |= l.rect();
    }
    if (visibleCount < 2) {
        setError(error, QStringLiteral("Could not complete the Merge Visible command because there are not enough visible layers."));
        return false;
    }
    doc->modify(QStringLiteral("Merge Visible"), [&] {
        QList<Layer>& layers = doc->layersRef();
        const bool bg = layers[target].isBackground;
        QRect r = bg ? doc->bounds() : area;
        QImage merged = compositeArea(layers, r);
        Layer result = layers[target];
        result.image = merged;
        result.offset = r.topLeft();
        result.opacity = result.fill = 1.0f;
        result.mode = BlendMode::Normal;
        QList<Layer> next;
        int newActive = 0;
        for (int i = 0; i < layers.size(); ++i) {
            if (i == target) {
                newActive = int(next.size());
                next.append(result);
            } else if (!layers[i].visible) {
                next.append(layers[i]);
            }
        }
        layers = next;
        doc->setActiveIndex(newActive);
    });
    return true;
}

void flatten(Document* doc)
{
    doc->modify(QStringLiteral("Flatten Image"), [&] {
        QImage flat(doc->size(), QImage::Format_ARGB32_Premultiplied);
        flat.fill(Qt::white);
        QImage comp = compositeArea(doc->layers(), doc->bounds());
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
        setError(error, cut ? QStringLiteral("Could not complete the New Layer via Cut command because the selected area is empty.")
                            : QStringLiteral("Could not complete the New Layer via Copy command because the selected area is empty."));
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
        doc->layersRef().insert(idx + 1, l);
        doc->setActiveIndex(idx + 1);
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
    QImage mask = Sel::fromLayerAlpha(doc->layerAt(index), doc->size());
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
    if (l->pixelsLocked()) {
        setError(error, QStringLiteral("Could not use the Fill command because the layer is locked."));
        return false;
    }
    if (!l->visible) {
        setError(error, QStringLiteral("Could not use the Fill command because the target layer is hidden."));
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
    if (l->pixelsLocked() || (l->lockTransparency && !l->isBackground)) {
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
        int idx = doc->activeIndex() + 1;
        doc->layersRef().insert(idx, l);
        doc->setActiveIndex(idx);
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
        for (Layer& l : doc->layersRef()) {
            if (l.image.isNull()) continue;
            l.offset -= rect.topLeft();
            if (l.isBackground) {
                QImage bg(rect.size(), QImage::Format_ARGB32_Premultiplied);
                bg.fill(Qt::white);
                QPainter p(&bg);
                p.drawImage(l.offset, l.image);
                p.end();
                l.image = bg;
                l.offset = QPoint();
            } else if (deleteCroppedPixels) {
                l.setGeometry(l.rect() & newBounds);
            }
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
    doc->modify(QStringLiteral("Image Size"), [&] {
        for (Layer& l : doc->layersRef()) {
            if (l.image.isNull()) continue;
            if (l.isBackground) {
                l.image = l.image.scaled(newSize, Qt::IgnoreAspectRatio, mode);
                continue;
            }
            QPoint o(int(std::lround(l.offset.x() * sx)), int(std::lround(l.offset.y() * sy)));
            QSize s(std::max(1, int(std::lround(l.image.width() * sx))),
                    std::max(1, int(std::lround(l.image.height() * sy))));
            l.image = l.image.scaled(s, Qt::IgnoreAspectRatio, mode);
            l.offset = o;
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
                l.offset += anchorOffset;
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
        for (Layer& l : doc->layersRef()) {
            if (l.image.isNull()) continue;
            QRect r = mapRect(l.rect(), how, old);
            l.image = transformImage(l.image, how);
            l.offset = r.topLeft();
        }
        if (doc->hasSelection())
            doc->setSelectionRaw(transformImage(doc->selection(), how).convertToFormat(QImage::Format_Grayscale8));
        if (how == Rotation::Rotate90CW || how == Rotation::Rotate90CCW)
            doc->setSizeRaw(old.transposed());
    });
}

} // namespace Ops
