#pragma once

#include "core/BlendMode.h"
#include "tools/Tool.h"

#include <QImage>
#include <QPointer>
#include <QToolButton>
#include <memory>
#include <vector>

class BlendModeCombo;
class Document;
class PixelEdit;
class ValueField;

// Toolbar button showing the current brush tip; opens the size/hardness picker.
class BrushPickerButton : public QToolButton {
    Q_OBJECT
public:
    explicit BrushPickerButton(QWidget* parent = nullptr);
    void setBrush(int size, int hardness);

signals:
    void brushChosen(int size, int hardness);

protected:
    void paintEvent(QPaintEvent* e) override;

private:
    void showPicker();
    int m_size = 13;
    int m_hardness = 100;
};

// Brush, Pencil and Eraser share one stroke engine.
//
// Each stroke accumulates dab coverage (scaled by Flow) in a 16-bit mask; the
// layer is then rebuilt from its original pixels with that mask capped at the
// stroke Opacity — so overlapping dabs within one stroke never exceed Opacity,
// matching Photoshop.
class BrushTool : public Tool {
    Q_OBJECT
public:
    enum class Kind { Brush, Pencil, Eraser };

    BrushTool(ToolManager* m, Kind kind);
    ~BrushTool() override;
    QString id() const override;
    QString name() const override;
    QString iconName() const override;
    QChar shortcut() const override { return m_kind == Kind::Eraser ? QLatin1Char('E') : QLatin1Char('B'); }
    QWidget* createOptions(QWidget* parent) override;

    void mousePress(CanvasView* v, const ToolEvent& e) override;
    void mouseMove(CanvasView* v, const ToolEvent& e) override;
    void mouseRelease(CanvasView* v, const ToolEvent& e) override;
    void deactivated(CanvasView* v) override;
    double brushOutlineSize() const override { return m_size; }
    bool setOpacityPercent(int v) override;
    void adjustSize(int direction) override;
    void adjustHardness(int direction) override;

private:
    bool begin(CanvasView* v, const ToolEvent& e);
    void strokeTo(const QPointF& p, double pressure);
    void dab(const QPointF& c, double pressure);
    void flushToLayer();
    void end();
    bool pencilTip() const;
    void syncOptions();

    Kind m_kind;
    // Settings
    int m_size = 13;
    int m_hardness = 100;
    int m_opacity = 100;
    int m_flow = 100;
    int m_spacing = 25;
    BlendMode m_mode = BlendMode::Normal;
    bool m_pressureSize = false;
    bool m_pressureOpacity = false;
    bool m_eraserPencil = false;

    // Stroke state
    std::unique_ptr<PixelEdit> m_edit;
    std::vector<uint16_t> m_mask;
    QRect m_maskRect; // canvas coordinates of the mask buffer
    QRect m_pending;  // canvas area needing to be pushed to the layer
    QImage m_selection;
    QRgb m_color = 0;
    bool m_erase = false;
    bool m_preserveAlpha = false;
    QPointF m_last;
    double m_lastPressure = 1.0;
    double m_carry = 0.0;
    QPointer<Document> m_lastDoc;
    QPointF m_lastStrokeEnd;
    bool m_hasLastStrokeEnd = false;

    // Options widgets
    QPointer<BrushPickerButton> m_pickerButton;
    QPointer<ValueField> m_opacityField;
    QPointer<ValueField> m_flowField;
    QPointer<BlendModeCombo> m_modeCombo;
};
