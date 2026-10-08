#include "core/Healing.h"

#include <QList>
#include <QtMath>
#include <algorithm>
#include <cmath>
#include <limits>

namespace Heal {

QImage blend(const QImage& targetIn, const QImage& sourceIn, const std::vector<uint8_t>& region)
{
    const QImage target = targetIn.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const QImage source = sourceIn.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const int W = target.width(), H = target.height();
    QImage out = source.copy();
    if (W == 0 || H == 0 || source.size() != target.size() || region.size() != size_t(W) * size_t(H)) return out;

    // Correction M per channel: fixed to target - source outside the region, smooth inside.
    const size_t N = size_t(W) * size_t(H);
    std::vector<float> m(N * 4);
    double edgeSum[4] = {0, 0, 0, 0};
    int edgeCount = 0;
    for (int y = 0; y < H; ++y) {
        const QRgb* t = reinterpret_cast<const QRgb*>(target.constScanLine(y));
        const QRgb* s = reinterpret_cast<const QRgb*>(source.constScanLine(y));
        for (int x = 0; x < W; ++x) {
            const size_t i = size_t(y) * size_t(W) + size_t(x);
            const float d[4] = {float(qAlpha(t[x]) - qAlpha(s[x])), float(qRed(t[x]) - qRed(s[x])),
                                float(qGreen(t[x]) - qGreen(s[x])), float(qBlue(t[x]) - qBlue(s[x]))};
            for (int c = 0; c < 4; ++c) m[i * 4 + size_t(c)] = d[c];
            if (!region[i]) {
                for (int c = 0; c < 4; ++c) edgeSum[c] += d[c];
                ++edgeCount;
            }
        }
    }
    // Start the unknowns at the mean edge difference so the solve converges quickly.
    for (size_t i = 0; i < N; ++i)
        if (region[i])
            for (int c = 0; c < 4; ++c) m[i * 4 + size_t(c)] = edgeCount ? float(edgeSum[c] / edgeCount) : 0.f;

    // Successive over-relaxation of Laplace's equation inside the region.
    const float omega = 1.9f;
    const int iterations = std::clamp(int(std::max(W, H) * 1.5), 40, 600);
    for (int it = 0; it < iterations; ++it) {
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                const size_t i = size_t(y) * size_t(W) + size_t(x);
                if (!region[i]) continue;
                float sum[4] = {0, 0, 0, 0};
                int n = 0;
                auto add = [&](size_t j) {
                    for (int c = 0; c < 4; ++c) sum[c] += m[j * 4 + size_t(c)];
                    ++n;
                };
                if (x > 0) add(i - 1);
                if (x < W - 1) add(i + 1);
                if (y > 0) add(i - size_t(W));
                if (y < H - 1) add(i + size_t(W));
                if (!n) continue;
                for (int c = 0; c < 4; ++c) {
                    float& v = m[i * 4 + size_t(c)];
                    v += omega * (sum[c] / float(n) - v);
                }
            }
        }
    }

    for (int y = 0; y < H; ++y) {
        const QRgb* s = reinterpret_cast<const QRgb*>(source.constScanLine(y));
        const QRgb* t = reinterpret_cast<const QRgb*>(target.constScanLine(y));
        QRgb* o = reinterpret_cast<QRgb*>(out.scanLine(y));
        for (int x = 0; x < W; ++x) {
            const size_t i = size_t(y) * size_t(W) + size_t(x);
            if (!region[i]) {
                o[x] = t[x];
                continue;
            }
            const float* d = &m[i * 4];
            const int a = std::clamp(int(std::lround(qAlpha(s[x]) + d[0])), 0, 255);
            auto ch = [&](int v, float dv) { return std::clamp(int(std::lround(v + dv)), 0, a); };
            o[x] = qRgba(ch(qRed(s[x]), d[1]), ch(qGreen(s[x]), d[2]), ch(qBlue(s[x]), d[3]), a);
        }
    }
    return out;
}

QPoint findSource(const QImage& image, const QRect& imageRect, const std::vector<uint8_t>& region,
                  const QRect& regionRect)
{
    const int W = regionRect.width(), H = regionRect.height();
    const int D = std::max(W, H);
    const int ring = std::max(4, D / 4);
    const QRect around = regionRect.adjusted(-ring, -ring, ring, ring);
    auto inRegion = [&](const QPoint& p) {
        if (!regionRect.contains(p)) return false;
        return region[size_t(p.y() - regionRect.top()) * size_t(W) + size_t(p.x() - regionRect.left())] != 0;
    };
    auto pixel = [&](const QPoint& p) -> QRgb {
        const QPoint l = p - imageRect.topLeft();
        if (!image.rect().contains(l)) return 0;
        return reinterpret_cast<const QRgb*>(image.constScanLine(l.y()))[l.x()];
    };
    // Sample the ring of known pixels around the region.
    QList<QPoint> samples;
    const int step = std::max(1, int(std::sqrt(double(around.width()) * around.height() / 1500.0)));
    for (int y = around.top(); y <= around.bottom(); y += step)
        for (int x = around.left(); x <= around.right(); x += step) {
            const QPoint p(x, y);
            if (!inRegion(p) && imageRect.contains(p)) samples.append(p);
        }

    QPoint best(D + ring, 0);
    double bestScore = std::numeric_limits<double>::max();
    for (double k : {1.15, 1.5, 2.0, 3.0}) {
        for (int a = 0; a < 16; ++a) {
            const double ang = a * M_PI / 8.0;
            const QPoint off(int(std::lround(std::cos(ang) * (D + ring) * k)), int(std::lround(std::sin(ang) * (D + ring) * k)));
            // The source and its surroundings must lie on the image and clear of the region.
            if (!imageRect.contains(around.translated(off)) || around.translated(off).intersects(regionRect)) continue;
            double score = 0;
            for (const QPoint& p : samples) {
                const QRgb t = pixel(p), s = pixel(p + off);
                const int da = qAlpha(t) - qAlpha(s), dr = qRed(t) - qRed(s), dg = qGreen(t) - qGreen(s), db = qBlue(t) - qBlue(s);
                score += da * da + dr * dr + dg * dg + db * db;
            }
            // Prefer closer texture when matches are equal.
            score = score / std::max<qsizetype>(1, samples.size()) * (1.0 + 0.05 * k);
            if (score < bestScore) {
                bestScore = score;
                best = off;
            }
        }
    }
    return best;
}

} // namespace Heal
