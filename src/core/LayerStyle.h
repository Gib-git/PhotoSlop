#pragma once

#include "core/BlendMode.h"

#include <QColor>
#include <QJsonObject>

// Layer > Layer Style effects, with Photoshop's defaults. Effects are drawn from the layer's
// pixels (after its mask) and are not affected by Fill opacity.
struct DropShadow {
    bool enabled = false;
    BlendMode mode = BlendMode::Multiply;
    QColor color = Qt::black;
    int opacity = 75;  // %
    int angle = 120;   // degrees, direction the light comes from
    int distance = 5;  // px
    int spread = 0;    // %
    int size = 5;      // px
    bool operator==(const DropShadow&) const = default;
};

struct OuterGlow {
    bool enabled = false;
    BlendMode mode = BlendMode::Screen;
    QColor color = QColor(255, 255, 190);
    int opacity = 75;
    int spread = 0;
    int size = 5;
    bool operator==(const OuterGlow&) const = default;
};

struct ColorOverlay {
    bool enabled = false;
    BlendMode mode = BlendMode::Normal;
    QColor color = QColor(255, 0, 0);
    int opacity = 100;
    bool operator==(const ColorOverlay&) const = default;
};

struct StrokeEffect {
    enum class Position { Outside, Inside, Center };
    bool enabled = false;
    int size = 3;
    Position position = Position::Outside;
    BlendMode mode = BlendMode::Normal;
    int opacity = 100;
    QColor color = Qt::black;
    bool operator==(const StrokeEffect&) const = default;
};

struct LayerStyle {
    DropShadow dropShadow;
    OuterGlow outerGlow;
    ColorOverlay colorOverlay;
    StrokeEffect stroke;
    bool visible = true; // Layer > Layer Style > Hide All Effects

    bool operator==(const LayerStyle&) const = default;
    bool anyEnabled() const
    {
        return dropShadow.enabled || outerGlow.enabled || colorOverlay.enabled || stroke.enabled;
    }
    bool active() const { return visible && anyEnabled(); }
    // How far the effects reach beyond the layer's pixels.
    int margin() const;

    QJsonObject toJson() const;
    static LayerStyle fromJson(const QJsonObject& o);
};
