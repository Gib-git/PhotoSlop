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
