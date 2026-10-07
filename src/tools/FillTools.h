#pragma once

#include "core/BlendMode.h"
#include "core/ImageOps.h"
#include "tools/Tool.h"

#include <QPointer>

class ValueField;

class GradientTool : public Tool {
    Q_OBJECT
public:
    using Tool::Tool;
    QString id() const override { return QStringLiteral("gradient"); }
    QString name() const override { return QStringLiteral("Gradient Tool"); }
    QString iconName() const override { return QStringLiteral("tool-gradient"); }
    QChar shortcut() const override { return QLatin1Char('G'); }
    QWidget* createOptions(QWidget* parent) override;
    void mousePress(CanvasView* v, const ToolEvent& e) override;
    void mouseMove(CanvasView* v, const ToolEvent& e) override;
    void mouseRelease(CanvasView* v, const ToolEvent& e) override;
    void paintOverlay(QPainter& p, CanvasView* v) override;
    QCursor cursor(CanvasView*, Qt::KeyboardModifiers) const override { return Qt::CrossCursor; }
    bool setOpacityPercent(int v) override;

private:
    QGradientStops stops() const;
    QPointF constrained(const QPointF& p, Qt::KeyboardModifiers mods) const;

    enum class Preset { FgToBg, FgToTransparent, BlackWhite, Spectrum, Copper };
    Preset m_preset = Preset::FgToBg;
    ImageOps::GradientType m_type = ImageOps::GradientType::Linear;
    BlendMode m_mode = BlendMode::Normal;
    int m_opacity = 100;
    bool m_reverse = false;
    bool m_dither = true;
    bool m_transparency = true;
    bool m_dragging = false;
    QPointF m_start, m_end;
    QPointer<ValueField> m_opacityField;
};

class PaintBucketTool : public Tool {
    Q_OBJECT
public:
    using Tool::Tool;
    QString id() const override { return QStringLiteral("bucket"); }
    QString name() const override { return QStringLiteral("Paint Bucket Tool"); }
    QString iconName() const override { return QStringLiteral("tool-bucket"); }
    QChar shortcut() const override { return QLatin1Char('G'); }
    QWidget* createOptions(QWidget* parent) override;
    void mousePress(CanvasView* v, const ToolEvent& e) override;
    QCursor cursor(CanvasView*, Qt::KeyboardModifiers) const override;
    bool setOpacityPercent(int v) override;

private:
    BlendMode m_mode = BlendMode::Normal;
    int m_opacity = 100;
    int m_tolerance = 32;
    bool m_antialias = true;
    bool m_contiguous = true;
    bool m_allLayers = false;
    QPointer<ValueField> m_opacityField;
};

class EyedropperTool : public Tool {
    Q_OBJECT
public:
    using Tool::Tool;
    QString id() const override { return QStringLiteral("eyedropper"); }
    QString name() const override { return QStringLiteral("Eyedropper Tool"); }
    QString iconName() const override { return QStringLiteral("tool-eyedropper"); }
    QChar shortcut() const override { return QLatin1Char('I'); }
    QWidget* createOptions(QWidget* parent) override;
    void mousePress(CanvasView* v, const ToolEvent& e) override;
    void mouseMove(CanvasView* v, const ToolEvent& e) override;
    void mouseRelease(CanvasView* v, const ToolEvent& e) override;
    QCursor cursor(CanvasView*, Qt::KeyboardModifiers) const override;

private:
    void sample(CanvasView* v, const ToolEvent& e);
    int m_sampleSize = 1; // 1, 3, 5, 11, 31, 51, 101
    bool m_allLayers = true;
    bool m_pressed = false;
};
