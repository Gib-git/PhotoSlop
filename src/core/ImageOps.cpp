#include "core/ImageOps.h"

#include <QPainter>
#include <cmath>
#include <vector>
#include <algorithm>
#include <iterator>

namespace ImageOps {

constexpr double kPi = 3.14159265358979323846;

QRgb premultiplied(const QColor& c) { return qPremultiply(c.rgba()); }

namespace {

// Produces a per-row coverage mask from the selection and extra mask.
// Returns nullptr when nothing restricts the row.
const uint8_t* rowMask(const QImage& selection, const QImage& extra, int y, int x, int count,
                       std::vector<uint8_t>& buf)
{
    const uint8_t* a = selection.isNull() ? nullptr : selection.constScanLine(y) + x;
    const uint8_t* b = extra.isNull() ? nullptr : extra.constScanLine(y) + x;
    if (!a) return b;
    if (!b) return a;
    buf.resize(size_t(count));
    for (int i = 0; i < count; ++i) buf[size_t(i)] = uint8_t((a[i] * b[i] + 127) / 255);
    return buf.data();
}

inline uint32_t hash2(int x, int y)
{
    uint32_t h = uint32_t(x) * 73856093u ^ uint32_t(y) * 19349663u;
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    return h ^ (h >> 15);
}

} // namespace

void compositeImage(Layer& layer, const QImage& srcCanvas, const QRect& canvasRect, BlendMode mode,
                    float opacity, const QImage& selection, bool preserveAlpha,
                    const QImage& extraMask)
{
    QRect r = canvasRect & layer.rect() & srcCanvas.rect();
    if (r.isEmpty()) return;
    std::vector<uint8_t> buf;
    for (int y = r.top(); y <= r.bottom(); ++y) {
        auto* dst = reinterpret_cast<uint32_t*>(layer.image.scanLine(y - layer.offset.y()))
            + (r.left() - layer.offset.x());
        auto* src = reinterpret_cast<const uint32_t*>(srcCanvas.constScanLine(y)) + r.left();
        const uint8_t* m = rowMask(selection, extraMask, y, r.left(), r.width(), buf);
        if (preserveAlpha)
            Blend::compositeRowPreserveAlpha(dst, src, r.width(), mode, opacity, m, r.left(), y);
        else
            Blend::compositeRow(dst, src, r.width(), mode, opacity, m, r.left(), y);
    }
}

void fillColor(Layer& layer, const QRect& canvasRect, QRgb premulColor, BlendMode mode,
               float opacity, const QImage& selection, bool preserveAlpha, const QImage& extraMask)
{
    QRect r = canvasRect & layer.rect();
    if (r.isEmpty()) return;
    std::vector<uint32_t> src(size_t(r.width()), premulColor);
    std::vector<uint8_t> buf;
    for (int y = r.top(); y <= r.bottom(); ++y) {
        auto* dst = reinterpret_cast<uint32_t*>(layer.image.scanLine(y - layer.offset.y()))
            + (r.left() - layer.offset.x());
        const uint8_t* m = rowMask(selection, extraMask, y, r.left(), r.width(), buf);
        if (preserveAlpha)
            Blend::compositeRowPreserveAlpha(dst, src.data(), r.width(), mode, opacity, m, r.left(), y);
        else
            Blend::compositeRow(dst, src.data(), r.width(), mode, opacity, m, r.left(), y);
    }
}

void clearPixels(Layer& layer, const QRect& canvasRect, const QImage& selection)
{
    QRect r = canvasRect & layer.rect();
    for (int y = r.top(); y <= r.bottom(); ++y) {
        auto* dst = reinterpret_cast<uint32_t*>(layer.image.scanLine(y - layer.offset.y()))
            + (r.left() - layer.offset.x());
        const uint8_t* m = selection.isNull() ? nullptr : selection.constScanLine(y) + r.left();
        for (int i = 0; i < r.width(); ++i) {
            if (!m) dst[i] = 0;
            else if (m[i]) dst[i] = Blend::byteMul(dst[i], 255 - m[i]);
        }
    }
}

QImage maskedPixels(const Layer& layer, const QRect& rect, const QImage& selection)
{
    QImage out(rect.size(), QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);
    QRect r = layer.rect() & rect;
    for (int y = r.top(); y <= r.bottom(); ++y) {
        auto* dst = reinterpret_cast<uint32_t*>(out.scanLine(y - rect.top())) + (r.left() - rect.left());
        auto* src = reinterpret_cast<const uint32_t*>(layer.image.constScanLine(y - layer.offset.y()))
            + (r.left() - layer.offset.x());
        const uchar* m = selection.isNull() ? nullptr : selection.constScanLine(y) + r.left();
        for (int i = 0; i < r.width(); ++i) dst[i] = m ? Blend::byteMul(src[i], m[i]) : src[i];
    }
    return out;
}

QImage floodMask(const QImage& img, const QPoint& seed, int tolerance, bool contiguous,
                 bool antialias, int sampleSize)
{
    const int w = img.width(), h = img.height();
    QImage mask(img.size(), QImage::Format_Grayscale8);
    mask.fill(0);
    if (!img.rect().contains(seed)) return mask;

    QRgb ref = img.pixel(seed);
    if (sampleSize > 1) {
        const int half = sampleSize / 2;
        const QRect area = QRect(seed.x() - half, seed.y() - half, sampleSize, sampleSize) & img.rect();
        int sum[4] = {0, 0, 0, 0};
        for (int y = area.top(); y <= area.bottom(); ++y)
            for (int x = area.left(); x <= area.right(); ++x) {
                const QRgb p = reinterpret_cast<const QRgb*>(img.constScanLine(y))[x];
                sum[0] += qRed(p);
                sum[1] += qGreen(p);
                sum[2] += qBlue(p);
                sum[3] += qAlpha(p);
            }
        const int n = area.width() * area.height();
        ref = qRgba((sum[0] + n / 2) / n, (sum[1] + n / 2) / n, (sum[2] + n / 2) / n, (sum[3] + n / 2) / n);
    }
    auto within = [&](QRgb p) {
        return std::abs(qRed(p) - qRed(ref)) <= tolerance
            && std::abs(qGreen(p) - qGreen(ref)) <= tolerance
            && std::abs(qBlue(p) - qBlue(ref)) <= tolerance
            && std::abs(qAlpha(p) - qAlpha(ref)) <= tolerance;
    };
    auto px = [&](int x, int y) { return reinterpret_cast<const QRgb*>(img.constScanLine(y))[x]; };

    if (!contiguous) {
        for (int y = 0; y < h; ++y) {
            uchar* m = mask.scanLine(y);
            for (int x = 0; x < w; ++x)
                if (within(px(x, y))) m[x] = 255;
        }
    } else {
        std::vector<QPoint> stack;
        stack.push_back(seed);
        while (!stack.empty()) {
            QPoint p = stack.back();
            stack.pop_back();
            int y = p.y();
            uchar* m = mask.scanLine(y);
            if (m[p.x()]) continue;
            int x0 = p.x(), x1 = p.x();
            while (x0 > 0 && !m[x0 - 1] && within(px(x0 - 1, y))) --x0;
            while (x1 < w - 1 && !m[x1 + 1] && within(px(x1 + 1, y))) ++x1;
            for (int x = x0; x <= x1; ++x) m[x] = 255;
            for (int ny : {y - 1, y + 1}) {
                if (ny < 0 || ny >= h) continue;
                const uchar* nm = mask.constScanLine(ny);
                bool inRun = false;
                for (int x = x0; x <= x1; ++x) {
                    bool ok = !nm[x] && within(px(x, ny));
                    if (ok && !inRun) stack.emplace_back(x, ny);
                    inRun = ok;
                }
            }
        }
    }

    if (antialias) {
        // Soften the boundary pixels with a 3x3 average restricted to edges.
        QImage src = mask.copy();
        for (int y = 0; y < h; ++y) {
            uchar* d = mask.scanLine(y);
            for (int x = 0; x < w; ++x) {
                int sum = 0, n = 0;
                bool edge = false;
                const uchar c = src.constScanLine(y)[x];
                for (int dy = -1; dy <= 1; ++dy) {
                    int yy = y + dy;
                    if (yy < 0 || yy >= h) continue;
                    const uchar* s = src.constScanLine(yy);
                    for (int dx = -1; dx <= 1; ++dx) {
                        int xx = x + dx;
                        if (xx < 0 || xx >= w) continue;
                        sum += s[xx];
                        ++n;
                        if (s[xx] != c) edge = true;
                    }
                }
                if (edge && c) d[x] = uchar(std::max<int>(sum / n, 128));
            }
        }
    }
    return mask;
}

QImage renderGradient(const QSize& size, const QPointF& p0, const QPointF& p1, GradientType type,
                      const QGradientStops& stopsIn, bool reverse, bool dither, bool useTransparency)
{
    // Build a lookup table interpolating straight colour + alpha, stored premultiplied.
    constexpr int kLut = 1024;
    std::vector<QRgb> lut(kLut);
    QGradientStops stops = stopsIn;
    if (stops.isEmpty()) stops = {{0.0, Qt::black}, {1.0, Qt::white}};
    for (int i = 0; i < kLut; ++i) {
        double t = double(i) / (kLut - 1);
        if (reverse) t = 1.0 - t;
        QColor a = stops.first().second, b = stops.last().second;
        double ta = stops.first().first, tb = stops.last().first;
        for (int s = 0; s + 1 < stops.size(); ++s) {
            if (t >= stops[s].first && t <= stops[s + 1].first) {
                a = stops[s].second;
                b = stops[s + 1].second;
                ta = stops[s].first;
                tb = stops[s + 1].first;
                break;
            }
        }
        double f = tb > ta ? std::clamp((t - ta) / (tb - ta), 0.0, 1.0) : (t >= tb ? 1.0 : 0.0);
        auto mix = [f](double x, double y) { return x + (y - x) * f; };
        int alpha = useTransparency ? int(std::lround(mix(a.alphaF(), b.alphaF()) * 255)) : 255;
        QColor c = QColor::fromRgbF(float(mix(a.redF(), b.redF())), float(mix(a.greenF(), b.greenF())),
                                    float(mix(a.blueF(), b.blueF())));
        c.setAlpha(alpha);
        lut[size_t(i)] = qPremultiply(c.rgba());
    }

    QImage out(size, QImage::Format_ARGB32_Premultiplied);
    const double dx = p1.x() - p0.x(), dy = p1.y() - p0.y();
    const double len2 = std::max(1e-9, dx * dx + dy * dy);
    const double len = std::sqrt(len2);
    const double baseAngle = std::atan2(dy, dx);

    for (int y = 0; y < size.height(); ++y) {
        auto* row = reinterpret_cast<QRgb*>(out.scanLine(y));
        for (int x = 0; x < size.width(); ++x) {
            const double px = x + 0.5 - p0.x(), py = y + 0.5 - p0.y();
            double t = 0.0;
            switch (type) {
            case GradientType::Linear: t = (px * dx + py * dy) / len2; break;
            case GradientType::Radial: t = std::sqrt(px * px + py * py) / len; break;
            case GradientType::Reflected: t = std::fabs(px * dx + py * dy) / len2; break;
            case GradientType::Diamond: {
                double u = std::fabs(px * dx + py * dy) / len2;
                double v = std::fabs(px * dy - py * dx) / len2;
                t = u + v;
                break;
            }
            case GradientType::Angle: {
                double a = std::atan2(py, px) - baseAngle;
                a = std::fmod(-a + 4.0 * kPi, 2.0 * kPi);
                t = a / (2.0 * kPi);
                break;
            }
            }
            if (dither) t += (double(hash2(x, y) & 0xff) / 255.0 - 0.5) / 255.0;
            t = std::clamp(t, 0.0, 1.0);
            row[x] = lut[size_t(t * (kLut - 1) + 0.5)];
        }
    }
    return out;
}

QImage flatten(const QList<Layer>& layers, const QSize& size, bool visibleOnly)
{
    QImage out(size, QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);
    for (const Layer& l : layers) {
        if ((visibleOnly && !l.visible) || l.image.isNull()) continue;
        QRect r = l.rect() & out.rect();
        for (int y = r.top(); y <= r.bottom(); ++y) {
            auto* dst = reinterpret_cast<uint32_t*>(out.scanLine(y)) + r.left();
            auto* src = reinterpret_cast<const uint32_t*>(l.image.constScanLine(y - l.offset.y()))
                + (r.left() - l.offset.x());
            Blend::compositeRow(dst, src, r.width(), l.mode, l.opacity * l.fill, nullptr, r.left(), y);
        }
    }
    return out;
}

QImage toExportImage(const QImage& composite, const QColor& matte)
{
    if (!matte.isValid()) return composite.convertToFormat(QImage::Format_ARGB32);
    QImage out(composite.size(), QImage::Format_RGB32);
    out.fill(matte);
    QPainter p(&out);
    p.drawImage(0, 0, composite);
    p.end();
    return out;
}

} // namespace ImageOps
