#include "core/Adjustments.h"

#include <QJsonArray>
#include <QJsonValue>
#include <algorithm>
#include <cmath>
#include <vector>

namespace Adjust {

namespace {

inline int clamp255(double v) { return int(std::clamp(std::lround(v), 0L, 255L)); }

Lut compose(const Lut& first, const Lut& then)
{
    Lut out;
    for (int i = 0; i < 256; ++i) out[i] = then[first[i]];
    return out;
}

PixelMap lutMap(const Lut& r, const Lut& g, const Lut& b)
{
    return [r, g, b](QRgb* px, int n) {
        for (int i = 0; i < n; ++i)
            px[i] = qRgba(r[qRed(px[i])], g[qGreen(px[i])], b[qBlue(px[i])], qAlpha(px[i]));
    };
}

PixelMap lutMap(const Lut& all) { return lutMap(all, all, all); }

// Black and white points that clip `clip` of the pixels at each end.
std::pair<int, int> clipRange(const std::array<quint32, 256>& h, quint64 count, double clip)
{
    const double limit = clip * double(count);
    int lo = 0, hi = 255;
    double acc = 0;
    while (lo < 255 && acc + h[lo] <= limit) acc += h[lo++];
    acc = 0;
    while (hi > 0 && acc + h[hi] <= limit) acc += h[hi--];
    return {lo, hi};
}

} // namespace

Lut identityLut()
{
    Lut l;
    for (int i = 0; i < 256; ++i) l[i] = uchar(i);
    return l;
}

Filters::Spec spec(const QString& name, PixelMap map)
{
    Filters::Spec s;
    s.name = name;
    s.fn = [map = std::move(map)](const QImage& src, const QPoint&, const Filters::CancelFlag* cancel) -> QImage {
        QImage out = src.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        uchar* base = out.bits();
        const qsizetype bpl = out.bytesPerLine();
        const int w = out.width();
        if (!Filters::parallelFor(out.height(), [&](int b, int e) {
                for (int y = b; y < e; ++y) {
                    auto* row = reinterpret_cast<QRgb*>(base + y * bpl);
                    for (int x = 0; x < w; ++x) row[x] = qUnpremultiply(row[x]);
                    map(row, w);
                    for (int x = 0; x < w; ++x) row[x] = qPremultiply(row[x]);
                }
            }, cancel))
            return QImage();
        return out;
    };
    return s;
}

int luminosity(int r, int g, int b) { return (r * 77 + g * 151 + b * 28 + 128) >> 8; }

// ---------------- Histogram ----------------

double Histogram::mean(int channel) const
{
    if (!count) return 0.0;
    double sum = 0;
    for (int v = 0; v < 256; ++v) sum += double(v) * channels[channel][v];
    return sum / double(count);
}

Histogram histogram(const QImage& source, const QImage& coverage)
{
    Histogram h;
    if (source.isNull()) return h;
    const QImage img = source.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const bool masked = !coverage.isNull() && coverage.size() == img.size();
    for (int y = 0; y < img.height(); ++y) {
        const auto* row = reinterpret_cast<const QRgb*>(img.constScanLine(y));
        const uchar* m = masked ? coverage.constScanLine(y) : nullptr;
        for (int x = 0; x < img.width(); ++x) {
            if (!qAlpha(row[x]) || (m && m[x] < 128)) continue;
            const QRgb p = qUnpremultiply(row[x]);
            const int r = qRed(p), g = qGreen(p), b = qBlue(p);
            ++h.channels[0][luminosity(r, g, b)];
            ++h.channels[1][r];
            ++h.channels[2][g];
            ++h.channels[3][b];
            ++h.count;
        }
    }
    return h;
}

// ---------------- Levels ----------------

Lut levelsLut(const LevelsChannel& c)
{
    Lut l;
    const double range = c.inWhite - c.inBlack;
    const double invGamma = 1.0 / std::clamp(c.gamma, 0.01, 9.99);
    for (int v = 0; v < 256; ++v) {
        double x = range > 0 ? std::clamp((v - c.inBlack) / range, 0.0, 1.0) : (v >= c.inWhite ? 1.0 : 0.0);
        x = std::pow(x, invGamma);
        l[v] = uchar(clamp255(c.outBlack + x * (c.outWhite - c.outBlack)));
    }
    return l;
}

PixelMap levelsMap(const Levels& levels)
{
    const Lut master = levelsLut(levels.channels[0]);
    return lutMap(compose(levelsLut(levels.channels[1]), master), compose(levelsLut(levels.channels[2]), master),
                  compose(levelsLut(levels.channels[3]), master));
}

Levels autoLevels(const Histogram& h, AutoMode mode, double clip)
{
    Levels out;
    if (!h.count) return out;
    if (mode == AutoMode::Contrast) {
        // One black and white point for all channels, so colours keep their balance.
        std::array<quint32, 256> all{};
        for (int v = 0; v < 256; ++v) all[v] = h.channels[1][v] + h.channels[2][v] + h.channels[3][v];
        auto [lo, hi] = clipRange(all, h.count * 3, clip);
        if (lo < hi) {
            out.channels[0].inBlack = lo;
            out.channels[0].inWhite = hi;
        }
        return out;
    }
    std::array<double, 3> stretchedMean{};
    for (int c = 1; c <= 3; ++c) {
        auto [lo, hi] = clipRange(h.channels[c], h.count, clip);
        if (lo >= hi) {
            stretchedMean[c - 1] = h.mean(c) / 255.0;
            continue;
        }
        out.channels[c].inBlack = lo;
        out.channels[c].inWhite = hi;
        stretchedMean[c - 1] = std::clamp((h.mean(c) - lo) / double(hi - lo), 0.0, 1.0);
    }
    if (mode == AutoMode::Color) {
        // Snap neutral midtones: bend each channel so its mean lands on the common grey.
        const double target = (stretchedMean[0] + stretchedMean[1] + stretchedMean[2]) / 3.0;
        for (int c = 1; c <= 3; ++c) {
            const double m = stretchedMean[c - 1];
            if (m > 0.01 && m < 0.99 && target > 0.01 && target < 0.99)
                out.channels[c].gamma = std::clamp(std::log(m) / std::log(target), 0.1, 9.99);
        }
    }
    return out;
}

// ---------------- Curves ----------------

CurvePoints identityCurve() { return {QPointF(0, 0), QPointF(255, 255)}; }

Lut curveLut(const CurvePoints& input)
{
    CurvePoints pts = input;
    std::sort(pts.begin(), pts.end(), [](const QPointF& a, const QPointF& b) { return a.x() < b.x(); });
    // Drop points that share an x (keep the last one).
    CurvePoints p;
    for (const QPointF& q : pts) {
        if (!p.isEmpty() && std::abs(p.last().x() - q.x()) < 1e-6) p.last() = q;
        else p.append(q);
    }
    Lut lut;
    if (p.isEmpty()) return identityLut();
    if (p.size() == 1) {
        lut.fill(uchar(clamp255(p[0].y())));
        return lut;
    }
    // Natural cubic spline: solve for the second derivatives.
    const int n = int(p.size());
    std::vector<double> m(n, 0.0), u(n, 0.0);
    for (int i = 1; i < n - 1; ++i) {
        const double sig = (p[i].x() - p[i - 1].x()) / (p[i + 1].x() - p[i - 1].x());
        const double q = sig * m[i - 1] + 2.0;
        m[i] = (sig - 1.0) / q;
        const double d = (p[i + 1].y() - p[i].y()) / (p[i + 1].x() - p[i].x())
                       - (p[i].y() - p[i - 1].y()) / (p[i].x() - p[i - 1].x());
        u[i] = (6.0 * d / (p[i + 1].x() - p[i - 1].x()) - sig * u[i - 1]) / q;
    }
    m[n - 1] = 0.0;
    for (int i = n - 2; i >= 0; --i) m[i] = m[i] * m[i + 1] + u[i];

    int seg = 0;
    for (int v = 0; v < 256; ++v) {
        double y;
        if (v <= p.first().x()) {
            y = p.first().y();
        } else if (v >= p.last().x()) {
            y = p.last().y();
        } else {
            while (seg < n - 2 && v > p[seg + 1].x()) ++seg;
            const double h = p[seg + 1].x() - p[seg].x();
            const double a = (p[seg + 1].x() - v) / h, b = (v - p[seg].x()) / h;
            y = a * p[seg].y() + b * p[seg + 1].y() + ((a * a * a - a) * m[seg] + (b * b * b - b) * m[seg + 1]) * h * h / 6.0;
        }
        lut[v] = uchar(clamp255(y));
    }
    return lut;
}

PixelMap curvesMap(const Curves& curves)
{
    const Lut master = curveLut(curves.channels[0]);
    return lutMap(compose(curveLut(curves.channels[1]), master), compose(curveLut(curves.channels[2]), master),
                  compose(curveLut(curves.channels[3]), master));
}

// ---------------- Simple adjustments ----------------

PixelMap brightnessContrastMap(int brightness, int contrast, bool legacy)
{
    Lut lut;
    if (legacy) {
        // Linear: brightness shifts every level, contrast stretches around the middle and clips.
        const double k = contrast >= 0 ? 1.0 / std::max(0.01, 1.0 - contrast / 100.0) : 1.0 + contrast / 100.0;
        for (int v = 0; v < 256; ++v) lut[v] = uchar(clamp255((v + brightness - 127.5) * k + 127.5));
        return lutMap(lut);
    }
    // Brightness bends the midtones (a gamma curve) and contrast is an S-curve, so black and
    // white stay put and nothing clips.
    const double g = std::pow(2.0, -brightness / 100.0);
    Lut bright;
    for (int v = 0; v < 256; ++v) bright[v] = uchar(clamp255(255.0 * std::pow(v / 255.0, g)));
    const double c = std::clamp(contrast, -50, 100) * 0.3;
    const Lut contrastLut = curveLut({QPointF(0, 0), QPointF(64, 64 - c), QPointF(192, 192 + c), QPointF(255, 255)});
    return lutMap(compose(bright, contrastLut));
}

PixelMap invertMap()
{
    Lut lut;
    for (int v = 0; v < 256; ++v) lut[v] = uchar(255 - v);
    return lutMap(lut);
}

PixelMap desaturateMap()
{
    return [](QRgb* px, int n) {
        for (int i = 0; i < n; ++i) {
            const int r = qRed(px[i]), g = qGreen(px[i]), b = qBlue(px[i]);
            // The average of the brightest and darkest channel, as Photoshop's Desaturate does.
            const int l = (std::max({r, g, b}) + std::min({r, g, b})) / 2;
            px[i] = qRgba(l, l, l, qAlpha(px[i]));
        }
    };
}

PixelMap posterizeMap(int levels)
{
    levels = std::clamp(levels, 2, 255);
    Lut lut;
    for (int v = 0; v < 256; ++v) lut[v] = uchar(clamp255(std::floor(v * levels / 256.0) * 255.0 / (levels - 1)));
    return lutMap(lut);
}

PixelMap thresholdMap(int level)
{
    return [level](QRgb* px, int n) {
        for (int i = 0; i < n; ++i) {
            const int v = luminosity(qRed(px[i]), qGreen(px[i]), qBlue(px[i])) >= level ? 255 : 0;
            px[i] = qRgba(v, v, v, qAlpha(px[i]));
        }
    };
}

// ---------------- HSL ----------------

void rgbToHsl(int r, int g, int b, double& h, double& s, double& l)
{
    const double rf = r / 255.0, gf = g / 255.0, bf = b / 255.0;
    const double mx = std::max({rf, gf, bf}), mn = std::min({rf, gf, bf});
    l = (mx + mn) / 2.0;
    const double d = mx - mn;
    if (d <= 0.0) {
        h = s = 0.0;
        return;
    }
    s = l > 0.5 ? d / (2.0 - mx - mn) : d / (mx + mn);
    if (mx == rf) h = (gf - bf) / d + (gf < bf ? 6.0 : 0.0);
    else if (mx == gf) h = (bf - rf) / d + 2.0;
    else h = (rf - gf) / d + 4.0;
    h *= 60.0;
}

QRgb hslToRgb(double h, double s, double l, int alpha)
{
    h = std::fmod(h, 360.0);
    if (h < 0) h += 360.0;
    s = std::clamp(s, 0.0, 1.0);
    l = std::clamp(l, 0.0, 1.0);
    if (s <= 0.0) {
        const int v = clamp255(l * 255.0);
        return qRgba(v, v, v, alpha);
    }
    const double q = l < 0.5 ? l * (1.0 + s) : l + s - l * s;
    const double p = 2.0 * l - q;
    auto channel = [&](double t) {
        if (t < 0) t += 1.0;
        if (t > 1) t -= 1.0;
        if (t < 1.0 / 6.0) return p + (q - p) * 6.0 * t;
        if (t < 0.5) return q;
        if (t < 2.0 / 3.0) return p + (q - p) * (2.0 / 3.0 - t) * 6.0;
        return p;
    };
    const double hk = h / 360.0;
    return qRgba(clamp255(channel(hk + 1.0 / 3.0) * 255.0), clamp255(channel(hk) * 255.0),
                 clamp255(channel(hk - 1.0 / 3.0) * 255.0), alpha);
}

// ---------------- Hue/Saturation ----------------

PixelMap hueSaturationMap(const HueSaturation& hs)
{
    return [hs](QRgb* px, int n) {
        auto applyLightness = [](QRgb c, int lightness) {
            if (!lightness) return c;
            const double k = lightness / 100.0;
            auto f = [&](int v) { return k < 0 ? clamp255(v * (1.0 + k)) : clamp255(v + (255 - v) * k); };
            return qRgba(f(qRed(c)), f(qGreen(c)), f(qBlue(c)), qAlpha(c));
        };
        const auto& master = hs.ranges[0];
        for (int i = 0; i < n; ++i) {
            double h, s, l;
            rgbToHsl(qRed(px[i]), qGreen(px[i]), qBlue(px[i]), h, s, l);
            if (hs.colorize) {
                const QRgb c = hslToRgb(master.hue, master.saturation / 100.0, l, qAlpha(px[i]));
                px[i] = applyLightness(c, master.lightness);
                continue;
            }
            double dh = master.hue, ds = master.saturation, dl = master.lightness;
            if (s > 0.0) {
                // Each colour range has full effect within 15° of its centre, fading out by 45°.
                for (int r = 1; r <= 6; ++r) {
                    const auto& range = hs.ranges[r];
                    if (range == HueSaturation::Range()) continue;
                    double d = std::abs(h - (r - 1) * 60.0);
                    d = std::min(d, 360.0 - d);
                    const double w = d <= 15.0 ? 1.0 : (d >= 45.0 ? 0.0 : (45.0 - d) / 30.0);
                    dh += w * range.hue;
                    ds += w * range.saturation;
                    dl += w * range.lightness;
                }
            }
            if (!dh && !ds && !dl) continue;
            s = std::clamp(s * (1.0 + std::clamp(ds, -100.0, 100.0) / 100.0), 0.0, 1.0);
            const QRgb c = hslToRgb(h + dh, s, l, qAlpha(px[i]));
            px[i] = applyLightness(c, int(std::lround(std::clamp(dl, -100.0, 100.0))));
        }
    };
}

// ---------------- Color Balance ----------------

PixelMap colorBalanceMap(const ColorBalance& cb)
{
    return [cb](QRgb* px, int n) {
        // Each tone range is weighted by the pixel's lightness (the classic GIMP/Photoshop masks).
        constexpr double a = 0.25, b = 0.333, scale = 0.7;
        for (int i = 0; i < n; ++i) {
            const int r0 = qRed(px[i]), g0 = qGreen(px[i]), b0 = qBlue(px[i]);
            double h, s, l;
            rgbToHsl(r0, g0, b0, h, s, l);
            const double shadows = std::clamp((l - b) / -a + 0.5, 0.0, 1.0) * scale;
            const double midtones = std::clamp((l - b) / a + 0.5, 0.0, 1.0) * std::clamp((l + b - 1.0) / -a + 0.5, 0.0, 1.0) * scale;
            const double highlights = std::clamp((l + b - 1.0) / a + 0.5, 0.0, 1.0) * scale;
            const int in[3] = {r0, g0, b0};
            int out[3];
            for (int c = 0; c < 3; ++c) {
                const double v = in[c] / 255.0 + cb.values[0][c] / 100.0 * shadows + cb.values[1][c] / 100.0 * midtones
                               + cb.values[2][c] / 100.0 * highlights;
                out[c] = clamp255(std::clamp(v, 0.0, 1.0) * 255.0);
            }
            if (cb.preserveLuminosity) {
                double h2, s2, l2;
                rgbToHsl(out[0], out[1], out[2], h2, s2, l2);
                px[i] = hslToRgb(h2, s2, l, qAlpha(px[i]));
            } else {
                px[i] = qRgba(out[0], out[1], out[2], qAlpha(px[i]));
            }
        }
    };
}

// ---------------- Black & White ----------------

PixelMap blackWhiteMap(const BlackWhite& bw)
{
    return [bw](QRgb* px, int n) {
        enum { Reds, Yellows, Greens, Cyans, Blues, Magentas };
        for (int i = 0; i < n; ++i) {
            const int c[3] = {qRed(px[i]), qGreen(px[i]), qBlue(px[i])};
            // Order the channels: the strongest picks the primary weight, the two strongest
            // together pick the secondary weight.
            int hi = 0, lo = 0;
            for (int k = 1; k < 3; ++k) {
                if (c[k] > c[hi]) hi = k;
                if (c[k] < c[lo]) lo = k;
            }
            if (hi == lo) lo = (hi + 1) % 3;
            const int mid = 3 - hi - lo;
            static constexpr int primary[3] = {Reds, Greens, Blues};
            // Secondary colour of two channels: R+G yellow, G+B cyan, R+B magenta.
            const int pair = (1 << hi) | (1 << mid);
            const int secondary = pair == 3 ? Yellows : (pair == 6 ? Cyans : Magentas);
            const double gray = c[lo] + (c[mid] - c[lo]) * bw.weights[secondary] / 100.0
                              + (c[hi] - c[mid]) * bw.weights[primary[hi]] / 100.0;
            const int g = clamp255(gray);
            if (bw.tint) px[i] = hslToRgb(bw.tintHue, bw.tintSaturation / 100.0, g / 255.0, qAlpha(px[i]));
            else px[i] = qRgba(g, g, g, qAlpha(px[i]));
        }
    };
}

// ---------------- Adjustment layers ----------------

QString kindName(Kind kind)
{
    switch (kind) {
    case Kind::BrightnessContrast: return QStringLiteral("Brightness/Contrast");
    case Kind::Levels: return QStringLiteral("Levels");
    case Kind::Curves: return QStringLiteral("Curves");
    case Kind::HueSaturation: return QStringLiteral("Hue/Saturation");
    case Kind::ColorBalance: return QStringLiteral("Color Balance");
    case Kind::BlackWhite: return QStringLiteral("Black & White");
    case Kind::Invert: return QStringLiteral("Invert");
    case Kind::Posterize: return QStringLiteral("Posterize");
    case Kind::Threshold: return QStringLiteral("Threshold");
    }
    return QString();
}

QString LayerSettings::name() const { return kindName(kind); }

PixelMap LayerSettings::buildMap() const
{
    switch (kind) {
    case Kind::BrightnessContrast: return brightnessContrastMap(brightness, contrast, legacy);
    case Kind::Levels: return levelsMap(levels);
    case Kind::Curves: return curvesMap(curves);
    case Kind::HueSaturation: return hueSaturationMap(hueSaturation);
    case Kind::ColorBalance: return colorBalanceMap(colorBalance);
    case Kind::BlackWhite: return blackWhiteMap(blackWhite);
    case Kind::Invert: return invertMap();
    case Kind::Posterize: return posterizeMap(posterizeLevels);
    case Kind::Threshold: return thresholdMap(thresholdLevel);
    }
    return {};
}

std::shared_ptr<const LayerSettings> LayerSettings::make(Kind kind)
{
    LayerSettings s;
    s.kind = kind;
    return s.finalized();
}

std::shared_ptr<const LayerSettings> LayerSettings::finalized() const
{
    auto out = std::make_shared<LayerSettings>(*this);
    out->map = out->buildMap();
    return out;
}

namespace {

QJsonArray curveToJson(const CurvePoints& pts)
{
    QJsonArray a;
    for (const QPointF& p : pts) a.append(QJsonArray{p.x(), p.y()});
    return a;
}

CurvePoints curveFromJson(const QJsonValue& v)
{
    CurvePoints pts;
    for (const QJsonValue& p : v.toArray()) {
        const QJsonArray xy = p.toArray();
        if (xy.size() == 2) pts.append(QPointF(xy[0].toDouble(), xy[1].toDouble()));
    }
    return pts.size() >= 2 ? pts : identityCurve();
}

QJsonArray intsToJson(const int* v, int n)
{
    QJsonArray a;
    for (int i = 0; i < n; ++i) a.append(v[i]);
    return a;
}

void intsFromJson(const QJsonValue& v, int* out, int n)
{
    const QJsonArray a = v.toArray();
    for (int i = 0; i < n && i < a.size(); ++i) out[i] = a[i].toInt(out[i]);
}

const char* kKindIds[] = {"brightnessContrast", "levels", "curves", "hueSaturation", "colorBalance",
                          "blackWhite", "invert", "posterize", "threshold"};

} // namespace

QJsonObject LayerSettings::toJson() const
{
    QJsonObject o;
    o.insert(QStringLiteral("kind"), QString::fromLatin1(kKindIds[int(kind)]));
    switch (kind) {
    case Kind::BrightnessContrast:
        o.insert(QStringLiteral("brightness"), brightness);
        o.insert(QStringLiteral("contrast"), contrast);
        o.insert(QStringLiteral("legacy"), legacy);
        break;
    case Kind::Levels: {
        QJsonArray chans;
        for (const LevelsChannel& c : levels.channels)
            chans.append(QJsonArray{c.inBlack, c.gamma, c.inWhite, c.outBlack, c.outWhite});
        o.insert(QStringLiteral("levels"), chans);
        break;
    }
    case Kind::Curves: {
        QJsonArray chans;
        for (const CurvePoints& c : curves.channels) chans.append(curveToJson(c));
        o.insert(QStringLiteral("curves"), chans);
        break;
    }
    case Kind::HueSaturation: {
        QJsonArray ranges;
        for (const HueSaturation::Range& r : hueSaturation.ranges) ranges.append(QJsonArray{r.hue, r.saturation, r.lightness});
        o.insert(QStringLiteral("ranges"), ranges);
        o.insert(QStringLiteral("colorize"), hueSaturation.colorize);
        break;
    }
    case Kind::ColorBalance: {
        QJsonArray tones;
        for (const auto& t : colorBalance.values) tones.append(intsToJson(t.data(), 3));
        o.insert(QStringLiteral("tones"), tones);
        o.insert(QStringLiteral("preserveLuminosity"), colorBalance.preserveLuminosity);
        break;
    }
    case Kind::BlackWhite:
        o.insert(QStringLiteral("weights"), intsToJson(blackWhite.weights.data(), 6));
        o.insert(QStringLiteral("tint"), blackWhite.tint);
        o.insert(QStringLiteral("tintHue"), blackWhite.tintHue);
        o.insert(QStringLiteral("tintSaturation"), blackWhite.tintSaturation);
        break;
    case Kind::Invert: break;
    case Kind::Posterize: o.insert(QStringLiteral("levels"), posterizeLevels); break;
    case Kind::Threshold: o.insert(QStringLiteral("level"), thresholdLevel); break;
    }
    return o;
}

std::shared_ptr<const LayerSettings> LayerSettings::fromJson(const QJsonObject& o)
{
    LayerSettings s;
    const QString id = o.value(QStringLiteral("kind")).toString();
    for (int i = 0; i < int(std::size(kKindIds)); ++i)
        if (id == QLatin1String(kKindIds[i])) s.kind = Kind(i);
    switch (s.kind) {
    case Kind::BrightnessContrast:
        s.brightness = o.value(QStringLiteral("brightness")).toInt();
        s.contrast = o.value(QStringLiteral("contrast")).toInt();
        s.legacy = o.value(QStringLiteral("legacy")).toBool();
        break;
    case Kind::Levels: {
        const QJsonArray chans = o.value(QStringLiteral("levels")).toArray();
        for (int i = 0; i < 4 && i < chans.size(); ++i) {
            const QJsonArray c = chans[i].toArray();
            if (c.size() != 5) continue;
            s.levels.channels[i] = {c[0].toInt(), c[1].toDouble(1.0), c[2].toInt(255), c[3].toInt(), c[4].toInt(255)};
        }
        break;
    }
    case Kind::Curves: {
        const QJsonArray chans = o.value(QStringLiteral("curves")).toArray();
        for (int i = 0; i < 4 && i < chans.size(); ++i) s.curves.channels[i] = curveFromJson(chans[i]);
        break;
    }
    case Kind::HueSaturation: {
        const QJsonArray ranges = o.value(QStringLiteral("ranges")).toArray();
        for (int i = 0; i < 7 && i < ranges.size(); ++i) {
            const QJsonArray r = ranges[i].toArray();
            if (r.size() == 3) s.hueSaturation.ranges[i] = {r[0].toInt(), r[1].toInt(), r[2].toInt()};
        }
        s.hueSaturation.colorize = o.value(QStringLiteral("colorize")).toBool();
        break;
    }
    case Kind::ColorBalance: {
        const QJsonArray tones = o.value(QStringLiteral("tones")).toArray();
        for (int i = 0; i < 3 && i < tones.size(); ++i) intsFromJson(tones[i], s.colorBalance.values[i].data(), 3);
        s.colorBalance.preserveLuminosity = o.value(QStringLiteral("preserveLuminosity")).toBool(true);
        break;
    }
    case Kind::BlackWhite:
        intsFromJson(o.value(QStringLiteral("weights")), s.blackWhite.weights.data(), 6);
        s.blackWhite.tint = o.value(QStringLiteral("tint")).toBool();
        s.blackWhite.tintHue = o.value(QStringLiteral("tintHue")).toInt(s.blackWhite.tintHue);
        s.blackWhite.tintSaturation = o.value(QStringLiteral("tintSaturation")).toInt(s.blackWhite.tintSaturation);
        break;
    case Kind::Invert: break;
    case Kind::Posterize: s.posterizeLevels = std::clamp(o.value(QStringLiteral("levels")).toInt(4), 2, 255); break;
    case Kind::Threshold: s.thresholdLevel = std::clamp(o.value(QStringLiteral("level")).toInt(128), 1, 255); break;
    }
    return s.finalized();
}

} // namespace Adjust
