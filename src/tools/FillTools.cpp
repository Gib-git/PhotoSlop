#include "tools/FillTools.h"

#include "core/ColorState.h"
#include "core/Commands.h"
#include "core/Document.h"
#include "core/Selection.h"
#include "tools/ToolManager.h"
#include "ui/CanvasView.h"
#include "ui/Widgets.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <cmath>
#include <algorithm>
#include <iterator>

namespace {

constexpr double kPi = 3.14159265358979323846;

bool checkLayer(const Tool* tool, Document* doc, const QString& toolName)
{
    return tool->manager()->preparePixelEdit(doc, QStringLiteral("Could not use the %1").arg(toolName));
}

QIcon gradientIcon(const QGradientStops& stops)
{
    QPixmap pm(48, 16);
    QPainter p(&pm);
    p.fillRect(pm.rect(), QBrush(Qt::lightGray, Qt::Dense4Pattern));
    QLinearGradient g(0, 0, 48, 0);
    g.setStops(stops);
    p.fillRect(pm.rect(), g);
    return QIcon(pm);
}

} // namespace

// ---------------- Gradient ----------------

QGradientStops GradientTool::stops() const
{
    const QColor fg = colors()->foreground(), bg = colors()->background();
    QColor clear = fg;
    clear.setAlpha(0);
    switch (m_preset) {
    case Preset::FgToBg: return {{0.0, fg}, {1.0, bg}};
    case Preset::FgToTransparent: return {{0.0, fg}, {1.0, clear}};
    case Preset::BlackWhite: return {{0.0, Qt::black}, {1.0, Qt::white}};
    case Preset::Spectrum:
        return {{0.0, QColor(255, 0, 0)},     {0.17, QColor(255, 255, 0)}, {0.33, QColor(0, 255, 0)},
                {0.5, QColor(0, 255, 255)},   {0.67, QColor(0, 0, 255)},   {0.83, QColor(255, 0, 255)},
                {1.0, QColor(255, 0, 0)}};
    case Preset::Copper:
        return {{0.0, QColor(151, 70, 26)}, {0.3, QColor(251, 216, 197)}, {0.83, QColor(108, 46, 22)},
                {1.0, QColor(239, 219, 205)}};
    }
    return {};
}

QWidget* GradientTool::createOptions(QWidget* parent)
{
    auto* w = new QWidget(parent);
    auto* lay = new QHBoxLayout(w);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(6);

    auto* presets = new QComboBox(w);
    presets->setIconSize(QSize(48, 16));
    const char* names[] = {"Foreground to Background", "Foreground to Transparent", "Black, White",
                           "Spectrum", "Copper"};
    auto refreshIcons = [this, presets, names] {
        Preset saved = m_preset;
        for (int i = 0; i < 5; ++i) {
            m_preset = Preset(i);
            presets->setItemIcon(i, gradientIcon(stops()));
        }
        m_preset = saved;
        Q_UNUSED(names);
    };
    for (const char* n : names) presets->addItem(QString(), QString::fromLatin1(n));
    for (int i = 0; i < 5; ++i) presets->setItemData(i, QString::fromLatin1(names[i]), Qt::ToolTipRole);
    refreshIcons();
    presets->setMinimumWidth(80);
    connect(colors(), &ColorState::changed, presets, refreshIcons);
    connect(presets, &QComboBox::currentIndexChanged, this, [this](int i) { m_preset = Preset(i); });
    lay->addWidget(presets);
    lay->addWidget(makeVSeparator(w));

    auto* grp = new QButtonGroup(w);
    const struct { const char* icon; const char* tip; ImageOps::GradientType t; } types[] = {
        {"grad-linear", "Linear Gradient", ImageOps::GradientType::Linear},
        {"grad-radial", "Radial Gradient", ImageOps::GradientType::Radial},
        {"grad-angle", "Angle Gradient", ImageOps::GradientType::Angle},
        {"grad-reflected", "Reflected Gradient", ImageOps::GradientType::Reflected},
        {"grad-diamond", "Diamond Gradient", ImageOps::GradientType::Diamond},
    };
    for (const auto& t : types) {
        auto* b = makeIconButton(QString::fromLatin1(t.icon), QString::fromLatin1(t.tip), w, true);
        grp->addButton(b);
        if (t.t == m_type) b->setChecked(true);
        const auto type = t.t;
        connect(b, &QToolButton::toggled, this, [this, type](bool on) {
            if (on) m_type = type;
        });
        lay->addWidget(b);
    }
    lay->addWidget(makeVSeparator(w));
    lay->addWidget(new QLabel(QStringLiteral("Mode:"), w));
    auto* mode = new BlendModeCombo(w);
    connect(mode, &BlendModeCombo::modeChanged, this, [this](BlendMode m) { m_mode = m; });
    lay->addWidget(mode);
    m_opacityField = new ValueField(QStringLiteral("Opacity:"), 1, 100, QStringLiteral("%"), true, w);
    m_opacityField->setValue(m_opacity);
    connect(m_opacityField, &ValueField::valueChanged, this, [this](int v) { m_opacity = v; });
    lay->addWidget(m_opacityField);
    auto addCheck = [&](const QString& text, bool* target) {
        auto* cb = new QCheckBox(text, w);
        cb->setChecked(*target);
        connect(cb, &QCheckBox::toggled, this, [target](bool on) { *target = on; });
        lay->addWidget(cb);
    };
    addCheck(QStringLiteral("Reverse"), &m_reverse);
    addCheck(QStringLiteral("Dither"), &m_dither);
    addCheck(QStringLiteral("Transparency"), &m_transparency);
    lay->addStretch();
    connect(this, &Tool::optionsChanged, this, [this] {
        if (m_opacityField) m_opacityField->setValue(m_opacity);
    });
    return w;
}

bool GradientTool::setOpacityPercent(int v)
{
    m_opacity = std::clamp(v, 1, 100);
    emit optionsChanged();
    return true;
}

QPointF GradientTool::constrained(const QPointF& p, Qt::KeyboardModifiers mods) const
{
    if (!(mods & Qt::ShiftModifier)) return p;
    QPointF d = p - m_start;
    double ang = std::round(std::atan2(d.y(), d.x()) / (kPi / 4)) * (kPi / 4);
    double len = std::hypot(d.x(), d.y());
    return m_start + QPointF(std::cos(ang) * len, std::sin(ang) * len);
}

void GradientTool::mousePress(CanvasView* v, const ToolEvent& e)
{
    if (!checkLayer(this, v->document(), QStringLiteral("Gradient Tool"))) return;
    m_dragging = true;
    m_start = m_end = e.pos;
    emit overlayChanged();
}

void GradientTool::mouseMove(CanvasView*, const ToolEvent& e)
{
    if (!m_dragging) return;
    m_end = constrained(e.pos, e.mods);
    emit overlayChanged();
}

void GradientTool::mouseRelease(CanvasView* v, const ToolEvent& e)
{
    if (!m_dragging) return;
    m_dragging = false;
    m_end = constrained(e.pos, e.mods);
    emit overlayChanged();
    if (QLineF(m_start, m_end).length() < 1.0) return;

    Document* doc = v->document();
    const Layer* l = doc->editLayer();
    const bool preserve = l->lockTransparency && !l->isBackground;
    PixelEdit edit(doc, doc->editIndex(), preserve ? QRect() : doc->bounds());
    const QRect region = doc->hasSelection() ? doc->selectionBounds() : doc->bounds();
    QImage grad = ImageOps::renderGradient(doc->size(), m_start, m_end, m_type, stops(), m_reverse,
                                           m_dither, m_transparency);
    ImageOps::compositeImage(edit.layer(), grad, region, m_mode, m_opacity / 100.f, doc->selection(), preserve);
    edit.markDirty(region);
    edit.commit(QStringLiteral("Gradient"));
}

void GradientTool::paintOverlay(QPainter& p, CanvasView* v)
{
    if (!m_dragging) return;
    QPointF a = v->canvasToView(m_start), b = v->canvasToView(m_end);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(Qt::black, 3));
    p.drawLine(a, b);
    p.setPen(QPen(Qt::white, 1));
    p.drawLine(a, b);
    p.setBrush(Qt::white);
    p.setPen(QPen(Qt::black, 1));
    p.drawEllipse(a, 3, 3);
    p.drawEllipse(b, 3, 3);
}

// ---------------- Paint Bucket ----------------

QWidget* PaintBucketTool::createOptions(QWidget* parent)
{
    auto* w = new QWidget(parent);
    auto* lay = new QHBoxLayout(w);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(6);
    auto* source = new QComboBox(w);
    source->addItems({QStringLiteral("Foreground"), QStringLiteral("Pattern")});
    source->setItemData(1, 0, Qt::UserRole - 1); // disable "Pattern"
    lay->addWidget(source);
    lay->addWidget(makeVSeparator(w));
    lay->addWidget(new QLabel(QStringLiteral("Mode:"), w));
    auto* mode = new BlendModeCombo(w);
    connect(mode, &BlendModeCombo::modeChanged, this, [this](BlendMode m) { m_mode = m; });
    lay->addWidget(mode);
    m_opacityField = new ValueField(QStringLiteral("Opacity:"), 1, 100, QStringLiteral("%"), true, w);
    m_opacityField->setValue(m_opacity);
    connect(m_opacityField, &ValueField::valueChanged, this, [this](int v) { m_opacity = v; });
    lay->addWidget(m_opacityField);
    lay->addWidget(makeVSeparator(w));
    auto* tol = new ValueField(QStringLiteral("Tolerance:"), 0, 255, QString(), false, w);
    tol->setValue(m_tolerance);
    tol->setFieldWidth(40);
    connect(tol, &ValueField::valueChanged, this, [this](int v) { m_tolerance = v; });
    lay->addWidget(tol);
    auto addCheck = [&](const QString& text, bool* target) {
        auto* cb = new QCheckBox(text, w);
        cb->setChecked(*target);
        connect(cb, &QCheckBox::toggled, this, [target](bool on) { *target = on; });
        lay->addWidget(cb);
    };
    addCheck(QStringLiteral("Anti-alias"), &m_antialias);
    addCheck(QStringLiteral("Contiguous"), &m_contiguous);
    addCheck(QStringLiteral("All Layers"), &m_allLayers);
    lay->addStretch();
    connect(this, &Tool::optionsChanged, this, [this] {
        if (m_opacityField) m_opacityField->setValue(m_opacity);
    });
    return w;
}

bool PaintBucketTool::setOpacityPercent(int v)
{
    m_opacity = std::clamp(v, 1, 100);
    emit optionsChanged();
    return true;
}

QCursor PaintBucketTool::cursor(CanvasView*, Qt::KeyboardModifiers) const
{
    return makeIconCursor(QStringLiteral("tool-bucket"), QPoint(20, 19));
}

void PaintBucketTool::mousePress(CanvasView* v, const ToolEvent& e)
{
    Document* doc = v->document();
    const QPoint px = e.pixel();
    if (!doc->bounds().contains(px)) return;
    if (!checkLayer(this, doc, QStringLiteral("Paint Bucket"))) return;
    const Layer* l = doc->editLayer();
    QImage sample = m_allLayers ? doc->composite() : l->toCanvasImage(doc->size());
    QImage mask = ImageOps::floodMask(sample, px, m_tolerance, m_contiguous, m_antialias);
    QRect region = Sel::bounds(mask);
    if (region.isEmpty()) return;
    const bool preserve = l->lockTransparency && !l->isBackground;
    PixelEdit edit(doc, doc->editIndex(), preserve ? QRect() : region);
    ImageOps::fillColor(edit.layer(), region, ImageOps::premultiplied(colors()->foreground()), m_mode,
                        m_opacity / 100.f, doc->selection(), preserve, mask);
    edit.markDirty(region);
    edit.commit(QStringLiteral("Paint Bucket"));
}

// ---------------- Eyedropper ----------------

QWidget* EyedropperTool::createOptions(QWidget* parent)
{
    auto* w = new QWidget(parent);
    auto* lay = new QHBoxLayout(w);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(6);
    lay->addWidget(new QLabel(QStringLiteral("Sample Size:"), w));
    auto* size = new QComboBox(w);
    const int sizes[] = {1, 3, 5, 11, 31, 51, 101};
    for (int s : sizes)
        size->addItem(s == 1 ? QStringLiteral("Point Sample") : QStringLiteral("%1 by %1 Average").arg(s), s);
    connect(size, &QComboBox::currentIndexChanged, this, [this, size](int) { m_sampleSize = size->currentData().toInt(); });
    lay->addWidget(size);
    lay->addWidget(new QLabel(QStringLiteral("Sample:"), w));
    auto* src = new QComboBox(w);
    src->addItems({QStringLiteral("Current Layer"), QStringLiteral("All Layers")});
    src->setCurrentIndex(m_allLayers ? 1 : 0);
    connect(src, &QComboBox::currentIndexChanged, this, [this](int i) { m_allLayers = i == 1; });
    lay->addWidget(src);
    auto* ring = new QCheckBox(QStringLiteral("Show Sampling Ring"), w);
    ring->setEnabled(false);
    lay->addWidget(ring);
    lay->addStretch();
    return w;
}

QCursor EyedropperTool::cursor(CanvasView*, Qt::KeyboardModifiers) const
{
    return makeIconCursor(QStringLiteral("tool-eyedropper"), QPoint(4, 19));
}

void EyedropperTool::sample(CanvasView* v, const ToolEvent& e)
{
    Document* doc = v->document();
    const QPoint c = e.pixel();
    if (!doc->bounds().contains(c)) return;
    const int half = m_sampleSize / 2;
    const QRect area = QRect(c.x() - half, c.y() - half, m_sampleSize, m_sampleSize) & doc->bounds();
    const Layer* layer = doc->editLayer();
    const QImage& comp = doc->composite();
    quint64 r = 0, g = 0, b = 0, a = 0;
    int n = 0;
    for (int y = area.top(); y <= area.bottom(); ++y) {
        for (int x = area.left(); x <= area.right(); ++x) {
            QRgb p = m_allLayers ? comp.pixel(x, y) : layer->pixelAt(QPoint(x, y));
            r += qRed(p);
            g += qGreen(p);
            b += qBlue(p);
            a += qAlpha(p);
            ++n;
        }
    }
    if (!n || !a) return;
    // Average premultiplied values, then unpremultiply.
    QColor col(int(std::min<quint64>(255, r * 255 / a)), int(std::min<quint64>(255, g * 255 / a)),
               int(std::min<quint64>(255, b * 255 / a)));
    // Alt picks the background colour, except when Alt itself summoned the eyedropper.
    if (e.alt() && !m_manager->hasTemporary()) colors()->setBackground(col);
    else colors()->setForeground(col);
}

void EyedropperTool::mousePress(CanvasView* v, const ToolEvent& e)
{
    m_pressed = true;
    sample(v, e);
}

void EyedropperTool::mouseMove(CanvasView* v, const ToolEvent& e)
{
    if (m_pressed) sample(v, e);
}

void EyedropperTool::mouseRelease(CanvasView*, const ToolEvent&) { m_pressed = false; }
