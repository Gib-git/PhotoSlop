#include "core/ColorModes.h"

#include <algorithm>
#include <cmath>

namespace ColorModes {

QString shortName(ColorMode mode)
{
    switch (mode) {
    case ColorMode::Grayscale: return QStringLiteral("Gray");
    case ColorMode::CMYK: return QStringLiteral("CMYK");
    case ColorMode::Lab: return QStringLiteral("Lab");
    default: return QStringLiteral("RGB");
    }
}

QString menuName(ColorMode mode)
{
    switch (mode) {
    case ColorMode::Grayscale: return QStringLiteral("Grayscale");
    case ColorMode::CMYK: return QStringLiteral("CMYK Color");
    case ColorMode::Lab: return QStringLiteral("Lab Color");
    default: return QStringLiteral("RGB Color");
    }
}

QStringList channelNames(ColorMode mode)
{
    switch (mode) {
    case ColorMode::Grayscale: return {QStringLiteral("Gray")};
    case ColorMode::CMYK:
        return {QStringLiteral("CMYK"), QStringLiteral("Cyan"), QStringLiteral("Magenta"), QStringLiteral("Yellow"),
                QStringLiteral("Black")};
    case ColorMode::Lab: return {QStringLiteral("Lab"), QStringLiteral("Lightness"), QStringLiteral("a"), QStringLiteral("b")};
    default: return {QStringLiteral("RGB"), QStringLiteral("Red"), QStringLiteral("Green"), QStringLiteral("Blue")};
    }
}

QString id(ColorMode mode)
{
    switch (mode) {
    case ColorMode::Grayscale: return QStringLiteral("gray");
    case ColorMode::CMYK: return QStringLiteral("cmyk");
    case ColorMode::Lab: return QStringLiteral("lab");
    default: return QStringLiteral("rgb");
    }
}

ColorMode fromId(const QString& id)
{
    if (id == QLatin1String("gray")) return ColorMode::Grayscale;
    if (id == QLatin1String("cmyk")) return ColorMode::CMYK;
    if (id == QLatin1String("lab")) return ColorMode::Lab;
    return ColorMode::RGB;
}

int gray(int r, int g, int b) { return (r * 77 + g * 151 + b * 28 + 128) >> 8; }

void grayscaleInPlace(QImage& image, const QRect& area)
{
    const QRect r = area.isNull() ? image.rect() : (area & image.rect());
    for (int y = r.top(); y <= r.bottom(); ++y) {
        auto* row = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = r.left(); x <= r.right(); ++x) {
            const QRgb p = row[x];
            // Premultiplied channels stay premultiplied: the grey of premultiplied values is
            // the premultiplied grey.
            const int v = gray(qRed(p), qGreen(p), qBlue(p));
            row[x] = qRgba(v, v, v, qAlpha(p));
        }
    }
}

std::array<double, 4> rgbToCmyk(int r, int g, int b)
{
    const double rf = r / 255.0, gf = g / 255.0, bf = b / 255.0;
    const double k = 1.0 - std::max({rf, gf, bf});
    if (k >= 1.0 - 1e-9) return {0.0, 0.0, 0.0, 1.0};
    return {(1.0 - rf - k) / (1.0 - k), (1.0 - gf - k) / (1.0 - k), (1.0 - bf - k) / (1.0 - k), k};
}

QRgb cmykToRgb(double c, double m, double y, double k, int alpha)
{
    auto ch = [k](double v) { return std::clamp(int(std::lround(255.0 * (1.0 - v) * (1.0 - k))), 0, 255); };
    return qRgba(ch(c), ch(m), ch(y), alpha);
}

double srgbToLinear(double v)
{
    return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
}

double linearToSrgb(double v)
{
    if (v <= 0.0) return 0.0;
    if (v >= 1.0) return 1.0;
    return v <= 0.0031308 ? v * 12.92 : 1.055 * std::pow(v, 1.0 / 2.4) - 0.055;
}

namespace {
// D50 reference white.
constexpr double Xn = 0.96422, Yn = 1.0, Zn = 0.82521;
constexpr double kEps = 216.0 / 24389.0, kKappa = 24389.0 / 27.0;

double labF(double t) { return t > kEps ? std::cbrt(t) : (kKappa * t + 16.0) / 116.0; }
double labFInv(double f)
{
    const double t = f * f * f;
    return t > kEps ? t : (116.0 * f - 16.0) / kKappa;
}
} // namespace

std::array<double, 3> rgbToLab(int r, int g, int b)
{
    const double R = srgbToLinear(r / 255.0), G = srgbToLinear(g / 255.0), B = srgbToLinear(b / 255.0);
    // sRGB to XYZ, Bradford-adapted to D50.
    const double X = 0.4360747 * R + 0.3850649 * G + 0.1430804 * B;
    const double Y = 0.2225045 * R + 0.7168786 * G + 0.0606169 * B;
    const double Z = 0.0139322 * R + 0.0971045 * G + 0.7141733 * B;
    const double fx = labF(X / Xn), fy = labF(Y / Yn), fz = labF(Z / Zn);
    return {116.0 * fy - 16.0, 500.0 * (fx - fy), 200.0 * (fy - fz)};
}

QRgb labToRgb(double L, double a, double b, int alpha)
{
    const double fy = (L + 16.0) / 116.0;
    const double fx = fy + a / 500.0, fz = fy - b / 200.0;
    const double X = labFInv(fx) * Xn, Y = (L > kKappa * kEps ? fy * fy * fy : L / kKappa) * Yn, Z = labFInv(fz) * Zn;
    const double R = 3.1338561 * X - 1.6168667 * Y - 0.4906146 * Z;
    const double G = -0.9787684 * X + 1.9161415 * Y + 0.0334540 * Z;
    const double B = 0.0719453 * X - 0.2289914 * Y + 1.4052427 * Z;
    auto ch = [](double v) { return std::clamp(int(std::lround(linearToSrgb(v) * 255.0)), 0, 255); };
    return qRgba(ch(R), ch(G), ch(B), alpha);
}

int channelCount(ColorMode mode)
{
    switch (mode) {
    case ColorMode::Grayscale: return 1;
    case ColorMode::CMYK: return 4;
    default: return 3;
    }
}

void toChannels(ColorMode mode, QRgb c, int* out)
{
    const int r = qRed(c), g = qGreen(c), b = qBlue(c);
    switch (mode) {
    case ColorMode::Grayscale: out[0] = gray(r, g, b); break;
    case ColorMode::CMYK: {
        const auto k = rgbToCmyk(r, g, b);
        for (int i = 0; i < 4; ++i) out[i] = int(std::lround(k[i] * 255.0));
        break;
    }
    case ColorMode::Lab: {
        const auto l = rgbToLab(r, g, b);
        out[0] = std::clamp(int(std::lround(l[0] * 255.0 / 100.0)), 0, 255);
        out[1] = std::clamp(int(std::lround(l[1])) + 128, 0, 255);
        out[2] = std::clamp(int(std::lround(l[2])) + 128, 0, 255);
        break;
    }
    default:
        out[0] = r;
        out[1] = g;
        out[2] = b;
        break;
    }
}

} // namespace ColorModes
