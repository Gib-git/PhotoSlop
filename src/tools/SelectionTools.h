#pragma once

#include "core/Document.h"
#include "core/Selection.h"
#include "tools/Tool.h"

#include <QElapsedTimer>
#include <QPainterPath>
#include <QPointer>
#include <QPolygonF>
#include <vector>

class QButtonGroup;
class QHBoxLayout;
class QSpinBox;

// Shared behaviour for marquee and lasso tools: selection mode buttons,
// feather and anti-alias options, and Shift/Alt modifier handling.
class SelectionTool : public Tool {
    Q_OBJECT
public:
    using Tool::Tool;
    QCursor cursor(CanvasView*, Qt::KeyboardModifiers) const override { return Qt::CrossCursor; }

protected:
    // `featherOption` false omits Feather (Magic Wand); `subsetOps` limits the mode buttons
    // to New / Add / Subtract (Quick Selection).
    void addSelectionOptions(QHBoxLayout* lay, QWidget* parent, bool antialiasOption,
                             bool featherOption = true, bool subsetOps = false);
    Sel::Op opFor(Qt::KeyboardModifiers mods) const;
    void applyShape(CanvasView* v, const QPainterPath& path, Sel::Op op, const QString& undoText);
    void applyMask(CanvasView* v, const QImage& mask, Sel::Op op, const QString& undoText);
    // Click-drag inside an existing selection moves its outline.
    bool beginMoveSelection(CanvasView* v, const ToolEvent& e);
    void updateMoveSelection(CanvasView* v, const ToolEvent& e);
    void endMoveSelection(CanvasView* v);
    bool nudgeSelection(CanvasView* v, QKeyEvent* e);
    static void drawOutline(QPainter& p, const QPainterPath& viewPath);

    Sel::Op m_mode = Sel::Op::Replace;
    QPointer<QButtonGroup> m_modeButtons;
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

// Edge-snapping lasso: a live-wire path follows the strongest edges near the pointer.
class MagneticLassoTool : public SelectionTool {
    Q_OBJECT
public:
    using SelectionTool::SelectionTool;
    QString id() const override { return QStringLiteral("lasso-magnetic"); }
    QString name() const override { return QStringLiteral("Magnetic Lasso Tool"); }
    QString iconName() const override { return QStringLiteral("tool-lasso-magnetic"); }
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

    // Exposed for tests: the strongest edge pixel within Width of `p`.
    QPoint snapToEdge(const QPoint& p) const;
    // Exposed for tests: cheapest 8-connected path from `a` to `b` along edges.
    std::vector<QPoint> liveWire(const QPoint& a, const QPoint& b) const;
    void prepare(Document* doc);

private:
    void addAnchor(const QPoint& p);
    void updateLive(const QPoint& target);
    void finish(CanvasView* v, bool closeMagnetically);

    int m_width = 10;
    int m_contrast = 10;
    int m_frequency = 57;
    bool m_active = false;
    Sel::Op m_op = Sel::Op::Replace;
    QSize m_size;
    std::vector<uchar> m_edges; // edge strength per pixel
    QPolygonF m_points; // committed path, pixel centres
    QList<QPoint> m_anchors;
    std::vector<QPoint> m_live;
};

class MagicWandTool : public SelectionTool {
    Q_OBJECT
public:
    using SelectionTool::SelectionTool;
    QString id() const override { return QStringLiteral("magic-wand"); }
    QString name() const override { return QStringLiteral("Magic Wand Tool"); }
    QString iconName() const override { return QStringLiteral("tool-magic-wand"); }
    QChar shortcut() const override { return QLatin1Char('W'); }
    QWidget* createOptions(QWidget* parent) override;
    void mousePress(CanvasView* v, const ToolEvent& e) override;
    QCursor cursor(CanvasView*, Qt::KeyboardModifiers) const override;
    // Select > Grow and Select > Similar use the same tolerance.
    int tolerance() const { return m_tolerance; }
    void setTolerance(int t) { m_tolerance = t; }
    void setContiguous(bool on) { m_contiguous = on; }

private:
    int m_tolerance = 32;
    int m_sampleSize = 1;
    bool m_contiguous = true;
    bool m_allLayers = false;
};

// Brush that grows the selection into similar, edge-bounded regions as you paint.
class QuickSelectionTool : public SelectionTool {
    Q_OBJECT
public:
    using SelectionTool::SelectionTool;
    QString id() const override { return QStringLiteral("quick-selection"); }
    QString name() const override { return QStringLiteral("Quick Selection Tool"); }
    QString iconName() const override { return QStringLiteral("tool-quick-selection"); }
    QChar shortcut() const override { return QLatin1Char('W'); }
    QWidget* createOptions(QWidget* parent) override;
    void mousePress(CanvasView* v, const ToolEvent& e) override;
    void mouseMove(CanvasView* v, const ToolEvent& e) override;
    void mouseRelease(CanvasView* v, const ToolEvent& e) override;
    double brushOutlineSize() const override { return m_size; }
    void adjustSize(int direction) override;

private:
    void dab(const QPointF& center);
    void preview(Document* doc, bool force);

    int m_size = 30;
    bool m_allLayers = false;
    bool m_autoEnhance = true;
    bool m_painting = false;
    Sel::Op m_op = Sel::Op::Add;
    QImage m_sample;     // canvas image being segmented
    QImage m_stroke;     // pixels reached by this stroke
    QImage m_base;       // selection before the stroke
    DocState m_before;
    QPointF m_last;
    QElapsedTimer m_previewTimer;
    QPointer<QSpinBox> m_sizeField;
};
