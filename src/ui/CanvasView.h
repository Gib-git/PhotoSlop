#pragma once

#include <QAbstractScrollArea>
#include <QPointer>
#include <QTimer>

class Document;
class Tool;
class ToolManager;
struct ToolEvent;

// Displays a document and routes pointer input to the current tool.
// Zoom is expressed in device pixels per image pixel, so 100% shows one image
// pixel per physical screen pixel on high-DPI displays (like Photoshop).
class CanvasView : public QAbstractScrollArea {
    Q_OBJECT
public:
    CanvasView(Document* doc, ToolManager* tools, QWidget* parent = nullptr);

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
    bool showPixelGrid() const { return m_pixelGrid; }
    void setShowPixelGrid(bool on);
    void setShowSelectionEdges(bool on);

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

    Document* m_doc;
    ToolManager* m_tools;
    double m_zoom = 1.0;
    bool m_userZoomed = false;
    QPointer<Tool> m_pressTool;
    bool m_panning = false;
    QPointF m_panLast;
    QPointF m_lastCanvasPos;
    QPointF m_lastViewPos;
    bool m_cursorInside = false;
    bool m_pixelGrid = true;
    bool m_showEdges = true;
    int m_antsPhase = 0;
    QTimer m_antsTimer;
};
