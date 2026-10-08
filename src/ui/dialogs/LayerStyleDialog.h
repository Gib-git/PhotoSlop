#pragma once

#include "core/BlendMode.h"
#include "core/LayerStyle.h"

#include <QDialog>
#include <QPointer>
#include <memory>

class BlendModeCombo;
class ColorButton;
class Document;
class QComboBox;
class QListWidget;
class QStackedWidget;
class SliderField;

// Layer > Layer Style: Blending Options plus Drop Shadow, Outer Glow, Color Overlay and
// Stroke, previewed live on the canvas. OK records one history state; Cancel restores.
class LayerStyleDialog : public QDialog {
    Q_OBJECT
public:
    enum Page { Blending, DropShadowPage, OuterGlowPage, ColorOverlayPage, StrokePage };

    LayerStyleDialog(Document* doc, int layerIndex, Page page = Blending, QWidget* parent = nullptr);
    ~LayerStyleDialog() override;

    LayerStyle style() const { return m_style; }
    void setStyle(const LayerStyle& style);
    void setPage(Page page);
    // Blending Options.
    void setOpacity(int percent);

    void accept() override;
    void reject() override;

private:
    void buildPages();
    void syncControls();
    void changed();  // a control changed: read it back and preview
    void preview();
    void restore();

    QPointer<Document> m_doc;
    int m_index;
    // What the layer had before, for Cancel and for the history step.
    std::shared_ptr<const LayerStyle> m_beforeStyle;
    BlendMode m_beforeMode;
    float m_beforeOpacity, m_beforeFill;

    LayerStyle m_style;
    BlendMode m_mode;
    int m_opacity, m_fill;
    bool m_syncing = false;
    bool m_done = false;

    QListWidget* m_list = nullptr;
    QStackedWidget* m_pages = nullptr;
    // Blending Options
    BlendModeCombo* m_blendMode = nullptr;
    SliderField* m_blendOpacity = nullptr;
    SliderField* m_blendFill = nullptr;
    // Drop Shadow
    BlendModeCombo* m_dsMode = nullptr;
    ColorButton* m_dsColor = nullptr;
    SliderField *m_dsOpacity = nullptr, *m_dsAngle = nullptr, *m_dsDistance = nullptr, *m_dsSpread = nullptr, *m_dsSize = nullptr;
    // Outer Glow
    BlendModeCombo* m_ogMode = nullptr;
    ColorButton* m_ogColor = nullptr;
    SliderField *m_ogOpacity = nullptr, *m_ogSpread = nullptr, *m_ogSize = nullptr;
    // Color Overlay
    BlendModeCombo* m_coMode = nullptr;
    ColorButton* m_coColor = nullptr;
    SliderField* m_coOpacity = nullptr;
    // Stroke
    SliderField* m_skSize = nullptr;
    QComboBox* m_skPosition = nullptr;
    BlendModeCombo* m_skMode = nullptr;
    SliderField* m_skOpacity = nullptr;
    ColorButton* m_skColor = nullptr;
};
