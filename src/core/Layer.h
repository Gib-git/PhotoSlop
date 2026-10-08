#pragma once

#include "core/BlendMode.h"

#include <QImage>
#include <QPoint>
#include <QRect>
#include <QString>
#include <algorithm>
#include <memory>

namespace Adjust { struct LayerSettings; }
struct LayerStyle;
struct TextData;
struct ShapeData;

// A nullable value held on the heap that copies deeply, so a struct can contain a value of
// its own type (a layer's mask is itself a Layer).
template <typename T>
class ValueBox {
public:
    ValueBox() = default;
    ValueBox(const ValueBox& o) : m_p(o.m_p ? std::make_unique<T>(*o.m_p) : nullptr) {}
    ValueBox(ValueBox&&) noexcept = default;
    ValueBox& operator=(const ValueBox& o)
    {
        if (this != &o) m_p = o.m_p ? std::make_unique<T>(*o.m_p) : nullptr;
        return *this;
    }
    ValueBox& operator=(ValueBox&&) noexcept = default;
    ~ValueBox() = default;

    explicit operator bool() const { return bool(m_p); }
    T* get() { return m_p.get(); }
    const T* get() const { return m_p.get(); }
    T* operator->() { return m_p.get(); }
    const T* operator->() const { return m_p.get(); }
    T& operator*() { return *m_p; }
    const T& operator*() const { return *m_p; }
    void set(const T& v) { m_p = std::make_unique<T>(v); }
    void reset() { m_p.reset(); }

private:
    std::unique_ptr<T> m_p;
};

enum class LayerKind : int { Pixel, Group, Adjustment, Text, Shape };

// One entry of the layer stack. Pixels are premultiplied ARGB32 and may extend beyond the
// canvas; `offset` is the canvas position of the image's top-left corner.
// QImage is implicitly shared, so copying a Layer is cheap until it is painted on.
//
// Groups are flattened into the document's list: a group's descendants sit directly below
// it, and every layer names its containing group in `parent`. Text and shape layers keep
// their vector data and a rendering of it in `image`.
struct Layer {
    quint64 id = 0;
    QString name;
    QImage image;
    QPoint offset;
    bool visible = true;
    float opacity = 1.0f;
    float fill = 1.0f;
    BlendMode mode = BlendMode::Normal;
    bool lockTransparency = false;
    bool lockPixels = false;
    bool lockPosition = false;
    bool lockAll = false;
    bool isBackground = false;

    LayerKind kind = LayerKind::Pixel;
    quint64 parent = 0;    // id of the containing group, 0 = top level
    bool expanded = true;  // groups: open in the Layers panel
    bool clipped = false;  // clipping mask: shows only where the layer below it has pixels

    // Layer mask: grey pixels (white reveals). Transparent pixels, and everything outside the
    // buffer, take `maskDefault`.
    ValueBox<Layer> mask;
    quint8 maskDefault = 255;
    bool maskEnabled = true;
    bool maskLinked = true; // moves with the layer

    // Shared and never changed in place; edits replace the pointer.
    std::shared_ptr<const Adjust::LayerSettings> adjustment;
    std::shared_ptr<const LayerStyle> style;
    std::shared_ptr<const TextData> text;
    std::shared_ptr<const ShapeData> shape;

    static quint64 nextId();
    static Layer create(const QString& name);

    bool isGroup() const { return kind == LayerKind::Group; }
    bool isVector() const { return kind == LayerKind::Text || kind == LayerKind::Shape; }
    // Holds pixels that painting tools may change directly.
    bool isPixel() const { return kind == LayerKind::Pixel; }
    bool hasMask() const { return bool(mask); }
    bool hasStyle() const;

    QRect rect() const { return image.isNull() ? QRect() : QRect(offset, image.size()); }
    bool pixelsLocked() const { return lockPixels || lockAll; }
    bool positionLocked() const { return lockPosition || lockAll || isBackground; }
    bool transparencyLocked() const { return lockTransparency || lockAll || isBackground; }
    bool hasAnyLock() const { return lockTransparency || lockPixels || lockPosition || lockAll; }

    // Grows (never shrinks) the pixel buffer so it covers `canvasRect`.
    void ensureCovers(const QRect& canvasRect);
    // Resizes the buffer to exactly `canvasRect`, keeping overlapping pixels.
    void setGeometry(const QRect& canvasRect);
    // Pixel at a canvas position (premultiplied), transparent outside.
    QRgb pixelAt(const QPoint& canvasPt) const;
    // Renders this layer's raw pixels into a canvas-sized image (no opacity/mode).
    QImage toCanvasImage(const QSize& canvasSize) const;
    // Shrinks the buffer to its non-transparent bounds.
    void trimToContent();

    // Mask value (0..255) at a canvas position; 255 without an enabled mask.
    int maskAt(const QPoint& canvasPt) const;
    // Mask values for `count` pixels of canvas row `y` starting at `x0`.
    void maskRow(int y, int x0, int count, uint8_t* out) const;

    // Moves the pixels, a linked mask and any vector data.
    void translate(const QPoint& delta);
    // Gives this layer (and its mask) fresh ids.
    void renewIds();
};

// Same layer content and properties (images compared by cache key).
bool sameLayer(const Layer& a, const Layer& b);

// Grey value a mask pixel stands for, given the mask's default for transparent pixels.
inline int maskValue(QRgb premul, int maskDefault)
{
    const int a = qAlpha(premul);
    // Premultiplied grey plus the default showing through the transparent part.
    return std::min(255, qGray(premul) + (maskDefault * (255 - a) + 127) / 255);
}
