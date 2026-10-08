#include "core/VectorLayers.h"

#include "core/Layer.h"

#include <QBuffer>
#include <QDataStream>
#include <QGuiApplication>
#include <QFontMetricsF>
#include <QJsonArray>
#include <QJsonValue>
#include <QPainter>
#include <QtMath>
#include <cmath>
#include <algorithm>

// ---------------- TextData ----------------

QFont TextData::font() const
{
    QFont f(family.isEmpty() ? QGuiApplication::font().family() : family);
    f.setPixelSize(std::max(1, size));
    f.setBold(bold);
    f.setItalic(italic);
    f.setUnderline(underline);
    f.setStyleStrategy(antialias ? QFont::PreferAntialias : QFont::NoAntialias);
    f.setHintingPreference(QFont::PreferNoHinting);
    return f;
}

double TextData::lineHeight() const
{
    // Photoshop's Auto leading is 120% of the type size.
    return std::max(1.0, size * 1.2);
}

QStringList TextData::lines() const { return text.split(QLatin1Char('\n')); }

namespace {

double lineX(const TextData& t, double width)
{
    switch (t.align) {
    case TextData::Align::Left: return t.position.x();
    case TextData::Align::Center: return t.position.x() - width / 2.0;
    case TextData::Align::Right: return t.position.x() - width;
    }
    return t.position.x();
}

} // namespace

QRectF TextData::localBounds() const
{
    const QFontMetricsF fm(font());
    const QStringList ls = lines();
    QRectF r;
    for (int i = 0; i < ls.size(); ++i) {
        const double w = std::max(1.0, fm.horizontalAdvance(ls[i]));
        const double base = position.y() + i * lineHeight();
        // Generous side bearings cover italic overhangs and antialiasing.
        const double pad = size * 0.25 + 2;
        r |= QRectF(lineX(*this, w) - pad, base - fm.ascent() - 2, w + 2 * pad, fm.ascent() + fm.descent() + 4);
    }
    return r;
}

QPolygonF TextData::canvasBounds() const { return transform.map(QPolygonF(localBounds())); }

QLineF TextData::caretLine(int index) const
{
    const QFontMetricsF fm(font());
    const QStringList ls = lines();
    index = std::clamp(index, 0, int(text.size()));
    int line = 0, start = 0;
    for (; line < ls.size() - 1; ++line) {
        if (index <= start + ls[line].size()) break;
        start += int(ls[line].size()) + 1;
    }
    const QString& s = ls[line];
    const double x = lineX(*this, fm.horizontalAdvance(s)) + fm.horizontalAdvance(s.left(index - start));
    const double base = position.y() + line * lineHeight();
    return transform.map(QLineF(x, base - fm.ascent(), x, base + fm.descent()));
}

int TextData::indexAt(const QPointF& canvasPt) const
{
    bool ok = false;
    const QPointF p = transform.inverted(&ok).map(canvasPt);
    const QFontMetricsF fm(font());
    const QStringList ls = lines();
    const int line = std::clamp(int(std::floor((p.y() - position.y() + fm.ascent()) / lineHeight())), 0, int(ls.size()) - 1);
    int start = 0;
    for (int i = 0; i < line; ++i) start += int(ls[i].size()) + 1;
    const QString& s = ls[line];
    const double x0 = lineX(*this, fm.horizontalAdvance(s));
    int best = 0;
    double bestDist = 1e18;
    for (int i = 0; i <= s.size(); ++i) {
        const double d = std::fabs(x0 + fm.horizontalAdvance(s.left(i)) - p.x());
        if (d < bestDist) {
            bestDist = d;
            best = i;
        }
    }
    return start + best;
}

static QJsonArray transformToJson(const QTransform& t)
{
    return QJsonArray{t.m11(), t.m12(), t.m13(), t.m21(), t.m22(), t.m23(), t.m31(), t.m32(), t.m33()};
}

static QTransform transformFromJson(const QJsonValue& v)
{
    const QJsonArray a = v.toArray();
    if (a.size() != 9) return QTransform();
    return QTransform(a[0].toDouble(), a[1].toDouble(), a[2].toDouble(), a[3].toDouble(), a[4].toDouble(),
                      a[5].toDouble(), a[6].toDouble(), a[7].toDouble(), a[8].toDouble());
}

QJsonObject TextData::toJson() const
{
    return QJsonObject{
        {QStringLiteral("text"), text},
        {QStringLiteral("family"), family},
        {QStringLiteral("size"), size},
        {QStringLiteral("color"), color.name(QColor::HexArgb)},
        {QStringLiteral("bold"), bold},
        {QStringLiteral("italic"), italic},
        {QStringLiteral("underline"), underline},
        {QStringLiteral("align"), int(align)},
        {QStringLiteral("antialias"), antialias},
        {QStringLiteral("x"), position.x()},
        {QStringLiteral("y"), position.y()},
        {QStringLiteral("transform"), transformToJson(transform)},
    };
}

TextData TextData::fromJson(const QJsonObject& o)
{
    TextData t;
    t.text = o.value(QStringLiteral("text")).toString();
    t.family = o.value(QStringLiteral("family")).toString();
    t.size = o.value(QStringLiteral("size")).toInt(48);
    t.color = QColor(o.value(QStringLiteral("color")).toString());
    if (!t.color.isValid()) t.color = Qt::black;
    t.bold = o.value(QStringLiteral("bold")).toBool();
    t.italic = o.value(QStringLiteral("italic")).toBool();
    t.underline = o.value(QStringLiteral("underline")).toBool();
    t.align = Align(std::clamp(o.value(QStringLiteral("align")).toInt(), 0, 2));
    t.antialias = o.value(QStringLiteral("antialias")).toBool(true);
    t.position = QPointF(o.value(QStringLiteral("x")).toDouble(), o.value(QStringLiteral("y")).toDouble());
    t.transform = transformFromJson(o.value(QStringLiteral("transform")));
    return t;
}

// ---------------- ShapeData ----------------

QJsonObject ShapeData::toJson() const
{
    QByteArray bytes;
    {
        QDataStream out(&bytes, QIODevice::WriteOnly);
        out.setVersion(QDataStream::Qt_6_0);
        out << path;
    }
    return QJsonObject{
        {QStringLiteral("path"), QString::fromLatin1(bytes.toBase64())},
        {QStringLiteral("fill"), fillEnabled},
        {QStringLiteral("fillColor"), fillColor.name(QColor::HexArgb)},
        {QStringLiteral("stroke"), strokeEnabled},
        {QStringLiteral("strokeColor"), strokeColor.name(QColor::HexArgb)},
        {QStringLiteral("strokeWidth"), strokeWidth},
    };
}

ShapeData ShapeData::fromJson(const QJsonObject& o)
{
    ShapeData s;
    const QByteArray bytes = QByteArray::fromBase64(o.value(QStringLiteral("path")).toString().toLatin1());
    QDataStream in(bytes);
    in.setVersion(QDataStream::Qt_6_0);
    in >> s.path;
    s.fillEnabled = o.value(QStringLiteral("fill")).toBool(true);
    s.fillColor = QColor(o.value(QStringLiteral("fillColor")).toString());
    if (!s.fillColor.isValid()) s.fillColor = Qt::black;
    s.strokeEnabled = o.value(QStringLiteral("stroke")).toBool();
    s.strokeColor = QColor(o.value(QStringLiteral("strokeColor")).toString());
    if (!s.strokeColor.isValid()) s.strokeColor = Qt::black;
    s.strokeWidth = o.value(QStringLiteral("strokeWidth")).toDouble(3.0);
    return s;
}

// ---------------- Rendering ----------------

namespace Vector {

namespace {

QImage blank(const QSize& s)
{
    QImage img(s, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    return img;
}

void rasterizeText(Layer& l, const TextData& t)
{
    const QRect r = t.canvasBounds().boundingRect().toAlignedRect();
    if (t.text.isEmpty() || r.isEmpty()) {
        l.image = QImage();
        l.offset = QPoint();
        return;
    }
    QImage img = blank(r.size());
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, t.antialias);
    p.setRenderHint(QPainter::TextAntialiasing, t.antialias);
    p.setTransform(t.transform * QTransform::fromTranslate(-r.left(), -r.top()));
    p.setFont(t.font());
    p.setPen(t.color);
    const QFontMetricsF fm(p.font());
    const QStringList ls = t.lines();
    for (int i = 0; i < ls.size(); ++i) {
        const double w = fm.horizontalAdvance(ls[i]);
        p.drawText(QPointF(lineX(t, w), t.position.y() + i * t.lineHeight()), ls[i]);
    }
    p.end();
    l.image = img;
    l.offset = r.topLeft();
}

void rasterizeShape(Layer& l, const ShapeData& s)
{
    const double pad = (s.strokeEnabled ? s.strokeWidth / 2.0 : 0.0) + 2.0;
    const QRect r = s.path.boundingRect().adjusted(-pad, -pad, pad, pad).toAlignedRect();
    if (s.path.isEmpty() || r.isEmpty() || (!s.fillEnabled && !s.strokeEnabled)) {
        l.image = QImage();
        l.offset = QPoint();
        return;
    }
    QImage img = blank(r.size());
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    p.translate(-r.left(), -r.top());
    if (s.fillEnabled) p.fillPath(s.path, s.fillColor);
    if (s.strokeEnabled && s.strokeWidth > 0) {
        QPen pen(s.strokeColor, s.strokeWidth);
        pen.setJoinStyle(Qt::MiterJoin);
        p.strokePath(s.path, pen);
    }
    p.end();
    l.image = img;
    l.offset = r.topLeft();
}

} // namespace

void rasterize(Layer& layer)
{
    if (layer.kind == LayerKind::Text && layer.text) rasterizeText(layer, *layer.text);
    else if (layer.kind == LayerKind::Shape && layer.shape) rasterizeShape(layer, *layer.shape);
}

void translateData(Layer& layer, const QPointF& delta)
{
    if (layer.kind == LayerKind::Text && layer.text) {
        TextData t = *layer.text;
        if (t.transform.isIdentity()) t.position += delta;
        else t.transform *= QTransform::fromTranslate(delta.x(), delta.y());
        layer.text = std::make_shared<const TextData>(t);
    } else if (layer.kind == LayerKind::Shape && layer.shape) {
        ShapeData s = *layer.shape;
        s.path.translate(delta);
        layer.shape = std::make_shared<const ShapeData>(s);
    }
}

void transform(Layer& layer, const QTransform& t)
{
    if (layer.kind == LayerKind::Text && layer.text) {
        TextData d = *layer.text;
        d.transform *= t;
        layer.text = std::make_shared<const TextData>(d);
    } else if (layer.kind == LayerKind::Shape && layer.shape) {
        ShapeData s = *layer.shape;
        s.path = t.map(s.path);
        layer.shape = std::make_shared<const ShapeData>(s);
    } else {
        return;
    }
    rasterize(layer);
}

void convertToPixels(Layer& layer)
{
    if (!layer.isVector()) return;
    layer.kind = LayerKind::Pixel;
    layer.text.reset();
    layer.shape.reset();
}

QPainterPath rectanglePath(const QRectF& r, double radius)
{
    QPainterPath path;
    radius = std::min({radius, r.width() / 2.0, r.height() / 2.0});
    if (radius > 0) path.addRoundedRect(r, radius, radius);
    else path.addRect(r);
    return path;
}

QPainterPath ellipsePath(const QRectF& r)
{
    QPainterPath path;
    path.addEllipse(r);
    return path;
}

QPainterPath polygonPath(const QRectF& r, int sides, bool star)
{
    sides = std::max(3, sides);
    const QPointF c = r.center();
    const double rx = r.width() / 2.0, ry = r.height() / 2.0;
    QPolygonF poly;
    const int n = star ? sides * 2 : sides;
    for (int i = 0; i < n; ++i) {
        // The first point is at the top, as Photoshop draws it.
        const double a = -M_PI / 2.0 + 2.0 * M_PI * i / n;
        const double k = (star && i % 2) ? 0.5 : 1.0;
        poly << QPointF(c.x() + std::cos(a) * rx * k, c.y() + std::sin(a) * ry * k);
    }
    QPainterPath path;
    path.addPolygon(poly);
    path.closeSubpath();
    return path;
}

QPainterPath linePath(const QPointF& a, const QPointF& b, double weight)
{
    QPainterPathStroker stroker;
    stroker.setWidth(std::max(1.0, weight));
    stroker.setCapStyle(Qt::FlatCap);
    QPainterPath line;
    line.moveTo(a);
    line.lineTo(b);
    return stroker.createStroke(line).simplified();
}

} // namespace Vector
