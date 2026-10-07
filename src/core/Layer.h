#pragma once

#include "core/BlendMode.h"

#include <QImage>
#include <QPoint>
#include <QRect>
#include <QString>

// A raster layer. Pixels are premultiplied ARGB32 and may extend beyond the
// canvas; `offset` is the canvas position of the image's top-left corner.
// QImage is implicitly shared, so copying a Layer is cheap until it is painted on.
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

    static quint64 nextId();
    static Layer create(const QString& name);

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
};

// Same layer content and properties (images compared by cache key).
bool sameLayer(const Layer& a, const Layer& b);
