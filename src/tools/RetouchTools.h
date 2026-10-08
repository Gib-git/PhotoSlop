#pragma once

#include "core/Document.h"
#include "tools/PaintTools.h"

#include <QHash>
#include <QPointer>
#include <array>
#include <vector>

class QComboBox;

// Retouching tools built on the brush stroke engine: each supplies the pixels a stroke
// paints, and the engine handles the tip, spacing, Flow and the Opacity cap.

// Clone Stamp (S) and Healing Brush (J): Alt-click sets the source point; painting copies
// from it. The Healing Brush then blends the copy into its surroundings.
class CloneStampTool : public BrushTool {
    Q_OBJECT
public:
    CloneStampTool(ToolManager* m, bool healing = false);
    QString id() const override { return m_healing ? QStringLiteral("healing-brush") : QStringLiteral("clone-stamp"); }
    QString name() const override { return m_healing ? QStringLiteral("Healing Brush Tool") : QStringLiteral("Clone Stamp Tool"); }
    QString iconName() const override { return m_healing ? QStringLiteral("tool-healing") : QStringLiteral("tool-clone-stamp"); }
    QChar shortcut() const override { return m_healing ? QLatin1Char('J') : QLatin1Char('S'); }
    QWidget* createOptions(QWidget* parent) override;
    void mousePress(CanvasView* v, const ToolEvent& e) override;
    void mouseMove(CanvasView* v, const ToolEvent& e) override;
    void paintOverlay(QPainter& p, CanvasView* v) override;
    QCursor cursor(CanvasView* v, Qt::KeyboardModifiers mods) const override;

    bool hasSource() const { return !m_sourceDoc.isNull(); }
    void setSource(Document* doc, const QPointF& pt);

protected:
    QString strokeName() const override { return m_healing ? QStringLiteral("Healing Brush") : QStringLiteral("Clone Stamp"); }
    bool strokeStarting(CanvasView* v, const ToolEvent& e) override;
    void sourceRow(int y, int x0, int count, uint32_t* out) override;
    void strokeFinishing() override;

private:
    bool m_healing;
    bool m_aligned = true;
    int m_sample = 0; // 0 current layer, 1 current and below, 2 all layers
    QPointer<Document> m_sourceDoc;
    QPointF m_sourcePt;
    QPoint m_offset;        // source minus destination
    bool m_offsetSet = false;
    QImage m_sourceImage;   // canvas-positioned at m_sourceOrigin
    QPoint m_sourceOrigin;
    QPointF m_cursor;
    bool m_painting = false;
};

// Spot Healing Brush (J): paint over a blemish; on release it is replaced by nearby texture
// matched to the surroundings (Proximity Match).
class SpotHealingTool : public BrushTool {
    Q_OBJECT
public:
    explicit SpotHealingTool(ToolManager* m);
    QString id() const override { return QStringLiteral("spot-healing"); }
    QString name() const override { return QStringLiteral("Spot Healing Brush Tool"); }
    QString iconName() const override { return QStringLiteral("tool-spot-healing"); }
    QChar shortcut() const override { return QLatin1Char('J'); }
    QWidget* createOptions(QWidget* parent) override;

protected:
    QString strokeName() const override { return QStringLiteral("Spot Healing Brush"); }
    bool mixesSource() const override { return true; }
    void sourceRow(int y, int x0, int count, uint32_t* out) override;
    void strokeFinishing() override;
};

// History Brush (Y): paints back the pixels of the History Brush source state.
class HistoryBrushTool : public BrushTool {
    Q_OBJECT
public:
    explicit HistoryBrushTool(ToolManager* m);
    QString id() const override { return QStringLiteral("history-brush"); }
    QString name() const override { return QStringLiteral("History Brush Tool"); }
    QString iconName() const override { return QStringLiteral("tool-history-brush"); }
    QChar shortcut() const override { return QLatin1Char('Y'); }
    QWidget* createOptions(QWidget* parent) override;

protected:
    QString strokeName() const override { return QStringLiteral("History Brush"); }
    bool strokeStarting(CanvasView* v, const ToolEvent& e) override;
    bool mixesSource() const override { return true; }
    void sourceRow(int y, int x0, int count, uint32_t* out) override;

private:
    Layer m_source;
};

// Dodge, Burn (O) and Sponge: lighten, darken or change saturation where painted.
class ToningTool : public BrushTool {
    Q_OBJECT
public:
    enum class Kind { Dodge, Burn, Sponge };
    ToningTool(ToolManager* m, Kind kind);
    QString id() const override;
    QString name() const override;
    QString iconName() const override;
    QChar shortcut() const override { return QLatin1Char('O'); }
    QWidget* createOptions(QWidget* parent) override;

protected:
    QString strokeName() const override;
    bool strokeStarting(CanvasView* v, const ToolEvent& e) override;
    bool mixesSource() const override { return true; }
    void sourceRow(int y, int x0, int count, uint32_t* out) override;

private:
    Kind m_tone;
    int m_range = 1;          // 0 shadows, 1 midtones, 2 highlights
    bool m_saturate = false;  // Sponge mode
    std::array<uint8_t, 256> m_lut{};
};

// Blur and Sharpen: soften or crisp up the painted area.
class FocusTool : public BrushTool {
    Q_OBJECT
public:
    FocusTool(ToolManager* m, bool sharpen);
    QString id() const override { return m_sharpen ? QStringLiteral("sharpen") : QStringLiteral("blur"); }
    QString name() const override { return m_sharpen ? QStringLiteral("Sharpen Tool") : QStringLiteral("Blur Tool"); }
    QString iconName() const override { return m_sharpen ? QStringLiteral("tool-sharpen") : QStringLiteral("tool-blur"); }
    QChar shortcut() const override { return QChar(); }
    QWidget* createOptions(QWidget* parent) override;

protected:
    QString strokeName() const override { return m_sharpen ? QStringLiteral("Sharpen Tool") : QStringLiteral("Blur Tool"); }
    bool strokeStarting(CanvasView* v, const ToolEvent& e) override;
    bool mixesSource() const override { return true; }
    void sourceRow(int y, int x0, int count, uint32_t* out) override;

private:
    const QImage& tile(int tx, int ty);
    bool m_sharpen;
    QHash<quint64, QImage> m_tiles; // filtered original, 128 px tiles
};

// Smudge: drags colour along the stroke.
class SmudgeTool : public BrushTool {
    Q_OBJECT
public:
    explicit SmudgeTool(ToolManager* m);
    QString id() const override { return QStringLiteral("smudge"); }
    QString name() const override { return QStringLiteral("Smudge Tool"); }
    QString iconName() const override { return QStringLiteral("tool-smudge"); }
    QChar shortcut() const override { return QChar(); }
    QWidget* createOptions(QWidget* parent) override;
    void mousePress(CanvasView* v, const ToolEvent& e) override;
    void mouseMove(CanvasView* v, const ToolEvent& e) override;
    void mouseRelease(CanvasView* v, const ToolEvent& e) override;

protected:
    QString strokeName() const override { return QStringLiteral("Smudge Tool"); }

private:
    void smudgeDab(const QPointF& c);
    std::vector<uint32_t> m_pickup; // picked-up colour, brush-sized
    int m_pickupSize = 0;
    bool m_smudging = false;
    QPointF m_lastPos;
};
