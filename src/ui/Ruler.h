#pragma once

#include <QPointF>
#include <QWidget>

class CanvasView;

// Photoshop-style ruler along the top or left of a document window. Shows the
// cursor position, and dragging out of it creates a guide.
class Ruler : public QWidget {
    Q_OBJECT
public:
    static constexpr int kThickness = 16;

    Ruler(Qt::Orientation orientation, CanvasView* view, QWidget* parent = nullptr);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void contextMenuEvent(QContextMenuEvent* e) override;

private:
    // Canvas pixels per ruler unit.
    double pixelsPerUnit() const;
    QPointF toViewport(const QPointF& localPos) const;
    QPointF fromViewport(const QPointF& viewportPos) const;

    Qt::Orientation m_orientation;
    CanvasView* m_view;
    QPointF m_cursor; // canvas position
    bool m_cursorInside = false;
    bool m_dragging = false;
};
