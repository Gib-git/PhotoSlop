#include "core/Filters.h"

#include "core/BlendMode.h"
#include "core/Commands.h"
#include "core/Document.h"
#include "core/DocumentOps.h"

#include <QThread>
#include <QtConcurrent/QtConcurrentMap>
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace Filters {

namespace {

// Row access without QImage::scanLine, which may detach and is not safe to call from
// several threads at once.
struct Pixels {
    uchar* base;
    qsizetype bpl;
    int w, h;
    explicit Pixels(QImage& img)
        : base(img.bits()), bpl(img.bytesPerLine()), w(img.width()), h(img.height()) {}
    QRgb* row(int y) const { return reinterpret_cast<QRgb*>(base + y * bpl); }
};

struct ConstPixels {
    const uchar* base;
    qsizetype bpl;
    int w, h;
    explicit ConstPixels(const QImage& img)
        : base(img.constBits()), bpl(img.bytesPerLine()), w(img.width()), h(img.height()) {}
    const QRgb* row(int y) const { return reinterpret_cast<const QRgb*>(base + y * bpl); }
    QRgb at(int x, int y) const { return row(std::clamp(y, 0, h - 1))[std::clamp(x, 0, w - 1)]; }
};

QImage blank(const QSize& size) { return QImage(size, QImage::Format_ARGB32_Premultiplied); }

QImage premultipliedCopy(const QImage& src)
{
    return src.format() == QImage::Format_ARGB32_Premultiplied ? src : src.convertToFormat(QImage::Format_ARGB32_Premultiplied);
}

inline int clamp255(int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }
inline int clampTo(int v, int max) { return v < 0 ? 0 : (v > max ? max : v); }

// Premultiplied pixel from channel values, keeping colours within alpha.
inline QRgb pack(int a, int r, int g, int b)
{
    a = clamp255(a);
    return qRgba(clampTo(r, a), clampTo(g, a), clampTo(b, a), a);
}

QImage transposed(const QImage& src, const CancelFlag* cancel)
{
    QImage dst = blank(QSize(src.height(), src.width()));
    ConstPixels in(src);
    Pixels out(dst);
    if (!parallelFor(out.h, [&](int b, int e) {
            for (int y = b; y < e; ++y) {
                QRgb* o = out.row(y);
                for (int x = 0; x < out.w; ++x) o[x] = in.row(x)[y];
            }
        }, cancel))
        return QImage();
    return dst;
}

// Applies `rowFn` along every row, then along every column (through a transpose).
using RowFn = std::function<void(const QRgb* in, QRgb* out, int w)>;

QImage separable(const QImage& src, const RowFn& rowFn, const CancelFlag* cancel)
{
    auto pass = [&](const QImage& img) -> QImage {
        QImage out = blank(img.size());
        ConstPixels in(img);
        Pixels o(out);
        if (!parallelFor(in.h, [&](int b, int e) {
                for (int y = b; y < e; ++y) rowFn(in.row(y), o.row(y), in.w);
            }, cancel))
            return QImage();
        return out;
    };
    QImage h = pass(src);
    if (h.isNull()) return h;
    QImage t = transposed(h, cancel);
    if (t.isNull()) return t;
    QImage v = pass(t);
    if (v.isNull()) return v;
    return transposed(v, cancel);
}

// Running-sum box average of radius `r` along a row, with repeated edges.
void boxRow(const QRgb* in, QRgb* out, int w, int r)
{
    const int n = 2 * r + 1;
    const int half = n / 2;
    int sa = 0, sr = 0, sg = 0, sb = 0;
    auto add = [&](QRgb p, int k) {
        sa += k * qAlpha(p);
        sr += k * qRed(p);
        sg += k * qGreen(p);
        sb += k * qBlue(p);
    };
    for (int i = -r; i <= r; ++i) add(in[std::clamp(i, 0, w - 1)], 1);
    for (int x = 0; x < w; ++x) {
        out[x] = qRgba((sr + half) / n, (sg + half) / n, (sb + half) / n, (sa + half) / n);
        add(in[std::min(x + r + 1, w - 1)], 1);
        add(in[std::max(x - r, 0)], -1);
    }
}

// Convolution with a symmetric float kernel (weights[0] is the centre).
void kernelRow(const QRgb* in, QRgb* out, int w, const std::vector<float>& k)
{
    const int r = int(k.size()) - 1;
    for (int x = 0; x < w; ++x) {
        float a = 0, rr = 0, g = 0, b = 0;
        for (int i = -r; i <= r; ++i) {
            const QRgb p = in[std::clamp(x + i, 0, w - 1)];
            const float f = k[std::abs(i)];
            a += f * qAlpha(p);
            rr += f * qRed(p);
            g += f * qGreen(p);
            b += f * qBlue(p);
        }
        out[x] = pack(int(a + 0.5f), int(rr + 0.5f), int(g + 0.5f), int(b + 0.5f));
    }
}

// Box widths whose three successive passes approximate a gaussian (Kutskir's method).
std::array<int, 3> boxesForGauss(double sigma)
{
    const int n = 3;
    const double wIdeal = std::sqrt(12.0 * sigma * sigma / n + 1.0);
    int wl = int(std::floor(wIdeal));
    if (wl % 2 == 0) --wl;
    const int wu = wl + 2;
    const double mIdeal = (12.0 * sigma * sigma - n * wl * wl - 4.0 * n * wl - 3.0 * n) / (-4.0 * wl - 4.0);
    const int m = int(std::round(mIdeal));
    std::array<int, 3> sizes{};
    for (int i = 0; i < n; ++i) sizes[i] = i < m ? wl : wu;
    return sizes;
}

inline quint32 hash32(quint32 x)
{
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

inline double unit(quint32 h) { return (h & 0xffffff) / double(0x1000000); } // [0, 1)

} // namespace

bool parallelFor(int count, const std::function<void(int begin, int end)>& fn, const CancelFlag* cancel)
{
    if (count <= 0) return !cancelled(cancel);
    const int threads = std::max(1, QThread::idealThreadCount());
    if (count < 32 || threads == 1) {
        // Small jobs: chunk anyway so cancelling stays responsive.
        for (int b = 0; b < count && !cancelled(cancel); b += 16) fn(b, std::min(count, b + 16));
        return !cancelled(cancel);
    }
    const int chunks = std::min(count, threads * 8);
    std::vector<int> ids(chunks);
    for (int i = 0; i < chunks; ++i) ids[i] = i;
    QtConcurrent::blockingMap(ids, [&](int c) {
        if (cancelled(cancel)) return;
        fn(int(qint64(count) * c / chunks), int(qint64(count) * (c + 1) / chunks));
    });
    return !cancelled(cancel);
}

// ---------------- Filters ----------------

QImage gaussianBlur(const QImage& source, double radius, const CancelFlag* cancel)
{
    if (source.isNull() || radius <= 0.0) return source;
    const QImage src = premultipliedCopy(source);
    if (radius < 3.0) {
        // Exact kernel for small radii, where box passes are too coarse.
        const int r = std::max(1, int(std::ceil(radius * 3.0)));
        std::vector<float> k(r + 1);
        float sum = 0;
        for (int i = 0; i <= r; ++i) {
            k[i] = float(std::exp(-0.5 * i * i / (radius * radius)));
            sum += i ? 2 * k[i] : k[i];
        }
        for (float& f : k) f /= sum;
        return separable(src, [&k](const QRgb* in, QRgb* out, int w) { kernelRow(in, out, w, k); }, cancel);
    }
    QImage img = src;
    for (int size : boxesForGauss(radius)) {
        const int r = (size - 1) / 2;
        img = separable(img, [r](const QRgb* in, QRgb* out, int w) { boxRow(in, out, w, r); }, cancel);
        if (img.isNull()) return img;
    }
    return img;
}

QImage boxBlur(const QImage& source, int radius, const CancelFlag* cancel)
{
    if (source.isNull() || radius <= 0) return source;
    return separable(premultipliedCopy(source), [radius](const QRgb* in, QRgb* out, int w) { boxRow(in, out, w, radius); }, cancel);
}

QImage motionBlur(const QImage& source, double angle, int distance, const CancelFlag* cancel)
{
    if (source.isNull() || distance <= 0) return source;
    const QImage src = premultipliedCopy(source);
    const double rad = angle * M_PI / 180.0;
    const double dx = std::cos(rad), dy = -std::sin(rad); // y points down
    const int n = distance + 1;
    // Sample offsets along the streak, centred on the pixel.
    std::vector<QPointF> offsets(n);
    for (int i = 0; i < n; ++i) {
        const double t = i - distance / 2.0;
        offsets[i] = QPointF(t * dx, t * dy);
    }
    QImage out = blank(src.size());
    ConstPixels in(src);
    Pixels o(out);
    if (!parallelFor(in.h, [&](int b, int e) {
            for (int y = b; y < e && !cancelled(cancel); ++y) {
                QRgb* row = o.row(y);
                for (int x = 0; x < in.w; ++x) {
                    double a = 0, r = 0, g = 0, bl = 0;
                    for (const QPointF& off : offsets) {
                        const double sx = x + off.x(), sy = y + off.y();
                        const int x0 = int(std::floor(sx)), y0 = int(std::floor(sy));
                        const double fx = sx - x0, fy = sy - y0;
                        const QRgb p00 = in.at(x0, y0), p10 = in.at(x0 + 1, y0);
                        const QRgb p01 = in.at(x0, y0 + 1), p11 = in.at(x0 + 1, y0 + 1);
                        const double w00 = (1 - fx) * (1 - fy), w10 = fx * (1 - fy), w01 = (1 - fx) * fy, w11 = fx * fy;
                        a += w00 * qAlpha(p00) + w10 * qAlpha(p10) + w01 * qAlpha(p01) + w11 * qAlpha(p11);
                        r += w00 * qRed(p00) + w10 * qRed(p10) + w01 * qRed(p01) + w11 * qRed(p11);
                        g += w00 * qGreen(p00) + w10 * qGreen(p10) + w01 * qGreen(p01) + w11 * qGreen(p11);
                        bl += w00 * qBlue(p00) + w10 * qBlue(p10) + w01 * qBlue(p01) + w11 * qBlue(p11);
                    }
                    row[x] = pack(int(a / n + 0.5), int(r / n + 0.5), int(g / n + 0.5), int(bl / n + 0.5));
                }
            }
        }, cancel))
        return QImage();
    return out;
}

QImage unsharpMask(const QImage& source, double amount, double radius, int threshold, const CancelFlag* cancel)
{
    if (source.isNull() || amount <= 0.0) return source;
    const QImage src = premultipliedCopy(source);
    const QImage blurred = gaussianBlur(src, radius, cancel);
    if (blurred.isNull()) return blurred;
    QImage out = blank(src.size());
    ConstPixels in(src), bl(blurred);
    Pixels o(out);
    const double k = amount / 100.0;
    if (!parallelFor(in.h, [&](int b, int e) {
            for (int y = b; y < e; ++y) {
                const QRgb* s = in.row(y);
                const QRgb* m = bl.row(y);
                QRgb* d = o.row(y);
                for (int x = 0; x < in.w; ++x) {
                    const int a = qAlpha(s[x]);
                    auto sharpen = [&](int sv, int bv) {
                        const int diff = sv - bv;
                        return std::abs(diff) < threshold ? sv : int(std::lround(sv + k * diff));
                    };
                    d[x] = pack(a, sharpen(qRed(s[x]), qRed(m[x])), sharpen(qGreen(s[x]), qGreen(m[x])),
                                sharpen(qBlue(s[x]), qBlue(m[x])));
                }
            }
        }, cancel))
        return QImage();
    return out;
}

QImage highPass(const QImage& source, double radius, const CancelFlag* cancel)
{
    if (source.isNull()) return source;
    const QImage src = premultipliedCopy(source);
    const QImage blurred = gaussianBlur(src, radius, cancel);
    if (blurred.isNull()) return blurred;
    QImage out = blank(src.size());
    ConstPixels in(src), bl(blurred);
    Pixels o(out);
    if (!parallelFor(in.h, [&](int b, int e) {
            for (int y = b; y < e; ++y) {
                const QRgb* s = in.row(y);
                const QRgb* m = bl.row(y);
                QRgb* d = o.row(y);
                for (int x = 0; x < in.w; ++x) {
                    const QRgb sp = qUnpremultiply(s[x]);
                    const QRgb bp = qAlpha(m[x]) ? qUnpremultiply(m[x]) : sp;
                    const QRgb hp = qRgba(clamp255(128 + qRed(sp) - qRed(bp)), clamp255(128 + qGreen(sp) - qGreen(bp)),
                                          clamp255(128 + qBlue(sp) - qBlue(bp)), qAlpha(s[x]));
                    d[x] = qPremultiply(hp);
                }
            }
        }, cancel))
        return QImage();
    return out;
}

QImage addNoise(const QImage& source, double amount, bool gaussian, bool monochromatic, const QPoint& origin,
                quint32 seed, const CancelFlag* cancel)
{
    if (source.isNull() || amount <= 0.0) return source;
    const QImage src = premultipliedCopy(source);
    QImage out = blank(src.size());
    ConstPixels in(src);
    Pixels o(out);
    // Uniform noise spans +-amount% of half the range; gaussian noise uses that as its spread.
    const double scale = amount / 100.0 * 128.0;
    auto noise = [&](quint32 h) {
        if (!gaussian) return (unit(h) * 2.0 - 1.0) * scale;
        // Sum of four uniforms, rescaled to unit variance (close enough to a normal distribution).
        double s = unit(h) + unit(hash32(h + 1)) + unit(hash32(h + 2)) + unit(hash32(h + 3));
        return (s - 2.0) * std::sqrt(3.0) * scale * 0.6;
    };
    if (!parallelFor(in.h, [&](int b, int e) {
            for (int y = b; y < e; ++y) {
                const QRgb* s = in.row(y);
                QRgb* d = o.row(y);
                const quint32 hy = hash32(quint32(y + origin.y()) ^ seed);
                for (int x = 0; x < in.w; ++x) {
                    if (!qAlpha(s[x])) {
                        d[x] = s[x];
                        continue;
                    }
                    const quint32 h = hash32(hy ^ (quint32(x + origin.x()) * 0x9e3779b1U));
                    const QRgb p = qUnpremultiply(s[x]);
                    int r, g, bl;
                    if (monochromatic) {
                        const int n = int(std::lround(noise(h)));
                        r = qRed(p) + n;
                        g = qGreen(p) + n;
                        bl = qBlue(p) + n;
                    } else {
                        r = qRed(p) + int(std::lround(noise(hash32(h ^ 0x1111))));
                        g = qGreen(p) + int(std::lround(noise(hash32(h ^ 0x2222))));
                        bl = qBlue(p) + int(std::lround(noise(hash32(h ^ 0x3333))));
                    }
                    d[x] = qPremultiply(qRgba(clamp255(r), clamp255(g), clamp255(bl), qAlpha(p)));
                }
            }
        }, cancel))
        return QImage();
    return out;
}

QImage median(const QImage& source, int radius, const CancelFlag* cancel)
{
    if (source.isNull() || radius <= 0) return source;
    const QImage src = premultipliedCopy(source);
    QImage out = blank(src.size());
    ConstPixels in(src);
    Pixels o(out);
    const int n = (2 * radius + 1) * (2 * radius + 1);
    const int want = n / 2 + 1;
    if (!parallelFor(in.h, [&](int b, int e) {
            // Per channel (a, r, g, b): a fine 256-bin histogram and a coarse 16-bin one,
            // so finding the median takes at most 32 steps (Huang's sliding window).
            std::array<std::array<int, 256>, 4> fine;
            std::array<std::array<int, 16>, 4> coarse;
            std::vector<const QRgb*> rows(2 * radius + 1);
            auto column = [&](int x, int k) {
                x = std::clamp(x, 0, in.w - 1);
                for (const QRgb* row : rows) {
                    const QRgb p = row[x];
                    const int v[4] = {qAlpha(p), qRed(p), qGreen(p), qBlue(p)};
                    for (int c = 0; c < 4; ++c) {
                        fine[c][v[c]] += k;
                        coarse[c][v[c] >> 4] += k;
                    }
                }
            };
            auto medianOf = [&](int c) {
                int acc = 0, block = 0;
                while (acc + coarse[c][block] < want) acc += coarse[c][block++];
                int v = block << 4;
                while (acc + fine[c][v] < want) acc += fine[c][v++];
                return v;
            };
            for (int y = b; y < e && !cancelled(cancel); ++y) {
                for (auto& f : fine) f.fill(0);
                for (auto& c : coarse) c.fill(0);
                for (int i = 0; i < int(rows.size()); ++i) rows[i] = in.row(std::clamp(y - radius + i, 0, in.h - 1));
                for (int x = -radius; x <= radius; ++x) column(x, 1);
                QRgb* d = o.row(y);
                for (int x = 0; x < in.w; ++x) {
                    d[x] = pack(medianOf(0), medianOf(1), medianOf(2), medianOf(3));
                    column(x + radius + 1, 1);
                    column(x - radius, -1);
                }
            }
        }, cancel))
        return QImage();
    return out;
}

QImage mosaic(const QImage& source, int cellSize, const QPoint& origin, const CancelFlag* cancel)
{
    if (source.isNull() || cellSize <= 1) return source;
    const QImage src = premultipliedCopy(source);
    QImage out = blank(src.size());
    ConstPixels in(src);
    Pixels o(out);
    auto floorDiv = [](int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); };
    const int firstCol = floorDiv(origin.x(), cellSize), lastCol = floorDiv(origin.x() + in.w - 1, cellSize);
    const int firstRow = floorDiv(origin.y(), cellSize), lastRow = floorDiv(origin.y() + in.h - 1, cellSize);
    if (!parallelFor(lastRow - firstRow + 1, [&](int b, int e) {
            for (int cr = firstRow + b; cr < firstRow + e; ++cr) {
                const int y0 = std::max(0, cr * cellSize - origin.y()), y1 = std::min(in.h, (cr + 1) * cellSize - origin.y());
                for (int cc = firstCol; cc <= lastCol; ++cc) {
                    const int x0 = std::max(0, cc * cellSize - origin.x()), x1 = std::min(in.w, (cc + 1) * cellSize - origin.x());
                    qint64 a = 0, r = 0, g = 0, bl = 0;
                    for (int y = y0; y < y1; ++y) {
                        const QRgb* row = in.row(y);
                        for (int x = x0; x < x1; ++x) {
                            a += qAlpha(row[x]);
                            r += qRed(row[x]);
                            g += qGreen(row[x]);
                            bl += qBlue(row[x]);
                        }
                    }
                    const qint64 cnt = qint64(y1 - y0) * (x1 - x0);
                    const QRgb avg = pack(int((a + cnt / 2) / cnt), int((r + cnt / 2) / cnt), int((g + cnt / 2) / cnt),
                                          int((bl + cnt / 2) / cnt));
                    for (int y = y0; y < y1; ++y) std::fill(o.row(y) + x0, o.row(y) + x1, avg);
                }
            }
        }, cancel))
        return QImage();
    return out;
}

// ---------------- Specs ----------------

namespace {
int blurMargin(double radius) { return int(std::ceil(radius * 3.0)) + 2; }
} // namespace

Spec gaussianBlurSpec(double radius)
{
    return {QStringLiteral("Gaussian Blur"),
            [radius](const QImage& s, const QPoint&, const CancelFlag* c) { return gaussianBlur(s, radius, c); },
            blurMargin(radius), true};
}

Spec boxBlurSpec(int radius)
{
    return {QStringLiteral("Box Blur"),
            [radius](const QImage& s, const QPoint&, const CancelFlag* c) { return boxBlur(s, radius, c); },
            radius + 1, true};
}

Spec motionBlurSpec(double angle, int distance)
{
    return {QStringLiteral("Motion Blur"),
            [angle, distance](const QImage& s, const QPoint&, const CancelFlag* c) { return motionBlur(s, angle, distance, c); },
            distance / 2 + 2, true};
}

Spec blurSpec()
{
    Spec s = gaussianBlurSpec(0.6);
    s.name = QStringLiteral("Blur");
    return s;
}

Spec blurMoreSpec()
{
    Spec s = gaussianBlurSpec(1.3);
    s.name = QStringLiteral("Blur More");
    return s;
}

Spec unsharpMaskSpec(double amount, double radius, int threshold)
{
    return {QStringLiteral("Unsharp Mask"),
            [=](const QImage& s, const QPoint&, const CancelFlag* c) { return unsharpMask(s, amount, radius, threshold, c); },
            blurMargin(radius), false};
}

Spec sharpenSpec()
{
    Spec s = unsharpMaskSpec(60, 0.6, 0);
    s.name = QStringLiteral("Sharpen");
    return s;
}

Spec sharpenMoreSpec()
{
    Spec s = unsharpMaskSpec(160, 0.6, 0);
    s.name = QStringLiteral("Sharpen More");
    return s;
}

Spec highPassSpec(double radius)
{
    return {QStringLiteral("High Pass"),
            [radius](const QImage& s, const QPoint&, const CancelFlag* c) { return highPass(s, radius, c); },
            blurMargin(radius), false};
}

Spec addNoiseSpec(double amount, bool gaussian, bool monochromatic, quint32 seed)
{
    return {QStringLiteral("Add Noise"),
            [=](const QImage& s, const QPoint& origin, const CancelFlag* c) {
                return addNoise(s, amount, gaussian, monochromatic, origin, seed, c);
            },
            0, false};
}

Spec medianSpec(int radius)
{
    return {QStringLiteral("Median"),
            [radius](const QImage& s, const QPoint&, const CancelFlag* c) { return median(s, radius, c); },
            radius + 1, true};
}

Spec mosaicSpec(int cellSize)
{
    return {QStringLiteral("Mosaic"),
            [cellSize](const QImage& s, const QPoint& origin, const CancelFlag* c) { return mosaic(s, cellSize, origin, c); },
            cellSize, true};
}

// ---------------- Session ----------------

Session::Session(Document* doc, const QString& name, bool spreads, const QRect& target)
{
    Layer* l = doc->editLayer();
    m_error = Ops::editTargetError(doc, QStringLiteral("Could not complete the %1 command").arg(name));
    if (!m_error.isEmpty()) return;
    QRect area = target;
    if (!area.isValid()) {
        area = (doc->hasSelection() ? doc->selectionBounds() : doc->bounds()) & doc->bounds();
        // Without spreading, only existing pixels change.
        if (!spreads) area &= l->rect();
    }
    if (area.isEmpty()) {
        m_error = QStringLiteral("Could not complete the %1 command because the selected area is empty.").arg(name);
        return;
    }
    m_edit = std::make_unique<PixelEdit>(doc, doc->editIndex(), area);
    const Layer& layer = m_edit->layer();
    m_layerId = layer.id;
    m_input.layer = layer.image;
    m_input.layerOffset = layer.offset;
    m_input.canvas = doc->bounds();
    m_input.target = area;
    m_input.selection = doc->selection();
    m_input.keepAlpha = layer.transparencyLocked();
}

Session::~Session() = default; // an uncommitted PixelEdit restores the original pixels

QImage Session::originalTarget() const
{
    return m_input.layer.copy(m_input.target.translated(-m_input.layerOffset));
}

QImage Session::selectionTarget() const
{
    return m_input.selection.isNull() ? QImage() : m_input.selection.copy(m_input.target);
}

QImage Session::render(const Input& in, const Spec& spec, const CancelFlag* cancel)
{
    const int m = std::max(0, spec.margin);
    const QRect srcRect = in.target.adjusted(-m, -m, m, m) & in.canvas;
    // Pixels outside the layer buffer are transparent (QImage::copy fills them with zero).
    const QImage source = in.layer.copy(srcRect.translated(-in.layerOffset));
    QImage filtered = spec.fn ? spec.fn(source, srcRect.topLeft(), cancel) : source;
    if (filtered.isNull() || cancelled(cancel)) return QImage();
    if (filtered.format() != QImage::Format_ARGB32_Premultiplied)
        filtered = filtered.convertToFormat(QImage::Format_ARGB32_Premultiplied);

    QImage result = blank(in.target.size());
    ConstPixels f(filtered), orig(in.layer);
    Pixels out(result);
    const QPoint fOff = in.target.topLeft() - srcRect.topLeft();
    const QPoint lOff = in.target.topLeft() - in.layerOffset;
    const bool hasSel = !in.selection.isNull();
    const uchar* selBase = hasSel ? in.selection.constBits() : nullptr;
    const qsizetype selBpl = hasSel ? in.selection.bytesPerLine() : 0;
    if (!parallelFor(in.target.height(), [&](int b, int e) {
            for (int y = b; y < e; ++y) {
                const QRgb* fr = f.row(y + fOff.y()) + fOff.x();
                const QRgb* orow = orig.row(y + lOff.y()) + lOff.x();
                const uchar* mrow = hasSel ? selBase + (y + in.target.top()) * selBpl + in.target.left() : nullptr;
                QRgb* d = out.row(y);
                for (int x = 0; x < in.target.width(); ++x) {
                    const QRgb o = orow[x];
                    QRgb v = fr[x];
                    if (in.keepAlpha && qAlpha(v) != qAlpha(o))
                        v = qAlpha(o) ? qPremultiply((qUnpremultiply(v) & 0x00ffffff) | (QRgb(qAlpha(o)) << 24)) : 0;
                    const int cover = mrow ? mrow[x] : 255;
                    if (cover == 0) v = o;
                    else if (cover != 255) v = Blend::byteMul(v, cover) + Blend::byteMul(o, 255 - cover);
                    d[x] = v;
                }
            }
        }, cancel))
        return QImage();
    return result;
}

void Session::show(const QImage& result)
{
    if (!m_edit || result.size() != m_input.target.size()) return;
    Layer& l = m_edit->layer();
    const QPoint local = m_input.target.topLeft() - l.offset;
    const int bytes = m_input.target.width() * 4;
    for (int y = 0; y < result.height(); ++y)
        memcpy(l.image.scanLine(local.y() + y) + local.x() * 4, result.constScanLine(y), bytes);
    m_edit->markDirty(m_input.target);
    m_shown = result;
}

void Session::showOriginal()
{
    show(originalTarget());
    m_shown = QImage();
}

void Session::commit(const QString& name)
{
    if (!m_edit) return;
    m_edit->commit(name);
}

bool apply(Document* doc, const Spec& spec, QString* error, Applied* applied)
{
    Session s(doc, spec.name, spec.spreads);
    if (!s.isValid()) {
        if (error) *error = s.error();
        return false;
    }
    const QImage result = Session::render(s.input(), spec);
    s.show(result);
    s.commit(spec.name);
    if (applied) *applied = {s.layerId(), s.target(), s.originalTarget(), result};
    return true;
}

Spec fadeSpec(const QString& name, const QImage& before, int mode, double opacity)
{
    Spec s;
    s.name = name;
    s.fn = [before, mode, opacity](const QImage& after, const QPoint&, const CancelFlag* cancel) -> QImage {
        if (after.size() != before.size()) return after;
        QImage out = before.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        Pixels o(out);
        ConstPixels in(after);
        const uint alpha = uint(std::lround(std::clamp(opacity, 0.0, 1.0) * 255.0));
        if (!parallelFor(o.h, [&](int b, int e) {
                for (int y = b; y < e; ++y) {
                    QRgb* d = o.row(y);
                    const QRgb* a = in.row(y);
                    if (BlendMode(mode) == BlendMode::Normal) {
                        // A straight mix of the two states, alpha included.
                        for (int x = 0; x < o.w; ++x) d[x] = Blend::byteMul(a[x], alpha) + Blend::byteMul(d[x], 255 - alpha);
                    } else {
                        Blend::compositeRow(d, a, o.w, BlendMode(mode), float(opacity), nullptr, 0, y);
                    }
                }
            }, cancel))
            return QImage();
        return out;
    };
    return s;
}

} // namespace Filters
