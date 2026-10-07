#pragma once

#include "core/Filters.h"

#include <QList>
#include <QPointF>
#include <array>
#include <functional>

// Image > Adjustments: colour corrections as pixel maps, plus the histograms their dialogs show.
namespace Adjust {

using Lut = std::array<uchar, 256>;
Lut identityLut();

// Changes `count` straight-alpha (unpremultiplied) pixels in place.
using PixelMap = std::function<void(QRgb* pixels, int count)>;
// A spec that runs `map` over every pixel. Adjustments never read neighbours or spread.
Filters::Spec spec(const QString& name, PixelMap map);

int luminosity(int r, int g, int b);

// ---- Histogram ----
struct Histogram {
    // Index 0 is the luminosity (Photoshop's "RGB" view), then red, green and blue.
    std::array<std::array<quint32, 256>, 4> channels{};
    quint64 count = 0;
    double mean(int channel) const;
};
// Counts visible pixels of a premultiplied image that are at least half selected
// (`coverage` is a same-size Grayscale8 mask, null = everything).
Histogram histogram(const QImage& image, const QImage& coverage = QImage());

// ---- Levels ----
struct LevelsChannel {
    int inBlack = 0;
    double gamma = 1.0;
    int inWhite = 255;
    int outBlack = 0;
    int outWhite = 255;
    bool operator==(const LevelsChannel&) const = default;
};
struct Levels {
    std::array<LevelsChannel, 4> channels; // 0 = RGB (applied after the colour channels), then R, G, B
    bool operator==(const Levels&) const = default;
};
Lut levelsLut(const LevelsChannel& c);
PixelMap levelsMap(const Levels& levels);

// Image > Auto Tone (per-channel stretch), Auto Contrast (one stretch for all channels) and
// Auto Color (per-channel stretch with neutral midtones). `clip` is the fraction of pixels
// allowed to clip at each end.
enum class AutoMode { Tone, Contrast, Color };
Levels autoLevels(const Histogram& h, AutoMode mode, double clip = 0.001);

// ---- Curves ----
using CurvePoints = QList<QPointF>; // 0..255 on both axes, sorted by x
CurvePoints identityCurve();
struct Curves {
    std::array<CurvePoints, 4> channels{identityCurve(), identityCurve(), identityCurve(), identityCurve()};
};
// Smooth (natural cubic spline) curve through the points, flat beyond the end points.
Lut curveLut(const CurvePoints& points);
PixelMap curvesMap(const Curves& curves);

// ---- Simple adjustments ----
// brightness -150..150, contrast -50..100 (legacy: -100..100).
PixelMap brightnessContrastMap(int brightness, int contrast, bool legacy);
PixelMap invertMap();
PixelMap desaturateMap();
PixelMap posterizeMap(int levels);
PixelMap thresholdMap(int level);

// ---- Hue/Saturation ----
struct HueSaturation {
    struct Range {
        int hue = 0;        // -180..180 (Colorize: 0..360)
        int saturation = 0; // -100..100 (Colorize: 0..100)
        int lightness = 0;  // -100..100
        bool operator==(const Range&) const = default;
    };
    // 0 = Master, then Reds, Yellows, Greens, Cyans, Blues and Magentas.
    std::array<Range, 7> ranges{};
    bool colorize = false; // tints everything with ranges[0]
};
PixelMap hueSaturationMap(const HueSaturation& hs);

// ---- Color Balance ----
struct ColorBalance {
    // [tone][axis]: tones are shadows, midtones and highlights; axes are cyan–red,
    // magenta–green and yellow–blue, each -100..100.
    std::array<std::array<int, 3>, 3> values{};
    bool preserveLuminosity = true;
};
PixelMap colorBalanceMap(const ColorBalance& cb);

// ---- Black & White ----
struct BlackWhite {
    std::array<int, 6> weights{40, 60, 40, 60, 20, 80}; // reds, yellows, greens, cyans, blues, magentas (%)
    bool tint = false;
    int tintHue = 42;
    int tintSaturation = 20;
};
PixelMap blackWhiteMap(const BlackWhite& bw);

// ---- HSL helpers (h in degrees, s and l in 0..1) ----
void rgbToHsl(int r, int g, int b, double& h, double& s, double& l);
QRgb hslToRgb(double h, double s, double l, int alpha = 255);

} // namespace Adjust
