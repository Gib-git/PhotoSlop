#pragma once

#include "tools/Tool.h"

#include <QPointF>
#include <QRectF>

class QToolButton;

class HandTool : public Tool {
    Q_OBJECT
public:
    using Tool::Tool;
    QString id() const override { return QStringLiteral("hand"); }
    QString name() const override { return QStringLiteral("Hand Tool"); }
    QString iconName() const override { return QStringLiteral("tool-hand"); }
    QChar shortcut() const override { return QLatin1Char('H'); }
    QWidget* createOptions(QWidget* parent) override;
    void mousePress(CanvasView* v, const ToolEvent& e) override;
    void mouseMove(CanvasView* v, const ToolEvent& e) override;
    void mouseRelease(CanvasView* v, const ToolEvent& e) override;
    QCursor cursor(CanvasView*, Qt::KeyboardModifiers) const override;
    void toolButtonDoubleClicked() override;

private:
    bool m_dragging = false;
    QPointF m_last;
};

class ZoomTool : public Tool {
    Q_OBJECT
public:
    using Tool::Tool;
    QString id() const override { return QStringLiteral("zoom"); }
    QString name() const override { return QStringLiteral("Zoom Tool"); }
    QString iconName() const override { return QStringLiteral("tool-zoom"); }
    QChar shortcut() const override { return QLatin1Char('Z'); }
    QWidget* createOptions(QWidget* parent) override;
    void mousePress(CanvasView* v, const ToolEvent& e) override;
    void mouseMove(CanvasView* v, const ToolEvent& e) override;
    void mouseRelease(CanvasView* v, const ToolEvent& e) override;
    void paintOverlay(QPainter& p, CanvasView* v) override;
    QCursor cursor(CanvasView*, Qt::KeyboardModifiers mods) const override;
    void toolButtonDoubleClicked() override;

private:
    bool zoomingOut(Qt::KeyboardModifiers mods) const;
    bool m_zoomOutMode = false;
    bool m_scrubby = true;
    bool m_pressed = false;
    bool m_moved = false;
    QPointF m_startView;
    QPointF m_curView;
    double m_startZoom = 1.0;
};

// Adds "100%", "Fit Screen" and "Fill Screen" buttons acting on the active view.
void addViewButtons(ToolManager* manager, QWidget* container);
