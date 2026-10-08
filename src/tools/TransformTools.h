#pragma once

#include "core/Document.h"
#include "tools/Tool.h"

#include <QImage>
#include <QPointer>
#include <QRectF>

class MoveTool : public Tool {
    Q_OBJECT
public:
    using Tool::Tool;
    QString id() const override { return QStringLiteral("move"); }
    QString name() const override { return QStringLiteral("Move Tool"); }
    QString iconName() const override { return QStringLiteral("tool-move"); }
    QChar shortcut() const override { return QLatin1Char('V'); }
    QWidget* createOptions(QWidget* parent) override;
    void mousePress(CanvasView* v, const ToolEvent& e) override;
    void mouseMove(CanvasView* v, const ToolEvent& e) override;
    void mouseRelease(CanvasView* v, const ToolEvent& e) override;
    bool keyPress(CanvasView* v, QKeyEvent* e) override;
    QCursor cursor(CanvasView*, Qt::KeyboardModifiers) const override;

private:
    bool begin(Document* doc);
    void moveTo(Document* doc, const QPoint& delta);
    void finish(Document* doc, const QString& text);

    bool m_autoSelect = false;
    bool m_autoSelectGroup = false;
    bool m_active = false;
    bool m_floating = false;
    QPointF m_start;
    QPoint m_delta;
    int m_index = -1;         // layer (or mask) being moved
    int m_subStart = -1;      // whole-layer moves: the layer and its contents
    QList<Layer> m_origLayers;
    QRect m_prevExtent;
    QImage m_float;
    QPoint m_floatOrigin;
    QImage m_cleared;
    QRect m_prevRect;
    QImage m_selOrig;
    QRect m_snapRect; // what is being moved, for snapping
    DocState m_before;
};

class CropTool : public Tool {
    Q_OBJECT
public:
    using Tool::Tool;
    QString id() const override { return QStringLiteral("crop"); }
    QString name() const override { return QStringLiteral("Crop Tool"); }
    QString iconName() const override { return QStringLiteral("tool-crop"); }
    QChar shortcut() const override { return QLatin1Char('C'); }
    QWidget* createOptions(QWidget* parent) override;
    void activated(CanvasView* v) override;
    void deactivated(CanvasView* v) override;
    void mousePress(CanvasView* v, const ToolEvent& e) override;
    void mouseMove(CanvasView* v, const ToolEvent& e) override;
    void mouseRelease(CanvasView* v, const ToolEvent& e) override;
    void mouseDoubleClick(CanvasView* v, const ToolEvent& e) override;
    void paintOverlay(QPainter& p, CanvasView* v) override;
    QCursor cursor(CanvasView*, Qt::KeyboardModifiers) const override;
    bool commit(CanvasView* v) override;
    bool cancel(CanvasView* v) override;

private:
    enum Handle { None = -1, TL, T, TR, R, BR, B, BL, L, Inside, Outside };
    Handle hitTest(CanvasView* v, const QPointF& viewPos) const;
    void reset(CanvasView* v);
    double ratio() const; // width / height, 0 = free

    QRectF m_rect;
    QPointer<Document> m_doc;
    Handle m_drag = None;
    QPointF m_pressPos;
    QRectF m_pressRect;
    bool m_deletePixels = true;
    int m_overlay = 0; // 0 thirds, 1 grid, 2 none
    double m_ratioW = 0, m_ratioH = 0;
};
