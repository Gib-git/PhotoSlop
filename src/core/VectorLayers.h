#pragma once

#include <QColor>
#include <QFont>
#include <QJsonObject>
#include <QLineF>
#include <QPainterPath>
#include <QPolygonF>
#include <QString>
#include <QTransform>

struct Layer;

// A point type layer (Type tool). `position` is the start of the first line's baseline at the
// alignment point; `transform` maps the laid-out text onto the canvas (canvas rotation, image
// size, Free Transform).
struct TextData {
    enum class Align { Left, Center, Right };
    QString text;
    QString family;
    int size = 48;         // px
    QColor color = Qt::black;
    bool bold = false;
    bool italic = false;
    bool underline = false;
    Align align = Align::Left;
    bool antialias = true;
    QPointF position;
    QTransform transform;

    bool operator==(const TextData&) const = default;

    QFont font() const;
    double lineHeight() const;
    QStringList lines() const;
    // Bounds of the text before `transform`.
    QRectF localBounds() const;
    QPolygonF canvasBounds() const;
    // Caret for a character index, on the canvas.
    QLineF caretLine(int index) const;
    // Character index nearest to a canvas position.
    int indexAt(const QPointF& canvasPt) const;

    QJsonObject toJson() const;
    static TextData fromJson(const QJsonObject& o);
};

// A shape layer (Rectangle, Ellipse, Polygon and Line tools): a path on the canvas with a fill
// and a stroke.
struct ShapeData {
    QPainterPath path;
    bool fillEnabled = true;
    QColor fillColor = Qt::black;
    bool strokeEnabled = false;
    QColor strokeColor = Qt::black;
    double strokeWidth = 3.0;

    bool operator==(const ShapeData& o) const
    {
        return path == o.path && fillEnabled == o.fillEnabled && fillColor == o.fillColor
            && strokeEnabled == o.strokeEnabled && strokeColor == o.strokeColor && strokeWidth == o.strokeWidth;
    }

    QJsonObject toJson() const;
    static ShapeData fromJson(const QJsonObject& o);
};

namespace Vector {

// Re-renders a text or shape layer's pixels from its vector data.
void rasterize(Layer& layer);
// Moves a text or shape layer's vector data (pixels are not touched).
void translateData(Layer& layer, const QPointF& delta);
// Applies a canvas transform to the vector data and re-renders.
void transform(Layer& layer, const QTransform& t);
// Turns a text or shape layer into an ordinary pixel layer.
void convertToPixels(Layer& layer);

// Shape tool outlines.
QPainterPath rectanglePath(const QRectF& r, double radius);
QPainterPath ellipsePath(const QRectF& r);
QPainterPath polygonPath(const QRectF& r, int sides, bool star = false);
QPainterPath linePath(const QPointF& a, const QPointF& b, double weight);

} // namespace Vector
