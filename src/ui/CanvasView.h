#pragma once

#include <QAbstractScrollArea>
#include <QBrush>
#include <QPointer>
#include <QTimer>

class Document;
class GpuViewport;
class QPainter;
class Tool;
class ToolManager;
class ViewOptions;
struct ToolEvent;

// Displays a document and routes pointer input to the current tool.
// Zoom is expressed in device pixels per image pixel, so 100% shows one image
// pixel per physical screen pixel on high-DPI displays (like Photoshop).
class CanvasView : public QAbstractScrollArea {
    Q_OBJECT
public:
    CanvasView(Document* doc, ToolManager* tools, ViewOptions* options, QWidget* parent = nullptr);

    Document* document() const { return m_doc; }
    double zoom() const { return m_zoom; }
    double scale() const; // logical pixels per image pixel

    void setZoom(double zoom, const QPointF& anchorView = QPointF(-1, -1));
    void zoomIn(const QPointF& anchorView = QPointF(-1, -1));
    void zoomOut(const QPointF& anchorView = QPointF(-1, -1));
    void fitOnScreen();
    void fillScreen();
    void actualPixels();
    void panBy(const QPointF& deltaView);
    void centerOn(const QPointF& canvasPt);

    QPointF viewToCanvas(const QPointF& p) const;
    QPointF canvasToView(const QPointF& p) const;
    QRectF canvasToView(const QRectF& r) const;
    QRectF visibleCanvasRect() const;

    void updateCursor();
    QPointF lastCanvasPos() const { return m_lastCanvasPos; }
    ViewOptions* options() const { return m_opts; }
    // A pen for overlay lines that show on any colour (white in Difference mode, or a black and
    // white pattern on the GPU canvas, which cannot blend that way).
    void setContrastPen(QPainter& p, qreal width) const;
    bool usesGpu() const { return m_gpu != nullptr; }
    // Hides the marching ants while a tool shows its own outline (Transform Selection).
    void setSelectionEdgesSuppressed(bool on);

    // ---- Snapping (View > Snap / Snap To) ----
    // Snaps each coordinate independently to the nearest guide, grid line or document edge.
    QPointF snapPoint(const QPointF& canvasPt) const;
    // Offset that snaps the closest edge (or centre) of `canvasRect` on each axis.
    QPointF snapRectOffset(const QRectF& canvasRect) const;

    // ---- Guides ----
    // Index of the guide under a viewport position, or -1.
    int guideAt(const QPointF& viewPos) const;
    bool canDragGuides() const;
    // Drags a new guide (index -1, e.g. out of a ruler) or an existing one. Releasing
    // outside the viewport deletes it.
    void beginGuideDrag(Qt::Orientation orientation, int index, bool fromRuler);
    void updateGuideDrag(const QPointF& viewPos, Qt::KeyboardModifiers mods);
    void endGuideDrag(const QPointF& viewPos);
    bool isDraggingGuide() const { return m_guideDrag.active; }

signals:
    void zoomChanged(double zoom);
    void viewChanged();
    void cursorMoved(const QPointF& canvasPos, bool inside);

protected:
    void paintEvent(QPaintEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;
    void showEvent(QShowEvent* e) override;
    void scrollContentsBy(int dx, int dy) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    void keyReleaseEvent(QKeyEvent* e) override;
    bool eventFilter(QObject* obj, QEvent* e) override;
    bool viewportEvent(QEvent* e) override;

private:
    void prepareViewport();
    void paintCanvas(QPainter& p, const QRect& clip);
    // Switches between the OpenGL and the plain viewport to follow Preferences.
    void applyGpuPreference();
    void updateScrollBars();
    bool initialFit();
    void applyZoom(double zoom, const QPointF& anchorView);
    QPointF origin() const;
    ToolEvent makeEvent(const QPointF& viewPos, Qt::KeyboardModifiers mods, Qt::MouseButton button,
                        Qt::MouseButtons buttons, qreal pressure, bool tablet) const;
    void handlePress(const ToolEvent& ev);
    void handleMove(const ToolEvent& ev);
    void handleRelease(const ToolEvent& ev);
    void onImageChanged(const QRect& r);
    void updateCursorArea(const QPointF& oldPos, const QPointF& newPos);
    QRectF selectionViewRect() const;
    double gridStep() const; // canvas pixels between grid subdivisions
    double snapValue(double v, bool xAxis, bool includeGuides, double* distance) const;
    void paintGridAndGuides(QPainter& p, const QRectF& canvasView);

    struct GuideDrag {
        bool active = false;
        bool fromRuler = false;
        bool visible = false;
        int index = -1;
        Qt::Orientation orientation = Qt::Horizontal;
        Qt::Orientation startOrientation = Qt::Horizontal;
        double position = 0.0;
    };

    Document* m_doc;
    ToolManager* m_tools;
    ViewOptions* m_opts;
    double m_zoom = 1.0;
    bool m_userZoomed = false;
    QPointer<Tool> m_pressTool;
    bool m_panning = false;
    QPointF m_panLast;
    QPointF m_lastCanvasPos;
    QPointF m_lastViewPos;
    bool m_cursorInside = false;
    bool m_suppressEdges = false;
    GuideDrag m_guideDrag;
    bool m_guideMouse = false;
    bool m_hoverGuide = false;
    int m_antsPhase = 0;
    QBrush m_checker;
    GpuViewport* m_gpu = nullptr;
    QTimer m_antsTimer;
};
