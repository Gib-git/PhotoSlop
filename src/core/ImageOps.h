#pragma once

#include "core/BlendMode.h"
#include "core/Layer.h"

#include <QColor>
#include <QGradient>
#include <QImage>
#include <QList>

namespace ImageOps {

enum class GradientType { Linear, Radial, Angle, Reflected, Diamond };

QRgb premultiplied(const QColor& c);

// Composites a canvas-sized premultiplied source image into `layer` within
// `canvasRect`, clipped by the selection (and an optional extra mask; both
// canvas-sized Grayscale8, null = none).
void compositeImage(Layer& layer, const QImage& srcCanvas, const QRect& canvasRect,
                    BlendMode mode, float opacity, const QImage& selection,
                    bool preserveAlpha, const QImage& extraMask = QImage());

// Same as compositeImage with a solid colour.
void fillColor(Layer& layer, const QRect& canvasRect, QRgb premulColor, BlendMode mode,
               float opacity, const QImage& selection, bool preserveAlpha,
               const QImage& extraMask = QImage());

// Removes pixels (or scales alpha down) by the selection coverage.
void clearPixels(Layer& layer, const QRect& canvasRect, const QImage& selection);

// Pixels of `layer` within `canvasRect`, multiplied by the selection coverage (null = all).
QImage maskedPixels(const Layer& layer, const QRect& canvasRect, const QImage& selection);

// Magic-wand style region (Grayscale8, canvas sized). The reference colour is the
// average of a `sampleSize` x `sampleSize` square around the seed.
QImage floodMask(const QImage& canvasImage, const QPoint& seed, int tolerance, bool contiguous,
                 bool antialias, int sampleSize = 1);

QImage renderGradient(const QSize& canvasSize, const QPointF& p0, const QPointF& p1,
                      GradientType type, const QGradientStops& stops, bool reverse, bool dither,
                      bool useTransparency);

// Flattens layers into a canvas-sized premultiplied image.
QImage flatten(const QList<Layer>& layers, const QSize& size, bool visibleOnly = true);

// Converts a premultiplied canvas image to a straight-alpha image composited over `matte`
// (pass an invalid colour to keep transparency).
QImage toExportImage(const QImage& composite, const QColor& matte);

} // namespace ImageOps
