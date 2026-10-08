#include "core/LayerStyle.h"

#include <QJsonValue>
#include <cmath>
#include <algorithm>

namespace {

QString colorName(const QColor& c) { return c.name(QColor::HexRgb); }

QColor colorFrom(const QJsonObject& o, const char* key, const QColor& def)
{
    const QColor c(o.value(QLatin1String(key)).toString());
    return c.isValid() ? c : def;
}

BlendMode modeFrom(const QJsonObject& o, BlendMode def)
{
    const QString id = o.value(QStringLiteral("mode")).toString();
    return id.isEmpty() ? def : Blend::fromId(id);
}

int intFrom(const QJsonObject& o, const char* key, int def) { return o.value(QLatin1String(key)).toInt(def); }

} // namespace

int LayerStyle::margin() const
{
    if (!active()) return 0;
    int m = 0;
    if (dropShadow.enabled) m = std::max(m, dropShadow.distance + dropShadow.size + 2);
    if (outerGlow.enabled) m = std::max(m, outerGlow.size + 2);
    // Inside strokes also need the transparent surroundings to measure distances from.
    if (stroke.enabled) m = std::max(m, stroke.size + 2);
    return m;
}

QJsonObject LayerStyle::toJson() const
{
    QJsonObject o;
    o.insert(QStringLiteral("visible"), visible);
    o.insert(QStringLiteral("dropShadow"), QJsonObject{
        {QStringLiteral("enabled"), dropShadow.enabled}, {QStringLiteral("mode"), Blend::id(dropShadow.mode)},
        {QStringLiteral("color"), colorName(dropShadow.color)}, {QStringLiteral("opacity"), dropShadow.opacity},
        {QStringLiteral("angle"), dropShadow.angle}, {QStringLiteral("distance"), dropShadow.distance},
        {QStringLiteral("spread"), dropShadow.spread}, {QStringLiteral("size"), dropShadow.size}});
    o.insert(QStringLiteral("outerGlow"), QJsonObject{
        {QStringLiteral("enabled"), outerGlow.enabled}, {QStringLiteral("mode"), Blend::id(outerGlow.mode)},
        {QStringLiteral("color"), colorName(outerGlow.color)}, {QStringLiteral("opacity"), outerGlow.opacity},
        {QStringLiteral("spread"), outerGlow.spread}, {QStringLiteral("size"), outerGlow.size}});
    o.insert(QStringLiteral("colorOverlay"), QJsonObject{
        {QStringLiteral("enabled"), colorOverlay.enabled}, {QStringLiteral("mode"), Blend::id(colorOverlay.mode)},
        {QStringLiteral("color"), colorName(colorOverlay.color)}, {QStringLiteral("opacity"), colorOverlay.opacity}});
    o.insert(QStringLiteral("stroke"), QJsonObject{
        {QStringLiteral("enabled"), stroke.enabled}, {QStringLiteral("size"), stroke.size},
        {QStringLiteral("position"), int(stroke.position)}, {QStringLiteral("mode"), Blend::id(stroke.mode)},
        {QStringLiteral("opacity"), stroke.opacity}, {QStringLiteral("color"), colorName(stroke.color)}});
    return o;
}

LayerStyle LayerStyle::fromJson(const QJsonObject& o)
{
    LayerStyle s;
    s.visible = o.value(QStringLiteral("visible")).toBool(true);
    const QJsonObject ds = o.value(QStringLiteral("dropShadow")).toObject();
    s.dropShadow.enabled = ds.value(QStringLiteral("enabled")).toBool();
    s.dropShadow.mode = modeFrom(ds, s.dropShadow.mode);
    s.dropShadow.color = colorFrom(ds, "color", s.dropShadow.color);
    s.dropShadow.opacity = intFrom(ds, "opacity", s.dropShadow.opacity);
    s.dropShadow.angle = intFrom(ds, "angle", s.dropShadow.angle);
    s.dropShadow.distance = intFrom(ds, "distance", s.dropShadow.distance);
    s.dropShadow.spread = intFrom(ds, "spread", s.dropShadow.spread);
    s.dropShadow.size = intFrom(ds, "size", s.dropShadow.size);
    const QJsonObject og = o.value(QStringLiteral("outerGlow")).toObject();
    s.outerGlow.enabled = og.value(QStringLiteral("enabled")).toBool();
    s.outerGlow.mode = modeFrom(og, s.outerGlow.mode);
    s.outerGlow.color = colorFrom(og, "color", s.outerGlow.color);
    s.outerGlow.opacity = intFrom(og, "opacity", s.outerGlow.opacity);
    s.outerGlow.spread = intFrom(og, "spread", s.outerGlow.spread);
    s.outerGlow.size = intFrom(og, "size", s.outerGlow.size);
    const QJsonObject co = o.value(QStringLiteral("colorOverlay")).toObject();
    s.colorOverlay.enabled = co.value(QStringLiteral("enabled")).toBool();
    s.colorOverlay.mode = modeFrom(co, s.colorOverlay.mode);
    s.colorOverlay.color = colorFrom(co, "color", s.colorOverlay.color);
    s.colorOverlay.opacity = intFrom(co, "opacity", s.colorOverlay.opacity);
    const QJsonObject st = o.value(QStringLiteral("stroke")).toObject();
    s.stroke.enabled = st.value(QStringLiteral("enabled")).toBool();
    s.stroke.size = intFrom(st, "size", s.stroke.size);
    s.stroke.position = StrokeEffect::Position(std::clamp(intFrom(st, "position", 0), 0, 2));
    s.stroke.mode = modeFrom(st, s.stroke.mode);
    s.stroke.opacity = intFrom(st, "opacity", s.stroke.opacity);
    s.stroke.color = colorFrom(st, "color", s.stroke.color);
    return s;
}
