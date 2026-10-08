#pragma once

#include "core/Document.h"
#include "core/VectorLayers.h"
#include "tools/Tool.h"

#include <QPointer>
#include <QTimer>

class ColorButton;
class QComboBox;
class QFontComboBox;
class QSpinBox;
class QToolButton;
class ValueField;

// Horizontal Type Tool (T): click to start a text layer (or into one to edit it), type, then
// Ctrl+Enter / Enter on the keypad / the options bar tick commits and Escape cancels. The
// whole edit is one history step.
class TypeTool : public Tool {
    Q_OBJECT
public:
    explicit TypeTool(ToolManager* m);
    QString id() const override { return QStringLiteral("type"); }
    QString name() const override { return QStringLiteral("Horizontal Type Tool"); }
    QString iconName() const override { return QStringLiteral("tool-type"); }
    QChar shortcut() const override { return QLatin1Char('T'); }
    QWidget* createOptions(QWidget* parent) override;

    void mousePress(CanvasView* v, const ToolEvent& e) override;
    bool keyPress(CanvasView* v, QKeyEvent* e) override;
    bool wantsKey(const QKeyEvent* e) const override;
    void paintOverlay(QPainter& p, CanvasView* v) override;
    QCursor cursor(CanvasView* v, Qt::KeyboardModifiers mods) const override;
    void deactivated(CanvasView* v) override;
    bool commit(CanvasView* v) override;
    bool cancel(CanvasView* v) override;

    bool isEditing() const { return !m_doc.isNull(); }
    // Starts editing the text layer at `index`, or a new one at `pos` when index is -1.
    bool beginEdit(CanvasView* v, int index, const QPointF& pos);
    // Inserts text at the caret (also used by tests).
    void insertText(const QString& text);
    void setCaret(int index);
    TextData textData() const { return m_text; }

private:
    void apply();          // pushes m_text into the layer being edited
    void optionsChanged(); // the options bar changed
    void syncOptions(const TextData& t);
    TextData fromOptions() const;
    void setEditingUi(bool on);

    // Options
    QString m_family;
    int m_size = 36;
    bool m_bold = false, m_italic = false;
    bool m_antialias = true;
    TextData::Align m_align = TextData::Align::Left;
    QPointer<QFontComboBox> m_fontBox;
    QPointer<QComboBox> m_styleBox;
    QPointer<QSpinBox> m_sizeBox;
    QPointer<QComboBox> m_aaBox;
    QPointer<QToolButton> m_alignButtons[3];
    QPointer<ColorButton> m_colorButton;
    QPointer<QToolButton> m_cancelButton;
    QPointer<QToolButton> m_commitButton;
    bool m_syncing = false;

    // Edit session
    QPointer<Document> m_doc;
    QPointer<CanvasView> m_view;
    quint64 m_layerId = 0;
    bool m_newLayer = false;
    DocState m_before;
    TextData m_text;
    int m_caret = 0;
    bool m_caretOn = true;
    QTimer m_blink;
};

// Rectangle, Ellipse, Polygon and Line tools (U): drag out a shape layer. Shift constrains,
// Alt draws from the centre. Changing the fill or stroke restyles the selected shape layer.
class ShapeTool : public Tool {
    Q_OBJECT
public:
    enum class Kind { Rectangle, Ellipse, Polygon, Line };
    ShapeTool(ToolManager* m, Kind kind);
    QString id() const override;
    QString name() const override;
    QString iconName() const override;
    QChar shortcut() const override { return QLatin1Char('U'); }
    QWidget* createOptions(QWidget* parent) override;

    void mousePress(CanvasView* v, const ToolEvent& e) override;
    void mouseMove(CanvasView* v, const ToolEvent& e) override;
    void mouseRelease(CanvasView* v, const ToolEvent& e) override;
    void paintOverlay(QPainter& p, CanvasView* v) override;
    QCursor cursor(CanvasView* v, Qt::KeyboardModifiers mods) const override;

    // The outline for a drag from `a` to `b` with the current options.
    QPainterPath outline(QPointF a, QPointF b, Qt::KeyboardModifiers mods) const;

private:
    void restyleActive();
    ShapeData style() const;

    Kind m_kind;
    // Shared by all shape tools, as in Photoshop.
    struct Settings {
        bool fill = true;
        QColor fillColor;
        bool stroke = false;
        QColor strokeColor = Qt::black;
        int strokeWidth = 3;
        int radius = 0;
        int sides = 5;
        bool star = false;
        int weight = 3;
    };
    static Settings& settings();

    QPointer<ColorButton> m_fillButton;
    QPointer<ColorButton> m_strokeButton;
    bool m_dragging = false;
    QPointF m_start, m_end;
    Qt::KeyboardModifiers m_mods;
};
