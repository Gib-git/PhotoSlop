#include "tools/RetouchTools.h"

#include "core/Adjustments.h"
#include "core/Commands.h"
#include "core/Compositor.h"
#include "core/Filters.h"
#include "core/Healing.h"
#include "core/LayerTree.h"
#include "tools/ToolManager.h"
#include "ui/CanvasView.h"
#include "ui/Widgets.h"

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <algorithm>
#include <cmath>

namespace {

QComboBox* labelledCombo(QWidget* w, QHBoxLayout* lay, const QString& label, const QStringList& items, int current)
{
    lay->addWidget(new QLabel(label, w));
    auto* c = new QComboBox(w);
    c->addItems(items);
    c->setCurrentIndex(current);
    lay->addWidget(c);
    return c;
}

QHBoxLayout* optionsLayout(QWidget* w)
{
    auto* lay = new QHBoxLayout(w);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(6);
    return lay;
}

// Writes `pixels` (covering `r`) mixed with the stroke's starting pixels by `cov`.
void writeMixed(Layer& layer, const QRect& r, const QImage& pixels, const QImage& original, const QPoint& origOffset,
                const std::vector<uint8_t>& cov)
{
    for (int y = r.top(); y <= r.bottom(); ++y) {
        auto* d = reinterpret_cast<uint32_t*>(layer.image.scanLine(y - layer.offset.y())) + (r.left() - layer.offset.x());
        const auto* o = reinterpret_cast<const uint32_t*>(original.constScanLine(y - origOffset.y())) + (r.left() - origOffset.x());
        const auto* p = reinterpret_cast<const uint32_t*>(pixels.constScanLine(y - r.top()));
        const uint8_t* c = cov.data() + size_t(y - r.top()) * size_t(r.width());
        for (int x = 0; x < r.width(); ++x)
            d[x] = c[x] == 255 ? p[x] : c[x] ? Blend::byteMul(p[x], c[x]) + Blend::byteMul(o[x], 255 - c[x]) : o[x];
    }
}

} // namespace

// ---------------- Clone Stamp / Healing Brush ----------------

CloneStampTool::CloneStampTool(ToolManager* m, bool healing)
    : BrushTool(m, BrushTool::Kind::Brush)
    , m_healing(healing)
{
    m_size = healing ? 19 : 21;
    m_hardness = healing ? 100 : 0;
}

QWidget* CloneStampTool::createOptions(QWidget* parent)
{
    auto* w = new QWidget(parent);
    auto* lay = optionsLayout(w);
    addBrushOptions(w, lay, QStringLiteral("Opacity:"), !m_healing, !m_healing);
    lay->addWidget(makeVSeparator(w));
    auto* aligned = new QCheckBox(QStringLiteral("Aligned"), w);
    aligned->setChecked(m_aligned);
    connect(aligned, &QCheckBox::toggled, this, [this](bool on) {
        m_aligned = on;
        m_offsetSet = false;
    });
    lay->addWidget(aligned);
    auto* sample = labelledCombo(w, lay, QStringLiteral("Sample:"),
                                 {QStringLiteral("Current Layer"), QStringLiteral("Current & Below"), QStringLiteral("All Layers")},
                                 m_sample);
    connect(sample, &QComboBox::currentIndexChanged, this, [this](int i) { m_sample = i; });
    lay->addStretch();
    return w;
}

void CloneStampTool::setSource(Document* doc, const QPointF& pt)
{
    m_sourceDoc = doc;
    m_sourcePt = pt;
    m_offsetSet = false;
}

void CloneStampTool::mousePress(CanvasView* v, const ToolEvent& e)
{
    m_cursor = e.pos;
    if (e.alt()) {
        setSource(v->document(), e.pos);
        emit overlayChanged();
        return;
    }
    if (!hasSource()) {
#ifdef Q_OS_MACOS
        const QString click = QStringLiteral("Option-click");
#else
        const QString click = QStringLiteral("Alt-click");
#endif
        alert(QStringLiteral("Could not use the %1 because the area to %2 has not been defined (%3 to define a source point).")
                  .arg(m_healing ? QStringLiteral("healing brush") : QStringLiteral("clone stamp"),
                       m_healing ? QStringLiteral("heal") : QStringLiteral("clone"), click));
        return;
    }
    m_painting = true;
    BrushTool::mousePress(v, e);
    if (!m_edit) m_painting = false;
    emit overlayChanged();
}

void CloneStampTool::mouseMove(CanvasView* v, const ToolEvent& e)
{
    m_cursor = e.pos;
    BrushTool::mouseMove(v, e);
    if (m_painting) emit overlayChanged();
}

bool CloneStampTool::strokeStarting(CanvasView* v, const ToolEvent& e)
{
    Document* doc = v->document();
    if (!m_offsetSet || !m_aligned) {
        const QPointF d = m_sourcePt - e.pos;
        m_offset = QPoint(int(std::lround(d.x())), int(std::lround(d.y())));
        m_offsetSet = true;
    }
    // The source pixels are frozen for the stroke, so it never copies what it just painted.
    Document* src = m_sourceDoc ? m_sourceDoc.data() : doc;
    if (src == doc && m_sample == 0) {
        m_sourceImage = m_original;
        m_sourceOrigin = m_originalOffset;
    } else if (src == doc && m_sample == 1) {
        QList<Layer> ls = doc->layers();
        const int active = doc->activeIndex();
        for (int i = 0; i < ls.size(); ++i)
            if (i > active && !Tree::isInside(ls, active, i)) ls[i].visible = false;
        m_sourceImage = Compositor::flatten(ls, doc->bounds());
        m_sourceOrigin = QPoint();
    } else {
        m_sourceImage = src->composite().copy();
        m_sourceOrigin = QPoint();
    }
    return true;
}

void CloneStampTool::sourceRow(int y, int x0, int count, uint32_t* out)
{
    const int sy = y + m_offset.y() - m_sourceOrigin.y();
    if (sy < 0 || sy >= m_sourceImage.height()) {
        std::fill(out, out + count, 0u);
        return;
    }
    const auto* row = reinterpret_cast<const uint32_t*>(m_sourceImage.constScanLine(sy));
    const int w = m_sourceImage.width();
    for (int i = 0; i < count; ++i) {
        const int sx = x0 + i + m_offset.x() - m_sourceOrigin.x();
        out[i] = sx >= 0 && sx < w ? row[sx] : 0u;
    }
}

void CloneStampTool::strokeFinishing()
{
    m_painting = false;
    emit overlayChanged();
    if (!m_healing || !m_edit) return;
    // Re-blend the copied pixels so their colours meet the surroundings.
    QRect bounds;
    std::vector<uint8_t> cov = strokeCoverage(&bounds);
    if (bounds.isEmpty()) return;
    const QRect r = bounds.adjusted(-2, -2, 2, 2) & m_maskRect;
    std::vector<uint8_t> region(size_t(r.width()) * size_t(r.height()), 0), mix(region.size(), 0);
    for (int y = bounds.top(); y <= bounds.bottom(); ++y)
        for (int x = bounds.left(); x <= bounds.right(); ++x) {
            const uint8_t c = cov[size_t(y - bounds.top()) * size_t(bounds.width()) + size_t(x - bounds.left())];
            const size_t i = size_t(y - r.top()) * size_t(r.width()) + size_t(x - r.left());
            region[i] = c ? 1 : 0;
            mix[i] = c;
        }
    QImage target(r.size(), QImage::Format_ARGB32_Premultiplied), source(r.size(), QImage::Format_ARGB32_Premultiplied);
    for (int y = r.top(); y <= r.bottom(); ++y) {
        auto* t = reinterpret_cast<uint32_t*>(target.scanLine(y - r.top()));
        for (int x = r.left(); x <= r.right(); ++x) t[x - r.left()] = originalAt(x, y);
        sourceRow(y, r.left(), r.width(), reinterpret_cast<uint32_t*>(source.scanLine(y - r.top())));
    }
    const QImage healed = Heal::blend(target, source, region);
    writeMixed(m_edit->layer(), r, healed, m_original, m_originalOffset, mix);
    m_edit->markDirty(r);
}

void CloneStampTool::paintOverlay(QPainter& p, CanvasView* v)
{
    if (!m_painting) return;
    // Crosshair where the source is being sampled.
    const QPointF c = v->canvasToView(m_cursor + QPointF(m_offset));
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setCompositionMode(QPainter::CompositionMode_Difference);
    p.setPen(QPen(Qt::white, 1));
    p.drawLine(QPointF(c.x() - 6, c.y()), QPointF(c.x() + 6, c.y()));
    p.drawLine(QPointF(c.x(), c.y() - 6), QPointF(c.x(), c.y() + 6));
}

QCursor CloneStampTool::cursor(CanvasView*, Qt::KeyboardModifiers mods) const
{
    return (mods & Qt::AltModifier) ? QCursor(Qt::CrossCursor) : QCursor(Qt::ArrowCursor);
}

// ---------------- Spot Healing Brush ----------------

SpotHealingTool::SpotHealingTool(ToolManager* m)
    : BrushTool(m, BrushTool::Kind::Brush)
{
    m_size = 19;
}

QWidget* SpotHealingTool::createOptions(QWidget* parent)
{
    auto* w = new QWidget(parent);
    auto* lay = optionsLayout(w);
    addBrushOptions(w, lay, QStringLiteral("Opacity:"), false, false, false); // healing always replaces fully
    auto* type = labelledCombo(w, lay, QStringLiteral("Type:"), {QStringLiteral("Proximity Match")}, 0);
    type->setToolTip(QStringLiteral("Uses the pixels around the selection edge to find an area to use as a patch"));
    lay->addStretch();
    return w;
}

void SpotHealingTool::sourceRow(int y, int x0, int count, uint32_t* out)
{
    // While painting, the stroke shows as a dark tint over the area to heal.
    for (int i = 0; i < count; ++i) {
        const uint32_t o = originalAt(x0 + i, y);
        out[i] = Blend::byteMul(o, 140) + 0x73000000u;
    }
}

void SpotHealingTool::strokeFinishing()
{
    if (!m_edit) return;
    QRect bounds;
    std::vector<uint8_t> cov = strokeCoverage(&bounds);
    if (bounds.isEmpty()) return;
    const QRect r = bounds.adjusted(-2, -2, 2, 2) & m_maskRect;
    std::vector<uint8_t> region(size_t(r.width()) * size_t(r.height()), 0), mix(region.size(), 0);
    for (int y = bounds.top(); y <= bounds.bottom(); ++y)
        for (int x = bounds.left(); x <= bounds.right(); ++x) {
            const uint8_t c = cov[size_t(y - bounds.top()) * size_t(bounds.width()) + size_t(x - bounds.left())];
            const size_t i = size_t(y - r.top()) * size_t(r.width()) + size_t(x - r.left());
            region[i] = c ? 1 : 0;
            mix[i] = c;
        }
    const QRect imageRect(m_originalOffset, m_original.size());
    const QPoint off = Heal::findSource(m_original, imageRect, region, r);
    QImage target(r.size(), QImage::Format_ARGB32_Premultiplied), source(r.size(), QImage::Format_ARGB32_Premultiplied);
    for (int y = r.top(); y <= r.bottom(); ++y) {
        auto* t = reinterpret_cast<uint32_t*>(target.scanLine(y - r.top()));
        auto* s = reinterpret_cast<uint32_t*>(source.scanLine(y - r.top()));
        for (int x = r.left(); x <= r.right(); ++x) {
            t[x - r.left()] = originalAt(x, y);
            s[x - r.left()] = originalAt(x + off.x(), y + off.y());
        }
    }
    const QImage healed = Heal::blend(target, source, region);
    writeMixed(m_edit->layer(), r, healed, m_original, m_originalOffset, mix);
    m_edit->markDirty(r);
}

// ---------------- History Brush ----------------

HistoryBrushTool::HistoryBrushTool(ToolManager* m)
    : BrushTool(m, BrushTool::Kind::Brush)
{
    m_size = 21;
    m_hardness = 0;
}

QWidget* HistoryBrushTool::createOptions(QWidget* parent)
{
    auto* w = new QWidget(parent);
    auto* lay = optionsLayout(w);
    addBrushOptions(w, lay);
    lay->addStretch();
    return w;
}

bool HistoryBrushTool::strokeStarting(CanvasView* v, const ToolEvent&)
{
    Document* doc = v->document();
    const DocState& src = doc->historySource();
    const QString fail = QStringLiteral("Could not use the history brush because the history state does not contain a corresponding layer.");
    if (src.size != doc->size()) {
        alert(QStringLiteral("Could not use the history brush because the history state has a different canvas size."));
        return false;
    }
    const int idx = doc->editIndex();
    if (idx == Document::kQuickMaskIndex) {
        alert(fail);
        return false;
    }
    const bool mask = Document::isMaskIndex(idx);
    const quint64 id = doc->layerAt(mask ? Document::maskOwner(idx) : idx).id;
    for (const Layer& l : src.layers) {
        if (l.id != id) continue;
        if (mask && !l.mask) break;
        m_source = mask ? *l.mask : l;
        // A layer that was text or a shape then is plain pixels now.
        m_source.kind = LayerKind::Pixel;
        return true;
    }
    alert(fail);
    return false;
}

void HistoryBrushTool::sourceRow(int y, int x0, int count, uint32_t* out)
{
    for (int i = 0; i < count; ++i) out[i] = m_source.pixelAt(QPoint(x0 + i, y));
}

// ---------------- Dodge / Burn / Sponge ----------------

ToningTool::ToningTool(ToolManager* m, Kind kind)
    : BrushTool(m, BrushTool::Kind::Brush)
    , m_tone(kind)
{
    m_size = 65;
    m_hardness = 0;
    m_opacity = 50;
}

QString ToningTool::id() const
{
    switch (m_tone) {
    case Kind::Dodge: return QStringLiteral("dodge");
    case Kind::Burn: return QStringLiteral("burn");
    case Kind::Sponge: return QStringLiteral("sponge");
    }
    return {};
}

QString ToningTool::name() const
{
    switch (m_tone) {
    case Kind::Dodge: return QStringLiteral("Dodge Tool");
    case Kind::Burn: return QStringLiteral("Burn Tool");
    case Kind::Sponge: return QStringLiteral("Sponge Tool");
    }
    return {};
}

QString ToningTool::iconName() const
{
    switch (m_tone) {
    case Kind::Dodge: return QStringLiteral("tool-dodge");
    case Kind::Burn: return QStringLiteral("tool-burn");
    case Kind::Sponge: return QStringLiteral("tool-sponge");
    }
    return {};
}

QString ToningTool::strokeName() const
{
    switch (m_tone) {
    case Kind::Dodge: return QStringLiteral("Dodge Tool");
    case Kind::Burn: return QStringLiteral("Burn Tool");
    case Kind::Sponge: return QStringLiteral("Sponge Tool");
    }
    return {};
}

QWidget* ToningTool::createOptions(QWidget* parent)
{
    auto* w = new QWidget(parent);
    auto* lay = optionsLayout(w);
    const bool sponge = m_tone == Kind::Sponge;
    addBrushOptions(w, lay, sponge ? QStringLiteral("Flow:") : QStringLiteral("Exposure:"), false, false);
    // Range (or the Sponge's Mode) sits after the brush picker, as in Photoshop.
    auto* label = new QLabel(sponge ? QStringLiteral("Mode:") : QStringLiteral("Range:"), w);
    auto* box = new QComboBox(w);
    if (sponge) {
        box->addItems({QStringLiteral("Desaturate"), QStringLiteral("Saturate")});
        box->setCurrentIndex(m_saturate ? 1 : 0);
        connect(box, &QComboBox::currentIndexChanged, this, [this](int i) { m_saturate = i == 1; });
    } else {
        box->addItems({QStringLiteral("Shadows"), QStringLiteral("Midtones"), QStringLiteral("Highlights")});
        box->setCurrentIndex(m_range);
        connect(box, &QComboBox::currentIndexChanged, this, [this](int i) { m_range = i; });
    }
    lay->insertWidget(3, label);
    lay->insertWidget(4, box);
    lay->insertWidget(5, makeVSeparator(w));
    lay->addStretch();
    return w;
}

bool ToningTool::strokeStarting(CanvasView*, const ToolEvent&)
{
    if (m_tone == Kind::Sponge) return true;
    // Full-strength curves; Exposure and the brush mix them in.
    const double e = m_tone == Kind::Dodge ? 1.0 : -1.0;
    for (int i = 0; i < 256; ++i) {
        const double v = i / 255.0;
        double out = v;
        switch (m_range) {
        case 0: // shadows
            if (e >= 0) {
                const double f = e / 3.0;
                out = f + v - f * v;
            } else {
                const double f = -e / 3.0;
                out = v < f ? 0.0 : (v - f) / (1.0 - f);
            }
            break;
        case 1: // midtones
            out = std::pow(v, e >= 0 ? 1.0 / (1.0 + e) : 1.0 - e / 3.0);
            break;
        case 2: { // highlights
            const double f = 1.0 + std::fabs(e) / 3.0;
            out = e >= 0 ? v * f : v / f;
            break;
        }
        }
        m_lut[size_t(i)] = uint8_t(std::clamp(std::lround(out * 255.0), 0L, 255L));
    }
    return true;
}

void ToningTool::sourceRow(int y, int x0, int count, uint32_t* out)
{
    for (int i = 0; i < count; ++i) {
        const uint32_t o = originalAt(x0 + i, y);
        const int a = int(o >> 24);
        if (!a) {
            out[i] = 0;
            continue;
        }
        const QRgb s = qUnpremultiply(o);
        int r = qRed(s), g = qGreen(s), b = qBlue(s);
        if (m_tone == Kind::Sponge) {
            double h, sat, l;
            Adjust::rgbToHsl(r, g, b, h, sat, l);
            sat = m_saturate ? std::min(1.0, sat * 2.0 + 0.05) : 0.0;
            out[i] = qPremultiply(Adjust::hslToRgb(h, sat, l, a));
            continue;
        }
        out[i] = qPremultiply(qRgba(m_lut[size_t(r)], m_lut[size_t(g)], m_lut[size_t(b)], a));
    }
}

// ---------------- Blur / Sharpen ----------------

FocusTool::FocusTool(ToolManager* m, bool sharpen)
    : BrushTool(m, BrushTool::Kind::Brush)
    , m_sharpen(sharpen)
{
    m_size = 13;
    m_hardness = 0;
    m_opacity = 50;
}

QWidget* FocusTool::createOptions(QWidget* parent)
{
    auto* w = new QWidget(parent);
    auto* lay = optionsLayout(w);
    addBrushOptions(w, lay, QStringLiteral("Strength:"), false, false);
    lay->addStretch();
    return w;
}

bool FocusTool::strokeStarting(CanvasView*, const ToolEvent&)
{
    m_tiles.clear();
    return true;
}

const QImage& FocusTool::tile(int tx, int ty)
{
    const quint64 key = (quint64(quint32(tx)) << 32) | quint32(ty);
    auto it = m_tiles.find(key);
    if (it != m_tiles.end()) return *it;
    constexpr int kTile = 128, kMargin = 6;
    const QRect t(tx * kTile, ty * kTile, kTile, kTile);
    const QRect src = t.adjusted(-kMargin, -kMargin, kMargin, kMargin);
    const QImage region = m_original.copy(src.translated(-m_originalOffset));
    QImage filtered = m_sharpen ? Filters::unsharpMask(region, 120, 1.0, 0) : Filters::gaussianBlur(region, 1.5);
    return *m_tiles.insert(key, filtered.copy(QRect(QPoint(kMargin, kMargin), t.size())));
}

void FocusTool::sourceRow(int y, int x0, int count, uint32_t* out)
{
    constexpr int kTile = 128;
    const int ty = int(std::floor(y / double(kTile)));
    for (int i = 0; i < count;) {
        const int x = x0 + i;
        const int tx = int(std::floor(x / double(kTile)));
        const QImage& img = tile(tx, ty);
        const auto* row = reinterpret_cast<const uint32_t*>(img.constScanLine(y - ty * kTile));
        const int end = std::min(count, (tx + 1) * kTile - x0);
        for (; i < end; ++i) out[i] = row[x0 + i - tx * kTile];
    }
}

// ---------------- Smudge ----------------

SmudgeTool::SmudgeTool(ToolManager* m)
    : BrushTool(m, BrushTool::Kind::Brush)
{
    m_size = 13;
    m_hardness = 0;
    m_opacity = 50;
}

QWidget* SmudgeTool::createOptions(QWidget* parent)
{
    auto* w = new QWidget(parent);
    auto* lay = optionsLayout(w);
    addBrushOptions(w, lay, QStringLiteral("Strength:"), false, false);
    lay->addStretch();
    return w;
}

void SmudgeTool::mousePress(CanvasView* v, const ToolEvent& e)
{
    if (!begin(v, e)) return;
    m_mask.clear(); // smudging paints straight into the layer
    m_smudging = true;
    // Pick up the colour under the brush.
    m_pickupSize = std::max(1, m_size);
    m_pickup.assign(size_t(m_pickupSize) * size_t(m_pickupSize), 0);
    const QPoint tl(int(std::floor(e.pos.x() - m_pickupSize / 2.0)), int(std::floor(e.pos.y() - m_pickupSize / 2.0)));
    const Layer& l = m_edit->layer();
    for (int y = 0; y < m_pickupSize; ++y)
        for (int x = 0; x < m_pickupSize; ++x) m_pickup[size_t(y) * size_t(m_pickupSize) + size_t(x)] = l.pixelAt(tl + QPoint(x, y));
    m_lastPos = e.pos;
}

void SmudgeTool::mouseMove(CanvasView*, const ToolEvent& e)
{
    if (!m_smudging) return;
    const QPointF d = e.pos - m_lastPos;
    const double dist = std::hypot(d.x(), d.y());
    const double step = std::max(1.0, m_size / 10.0);
    if (dist < step) return;
    const int n = int(dist / step);
    for (int i = 1; i <= n; ++i) smudgeDab(m_lastPos + d * (i * step / dist));
    m_lastPos += d * (n * step / dist);
}

void SmudgeTool::mouseRelease(CanvasView*, const ToolEvent&)
{
    if (!m_smudging) return;
    m_smudging = false;
    m_edit->commit(strokeName());
    m_edit.reset();
    m_pickup.clear();
    m_original = QImage();
    m_selection = QImage();
}

void SmudgeTool::smudgeDab(const QPointF& c)
{
    Layer& l = m_edit->layer();
    const double r = m_pickupSize / 2.0;
    const QPoint tl(int(std::floor(c.x() - r)), int(std::floor(c.y() - r)));
    const QRect box = QRect(tl, QSize(m_pickupSize, m_pickupSize)) & m_maskRect & l.rect();
    const double strength = m_opacity / 100.0;
    for (int y = box.top(); y <= box.bottom(); ++y) {
        auto* row = reinterpret_cast<uint32_t*>(l.image.scanLine(y - l.offset.y())) - l.offset.x();
        const uchar* sel = m_selection.isNull() ? nullptr : m_selection.constScanLine(y);
        for (int x = box.left(); x <= box.right(); ++x) {
            const double dist = std::hypot(x + 0.5 - c.x(), y + 0.5 - c.y());
            double a = tipAlpha(dist, r) * strength;
            if (sel) a *= sel[x] / 255.0;
            if (a <= 0.0) continue;
            uint32_t& carried = m_pickup[size_t(y - tl.y()) * size_t(m_pickupSize) + size_t(x - tl.x())];
            const uint32_t k = uint32_t(std::lround(a * 255.0));
            uint32_t v = Blend::byteMul(carried, k) + Blend::byteMul(row[x], 255 - k);
            if (m_preserveAlpha) v = qAlpha(row[x]) ? qPremultiply((qUnpremultiply(v) & 0x00ffffffu) | (row[x] & 0xff000000u)) : 0;
            row[x] = v;
            // The brush picks up some of what it leaves behind; at 100% strength it keeps
            // dragging the colour it started with.
            const uint32_t pick = uint32_t(std::lround((1.0 - strength) * tipAlpha(dist, r) * 255.0));
            carried = Blend::byteMul(v, pick) + Blend::byteMul(carried, 255 - pick);
        }
    }
    m_edit->markDirty(box);
}
