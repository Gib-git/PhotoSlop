#include "core/Selection.h"

#include "core/Layer.h"

#include <QPainter>
#include <cmath>
#include <vector>
#include <algorithm>
#include <iterator>

namespace Sel {

QImage empty(const QSize& size)
{
    QImage m(size, QImage::Format_Grayscale8);
    m.fill(0);
    return m;
}

QImage full(const QSize& size)
{
    QImage m(size, QImage::Format_Grayscale8);
    m.fill(255);
    return m;
}

QImage pathMask(const QSize& size, const QPainterPath& path, bool antialias)
{
    QImage m = empty(size);
    QPainter p(&m);
    p.setRenderHint(QPainter::Antialiasing, antialias);
    p.setPen(Qt::NoPen);
    p.setBrush(Qt::white);
    p.drawPath(path);
    return m;
}

QImage combine(const QImage& base, const QImage& shape, Op op)
{
    QImage result;
    if (op == Op::Replace || base.isNull()) {
        // With nothing selected, subtract/intersect leave nothing selected.
        if (op == Op::Subtract || op == Op::Intersect) return QImage();
        result = shape;
    } else {
        result = base.copy();
        const int w = result.width(), h = result.height();
        for (int y = 0; y < h; ++y) {
            uchar* d = result.scanLine(y);
            const uchar* s = shape.constScanLine(y);
            for (int x = 0; x < w; ++x) {
                switch (op) {
                case Op::Add: d[x] = std::max(d[x], s[x]); break;
                case Op::Subtract: d[x] = uchar(d[x] * (255 - s[x]) / 255); break;
                case Op::Intersect: d[x] = std::min(d[x], s[x]); break;
                case Op::Replace: break;
                }
            }
        }
    }
    return isEmpty(result) ? QImage() : result;
}

QImage inverted(const QImage& mask, const QSize& size)
{
    if (mask.isNull()) return QImage(); // inverse of "everything" is nothing
    QImage r = mask.copy();
    for (int y = 0; y < r.height(); ++y) {
        uchar* d = r.scanLine(y);
        for (int x = 0; x < r.width(); ++x) d[x] = 255 - d[x];
    }
    Q_UNUSED(size);
    return isEmpty(r) ? QImage() : r;
}

QImage translated(const QImage& mask, const QPoint& delta)
{
    if (mask.isNull()) return mask;
    QImage r = empty(mask.size());
    QPainter p(&r);
    p.setCompositionMode(QPainter::CompositionMode_Source);
    p.drawImage(delta, mask);
    p.end();
    return isEmpty(r) ? QImage() : r;
}

QImage remapped(const QImage& mask, const QSize& newSize, const QPoint& offset)
{
    if (mask.isNull()) return mask;
    QImage r = empty(newSize);
    QPainter p(&r);
    p.setCompositionMode(QPainter::CompositionMode_Source);
    p.drawImage(offset, mask);
    p.end();
    return isEmpty(r) ? QImage() : r;
}

namespace {

void boxBlurH(const QImage& src, QImage& dst, int r)
{
    const int w = src.width(), h = src.height();
    const int div = 2 * r + 1;
    for (int y = 0; y < h; ++y) {
        const uchar* s = src.constScanLine(y);
        uchar* d = dst.scanLine(y);
        int sum = 0;
        for (int i = -r; i <= r; ++i) sum += s[std::clamp(i, 0, w - 1)];
        for (int x = 0; x < w; ++x) {
            d[x] = uchar((sum + div / 2) / div);
            sum += s[std::min(x + r + 1, w - 1)] - s[std::max(x - r, 0)];
        }
    }
}

void boxBlurV(const QImage& src, QImage& dst, int r)
{
    const int w = src.width(), h = src.height();
    const int div = 2 * r + 1;
    std::vector<int> sum(w, 0);
    for (int i = -r; i <= r; ++i) {
        const uchar* s = src.constScanLine(std::clamp(i, 0, h - 1));
        for (int x = 0; x < w; ++x) sum[x] += s[x];
    }
    for (int y = 0; y < h; ++y) {
        uchar* d = dst.scanLine(y);
        const uchar* add = src.constScanLine(std::min(y + r + 1, h - 1));
        const uchar* sub = src.constScanLine(std::max(y - r, 0));
        for (int x = 0; x < w; ++x) {
            d[x] = uchar((sum[x] + div / 2) / div);
            sum[x] += add[x] - sub[x];
        }
    }
}

} // namespace

void feather(QImage& mask, double radius)
{
    if (mask.isNull() || radius <= 0.0) return;
    // Three box blurs approximate a gaussian with sigma = radius / 2.
    double sigma = radius / 2.0;
    int boxW = int(std::sqrt(12.0 * sigma * sigma / 3.0 + 1.0));
    int r = std::max(1, boxW / 2);
    QImage tmp(mask.size(), QImage::Format_Grayscale8);
    for (int pass = 0; pass < 3; ++pass) {
        boxBlurH(mask, tmp, r);
        boxBlurV(tmp, mask, r);
    }
}

QRect bounds(const QImage& mask)
{
    if (mask.isNull()) return QRect();
    int minX = mask.width(), minY = mask.height(), maxX = -1, maxY = -1;
    for (int y = 0; y < mask.height(); ++y) {
        const uchar* s = mask.constScanLine(y);
        int first = -1, last = -1;
        for (int x = 0; x < mask.width(); ++x) {
            if (s[x]) {
                if (first < 0) first = x;
                last = x;
            }
        }
        if (first >= 0) {
            minX = std::min(minX, first);
            maxX = std::max(maxX, last);
            minY = std::min(minY, y);
            maxY = y;
        }
    }
    if (maxX < 0) return QRect();
    return QRect(minX, minY, maxX - minX + 1, maxY - minY + 1);
}

bool isEmpty(const QImage& mask)
{
    if (mask.isNull()) return true;
    for (int y = 0; y < mask.height(); ++y) {
        const uchar* s = mask.constScanLine(y);
        for (int x = 0; x < mask.width(); ++x)
            if (s[x]) return false;
    }
    return true;
}

QVector<QLine> edges(const QImage& mask)
{
    QVector<QLine> out;
    if (mask.isNull()) return out;
    const int w = mask.width(), h = mask.height();
    auto in = [&](int x, int y) -> bool {
        if (x < 0 || y < 0 || x >= w || y >= h) return false;
        return mask.constScanLine(y)[x] >= 128;
    };
    // Horizontal boundaries between row y-1 and row y.
    for (int y = 0; y <= h; ++y) {
        int start = -1;
        for (int x = 0; x <= w; ++x) {
            bool diff = x < w && in(x, y - 1) != in(x, y);
            if (diff && start < 0) start = x;
            if (!diff && start >= 0) {
                out.append(QLine(start, y, x, y));
                start = -1;
            }
        }
    }
    // Vertical boundaries between column x-1 and column x.
    std::vector<int> open(w + 1, -1);
    for (int y = 0; y <= h; ++y) {
        const uchar* row = y < h ? mask.constScanLine(y) : nullptr;
        bool prev = false;
        for (int x = 0; x <= w; ++x) {
            bool cur = row && x < w && row[x] >= 128;
            bool diff = row && (prev != cur);
            if (diff && open[x] < 0) open[x] = y;
            if (!diff && open[x] >= 0) {
                out.append(QLine(x, open[x], x, y));
                open[x] = -1;
            }
            prev = cur;
        }
    }
    return out;
}

namespace {

// Squared Euclidean distance transform of a 1D sampled function (Felzenszwalb & Huttenlocher).
void edt1d(const float* f, int n, float* d, int* v, float* z)
{
    constexpr float inf = 1e20f;
    int k = 0;
    v[0] = 0;
    z[0] = -inf;
    z[1] = inf;
    for (int q = 1; q < n; ++q) {
        float s = ((f[q] + float(q) * q) - (f[v[k]] + float(v[k]) * v[k])) / (2.0f * q - 2.0f * v[k]);
        while (s <= z[k]) {
            --k;
            s = ((f[q] + float(q) * q) - (f[v[k]] + float(v[k]) * v[k])) / (2.0f * q - 2.0f * v[k]);
        }
        ++k;
        v[k] = q;
        z[k] = s;
        z[k + 1] = inf;
    }
    k = 0;
    for (int q = 0; q < n; ++q) {
        while (z[k + 1] < q) ++k;
        d[q] = float(q - v[k]) * float(q - v[k]) + f[v[k]];
    }
}

// Distance from every pixel to the nearest pixel where `feature(x, y)` is true. With
// `borderIsFeature`, pixels just outside the image count as features.
template <typename F>
std::vector<float> distanceTo(int w, int h, F feature, bool borderIsFeature)
{
    constexpr float inf = 1e20f;
    const int pad = borderIsFeature ? 1 : 0;
    const int W = w + 2 * pad, H = h + 2 * pad;
    std::vector<float> grid(size_t(W) * H);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            const int ix = x - pad, iy = y - pad;
            const bool inside = ix >= 0 && iy >= 0 && ix < w && iy < h;
            grid[size_t(y) * W + x] = (inside ? feature(ix, iy) : true) ? 0.0f : inf;
        }
    const int n = std::max(W, H);
    std::vector<float> f(n), d(n), z(n + 1);
    std::vector<int> v(n);
    for (int x = 0; x < W; ++x) {
        for (int y = 0; y < H; ++y) f[y] = grid[size_t(y) * W + x];
        edt1d(f.data(), H, d.data(), v.data(), z.data());
        for (int y = 0; y < H; ++y) grid[size_t(y) * W + x] = d[y];
    }
    for (int y = 0; y < H; ++y) {
        float* row = grid.data() + size_t(y) * W;
        std::copy(row, row + W, f.begin());
        edt1d(f.data(), W, d.data(), v.data(), z.data());
        std::copy(d.begin(), d.begin() + W, row);
    }
    std::vector<float> out(size_t(w) * h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) out[size_t(y) * w + x] = std::sqrt(grid[size_t(y + pad) * W + x + pad]);
    return out;
}

bool selectedAt(const QImage& m, int x, int y) { return m.constScanLine(y)[x] >= 128; }

} // namespace

QImage expanded(const QImage& mask, int radius)
{
    if (mask.isNull() || radius <= 0) return mask;
    const int w = mask.width(), h = mask.height();
    auto dist = distanceTo(w, h, [&](int x, int y) { return selectedAt(mask, x, y); }, false);
    QImage r = mask.copy();
    for (int y = 0; y < h; ++y) {
        uchar* d = r.scanLine(y);
        for (int x = 0; x < w; ++x) {
            const float cover = std::clamp(radius + 1.0f - dist[size_t(y) * w + x], 0.0f, 1.0f);
            d[x] = std::max(d[x], uchar(std::lround(cover * 255)));
        }
    }
    return r;
}

QImage contracted(const QImage& mask, int radius, bool atCanvasBounds)
{
    if (mask.isNull() || radius <= 0) return mask;
    const int w = mask.width(), h = mask.height();
    auto dist = distanceTo(w, h, [&](int x, int y) { return !selectedAt(mask, x, y); }, atCanvasBounds);
    QImage r = mask.copy();
    for (int y = 0; y < h; ++y) {
        uchar* d = r.scanLine(y);
        for (int x = 0; x < w; ++x) {
            const float cover = std::clamp(dist[size_t(y) * w + x] - radius, 0.0f, 1.0f);
            d[x] = std::min(d[x], uchar(std::lround(cover * 255)));
        }
    }
    return isEmpty(r) ? QImage() : r;
}

QImage border(const QImage& mask, int width)
{
    if (mask.isNull() || width <= 0) return mask;
    // A band centred on the selection edge.
    QImage outer = expanded(mask, (width + 1) / 2);
    QImage inner = contracted(mask, width / 2, false);
    if (inner.isNull()) return outer;
    for (int y = 0; y < outer.height(); ++y) {
        uchar* d = outer.scanLine(y);
        const uchar* s = inner.constScanLine(y);
        for (int x = 0; x < outer.width(); ++x) d[x] = uchar(d[x] * (255 - s[x]) / 255);
    }
    return isEmpty(outer) ? QImage() : outer;
}

QImage smoothed(const QImage& mask, int radius, bool atCanvasBounds)
{
    if (mask.isNull() || radius <= 0) return mask;
    // Majority vote over a (2r+1)² square, using a summed-area table.
    const int w = mask.width(), h = mask.height();
    std::vector<int> sat(size_t(w + 1) * (h + 1), 0);
    for (int y = 0; y < h; ++y) {
        const uchar* s = mask.constScanLine(y);
        int run = 0;
        for (int x = 0; x < w; ++x) {
            run += s[x] >= 128 ? 1 : 0;
            sat[size_t(y + 1) * (w + 1) + x + 1] = sat[size_t(y) * (w + 1) + x + 1] + run;
        }
    }
    QImage r(mask.size(), QImage::Format_Grayscale8);
    for (int y = 0; y < h; ++y) {
        uchar* d = r.scanLine(y);
        const int y0 = std::max(0, y - radius), y1 = std::min(h, y + radius + 1);
        for (int x = 0; x < w; ++x) {
            const int x0 = std::max(0, x - radius), x1 = std::min(w, x + radius + 1);
            const int on = sat[size_t(y1) * (w + 1) + x1] - sat[size_t(y0) * (w + 1) + x1]
                - sat[size_t(y1) * (w + 1) + x0] + sat[size_t(y0) * (w + 1) + x0];
            // Outside the canvas counts as selected unless the effect applies at the bounds.
            const int total = (2 * radius + 1) * (2 * radius + 1);
            const int inside = (x1 - x0) * (y1 - y0);
            const int votes = on + (atCanvasBounds ? 0 : total - inside);
            d[x] = votes * 2 > total ? 255 : 0;
        }
    }
    return isEmpty(r) ? QImage() : r;
}

QImage grown(const QImage& mask, const QImage& image, int tolerance, bool contiguous)
{
    if (mask.isNull()) return mask;
    const int w = mask.width(), h = mask.height();
    int lo[4] = {255, 255, 255, 255}, hi[4] = {0, 0, 0, 0};
    auto channels = [](QRgb p, int out[4]) {
        out[0] = qRed(p);
        out[1] = qGreen(p);
        out[2] = qBlue(p);
        out[3] = qAlpha(p);
    };
    bool any = false;
    for (int y = 0; y < h; ++y) {
        const uchar* m = mask.constScanLine(y);
        const QRgb* row = reinterpret_cast<const QRgb*>(image.constScanLine(y));
        for (int x = 0; x < w; ++x) {
            if (m[x] < 128) continue;
            int c[4];
            channels(row[x], c);
            for (int i = 0; i < 4; ++i) {
                lo[i] = std::min(lo[i], c[i]);
                hi[i] = std::max(hi[i], c[i]);
            }
            any = true;
        }
    }
    if (!any) return mask;
    const int slack = (tolerance + 1) / 2;
    auto within = [&](int x, int y) {
        int c[4];
        channels(reinterpret_cast<const QRgb*>(image.constScanLine(y))[x], c);
        for (int i = 0; i < 4; ++i)
            if (c[i] < lo[i] - slack || c[i] > hi[i] + slack) return false;
        return true;
    };
    QImage r = mask.copy();
    if (!contiguous) {
        for (int y = 0; y < h; ++y) {
            uchar* d = r.scanLine(y);
            for (int x = 0; x < w; ++x)
                if (within(x, y)) d[x] = 255;
        }
        return r;
    }
    std::vector<QPoint> stack;
    for (int y = 0; y < h; ++y) {
        const uchar* m = mask.constScanLine(y);
        for (int x = 0; x < w; ++x)
            if (m[x] >= 128) stack.emplace_back(x, y);
    }
    while (!stack.empty()) {
        const QPoint p = stack.back();
        stack.pop_back();
        const QPoint nbrs[4] = {{p.x() - 1, p.y()}, {p.x() + 1, p.y()}, {p.x(), p.y() - 1}, {p.x(), p.y() + 1}};
        for (const QPoint& q : nbrs) {
            if (q.x() < 0 || q.y() < 0 || q.x() >= w || q.y() >= h) continue;
            uchar& d = r.scanLine(q.y())[q.x()];
            if (d >= 128 || !within(q.x(), q.y())) continue;
            d = 255;
            stack.push_back(q);
        }
    }
    return r;
}

QImage fromLayerAlpha(const Layer& layer, const QSize& canvasSize)
{
    QImage m = empty(canvasSize);
    QRect r = layer.rect().intersected(QRect(QPoint(), canvasSize));
    for (int y = r.top(); y <= r.bottom(); ++y) {
        const QRgb* s = reinterpret_cast<const QRgb*>(layer.image.constScanLine(y - layer.offset.y()));
        uchar* d = m.scanLine(y);
        for (int x = r.left(); x <= r.right(); ++x) d[x] = uchar(qAlpha(s[x - layer.offset.x()]));
    }
    return isEmpty(m) ? QImage() : m;
}

} // namespace Sel
