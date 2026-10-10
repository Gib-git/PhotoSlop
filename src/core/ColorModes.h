#pragma once

#include <QImage>
#include <QRgb>
#include <QString>
#include <QStringList>
#include <array>

// Image > Mode: the colour mode a document is edited in. Pixels are always stored as 8-bit
// RGB; the mode decides what colours the document can hold (Grayscale), how colour values are
// read out (Info, Channels) and how files are written (PSD channels).
enum class ColorMode : int { RGB, Grayscale, CMYK, Lab };

namespace ColorModes {

// "RGB", "Gray", "CMYK", "Lab": the short name in document titles ("RGB/8").
QString shortName(ColorMode mode);
// "RGB Color", "Grayscale"...: the Image > Mode menu name.
QString menuName(ColorMode mode);
// The composite channel first, then each component ("RGB", "Red", "Green", "Blue").
QStringList channelNames(ColorMode mode);
QString id(ColorMode mode);
ColorMode fromId(const QString& id);

// The grey a colour becomes in Grayscale mode (Photoshop-like luminosity weights).
int gray(int r, int g, int b);
inline QRgb toGray(QRgb c)
{
    const int v = gray(qRed(c), qGreen(c), qBlue(c));
    return qRgba(v, v, v, qAlpha(c));
}
// Converts a premultiplied ARGB32 image area to grey in place.
void grayscaleInPlace(QImage& image, const QRect& area = QRect());

// ---- CMYK (device CMYK without a profile: the conversion round-trips exactly) ----
// Ink amounts 0..1.
std::array<double, 4> rgbToCmyk(int r, int g, int b);
QRgb cmykToRgb(double c, double m, double y, double k, int alpha = 255);

// ---- CIE L*a*b* (D50, as Photoshop uses) from and to sRGB ----
// L 0..100, a and b about -128..127.
std::array<double, 3> rgbToLab(int r, int g, int b);
QRgb labToRgb(double L, double a, double b, int alpha = 255);

// sRGB transfer function, linear light 0..1 to an encoded value 0..1 and back.
double linearToSrgb(double v);
double srgbToLinear(double v);

// Channel values 0..255 of a straight (unpremultiplied) colour as the mode stores them,
// in channelNames() order without the composite: RGB 3, Gray 1, CMYK 4 (ink), Lab 3
// (L scaled to 0..255, a and b offset by 128).
int channelCount(ColorMode mode);
void toChannels(ColorMode mode, QRgb straight, int* out);

} // namespace ColorModes
