#include "core/Transform.h"

#include <QPainter>
#include <cmath>
#include <thread>
#include <vector>
#include <algorithm>
#include <iterator>

namespace Xform {

QPolygonF rectQuad(const QRectF& r)
{
    return QPolygonF{r.topLeft(), r.topRight(), r.bottomRight(), r.bottomLeft()};
}

bool quadTransform(const QRectF& src, const QPolygonF& quad, QTransform* out)
{
    if (quad.size() != 4 || src.isEmpty()) return false;
    return QTransform::quadToQuad(rectQuad(src), quad, *out);
}

bool isConvex(const QPolygonF& q)
{
    if (q.size() != 4) return false;
    int sign = 0;
    for (int i = 0; i < 4; ++i) {
        const QPointF a = q[(i + 1) % 4] - q[i], b = q[(i + 2) % 4] - q[(i + 1) % 4];
        const double cross = a.x() * b.y() - a.y() * b.x();
        if (std::fabs(cross) < 1e-9) return false;
        const int s = cross > 0 ? 1 : -1;
        if (sign && s != sign) return false;
        sign = s;
    }
    return true;
}

double bernstein(int i, double t)
{
    const double s = 1.0 - t;
    switch (i) {
    case 0: return s * s * s;
    case 1: return 3 * t * s * s;
    case 2: return 3 * t * t * s;
    default: return t * t * t;
    }
}

QPointF Patch::eval(double u, double v) const
{
    QPointF p;
    for (int r = 0; r < 4; ++r) {
        const double bv = bernstein(r, v);
        for (int c = 0; c < 4; ++c) p += at(r, c) * (bv * bernstein(c, u));
    }
    return p;
}

Patch Patch::fromTransform(const QRectF& src, const QTransform& t)
{
    Patch p;
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            p.at(r, c) = t.map(QPointF(src.left() + src.width() * c / 3.0, src.top() + src.height() * r / 3.0));
    return p;
}

namespace {

inline QRgb fetch(const QImage& img, int x, int y)
{
    if (x < 0 || y < 0 || x >= img.width() || y >= img.height()) return 0;
    return reinterpret_cast<const QRgb*>(img.constScanLine(y))[x];
}

// `fx`, `fy` are in pixel-centre coordinates (pixel (0,0) is centred on 0,0).
QRgb sampleBilinear(const QImage& img, double fx, double fy)
{
    const int x0 = int(std::floor(fx)), y0 = int(std::floor(fy));
    if (x0 < -1 || y0 < -1 || x0 >= img.width() || y0 >= img.height()) return 0;
    const double ax = fx - x0, ay = fy - y0;
    const QRgb p00 = fetch(img, x0, y0), p10 = fetch(img, x0 + 1, y0);
    const QRgb p01 = fetch(img, x0, y0 + 1), p11 = fetch(img, x0 + 1, y0 + 1);
    if ((p00 | p10 | p01 | p11) == 0) return 0;
    const double w00 = (1 - ax) * (1 - ay), w10 = ax * (1 - ay), w01 = (1 - ax) * ay, w11 = ax * ay;
    auto ch = [&](int shift) {
        const double v = ((p00 >> shift) & 0xff) * w00 + ((p10 >> shift) & 0xff) * w10
            + ((p01 >> shift) & 0xff) * w01 + ((p11 >> shift) & 0xff) * w11;
        return uint32_t(std::lround(v)) & 0xff;
    };
    return (ch(24) << 24) | (ch(16) << 16) | (ch(8) << 8) | ch(0);
}

inline double cubicWeight(double t)
{
    // Catmull-Rom (a = -0.5).
    t = std::fabs(t);
    if (t < 1) return 1.5 * t * t * t - 2.5 * t * t + 1;
    if (t < 2) return -0.5 * t * t * t + 2.5 * t * t - 4 * t + 2;
    return 0;
}

QRgb sampleBicubic(const QImage& img, double fx, double fy)
{
    const int x0 = int(std::floor(fx)), y0 = int(std::floor(fy));
    if (x0 < -2 || y0 < -2 || x0 > img.width() || y0 > img.height()) return 0;
    double wx[4], wy[4];
    for (int i = 0; i < 4; ++i) {
        wx[i] = cubicWeight(fx - (x0 - 1 + i));
        wy[i] = cubicWeight(fy - (y0 - 1 + i));
    }
    double acc[4] = {0, 0, 0, 0};
    for (int j = 0; j < 4; ++j) {
        for (int i = 0; i < 4; ++i) {
            const QRgb p = fetch(img, x0 - 1 + i, y0 - 1 + j);
            if (!p) continue;
            const double w = wx[i] * wy[j];
            acc[0] += qAlpha(p) * w;
            acc[1] += qRed(p) * w;
            acc[2] += qGreen(p) * w;
            acc[3] += qBlue(p) * w;
        }
    }
    const int a = std::clamp(int(std::lround(acc[0])), 0, 255);
    if (!a) return 0;
    // Keep the result a valid premultiplied colour.
    auto c = [&](double v) { return std::clamp(int(std::lround(v)), 0, a); };
    return qRgba(c(acc[1]), c(acc[2]), c(acc[3]), a);
}

inline QRgb sample(const QImage& img, double fx, double fy, Interp interp)
{
    switch (interp) {
    case Interp::NearestNeighbor: return fetch(img, int(std::floor(fx + 0.5)), int(std::floor(fy + 0.5)));
    case Interp::Bilinear: return sampleBilinear(img, fx, fy);
    case Interp::Bicubic: return sampleBicubic(img, fx, fy);
    }
    return 0;
}

// Box-filtered half-size image (premultiplied, so averaging is correct).
QImage halved(const QImage& src)
{
    const int w = std::max(1, (src.width() + 1) / 2), h = std::max(1, (src.height() + 1) / 2);
    QImage out(w, h, QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < h; ++y) {
        auto* d = reinterpret_cast<QRgb*>(out.scanLine(y));
        for (int x = 0; x < w; ++x) {
            const QRgb p[4] = {fetch(src, 2 * x, 2 * y), fetch(src, 2 * x + 1, 2 * y), fetch(src, 2 * x, 2 * y + 1),
                               fetch(src, 2 * x + 1, 2 * y + 1)};
            uint32_t o = 0;
            for (int shift = 0; shift < 32; shift += 8) {
                uint32_t sum = 0;
                for (QRgb q : p) sum += (q >> shift) & 0xff;
                o |= ((sum + 2) / 4) << shift;
            }
            d[x] = o;
        }
    }
    return out;
}

// Runs fn(firstRow, endRow) over `rows` rows split across threads.
template <typename F>
void parallelRows(int rows, const F& fn)
{
    const int threads = std::clamp(int(std::thread::hardware_concurrency()), 1, 16);
    if (rows < 64 || threads == 1) {
        fn(0, rows);
        return;
    }
    const int chunk = (rows + threads - 1) / threads;
    std::vector<std::thread> pool;
    for (int a = 0; a < rows; a += chunk) pool.emplace_back([&fn, a, b = std::min(rows, a + chunk)] { fn(a, b); });
    for (std::thread& t : pool) t.join();
}

QImage maskToImage(const QImage& mask)
{
    QImage img(mask.size(), QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < mask.height(); ++y) {
        const uchar* s = mask.constScanLine(y);
        auto* d = reinterpret_cast<QRgb*>(img.scanLine(y));
        for (int x = 0; x < mask.width(); ++x) d[x] = qRgba(s[x], s[x], s[x], s[x]);
    }
    return img;
}

QImage alphaToMask(const QImage& img, const QRect& imgRect, const QSize& canvas)
{
    QImage m(canvas, QImage::Format_Grayscale8);
    m.fill(0);
    const QRect r = imgRect & QRect(QPoint(), canvas);
    for (int y = r.top(); y <= r.bottom(); ++y) {
        const QRgb* s = reinterpret_cast<const QRgb*>(img.constScanLine(y - imgRect.top())) + (r.left() - imgRect.left());
        uchar* d = m.scanLine(y) + r.left();
        for (int i = 0; i < r.width(); ++i) d[i] = uchar(qAlpha(s[i]));
    }
    return m;
}

} // namespace

QImage transformed(const QImage& srcIn, const QTransform& srcToDest, Interp interp, const QRect& clip,
                   QRect* outRect)
{
    *outRect = QRect();
    if (srcIn.isNull()) return QImage();
    bool invertible = false;
    QTransform inv = srcToDest.inverted(&invertible);
    if (!invertible) return QImage();
    const QRect dest = srcToDest.mapRect(QRectF(srcIn.rect())).toAlignedRect() & clip;
    if (dest.isEmpty()) return QImage();

    // Strong minification aliases; sample from a pre-shrunk copy instead.
    QImage src = srcIn;
    const QPointF c = QRectF(dest).center();
    const QPointF c0 = inv.map(c);
    const double stretch = std::max(QLineF(c0, inv.map(c + QPointF(1, 0))).length(),
                                    QLineF(c0, inv.map(c + QPointF(0, 1))).length());
    double factor = 1.0;
    if (interp != Interp::NearestNeighbor) {
        while (stretch / factor >= 2.0 && src.width() > 1 && src.height() > 1) {
            src = halved(src);
            factor *= 2.0;
        }
        if (factor > 1.0) inv = inv * QTransform::fromScale(1.0 / factor, 1.0 / factor);
    }

    QImage out(dest.size(), QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);
    const double m11 = inv.m11(), m12 = inv.m12(), m13 = inv.m13();
    const double m21 = inv.m21(), m22 = inv.m22(), m23 = inv.m23();
    const double m31 = inv.m31(), m32 = inv.m32(), m33 = inv.m33();
    uchar* bits = out.bits();
    const qsizetype bpl = out.bytesPerLine();
    parallelRows(dest.height(), [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            auto* d = reinterpret_cast<QRgb*>(bits + y * bpl);
            const double dy = dest.top() + y + 0.5;
            for (int x = 0; x < dest.width(); ++x) {
                const double dx = dest.left() + x + 0.5;
                const double w = m13 * dx + m23 * dy + m33;
                if (w <= 1e-12) continue;
                const double sx = (m11 * dx + m21 * dy + m31) / w - 0.5;
                const double sy = (m12 * dx + m22 * dy + m32) / w - 0.5;
                d[x] = sample(src, sx, sy, interp);
            }
        }
    });
    *outRect = dest;
    return out;
}

QImage warped(const QImage& src, const Patch& patch, Interp interp, const QRect& clip, QRect* outRect)
{
    *outRect = QRect();
    if (src.isNull()) return QImage();
    // Tessellate the patch; each cell is drawn as two affinely mapped triangles.
    const int n = std::clamp(std::max(src.width(), src.height()) / 12, 8, 48);
    std::vector<QPointF> grid(size_t(n + 1) * (n + 1));
    QRectF bounds;
    for (int i = 0; i <= n; ++i)
        for (int j = 0; j <= n; ++j) {
            const QPointF p = patch.eval(double(j) / n, double(i) / n);
            grid[size_t(i) * (n + 1) + j] = p;
            bounds = bounds.isNull() ? QRectF(p, QSizeF(0.001, 0.001)) : bounds.united(QRectF(p, QSizeF(0.001, 0.001)));
        }
    const QRect dest = bounds.toAlignedRect().adjusted(-1, -1, 1, 1) & clip;
    if (dest.isEmpty()) return QImage();
    QImage out(dest.size(), QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);
    const double sw = src.width(), sh = src.height();

    uchar* bits = out.bits();
    const qsizetype bpl = out.bytesPerLine();
    // Each thread rasterises every triangle, but only within its own band of rows.
    auto triangle = [&](int bandTop, int bandBottom, QPointF a, QPointF b, QPointF c, QPointF sa, QPointF sb, QPointF sc) {
        const double area = (b.x() - a.x()) * (c.y() - a.y()) - (b.y() - a.y()) * (c.x() - a.x());
        if (std::fabs(area) < 1e-9) return;
        const QRect box = QRectF(QPointF(std::min({a.x(), b.x(), c.x()}), std::min({a.y(), b.y(), c.y()})),
                                 QPointF(std::max({a.x(), b.x(), c.x()}), std::max({a.y(), b.y(), c.y()})))
                              .toAlignedRect() & dest & QRect(dest.left(), bandTop, dest.width(), bandBottom - bandTop);
        const double eps = -1e-7 * std::fabs(area);
        for (int y = box.top(); y <= box.bottom(); ++y) {
            auto* d = reinterpret_cast<QRgb*>(bits + (y - dest.top()) * bpl);
            const double py = y + 0.5;
            for (int x = box.left(); x <= box.right(); ++x) {
                const double px = x + 0.5;
                const double w0 = ((b.x() - px) * (c.y() - py) - (b.y() - py) * (c.x() - px)) / area;
                const double w1 = ((c.x() - px) * (a.y() - py) - (c.y() - py) * (a.x() - px)) / area;
                const double w2 = 1.0 - w0 - w1;
                if (w0 * area < eps || w1 * area < eps || w2 * area < eps) continue;
                const QPointF s = sa * w0 + sb * w1 + sc * w2;
                d[x - dest.left()] = sample(src, s.x() - 0.5, s.y() - 0.5, interp);
            }
        }
    };
    parallelRows(dest.height(), [&](int r0, int r1) {
        const int top = dest.top() + r0, bottom = dest.top() + r1;
        for (int i = 0; i < n; ++i)
            for (int j = 0; j < n; ++j) {
                const QPointF g00 = grid[size_t(i) * (n + 1) + j], g10 = grid[size_t(i) * (n + 1) + j + 1];
                const QPointF g01 = grid[size_t(i + 1) * (n + 1) + j], g11 = grid[size_t(i + 1) * (n + 1) + j + 1];
                const QPointF s00(sw * j / n, sh * i / n), s10(sw * (j + 1) / n, sh * i / n);
                const QPointF s01(sw * j / n, sh * (i + 1) / n), s11(sw * (j + 1) / n, sh * (i + 1) / n);
                triangle(top, bottom, g00, g10, g11, s00, s10, s11);
                triangle(top, bottom, g00, g11, g01, s00, s11, s01);
            }
    });
    *outRect = dest;
    return out;
}

QImage transformedMask(const QImage& mask, const QPoint& maskOrigin, const QTransform& srcToDest, const QSize& canvas)
{
    QRect r;
    QImage img = transformed(maskToImage(mask), QTransform::fromTranslate(maskOrigin.x(), maskOrigin.y()) * srcToDest,
                             Interp::Bilinear, QRect(QPoint(), canvas), &r);
    return alphaToMask(img, r, canvas);
}

QImage warpedMask(const QImage& mask, const Patch& patch, const QSize& canvas)
{
    QRect r;
    QImage img = warped(maskToImage(mask), patch, Interp::Bilinear, QRect(QPoint(), canvas), &r);
    return alphaToMask(img, r, canvas);
}

} // namespace Xform
