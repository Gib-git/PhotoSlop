#pragma once

#include <QImage>
#include <QLine>
#include <QPainterPath>
#include <QVector>

struct Layer;

// Selections are canvas-sized 8-bit masks (Format_Grayscale8). A null QImage
// means "no selection" (everything is editable).
namespace Sel {

enum class Op { Replace, Add, Subtract, Intersect };

QImage empty(const QSize& size);
QImage full(const QSize& size);
QImage pathMask(const QSize& size, const QPainterPath& path, bool antialias);
// Combines `shape` into `base` (which may be null). Returns null when the result is empty.
QImage combine(const QImage& base, const QImage& shape, Op op);
QImage inverted(const QImage& mask, const QSize& size);
QImage translated(const QImage& mask, const QPoint& delta);
void feather(QImage& mask, double radius);
QRect bounds(const QImage& mask);
bool isEmpty(const QImage& mask);
// Boundary of the >=50% region, as axis-aligned segments in canvas coordinates.
QVector<QLine> edges(const QImage& mask);
QImage fromLayerAlpha(const Layer& layer, const QSize& canvasSize);
// Select > Modify. Radii are in pixels; results are null when nothing stays selected.
// With `atCanvasBounds`, the canvas edge counts as unselected (Photoshop's
// "Apply effect at canvas bounds").
QImage expanded(const QImage& mask, int radius);
QImage contracted(const QImage& mask, int radius, bool atCanvasBounds);
QImage border(const QImage& mask, int width);
QImage smoothed(const QImage& mask, int radius, bool atCanvasBounds);
// Select > Grow (contiguous) and Select > Similar: adds pixels of `image` (canvas-sized,
// premultiplied) whose colour lies within the selected colour range widened by `tolerance`.
QImage grown(const QImage& mask, const QImage& image, int tolerance, bool contiguous);
// Resize/crop helpers used by canvas operations.
QImage remapped(const QImage& mask, const QSize& newSize, const QPoint& offset);

} // namespace Sel
