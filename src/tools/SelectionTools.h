#pragma once

#include "core/Document.h"
#include "core/Selection.h"
#include "tools/Tool.h"

#include <QPainterPath>
#include <QPolygonF>

class QHBoxLayout;

// Shared behaviour for marquee and lasso tools: selection mode buttons,
// feather and anti-alias options, and Shift/Alt modifier handling.
class SelectionTool : public Tool {
    Q_OBJECT
public:
    using Tool::Tool;
    QCursor cursor(CanvasView*, Qt::KeyboardModifiers) const override { return Qt::CrossCursor; }

protected:
    void addSelectionOptions(QHBoxLayout* lay, QWidget* parent, bool antialiasOption);
    Sel::Op opFor(Qt::KeyboardModifiers mods) const;
    void applyShape(CanvasView* v, const QPainterPath& path, Sel::Op op, const QString& undoText);
    // Click-drag inside an existing selection moves its outline.
    bool beginMoveSelection(CanvasView* v, const ToolEvent& e);
    void updateMoveSelection(CanvasView* v, const ToolEvent& e);
    void endMoveSelection(CanvasView* v);
    bool nudgeSelection(CanvasView* v, QKeyEvent* e);
    static void drawOutline(QPainter& p, const QPainterPath& viewPath);

    Sel::Op m_mode = Sel::Op::Replace;
    double m_feather = 0.0;
    bool m_antialias = true;
    bool m_movingSelection = false;
    QPointF m_moveStart;
    QImage m_moveOriginal;
    DocState m_moveBefore;
};

class MarqueeTool : public SelectionTool {
    Q_OBJECT
public:
    MarqueeTool(ToolManager* m, bool elliptical) : SelectionTool(m), m_elliptical(elliptical) {}
    QString id() const override { return m_elliptical ? QStringLiteral("marquee-ellipse") : QStringLiteral("marquee-rect"); }
    QString name() const override { return m_elliptical ? QStringLiteral("Elliptical Marquee Tool") : QStringLiteral("Rectangular Marquee Tool"); }
    QString iconName() const override { return m_elliptical ? QStringLiteral("tool-marquee-ellipse") : QStringLiteral("tool-marquee-rect"); }
    QChar shortcut() const override { return QLatin1Char('M'); }
    QWidget* createOptions(QWidget* parent) override;
    void mousePress(CanvasView* v, const ToolEvent& e) override;
    void mouseMove(CanvasView* v, const ToolEvent& e) override;
    void mouseRelease(CanvasView* v, const ToolEvent& e) override;
    bool keyPress(CanvasView* v, QKeyEvent* e) override;
    void paintOverlay(QPainter& p, CanvasView* v) override;

private:
    QRectF currentRect(Qt::KeyboardModifiers mods) const;
    QPainterPath shapePath(const QRectF& r) const;

    enum class Style { Normal, FixedRatio, FixedSize };
    bool m_elliptical;
    Style m_style = Style::Normal;
    double m_fixedW = 1.0, m_fixedH = 1.0;
    bool m_drawing = false;
    Sel::Op m_op = Sel::Op::Replace;
    Qt::KeyboardModifiers m_pressMods;
    QPointF m_start, m_current;
    Qt::KeyboardModifiers m_mods;
};

class LassoTool : public SelectionTool {
    Q_OBJECT
public:
    LassoTool(ToolManager* m, bool polygonal) : SelectionTool(m), m_polygonal(polygonal) {}
    QString id() const override { return m_polygonal ? QStringLiteral("lasso-poly") : QStringLiteral("lasso"); }
    QString name() const override { return m_polygonal ? QStringLiteral("Polygonal Lasso Tool") : QStringLiteral("Lasso Tool"); }
    QString iconName() const override { return m_polygonal ? QStringLiteral("tool-lasso-poly") : QStringLiteral("tool-lasso"); }
    QChar shortcut() const override { return QLatin1Char('L'); }
    QWidget* createOptions(QWidget* parent) override;
    void mousePress(CanvasView* v, const ToolEvent& e) override;
    void mouseMove(CanvasView* v, const ToolEvent& e) override;
    void mouseRelease(CanvasView* v, const ToolEvent& e) override;
    void mouseDoubleClick(CanvasView* v, const ToolEvent& e) override;
    bool keyPress(CanvasView* v, QKeyEvent* e) override;
    bool commit(CanvasView* v) override;
    bool cancel(CanvasView* v) override;
    void deactivated(CanvasView* v) override;
    void paintOverlay(QPainter& p, CanvasView* v) override;

private:
    void finish(CanvasView* v);
    bool m_polygonal;
    bool m_active = false;
    Sel::Op m_op = Sel::Op::Replace;
    QPolygonF m_points;
    QPointF m_hover;
};
