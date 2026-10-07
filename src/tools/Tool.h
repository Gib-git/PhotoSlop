#pragma once

#include <QCursor>
#include <QObject>
#include <QPointF>
#include <QString>

#include <cmath>

class CanvasView;
class ColorState;
class Document;
class QKeyEvent;
class QPainter;
class QWidget;
class ToolManager;

struct ToolEvent {
    QPointF pos;     // canvas coordinates
    QPointF viewPos; // viewport coordinates
    Qt::KeyboardModifiers mods;
    Qt::MouseButton button = Qt::NoButton;
    Qt::MouseButtons buttons;
    qreal pressure = 1.0;
    bool tablet = false;

    QPoint pixel() const { return QPoint(int(std::floor(pos.x())), int(std::floor(pos.y()))); }
    bool shift() const { return mods & Qt::ShiftModifier; }
    bool alt() const { return mods & Qt::AltModifier; }
    bool ctrl() const { return mods & Qt::ControlModifier; }
};

class Tool : public QObject {
    Q_OBJECT
public:
    explicit Tool(ToolManager* manager);

    virtual QString id() const = 0;
    virtual QString name() const = 0; // e.g. "Brush Tool"
    virtual QString iconName() const = 0;
    virtual QChar shortcut() const = 0;

    // Options bar contents for this tool (created once, owned by the caller).
    virtual QWidget* createOptions(QWidget* parent);

    virtual void activated(CanvasView*) {}
    virtual void deactivated(CanvasView*) {}
    virtual void mousePress(CanvasView*, const ToolEvent&) {}
    virtual void mouseMove(CanvasView*, const ToolEvent&) {}
    virtual void mouseRelease(CanvasView*, const ToolEvent&) {}
    virtual void mouseDoubleClick(CanvasView*, const ToolEvent&) {}
    virtual bool keyPress(CanvasView*, QKeyEvent*) { return false; }
    virtual void paintOverlay(QPainter&, CanvasView*) {}
    virtual QCursor cursor(CanvasView*, Qt::KeyboardModifiers) const { return Qt::ArrowCursor; }
    // Brush outline diameter in canvas pixels (0 = none).
    virtual double brushOutlineSize() const { return 0.0; }
    // Enter / Escape. Return true when handled.
    virtual bool commit(CanvasView*) { return false; }
    virtual bool cancel(CanvasView*) { return false; }
    // Number keys: return true if the tool consumed the opacity change.
    virtual bool setOpacityPercent(int) { return false; }
    // [ / ] and Shift+[ / Shift+].
    virtual void adjustSize(int /*direction*/) {}
    virtual void adjustHardness(int /*direction*/) {}
    // Double-clicking the tool's toolbox button (Hand = fit, Zoom = 100%).
    virtual void toolButtonDoubleClicked() {}

    ToolManager* manager() const { return m_manager; }
    ColorState* colors() const;
    void alert(const QString& message) const;

signals:
    void overlayChanged();
    void optionsChanged();

protected:
    ToolManager* m_manager;
};

// Builds an outlined cursor from one of the theme icons.
QCursor makeIconCursor(const QString& iconName, const QPoint& hotspot, int size = 22);
