#include "core/BlendMode.h"

#include <QHash>
#include <algorithm>
#include <cmath>
#include <iterator>

namespace Blend {

namespace {

struct ModeInfo {
    BlendMode mode;
    const char* id;
    const char* name;
};

const ModeInfo kModes[] = {
    {BlendMode::Normal, "normal", "Normal"},
    {BlendMode::Dissolve, "dissolve", "Dissolve"},
    {BlendMode::Darken, "darken", "Darken"},
    {BlendMode::Multiply, "multiply", "Multiply"},
    {BlendMode::ColorBurn, "colorBurn", "Color Burn"},
    {BlendMode::LinearBurn, "linearBurn", "Linear Burn"},
    {BlendMode::DarkerColor, "darkerColor", "Darker Color"},
    {BlendMode::Lighten, "lighten", "Lighten"},
    {BlendMode::Screen, "screen", "Screen"},
    {BlendMode::ColorDodge, "colorDodge", "Color Dodge"},
    {BlendMode::LinearDodge, "linearDodge", "Linear Dodge (Add)"},
    {BlendMode::LighterColor, "lighterColor", "Lighter Color"},
    {BlendMode::Overlay, "overlay", "Overlay"},
    {BlendMode::SoftLight, "softLight", "Soft Light"},
    {BlendMode::HardLight, "hardLight", "Hard Light"},
    {BlendMode::VividLight, "vividLight", "Vivid Light"},
    {BlendMode::LinearLight, "linearLight", "Linear Light"},
    {BlendMode::PinLight, "pinLight", "Pin Light"},
    {BlendMode::HardMix, "hardMix", "Hard Mix"},
    {BlendMode::Difference, "difference", "Difference"},
    {BlendMode::Exclusion, "exclusion", "Exclusion"},
    {BlendMode::Subtract, "subtract", "Subtract"},
    {BlendMode::Divide, "divide", "Divide"},
    {BlendMode::Hue, "hue", "Hue"},
    {BlendMode::Saturation, "saturation", "Saturation"},
    {BlendMode::Color, "color", "Color"},
    {BlendMode::Luminosity, "luminosity", "Luminosity"},
};

inline float clamp01(float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }

// ---- Separable blend functions B(Cb, Cs) on unpremultiplied 0..1 values ----
struct FMultiply { static float f(float b, float s) { return b * s; } };
struct FScreen { static float f(float b, float s) { return b + s - b * s; } };
struct FDarken { static float f(float b, float s) { return std::min(b, s); } };
struct FLighten { static float f(float b, float s) { return std::max(b, s); } };
struct FColorDodge {
    static float f(float b, float s)
    {
        if (b <= 0.f) return 0.f;
        if (s >= 1.f) return 1.f;
        return std::min(1.f, b / (1.f - s));
    }
};
struct FColorBurn {
    static float f(float b, float s)
    {
        if (b >= 1.f) return 1.f;
        if (s <= 0.f) return 0.f;
        return 1.f - std::min(1.f, (1.f - b) / s);
    }
};
struct FHardLight {
    static float f(float b, float s)
    {
        return s <= 0.5f ? b * 2.f * s : FScreen::f(b, 2.f * s - 1.f);
    }
};
struct FOverlay { static float f(float b, float s) { return FHardLight::f(s, b); } };
struct FSoftLight {
    // Photoshop's soft light formula.
    static float f(float b, float s)
    {
        if (s <= 0.5f) return 2.f * b * s + b * b * (1.f - 2.f * s);
        return 2.f * b * (1.f - s) + std::sqrt(b) * (2.f * s - 1.f);
    }
};
struct FDifference { static float f(float b, float s) { return std::fabs(b - s); } };
struct FExclusion { static float f(float b, float s) { return b + s - 2.f * b * s; } };
struct FLinearBurn { static float f(float b, float s) { return clamp01(b + s - 1.f); } };
struct FLinearDodge { static float f(float b, float s) { return std::min(1.f, b + s); } };
struct FVividLight {
    static float f(float b, float s)
    {
        return s <= 0.5f ? FColorBurn::f(b, 2.f * s) : FColorDodge::f(b, 2.f * s - 1.f);
    }
};
struct FLinearLight { static float f(float b, float s) { return clamp01(b + 2.f * s - 1.f); } };
struct FPinLight {
    static float f(float b, float s)
    {
        return s <= 0.5f ? std::min(b, 2.f * s) : std::max(b, 2.f * s - 1.f);
    }
};
struct FHardMix { static float f(float b, float s) { return (b + s >= 1.f) ? 1.f : 0.f; } };
struct FSubtract { static float f(float b, float s) { return std::max(0.f, b - s); } };
struct FDivide {
    static float f(float b, float s)
    {
        if (s <= 0.f) return b <= 0.f ? 0.f : 1.f;
        return std::min(1.f, b / s);
    }
};

// ---- Non-separable helpers (W3C compositing spec) ----
inline float lum(float r, float g, float b) { return 0.3f * r + 0.59f * g + 0.11f * b; }

inline void clipColor(float& r, float& g, float& b)
{
    float l = lum(r, g, b);
    float n = std::min({r, g, b});
    float x = std::max({r, g, b});
    if (n < 0.f) {
        float d = l - n;
        if (d > 1e-6f) {
            r = l + (r - l) * l / d;
            g = l + (g - l) * l / d;
            b = l + (b - l) * l / d;
        }
    }
    if (x > 1.f) {
        float d = x - l;
        if (d > 1e-6f) {
            r = l + (r - l) * (1.f - l) / d;
            g = l + (g - l) * (1.f - l) / d;
            b = l + (b - l) * (1.f - l) / d;
        }
    }
}

inline void setLum(float& r, float& g, float& b, float l)
{
    float d = l - lum(r, g, b);
    r += d;
    g += d;
    b += d;
    clipColor(r, g, b);
}

inline float sat(float r, float g, float b) { return std::max({r, g, b}) - std::min({r, g, b}); }

inline void setSat(float& r, float& g, float& b, float s)
{
    float* c[3] = {&r, &g, &b};
    std::sort(c, c + 3, [](float* a, float* bb) { return *a < *bb; });
    float& cmin = *c[0];
    float& cmid = *c[1];
    float& cmax = *c[2];
    if (cmax > cmin) {
        cmid = (cmid - cmin) * s / (cmax - cmin);
        cmax = s;
    } else {
        cmid = cmax = 0.f;
    }
    cmin = 0.f;
}

inline uint32_t dissolveHash(int x, int y)
{
    uint32_t h = uint32_t(x) * 374761393u + uint32_t(y) * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return (h ^ (h >> 16)) & 0xff;
}

// Effective source alpha scale (0..255) for a pixel given opacity and mask.
inline uint32_t coverage(uint32_t op255, const uint8_t* mask, int i)
{
    if (!mask) return op255;
    return (op255 * mask[i] + 127) / 255;
}

void normalRow(uint32_t* dst, const uint32_t* src, int count, uint32_t op255, const uint8_t* mask)
{
    for (int i = 0; i < count; ++i) {
        uint32_t s = src[i];
        if (!s) continue;
        uint32_t c = coverage(op255, mask, i);
        if (!c) continue;
        if (c != 255) s = byteMul(s, c);
        uint32_t sa = s >> 24;
        if (sa == 255)
            dst[i] = s;
        else
            dst[i] = s + byteMul(dst[i], 255 - sa);
    }
}

inline void unpack(uint32_t p, float& r, float& g, float& b, float& a)
{
    a = float(p >> 24) / 255.f;
    r = float((p >> 16) & 0xff) / 255.f;
    g = float((p >> 8) & 0xff) / 255.f;
    b = float(p & 0xff) / 255.f;
}

inline uint32_t pack(float r, float g, float b, float a)
{
    auto q = [](float v) { return uint32_t(clamp01(v) * 255.f + 0.5f); };
    uint32_t A = q(a);
    uint32_t R = std::min(q(r), A), G = std::min(q(g), A), B = std::min(q(b), A);
    return (A << 24) | (R << 16) | (G << 8) | B;
}

// Generic composite: co = cs*(1-ab) + cb*(1-as) + as*ab*B(Cb,Cs)
template <typename BlendFn>
void genericRow(uint32_t* dst, const uint32_t* src, int count, uint32_t op255,
                const uint8_t* mask, BlendFn blendFn)
{
    for (int i = 0; i < count; ++i) {
        uint32_t s = src[i];
        if (!s) continue;
        uint32_t c = coverage(op255, mask, i);
        if (!c) continue;
        if (c != 255) s = byteMul(s, c);
        uint32_t d = dst[i];
        if (!(d >> 24)) {
            dst[i] = s;
            continue;
        }
        float sr, sg, sb, sa, dr, dg, db, da;
        unpack(s, sr, sg, sb, sa);
        unpack(d, dr, dg, db, da);
        // Unpremultiply
        float Sr = sr / sa, Sg = sg / sa, Sb = sb / sa;
        float Dr = dr / da, Dg = dg / da, Db = db / da;
        float Br, Bg, Bb;
        blendFn(Dr, Dg, Db, Sr, Sg, Sb, Br, Bg, Bb);
        float both = sa * da;
        float r = sr * (1.f - da) + dr * (1.f - sa) + both * Br;
        float g = sg * (1.f - da) + dg * (1.f - sa) + both * Bg;
        float b = sb * (1.f - da) + db * (1.f - sa) + both * Bb;
        float a = sa + da * (1.f - sa);
        dst[i] = pack(r, g, b, a);
    }
}

template <typename F>
void separableRow(uint32_t* dst, const uint32_t* src, int count, uint32_t op255, const uint8_t* mask)
{
    genericRow(dst, src, count, op255, mask,
               [](float Dr, float Dg, float Db, float Sr, float Sg, float Sb, float& r, float& g,
                  float& b) {
                   r = F::f(Dr, Sr);
                   g = F::f(Dg, Sg);
                   b = F::f(Db, Sb);
               });
}

} // namespace

QString name(BlendMode mode)
{
    for (const auto& m : kModes)
        if (m.mode == mode) return QString::fromLatin1(m.name);
    return QStringLiteral("Normal");
}

QString id(BlendMode mode)
{
    for (const auto& m : kModes)
        if (m.mode == mode) return QString::fromLatin1(m.id);
    return QStringLiteral("normal");
}

BlendMode fromId(const QString& id)
{
    for (const auto& m : kModes)
        if (id == QLatin1String(m.id)) return m.mode;
    return BlendMode::Normal;
}

QList<QList<BlendMode>> groups()
{
    using B = BlendMode;
    return {
        {B::Normal, B::Dissolve},
        {B::Darken, B::Multiply, B::ColorBurn, B::LinearBurn, B::DarkerColor},
        {B::Lighten, B::Screen, B::ColorDodge, B::LinearDodge, B::LighterColor},
        {B::Overlay, B::SoftLight, B::HardLight, B::VividLight, B::LinearLight, B::PinLight,
         B::HardMix},
        {B::Difference, B::Exclusion, B::Subtract, B::Divide},
        {B::Hue, B::Saturation, B::Color, B::Luminosity},
    };
}

void compositeRow(uint32_t* dst, const uint32_t* src, int count, BlendMode mode, float opacity,
                  const uint8_t* mask, int x0, int y)
{
    uint32_t op255 = uint32_t(clamp01(opacity) * 255.f + 0.5f);
    if (op255 == 0 || count <= 0) return;

    switch (mode) {
    case BlendMode::Normal:
        normalRow(dst, src, count, op255, mask);
        return;
    case BlendMode::Dissolve:
        for (int i = 0; i < count; ++i) {
            uint32_t s = src[i];
            uint32_t sa = s >> 24;
            if (!sa) continue;
            uint32_t c = (coverage(op255, mask, i) * sa + 127) / 255;
            if (dissolveHash(x0 + i, y) >= c) continue;
            // Draw the pixel fully opaque (unpremultiplied color).
            float r, g, b, a;
            unpack(s, r, g, b, a);
            dst[i] = pack(r / a, g / a, b / a, 1.f);
        }
        return;
    case BlendMode::Multiply: separableRow<FMultiply>(dst, src, count, op255, mask); return;
    case BlendMode::Screen: separableRow<FScreen>(dst, src, count, op255, mask); return;
    case BlendMode::Darken: separableRow<FDarken>(dst, src, count, op255, mask); return;
    case BlendMode::Lighten: separableRow<FLighten>(dst, src, count, op255, mask); return;
    case BlendMode::ColorBurn: separableRow<FColorBurn>(dst, src, count, op255, mask); return;
    case BlendMode::ColorDodge: separableRow<FColorDodge>(dst, src, count, op255, mask); return;
    case BlendMode::LinearBurn: separableRow<FLinearBurn>(dst, src, count, op255, mask); return;
    case BlendMode::LinearDodge: separableRow<FLinearDodge>(dst, src, count, op255, mask); return;
    case BlendMode::Overlay: separableRow<FOverlay>(dst, src, count, op255, mask); return;
    case BlendMode::SoftLight: separableRow<FSoftLight>(dst, src, count, op255, mask); return;
    case BlendMode::HardLight: separableRow<FHardLight>(dst, src, count, op255, mask); return;
    case BlendMode::VividLight: separableRow<FVividLight>(dst, src, count, op255, mask); return;
    case BlendMode::LinearLight: separableRow<FLinearLight>(dst, src, count, op255, mask); return;
    case BlendMode::PinLight: separableRow<FPinLight>(dst, src, count, op255, mask); return;
    case BlendMode::HardMix: separableRow<FHardMix>(dst, src, count, op255, mask); return;
    case BlendMode::Difference: separableRow<FDifference>(dst, src, count, op255, mask); return;
    case BlendMode::Exclusion: separableRow<FExclusion>(dst, src, count, op255, mask); return;
    case BlendMode::Subtract: separableRow<FSubtract>(dst, src, count, op255, mask); return;
    case BlendMode::Divide: separableRow<FDivide>(dst, src, count, op255, mask); return;
    case BlendMode::DarkerColor:
        genericRow(dst, src, count, op255, mask,
                   [](float Dr, float Dg, float Db, float Sr, float Sg, float Sb, float& r,
                      float& g, float& b) {
                       bool useS = lum(Sr, Sg, Sb) < lum(Dr, Dg, Db);
                       r = useS ? Sr : Dr;
                       g = useS ? Sg : Dg;
                       b = useS ? Sb : Db;
                   });
        return;
    case BlendMode::LighterColor:
        genericRow(dst, src, count, op255, mask,
                   [](float Dr, float Dg, float Db, float Sr, float Sg, float Sb, float& r,
                      float& g, float& b) {
                       bool useS = lum(Sr, Sg, Sb) > lum(Dr, Dg, Db);
                       r = useS ? Sr : Dr;
                       g = useS ? Sg : Dg;
                       b = useS ? Sb : Db;
                   });
        return;
    case BlendMode::Hue:
        genericRow(dst, src, count, op255, mask,
                   [](float Dr, float Dg, float Db, float Sr, float Sg, float Sb, float& r,
                      float& g, float& b) {
                       r = Sr, g = Sg, b = Sb;
                       setSat(r, g, b, sat(Dr, Dg, Db));
                       setLum(r, g, b, lum(Dr, Dg, Db));
                   });
        return;
    case BlendMode::Saturation:
        genericRow(dst, src, count, op255, mask,
                   [](float Dr, float Dg, float Db, float Sr, float Sg, float Sb, float& r,
                      float& g, float& b) {
                       r = Dr, g = Dg, b = Db;
                       setSat(r, g, b, sat(Sr, Sg, Sb));
                       setLum(r, g, b, lum(Dr, Dg, Db));
                   });
        return;
    case BlendMode::Color:
        genericRow(dst, src, count, op255, mask,
                   [](float Dr, float Dg, float Db, float Sr, float Sg, float Sb, float& r,
                      float& g, float& b) {
                       r = Sr, g = Sg, b = Sb;
                       setLum(r, g, b, lum(Dr, Dg, Db));
                   });
        return;
    case BlendMode::Luminosity:
        genericRow(dst, src, count, op255, mask,
                   [](float Dr, float Dg, float Db, float Sr, float Sg, float Sb, float& r,
                      float& g, float& b) {
                       r = Dr, g = Dg, b = Db;
                       setLum(r, g, b, lum(Sr, Sg, Sb));
                   });
        return;
    case BlendMode::Count:
        return;
    }
}

void compositeRowPreserveAlpha(uint32_t* dst, const uint32_t* src, int count, BlendMode mode,
                               float opacity, const uint8_t* mask, int x0, int y)
{
    // Composite onto an opaque copy of the destination, then reapply the original alpha.
    constexpr int kChunk = 512;
    uint32_t tmp[kChunk];
    for (int start = 0; start < count; start += kChunk) {
        int n = std::min(kChunk, count - start);
        for (int i = 0; i < n; ++i) {
            uint32_t d = dst[start + i];
            uint32_t a = d >> 24;
            if (a == 0) {
                tmp[i] = 0xff000000;
            } else if (a == 255) {
                tmp[i] = d;
            } else {
                uint32_t r = std::min(255u, ((d >> 16) & 0xff) * 255 / a);
                uint32_t g = std::min(255u, ((d >> 8) & 0xff) * 255 / a);
                uint32_t b = std::min(255u, (d & 0xff) * 255 / a);
                tmp[i] = 0xff000000 | (r << 16) | (g << 8) | b;
            }
        }
        compositeRow(tmp, src + start, n, mode, opacity, mask ? mask + start : nullptr,
                     x0 + start, y);
        for (int i = 0; i < n; ++i) {
            uint32_t a = dst[start + i] >> 24;
            if (a == 0) continue;
            uint32_t t = tmp[i] | 0xff000000;
            dst[start + i] = a == 255 ? t : ((byteMul(t, a) & 0x00ffffff) | (a << 24));
        }
    }
}

} // namespace Blend
