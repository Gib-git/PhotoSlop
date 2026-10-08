#include "ui/dialogs/LayerStyleDialog.h"

#include "core/Document.h"
#include "core/DocumentOps.h"
#include "ui/Widgets.h"
#include "ui/dialogs/AdjustmentDialogs.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace {

// List rows, in Photoshop's order.
constexpr LayerStyleDialog::Page kRows[] = {LayerStyleDialog::Blending, LayerStyleDialog::StrokePage,
                                            LayerStyleDialog::ColorOverlayPage, LayerStyleDialog::OuterGlowPage,
                                            LayerStyleDialog::DropShadowPage};

int rowOf(LayerStyleDialog::Page p)
{
    for (int i = 0; i < 5; ++i)
        if (kRows[i] == p) return i;
    return 0;
}

QWidget* pageWidget(const QString& title, QVBoxLayout*& lay)
{
    auto* w = new QWidget;
    auto* outer = new QVBoxLayout(w);
    outer->setContentsMargins(0, 0, 0, 0);
    auto* box = new QGroupBox(title, w);
    lay = new QVBoxLayout(box);
    outer->addWidget(box);
    outer->addStretch();
    return w;
}

QHBoxLayout* modeRow(QWidget* parent, BlendModeCombo*& mode, ColorButton*& color, const QString& colorTitle)
{
    auto* row = new QHBoxLayout;
    row->addWidget(new QLabel(QStringLiteral("Blend Mode:"), parent));
    mode = new BlendModeCombo(parent);
    row->addWidget(mode, 1);
    if (!colorTitle.isEmpty()) {
        color = new ColorButton(colorTitle, parent);
        row->addWidget(color);
    }
    return row;
}

} // namespace

LayerStyleDialog::LayerStyleDialog(Document* doc, int layerIndex, Page page, QWidget* parent)
    : QDialog(parent)
    , m_doc(doc)
    , m_index(layerIndex)
{
    setWindowTitle(QStringLiteral("Layer Style"));
    const Layer& l = doc->layerAt(layerIndex);
    m_beforeStyle = l.style;
    m_beforeMode = l.mode;
    m_beforeOpacity = l.opacity;
    m_beforeFill = l.fill;
    m_style = l.style ? *l.style : LayerStyle();
    m_style.visible = true;
    m_mode = l.mode;
    m_opacity = int(std::lround(l.opacity * 100));
    m_fill = int(std::lround(l.fill * 100));

    auto* root = new QHBoxLayout(this);
    m_list = new QListWidget(this);
    m_list->setFixedWidth(170);
    const char* names[] = {"Blending Options", "Stroke", "Color Overlay", "Outer Glow", "Drop Shadow"};
    for (int i = 0; i < 5; ++i) {
        auto* item = new QListWidgetItem(QString::fromLatin1(names[i]), m_list);
        if (i > 0) item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
    }
    root->addWidget(m_list);
    m_pages = new QStackedWidget(this);
    root->addWidget(m_pages, 1);
    buildPages();

    auto* buttons = new QVBoxLayout;
    auto* ok = new QPushButton(QStringLiteral("OK"), this);
    ok->setDefault(true);
    auto* cancel = new QPushButton(QStringLiteral("Cancel"), this);
    buttons->addWidget(ok);
    buttons->addWidget(cancel);
    buttons->addStretch();
    root->addLayout(buttons);
    connect(ok, &QPushButton::clicked, this, &QDialog::accept);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);

    connect(m_list, &QListWidget::currentRowChanged, this, [this](int row) {
        m_pages->setCurrentIndex(row);
        // Clicking an effect's name also turns it on, as in Photoshop.
        if (row > 0 && !m_syncing) {
            QListWidgetItem* it = m_list->item(row);
            if (it->checkState() != Qt::Checked) it->setCheckState(Qt::Checked);
        }
    });
    connect(m_list, &QListWidget::itemChanged, this, [this] { changed(); });

    syncControls();
    setPage(page);
    preview();
}

LayerStyleDialog::~LayerStyleDialog()
{
    if (!m_done) restore();
}

void LayerStyleDialog::buildPages()
{
    QVBoxLayout* lay = nullptr;
    auto slider = [&](const QString& label, double min, double max, double value, const QString& unit, double sliderMax = 0) {
        auto* s = new SliderField(label, min, max, value, 0, unit, sliderMax, this);
        connect(s, &SliderField::valueChanged, this, [this] { changed(); });
        lay->addWidget(s);
        return s;
    };
    auto hookMode = [this](BlendModeCombo* c) { connect(c, &BlendModeCombo::modeChanged, this, [this] { changed(); }); };
    auto hookColor = [this](ColorButton* c) { connect(c, &ColorButton::colorChanged, this, [this] { changed(); }); };

    // Blending Options
    QWidget* p = pageWidget(QStringLiteral("General Blending"), lay);
    auto* row = new QHBoxLayout;
    row->addWidget(new QLabel(QStringLiteral("Blend Mode:"), p));
    m_blendMode = new BlendModeCombo(p);
    row->addWidget(m_blendMode, 1);
    lay->addLayout(row);
    hookMode(m_blendMode);
    m_blendOpacity = slider(QStringLiteral("Opacity:"), 0, 100, m_opacity, QStringLiteral("%"));
    m_blendFill = slider(QStringLiteral("Fill Opacity:"), 0, 100, m_fill, QStringLiteral("%"));
    m_pages->addWidget(p);

    // Stroke
    p = pageWidget(QStringLiteral("Stroke"), lay);
    m_skSize = slider(QStringLiteral("Size:"), 1, 250, m_style.stroke.size, QStringLiteral("px"));
    row = new QHBoxLayout;
    row->addWidget(new QLabel(QStringLiteral("Position:"), p));
    m_skPosition = new QComboBox(p);
    m_skPosition->addItems({QStringLiteral("Outside"), QStringLiteral("Inside"), QStringLiteral("Center")});
    connect(m_skPosition, &QComboBox::currentIndexChanged, this, [this] { changed(); });
    row->addWidget(m_skPosition, 1);
    lay->addLayout(row);
    lay->addLayout(modeRow(p, m_skMode, m_skColor, QStringLiteral("Select stroke color:")));
    hookMode(m_skMode);
    hookColor(m_skColor);
    m_skOpacity = slider(QStringLiteral("Opacity:"), 0, 100, m_style.stroke.opacity, QStringLiteral("%"));
    m_pages->addWidget(p);

    // Color Overlay
    p = pageWidget(QStringLiteral("Color Overlay"), lay);
    lay->addLayout(modeRow(p, m_coMode, m_coColor, QStringLiteral("Select overlay color:")));
    hookMode(m_coMode);
    hookColor(m_coColor);
    m_coOpacity = slider(QStringLiteral("Opacity:"), 0, 100, m_style.colorOverlay.opacity, QStringLiteral("%"));
    m_pages->addWidget(p);

    // Outer Glow
    p = pageWidget(QStringLiteral("Outer Glow"), lay);
    lay->addLayout(modeRow(p, m_ogMode, m_ogColor, QStringLiteral("Select glow color:")));
    hookMode(m_ogMode);
    hookColor(m_ogColor);
    m_ogOpacity = slider(QStringLiteral("Opacity:"), 0, 100, m_style.outerGlow.opacity, QStringLiteral("%"));
    m_ogSpread = slider(QStringLiteral("Spread:"), 0, 100, m_style.outerGlow.spread, QStringLiteral("%"));
    m_ogSize = slider(QStringLiteral("Size:"), 0, 250, m_style.outerGlow.size, QStringLiteral("px"));
    m_pages->addWidget(p);

    // Drop Shadow
    p = pageWidget(QStringLiteral("Drop Shadow"), lay);
    lay->addLayout(modeRow(p, m_dsMode, m_dsColor, QStringLiteral("Select shadow color:")));
    hookMode(m_dsMode);
    hookColor(m_dsColor);
    m_dsOpacity = slider(QStringLiteral("Opacity:"), 0, 100, m_style.dropShadow.opacity, QStringLiteral("%"));
    m_dsAngle = slider(QStringLiteral("Angle:"), -180, 180, m_style.dropShadow.angle, QStringLiteral("°"));
    m_dsDistance = slider(QStringLiteral("Distance:"), 0, 30000, m_style.dropShadow.distance, QStringLiteral("px"), 250);
    m_dsSpread = slider(QStringLiteral("Spread:"), 0, 100, m_style.dropShadow.spread, QStringLiteral("%"));
    m_dsSize = slider(QStringLiteral("Size:"), 0, 250, m_style.dropShadow.size, QStringLiteral("px"));
    m_pages->addWidget(p);
}

void LayerStyleDialog::syncControls()
{
    m_syncing = true;
    const bool on[5] = {false, m_style.stroke.enabled, m_style.colorOverlay.enabled, m_style.outerGlow.enabled,
                        m_style.dropShadow.enabled};
    for (int i = 1; i < 5; ++i) m_list->item(i)->setCheckState(on[i] ? Qt::Checked : Qt::Unchecked);
    m_blendMode->setMode(m_mode);
    m_blendOpacity->setValue(m_opacity);
    m_blendFill->setValue(m_fill);

    m_skSize->setValue(m_style.stroke.size);
    {
        QSignalBlocker b(m_skPosition);
        m_skPosition->setCurrentIndex(int(m_style.stroke.position));
    }
    m_skMode->setMode(m_style.stroke.mode);
    m_skOpacity->setValue(m_style.stroke.opacity);
    m_skColor->setColor(m_style.stroke.color);

    m_coMode->setMode(m_style.colorOverlay.mode);
    m_coColor->setColor(m_style.colorOverlay.color);
    m_coOpacity->setValue(m_style.colorOverlay.opacity);

    m_ogMode->setMode(m_style.outerGlow.mode);
    m_ogColor->setColor(m_style.outerGlow.color);
    m_ogOpacity->setValue(m_style.outerGlow.opacity);
    m_ogSpread->setValue(m_style.outerGlow.spread);
    m_ogSize->setValue(m_style.outerGlow.size);

    m_dsMode->setMode(m_style.dropShadow.mode);
    m_dsColor->setColor(m_style.dropShadow.color);
    m_dsOpacity->setValue(m_style.dropShadow.opacity);
    m_dsAngle->setValue(m_style.dropShadow.angle);
    m_dsDistance->setValue(m_style.dropShadow.distance);
    m_dsSpread->setValue(m_style.dropShadow.spread);
    m_dsSize->setValue(m_style.dropShadow.size);
    m_syncing = false;
}

void LayerStyleDialog::changed()
{
    if (m_syncing) return;
    m_style.stroke.enabled = m_list->item(1)->checkState() == Qt::Checked;
    m_style.colorOverlay.enabled = m_list->item(2)->checkState() == Qt::Checked;
    m_style.outerGlow.enabled = m_list->item(3)->checkState() == Qt::Checked;
    m_style.dropShadow.enabled = m_list->item(4)->checkState() == Qt::Checked;
    m_mode = m_blendMode->mode();
    m_opacity = int(m_blendOpacity->value());
    m_fill = int(m_blendFill->value());

    m_style.stroke.size = int(m_skSize->value());
    m_style.stroke.position = StrokeEffect::Position(m_skPosition->currentIndex());
    m_style.stroke.mode = m_skMode->mode();
    m_style.stroke.opacity = int(m_skOpacity->value());
    m_style.stroke.color = m_skColor->color();

    m_style.colorOverlay.mode = m_coMode->mode();
    m_style.colorOverlay.color = m_coColor->color();
    m_style.colorOverlay.opacity = int(m_coOpacity->value());

    m_style.outerGlow.mode = m_ogMode->mode();
    m_style.outerGlow.color = m_ogColor->color();
    m_style.outerGlow.opacity = int(m_ogOpacity->value());
    m_style.outerGlow.spread = int(m_ogSpread->value());
    m_style.outerGlow.size = int(m_ogSize->value());

    m_style.dropShadow.mode = m_dsMode->mode();
    m_style.dropShadow.color = m_dsColor->color();
    m_style.dropShadow.opacity = int(m_dsOpacity->value());
    m_style.dropShadow.angle = int(m_dsAngle->value());
    m_style.dropShadow.distance = int(m_dsDistance->value());
    m_style.dropShadow.spread = int(m_dsSpread->value());
    m_style.dropShadow.size = int(m_dsSize->value());
    preview();
}

void LayerStyleDialog::setStyle(const LayerStyle& style)
{
    m_style = style;
    syncControls();
    preview();
}

void LayerStyleDialog::setPage(Page page)
{
    m_syncing = true;
    m_list->setCurrentRow(rowOf(page));
    m_syncing = false;
    // Opening on an effect's page turns it on.
    if (page != Blending && m_list->item(rowOf(page))->checkState() != Qt::Checked)
        m_list->item(rowOf(page))->setCheckState(Qt::Checked);
}

void LayerStyleDialog::setOpacity(int percent)
{
    m_blendOpacity->setValue(percent);
    changed();
}

void LayerStyleDialog::preview()
{
    if (!m_doc || m_index >= m_doc->layerCount()) return;
    Layer& l = m_doc->layerRef(m_index);
    l.style = m_style.anyEnabled() ? std::make_shared<const LayerStyle>(m_style) : nullptr;
    if (!l.isBackground) {
        l.mode = m_mode;
        l.opacity = m_opacity / 100.f;
        l.fill = m_fill / 100.f;
    }
    m_doc->invalidate();
}

void LayerStyleDialog::restore()
{
    if (!m_doc || m_index >= m_doc->layerCount()) return;
    Layer& l = m_doc->layerRef(m_index);
    l.style = m_beforeStyle;
    l.mode = m_beforeMode;
    l.opacity = m_beforeOpacity;
    l.fill = m_beforeFill;
    m_doc->invalidate();
}

void LayerStyleDialog::accept()
{
    m_done = true;
    restore();
    if (m_doc && m_index < m_doc->layerCount()) {
        const LayerStyle st = m_style;
        const BlendMode mode = m_mode;
        const float opacity = m_opacity / 100.f, fill = m_fill / 100.f;
        m_doc->modify(QStringLiteral("Layer Style"), [&] {
            Layer& l = m_doc->layerRef(m_index);
            l.style = st.anyEnabled() ? std::make_shared<const LayerStyle>(st) : nullptr;
            if (!l.isBackground) {
                l.mode = mode;
                l.opacity = opacity;
                l.fill = fill;
            }
        });
    }
    QDialog::accept();
}

void LayerStyleDialog::reject()
{
    m_done = true;
    restore();
    QDialog::reject();
}
