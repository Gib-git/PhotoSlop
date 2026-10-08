#pragma once

#include <QList>
#include <QString>
#include <cstdint>

// Layer / paint blend modes, in the same order and grouping Photoshop uses.
enum class BlendMode : int {
    Normal,
    Dissolve,
    Darken,
    Multiply,
    ColorBurn,
    LinearBurn,
    DarkerColor,
    Lighten,
    Screen,
    ColorDodge,
    LinearDodge,
    LighterColor,
    Overlay,
    SoftLight,
    HardLight,
    VividLight,
    LinearLight,
    PinLight,
    HardMix,
    Difference,
    Exclusion,
    Subtract,
    Divide,
    Hue,
    Saturation,
    Color,
    Luminosity,
    PassThrough, // groups only: children blend straight into what is below
    Count
};

namespace Blend {

QString name(BlendMode mode);
QString id(BlendMode mode);
BlendMode fromId(const QString& id);

// Modes grouped as Photoshop separates them in its blend-mode menus.
QList<QList<BlendMode>> groups();

// Composites `count` premultiplied ARGB32 pixels of `src` onto `dst`.
// `opacity` is 0..1. `mask` (optional) is an 8-bit coverage per pixel.
// `x0`/`y` give the canvas position of the row (used by Dissolve's noise).
void compositeRow(uint32_t* dst, const uint32_t* src, int count, BlendMode mode,
                  float opacity, const uint8_t* mask = nullptr, int x0 = 0, int y = 0);

// Same as compositeRow, but keeps the destination alpha (lock transparency).
void compositeRowPreserveAlpha(uint32_t* dst, const uint32_t* src, int count, BlendMode mode,
                               float opacity, const uint8_t* mask = nullptr, int x0 = 0,
                               int y = 0);

// Multiplies every channel of a premultiplied pixel by a/255.
inline uint32_t byteMul(uint32_t x, uint32_t a)
{
    uint32_t t = (x & 0xff00ff) * a;
    t = (t + ((t >> 8) & 0xff00ff) + 0x800080) >> 8;
    t &= 0xff00ff;
    x = ((x >> 8) & 0xff00ff) * a;
    x = (x + ((x >> 8) & 0xff00ff) + 0x800080);
    x &= 0xff00ff00;
    return x | t;
}

} // namespace Blend
