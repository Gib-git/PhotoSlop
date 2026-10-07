#pragma once

#include <QImage>
#include <QPolygonF>
#include <QRectF>
#include <QTransform>

// Geometry and resampling behind Free Transform, Transform Selection and Warp.
namespace Xform {

enum class Interp { NearestNeighbor, Bilinear, Bicubic };

// Corners of a rectangle in Photoshop's handle order: top-left, top-right, bottom-right, bottom-left.
QPolygonF rectQuad(const QRectF& r);
// Projective map taking `src` onto `quad` (same corner order). False if the quad is degenerate.
bool quadTransform(const QRectF& src, const QPolygonF& quad, QTransform* out);
// True for a quad whose corners turn consistently (flipped quads count as convex).
bool isConvex(const QPolygonF& quad);

// A bicubic Bezier patch: 4 x 4 control points, row-major (rows run along v, columns along u).
struct Patch {
    QPointF pts[16];
    QPointF& at(int row, int col) { return pts[row * 4 + col]; }
    const QPointF& at(int row, int col) const { return pts[row * 4 + col]; }
    QPointF eval(double u, double v) const;
    // Control points placed at the thirds of `src` mapped through `t`.
    static Patch fromTransform(const QRectF& src, const QTransform& t);
};

// Bernstein basis of degree 3.
double bernstein(int i, double t);

// Resamples `src` (premultiplied ARGB32) through `srcToDest`, which maps source pixel
// coordinates to destination coordinates. Only the part inside `clip` is rendered;
// `outRect` receives the destination rectangle of the returned image.
QImage transformed(const QImage& src, const QTransform& srcToDest, Interp interp, const QRect& clip,
                   QRect* outRect);
// Same, for a warp patch given in destination coordinates (u and v span the whole source).
QImage warped(const QImage& src, const Patch& patch, Interp interp, const QRect& clip, QRect* outRect);

// Grayscale8 mask variants; `canvas` is the size of the returned mask.
QImage transformedMask(const QImage& mask, const QPoint& maskOrigin, const QTransform& srcToDest,
                       const QSize& canvas);
QImage warpedMask(const QImage& mask, const Patch& patch, const QSize& canvas);

} // namespace Xform
