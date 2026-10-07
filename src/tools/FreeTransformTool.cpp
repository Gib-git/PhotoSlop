#include "tools/FreeTransformTool.h"

#include "app/Theme.h"
#include "core/ColorState.h"
#include "core/ImageOps.h"
#include "core/Selection.h"
#include "tools/ToolManager.h"
#include "ui/CanvasView.h"
#include "ui/Widgets.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QUndoStack>
#include <cmath>
#include <limits>
#include <algorithm>
#include <iterator>

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kHandleRadius = 6.0;

double deg(double rad) { return rad * 180.0 / kPi; }
double rad(double deg) { return deg * kPi / 180.0; }

QPointF unit(const QPointF& v)
{
    const double len = std::hypot(v.x(), v.y());
    return len > 1e-12 ? v / len : QPointF();
}

QCursor rotateCursor()
{
    static QCursor cursor = [] {
        QPixmap pm(QSize(24, 24) * 2);
        pm.setDevicePixelRatio(2);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        QPainterPath arc;
        arc.arcMoveTo(QRectF(5, 5, 14, 14), 200);
        arc.arcTo(QRectF(5, 5, 14, 14), 200, 140);
        auto draw = [&](const QColor& c, double w) {
            p.setPen(QPen(c, w, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.setBrush(Qt::NoBrush);
            p.drawPath(arc);
            const QPointF a = arc.pointAtPercent(0), b = arc.pointAtPercent(1);
            p.setBrush(c);
            p.drawPolygon(QPolygonF{a + QPointF(-3, -1), a + QPointF(2, -2), a + QPointF(0, 3)});
            p.drawPolygon(QPolygonF{b + QPointF(3, -1), b + QPointF(-2, -2), b + QPointF(0, 3)});
        };
        draw(Qt::white, 3.5);
        draw(Qt::black, 1.4);
        return QCursor(pm, 12, 12);
    }();
    return cursor;
}

} // namespace

// ---------------- Options bar ----------------

QWidget* FreeTransformTool::createOptions(QWidget* parent)
{
    auto* w = new QWidget(parent);
    auto* lay = new QHBoxLayout(w);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(6);

    auto* pivot = makeIconButton(QStringLiteral("ref-point"), QStringLiteral("Toggle reference point"), w, true);
    pivot->setChecked(m_showPivot);
    connect(pivot, &QToolButton::toggled, this, [this](bool on) {
        m_showPivot = on;
        emit overlayChanged();
    });
    lay->addWidget(pivot);
    lay->addWidget(makeVSeparator(w));

    m_optionPages = new QStackedWidget(w);
    auto* fields = new QWidget(m_optionPages);
    auto* fl = new QHBoxLayout(fields);
    fl->setContentsMargins(0, 0, 0, 0);
    fl->setSpacing(4);
    auto spin = [&](const QString& label, const QString& suffix, double min, double max, int decimals,
                    QPointer<QDoubleSpinBox>& out, const QString& tip) {
        auto* l = new QLabel(label, fields);
        l->setToolTip(tip);
        fl->addWidget(l);
        auto* s = new QDoubleSpinBox(fields);
        s->setRange(min, max);
        s->setDecimals(decimals);
        s->setSuffix(suffix);
        s->setButtonSymbols(QAbstractSpinBox::NoButtons);
        s->setFixedWidth(68);
        s->setKeyboardTracking(false);
        s->setToolTip(tip);
        fl->addWidget(s);
        out = s;
        return s;
    };
    spin(QStringLiteral("X:"), QStringLiteral(" px"), -1e6, 1e6, 1, m_fx, QStringLiteral("Reference point horizontal position"));
    spin(QStringLiteral("Y:"), QStringLiteral(" px"), -1e6, 1e6, 1, m_fy, QStringLiteral("Reference point vertical position"));
    fl->addSpacing(6);
    spin(QStringLiteral("W:"), QStringLiteral("%"), -1e5, 1e5, 2, m_fw, QStringLiteral("Horizontal scale"));
    auto* link = makeIconButton(QStringLiteral("link"), QStringLiteral("Maintain aspect ratio"), fields, true);
    link->setChecked(m_link);
    connect(link, &QToolButton::toggled, this, [this](bool on) { m_link = on; });
    fl->addWidget(link);
    spin(QStringLiteral("H:"), QStringLiteral("%"), -1e5, 1e5, 2, m_fh, QStringLiteral("Vertical scale"));
    fl->addSpacing(6);
    spin(QStringLiteral("∠"), QStringLiteral("°"), -360, 360, 2, m_fa, QStringLiteral("Rotate"));
    fl->addSpacing(6);
    spin(QStringLiteral("H:"), QStringLiteral("°"), -89, 89, 2, m_fsh, QStringLiteral("Horizontal skew"));
    spin(QStringLiteral("V:"), QStringLiteral("°"), -89, 89, 2, m_fsv, QStringLiteral("Vertical skew"));
    connect(m_fx, &QDoubleSpinBox::valueChanged, this, [this](double v) { m_px = v; applyParams(); });
    connect(m_fy, &QDoubleSpinBox::valueChanged, this, [this](double v) { m_py = v; applyParams(); });
    connect(m_fw, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        if (m_link && m_sx != 0) m_sy *= (v / 100.0) / m_sx;
        m_sx = v / 100.0;
        applyParams();
    });
    connect(m_fh, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        if (m_link && m_sy != 0) m_sx *= (v / 100.0) / m_sy;
        m_sy = v / 100.0;
        applyParams();
    });
    connect(m_fa, &QDoubleSpinBox::valueChanged, this, [this](double v) { m_angle = v; applyParams(); });
    connect(m_fsh, &QDoubleSpinBox::valueChanged, this, [this](double v) { m_skewH = v; applyParams(); });
    connect(m_fsv, &QDoubleSpinBox::valueChanged, this, [this](double v) { m_skewV = v; applyParams(); });
    fl->addSpacing(6);
    fl->addWidget(new QLabel(QStringLiteral("Interpolation:"), fields));
    auto* interp = new QComboBox(fields);
    interp->addItems({QStringLiteral("Nearest Neighbor"), QStringLiteral("Bilinear"), QStringLiteral("Bicubic")});
    interp->setCurrentIndex(int(m_interp));
    connect(interp, &QComboBox::currentIndexChanged, this, [this](int i) {
        m_interp = Xform::Interp(i);
        if (isActive() && !m_previewing) render();
    });
    fl->addWidget(interp);
    m_optionPages->addWidget(fields);

    auto* warpPage = new QWidget(m_optionPages);
    auto* wl = new QHBoxLayout(warpPage);
    wl->setContentsMargins(0, 0, 0, 0);
    wl->addWidget(new QLabel(QStringLiteral("Warp:"), warpPage));
    auto* style = new QComboBox(warpPage);
    style->addItem(QStringLiteral("Custom"));
    wl->addWidget(style);
    wl->addWidget(new QLabel(QStringLiteral("Drag the handles or the grid to bend the image."), warpPage));
    wl->addStretch();
    m_optionPages->addWidget(warpPage);
    lay->addWidget(m_optionPages, 1);

    lay->addWidget(makeVSeparator(w));
    auto* warp = makeIconButton(QStringLiteral("warp"), QStringLiteral("Switch between free transform and warp modes"), w, true);
    m_warpButton = warp;
    connect(warp, &QToolButton::toggled, this, [this](bool on) {
        if (on != m_warp) setMode(on ? Mode::Warp : Mode::Free);
    });
    lay->addWidget(warp);
    auto* cancelBtn = makeIconButton(QStringLiteral("cancel"), QStringLiteral("Cancel transform (Esc)"), w);
    auto* commitBtn = makeIconButton(QStringLiteral("check"), QStringLiteral("Commit transform (Enter)"), w);
    connect(cancelBtn, &QToolButton::clicked, this, [this] { cancel(m_view); });
    connect(commitBtn, &QToolButton::clicked, this, [this] { commit(m_view); });
    lay->addWidget(cancelBtn);
    lay->addWidget(commitBtn);
    connect(this, &Tool::optionsChanged, w, [this] { updateFields(); });
    updateFields();
    return w;
}

void FreeTransformTool::updateFields()
{
    if (m_optionPages) m_optionPages->setCurrentIndex(m_warp ? 1 : 0);
    if (m_warpButton) {
        QSignalBlocker block(m_warpButton);
        m_warpButton->setChecked(m_warp);
        m_warpButton->setEnabled(!m_selectionOnly);
    }
    const struct { QPointer<QDoubleSpinBox> field; double value; } values[] = {
        {m_fx, m_px}, {m_fy, m_py}, {m_fw, m_sx * 100}, {m_fh, m_sy * 100},
        {m_fa, m_angle}, {m_fsh, m_skewH}, {m_fsv, m_skewV},
    };
    for (const auto& v : values) {
        if (!v.field) continue;
        QSignalBlocker block(v.field);
        v.field->setValue(v.value);
    }
}

// ---------------- Geometry ----------------

QTransform FreeTransformTool::xf() const
{
    QTransform t;
    if (!Xform::quadTransform(srcRectF(), m_quad, &t)) return QTransform();
    return t;
}

QTransform FreeTransformTool::srcToDest() const
{
    return QTransform::fromTranslate(m_srcRect.left(), m_srcRect.top()) * xf();
}

QPointF FreeTransformTool::handleSource(int h) const
{
    const QRectF r = srcRectF();
    switch (h) {
    case 0: return r.topLeft();
    case 1: return QPointF(r.center().x(), r.top());
    case 2: return r.topRight();
    case 3: return QPointF(r.right(), r.center().y());
    case 4: return r.bottomRight();
    case 5: return QPointF(r.center().x(), r.bottom());
    case 6: return r.bottomLeft();
    default: return QPointF(r.left(), r.center().y());
    }
}

QPointF FreeTransformTool::handlePos(int h) const { return xf().map(handleSource(h)); }

void FreeTransformTool::syncParams()
{
    if (m_warp || m_srcRect.isEmpty()) return;
    const QTransform t = xf();
    const QPointF p = t.map(m_pivot);
    m_px = p.x();
    m_py = p.y();
    // Decompose the (assumed affine) map as rotate * skew * scale.
    const QPointF u = (m_quad[1] - m_quad[0]) / m_srcRect.width();
    const QPointF v = (m_quad[3] - m_quad[0]) / m_srcRect.height();
    const double a = std::atan2(u.y(), u.x());
    const double c = std::cos(-a), s = std::sin(-a);
    const QPointF vr(v.x() * c - v.y() * s, v.x() * s + v.y() * c);
    m_angle = deg(a);
    m_sx = std::hypot(u.x(), u.y());
    m_sy = vr.y();
    m_skewH = std::fabs(m_sy) > 1e-9 ? deg(std::atan(vr.x() / m_sy)) : 0.0;
    m_skewV = 0.0;
}

void FreeTransformTool::applyParams()
{
    if (!isActive() || m_warp) return;
    const double ca = std::cos(rad(m_angle)), sa = std::sin(rad(m_angle));
    const double th = std::tan(rad(m_skewH)), tv = std::tan(rad(m_skewV));
    // A = R * K * S, acting on column vectors.
    const double k11 = m_sx, k12 = th * m_sy, k21 = tv * m_sx, k22 = m_sy;
    const double a11 = ca * k11 - sa * k21, a12 = ca * k12 - sa * k22;
    const double a21 = sa * k11 + ca * k21, a22 = sa * k12 + ca * k22;
    const QTransform t = QTransform::fromTranslate(-m_pivot.x(), -m_pivot.y()) * QTransform(a11, a21, a12, a22, 0, 0)
        * QTransform::fromTranslate(m_px, m_py);
    const QPolygonF q = t.map(Xform::rectQuad(srcRectF()));
    if (Xform::isConvex(q)) {
        m_quad = q;
        render();
        emit overlayChanged();
    }
}

void FreeTransformTool::setQuad(const QPolygonF& quad)
{
    if (!isActive() || !Xform::isConvex(quad)) return;
    m_quad = quad;
    m_warp = false;
    render();
    syncParams();
    updateFields();
    emit overlayChanged();
}

void FreeTransformTool::setPatch(const Xform::Patch& patch)
{
    if (!isActive()) return;
    m_patch = patch;
    m_warp = true;
    render();
    updateFields();
    emit overlayChanged();
}

bool FreeTransformTool::acceptQuad(const QPolygonF& q)
{
    if (!Xform::isConvex(q)) return false;
    m_quad = q;
    return true;
}

// ---------------- Session ----------------

bool FreeTransformTool::begin(CanvasView* v, bool selectionOnly, Mode mode)
{
    if (isActive() || !v) return false;
    Document* doc = v->document();
    const QString command = selectionOnly ? QStringLiteral("Transform Selection") : QStringLiteral("Free Transform");
    m_before = doc->state();
    m_selSrc = QImage();
    m_src = QImage();
    if (selectionOnly) {
        if (!doc->hasSelection()) return false;
        m_srcRect = doc->selectionBounds();
        m_selSrc = doc->selection().copy(m_srcRect);
        m_layerId = 0;
    } else {
        Layer* l = doc->editLayer();
        if (!l) return false;
        if (!l->visible) {
            alert(QStringLiteral("Could not complete the %1 command because the target layer is hidden.").arg(command));
            return false;
        }
        if (l->pixelsLocked() || (!doc->hasSelection() && l->positionLocked())) {
            alert(QStringLiteral("Could not complete the %1 command because the layer is locked.").arg(command));
            return false;
        }
        m_layerId = l->id;
        if (doc->hasSelection()) {
            const QImage& sel = doc->selection();
            m_srcRect = doc->selectionBounds();
            m_src = ImageOps::maskedPixels(*l, m_srcRect, sel);
            m_selSrc = sel.copy(m_srcRect);
            Layer probe;
            probe.image = m_src;
            probe.trimToContent();
            if (probe.image.isNull()) {
                alert(QStringLiteral("Could not complete the %1 command because the selected area is empty.").arg(command));
                return false;
            }
            // Lift the pixels; the Background layer is left filled with the background colour.
            if (l->isBackground)
                ImageOps::fillColor(*l, m_srcRect, ImageOps::premultiplied(colors()->background()), BlendMode::Normal, 1.f, sel, false);
            else
                ImageOps::clearPixels(*l, m_srcRect, sel);
        } else {
            Layer content = *l;
            content.trimToContent();
            if (content.image.isNull()) {
                alert(QStringLiteral("Could not complete the %1 command because the layer is empty.").arg(command));
                return false;
            }
            m_srcRect = content.rect();
            m_src = content.image;
            QImage blank(l->image.size(), QImage::Format_ARGB32_Premultiplied);
            blank.fill(Qt::transparent);
            l->image = blank;
        }
        m_cleared = l->image;
        m_clearedRect = l->rect();
    }
    m_doc = doc;
    m_view = v;
    m_selectionOnly = selectionOnly;
    m_quad = Xform::rectQuad(srcRectF());
    m_pivot = srcRectF().center();
    m_warp = false;
    m_previewing = false;
    m_historyOverride.clear();
    m_mode = Mode::Free;
    m_drag = m_hover = HitResult();
    // Undo or a History click during the session abandons it.
    connect(doc->undoStack(), &QUndoStack::indexChanged, this, [this] {
        end();
        m_manager->exitModal();
    });
    if (!selectionOnly) v->setSelectionEdgesSuppressed(true);
    render();
    syncParams();
    m_manager->enterModal(this);
    setMode(mode);
    updateFields();
    emit overlayChanged();
    return true;
}

void FreeTransformTool::setMode(Mode mode)
{
    if (!isActive()) return;
    if (mode == Mode::Warp && m_selectionOnly) mode = Mode::Free;
    m_mode = mode;
    const bool warp = mode == Mode::Warp;
    if (warp != m_warp) {
        if (warp) {
            m_patch = Xform::Patch::fromTransform(srcRectF(), xf());
        } else {
            const QPolygonF corners{m_patch.at(0, 0), m_patch.at(0, 3), m_patch.at(3, 3), m_patch.at(3, 0)};
            if (Xform::isConvex(corners)) m_quad = corners;
        }
        m_warp = warp;
        render();
        syncParams();
    }
    updateFields();
    emit overlayChanged();
}

QString FreeTransformTool::historyName() const
{
    if (!m_historyOverride.isEmpty()) return m_historyOverride;
    if (m_selectionOnly) return QStringLiteral("Transform Selection");
    switch (m_mode) {
    case Mode::Scale: return QStringLiteral("Scale");
    case Mode::Rotate: return QStringLiteral("Rotate");
    case Mode::Skew: return QStringLiteral("Skew");
    case Mode::Distort: return QStringLiteral("Distort");
    case Mode::Perspective: return QStringLiteral("Perspective");
    case Mode::Warp: return QStringLiteral("Warp");
    case Mode::Free: break;
    }
    return m_warp ? QStringLiteral("Warp") : QStringLiteral("Free Transform");
}

void FreeTransformTool::beginPreview()
{
    // While dragging, the pixels are drawn as a fast overlay above a hole in the layer.
    if (m_previewing || !isActive()) return;
    m_previewing = true;
    if (m_selectionOnly) {
        if (m_view) m_view->setSelectionEdgesSuppressed(true);
        return;
    }
    const int idx = m_doc->indexOfId(m_layerId);
    if (idx == -1) return;
    Layer& l = m_doc->layerRef(idx);
    const QRect old = l.rect();
    l.image = m_cleared;
    l.offset = m_clearedRect.topLeft();
    m_doc->notifyLayerPixels(idx, old | m_clearedRect);
}

void FreeTransformTool::render()
{
    if (!isActive()) return;
    Document* doc = m_doc;
    m_previewing = false;
    const bool identity = !m_warp && m_quad == Xform::rectQuad(srcRectF());
    if (m_selectionOnly) {
        if (identity) {
            doc->setSelectionRaw(m_before.selection);
        } else {
            QImage mask = m_warp ? Xform::warpedMask(m_selSrc, m_patch, doc->size())
                                 : Xform::transformedMask(m_selSrc, m_srcRect.topLeft(), xf(), doc->size());
            doc->setSelectionRaw(Sel::isEmpty(mask) ? QImage() : mask);
        }
        if (m_view) m_view->setSelectionEdgesSuppressed(false);
        return;
    }
    const int idx = doc->indexOfId(m_layerId);
    if (idx == -1) return;
    Layer& l = doc->layerRef(idx);
    QRect dirty = l.rect() | m_clearedRect;
    if (identity) {
        // Nothing moved: show the original pixels without resampling.
        const Layer* orig = m_before.quickMaskLayer.id == m_layerId ? &m_before.quickMaskLayer : nullptr;
        for (const Layer& b : m_before.layers)
            if (b.id == m_layerId) orig = &b;
        if (orig) {
            l.image = orig->image;
            l.offset = orig->offset;
            doc->notifyLayerPixels(idx, dirty | l.rect());
            doc->setSelectionRaw(m_before.selection);
            return;
        }
    }
    l.image = m_cleared;
    l.offset = m_clearedRect.topLeft();
    const QRect canvas = doc->bounds();
    const QRect clip = l.isBackground ? canvas
                                      : canvas.adjusted(-2 * canvas.width(), -2 * canvas.height(), 2 * canvas.width(), 2 * canvas.height());
    QRect out;
    const QImage img = m_warp ? Xform::warped(m_src, m_patch, m_interp, clip, &out)
                              : Xform::transformed(m_src, srcToDest(), m_interp, clip, &out);
    if (!img.isNull()) {
        if (!l.isBackground) l.ensureCovers(out);
        const QRect r = out & l.rect();
        for (int y = r.top(); y <= r.bottom(); ++y) {
            auto* d = reinterpret_cast<uint32_t*>(l.image.scanLine(y - l.offset.y())) + (r.left() - l.offset.x());
            auto* s = reinterpret_cast<const uint32_t*>(img.constScanLine(y - out.top())) + (r.left() - out.left());
            Blend::compositeRow(d, s, r.width(), BlendMode::Normal, 1.f);
        }
        dirty |= out;
    }
    doc->notifyLayerPixels(idx, dirty);
    if (!m_selSrc.isNull()) {
        QImage mask = m_warp ? Xform::warpedMask(m_selSrc, m_patch, doc->size())
                             : Xform::transformedMask(m_selSrc, m_srcRect.topLeft(), xf(), doc->size());
        doc->setSelectionRaw(Sel::isEmpty(mask) ? QImage() : mask);
    }
}

void FreeTransformTool::end()
{
    if (m_doc) disconnect(m_doc->undoStack(), nullptr, this, nullptr);
    if (m_view) m_view->setSelectionEdgesSuppressed(false);
    m_doc = nullptr;
    m_view = nullptr;
    m_before = DocState();
    m_src = m_selSrc = m_cleared = QImage();
    m_srcRect = m_clearedRect = QRect();
    m_quad.clear();
    m_warp = false;
    m_previewing = false;
    m_mode = Mode::Free;
    m_drag = m_hover = HitResult();
    emit overlayChanged();
}

bool FreeTransformTool::commit(CanvasView*)
{
    if (!isActive()) return false;
    if (m_previewing) render();
    bool changed = !m_warp && m_quad != Xform::rectQuad(srcRectF());
    if (m_warp) {
        const Xform::Patch flat = Xform::Patch::fromTransform(srcRectF(), QTransform());
        for (int i = 0; i < 16 && !changed; ++i) changed = QLineF(flat.pts[i], m_patch.pts[i]).length() > 1e-6;
    }
    if (changed && !m_warp) {
        s_last = xf();
        s_hasLast = true;
    }
    Document* doc = m_doc;
    const DocState before = m_before;
    const QString name = historyName();
    end();
    if (changed) doc->pushSnapshot(name, before);
    else doc->restoreState(before);
    m_manager->exitModal();
    return true;
}

bool FreeTransformTool::cancel(CanvasView*)
{
    if (!isActive()) return false;
    Document* doc = m_doc;
    const DocState before = m_before;
    end();
    doc->restoreState(before);
    m_manager->exitModal();
    return true;
}

void FreeTransformTool::rotateBy(double degrees, const QString& historyName)
{
    if (!isActive() || m_warp) return;
    const QPointF p = xf().map(m_pivot);
    const QTransform r = QTransform::fromTranslate(-p.x(), -p.y()) * QTransform().rotate(degrees) * QTransform::fromTranslate(p.x(), p.y());
    QPolygonF q = r.map(m_quad);
    // Keep right-angle turns on the pixel grid so they stay lossless.
    const QPointF tl = q.boundingRect().topLeft();
    q.translate(QPointF(std::round(tl.x()), std::round(tl.y())) - tl);
    for (QPointF& pt : q) pt = QPointF(std::round(pt.x() * 1e6) / 1e6, std::round(pt.y() * 1e6) / 1e6);
    if (!acceptQuad(q)) return;
    if (m_historyOverride.isEmpty()) m_historyOverride = historyName;
    render();
    syncParams();
    updateFields();
    emit overlayChanged();
}

void FreeTransformTool::flip(bool horizontal, const QString& historyName)
{
    if (!isActive() || m_warp) return;
    const QPointF a = m_pivot;
    const QTransform s = QTransform::fromTranslate(-a.x(), -a.y()) * QTransform::fromScale(horizontal ? -1 : 1, horizontal ? 1 : -1)
        * QTransform::fromTranslate(a.x(), a.y());
    QPolygonF q = (s * xf()).map(Xform::rectQuad(srcRectF()));
    const QPointF tl = q.boundingRect().topLeft();
    q.translate(QPointF(std::round(tl.x()), std::round(tl.y())) - tl);
    if (!acceptQuad(q)) return;
    if (m_historyOverride.isEmpty()) m_historyOverride = historyName;
    render();
    syncParams();
    updateFields();
    emit overlayChanged();
}

void FreeTransformTool::applyLastTransform()
{
    if (!isActive() || !s_hasLast) return;
    if (!acceptQuad(s_last.map(Xform::rectQuad(srcRectF())))) return;
    m_historyOverride = QStringLiteral("Transform Again");
    render();
    syncParams();
    updateFields();
}

// ---------------- Interaction ----------------

FreeTransformTool::HitResult FreeTransformTool::hitTest(CanvasView* v, const QPointF& viewPos) const
{
    HitResult r;
    if (!isActive()) return r;
    auto near = [&](const QPointF& canvasPt) { return QLineF(v->canvasToView(canvasPt), viewPos).length() <= kHandleRadius; };
    if (m_warp) {
        // Corners first, then the other control points.
        static const int order[16] = {0, 3, 12, 15, 1, 2, 4, 8, 7, 11, 13, 14, 5, 6, 9, 10};
        for (int k : order)
            if (near(m_patch.pts[k])) return {Hit::WarpPoint, k};
        QPolygonF outline;
        for (int i = 0; i <= 16; ++i) outline << v->canvasToView(m_patch.eval(i / 16.0, 0));
        for (int i = 0; i <= 16; ++i) outline << v->canvasToView(m_patch.eval(1, i / 16.0));
        for (int i = 16; i >= 0; --i) outline << v->canvasToView(m_patch.eval(i / 16.0, 1));
        for (int i = 16; i >= 0; --i) outline << v->canvasToView(m_patch.eval(0, i / 16.0));
        if (outline.containsPoint(viewPos, Qt::OddEvenFill)) return {Hit::WarpSurface, -1};
        return r;
    }
    if (m_showPivot && near(xf().map(m_pivot))) return {Hit::Pivot, -1};
    for (int h = 0; h < 8; ++h)
        if (near(handlePos(h))) {
            if (m_mode == Mode::Rotate) return {Hit::Rotate, -1};
            return {h % 2 == 0 ? Hit::Corner : Hit::Edge, h};
        }
    QPolygonF vq;
    for (const QPointF& p : m_quad) vq << v->canvasToView(p);
    if (vq.containsPoint(viewPos, Qt::OddEvenFill)) return {Hit::Move, -1};
    if (m_mode == Mode::Free || m_mode == Mode::Rotate) return {Hit::Rotate, -1};
    return r;
}

QCursor FreeTransformTool::cursor(CanvasView*, Qt::KeyboardModifiers mods) const
{
    switch (m_hover.hit) {
    case Hit::Rotate: return rotateCursor();
    case Hit::Move: return Qt::ArrowCursor;
    case Hit::Pivot: return Qt::CrossCursor;
    case Hit::WarpPoint:
    case Hit::WarpSurface: return Qt::PointingHandCursor;
    case Hit::Corner:
    case Hit::Edge: {
        if (mods & Qt::ControlModifier) return Qt::ArrowCursor; // distort / skew
        // Pick a resize cursor matching the handle's direction from the centre.
        const QPointF d = handlePos(m_hover.index) - xf().map(srcRectF().center());
        double a = std::fmod(deg(std::atan2(d.y(), d.x())) + 360.0, 180.0);
        if (a < 22.5 || a >= 157.5) return Qt::SizeHorCursor;
        if (a < 67.5) return Qt::SizeFDiagCursor;
        if (a < 112.5) return Qt::SizeVerCursor;
        return Qt::SizeBDiagCursor;
    }
    case Hit::None: break;
    }
    return Qt::ArrowCursor;
}

void FreeTransformTool::mousePress(CanvasView* v, const ToolEvent& e)
{
    if (!isActive() || v->document() != m_doc) return;
    m_drag = hitTest(v, e.viewPos);
    if (m_drag.hit == Hit::None) return;
    m_pressPos = e.pos;
    m_pressQuad = m_quad;
    m_pressPatch = m_patch;
    m_pressXf = xf();
    m_pressPivot = m_pivot;
    if (m_drag.hit == Hit::WarpSurface) {
        // Find the (u, v) under the pointer so that point follows the drag.
        double best = std::numeric_limits<double>::max();
        for (int i = 0; i <= 32; ++i)
            for (int j = 0; j <= 32; ++j) {
                const double d = QLineF(m_patch.eval(j / 32.0, i / 32.0), e.pos).length();
                if (d < best) {
                    best = d;
                    m_warpU = j / 32.0;
                    m_warpV = i / 32.0;
                }
            }
    }
    if (m_drag.hit != Hit::Pivot) beginPreview();
}

void FreeTransformTool::dragHandle(CanvasView* v, const ToolEvent& e)
{
    const int h = m_drag.index;
    const bool corner = m_drag.hit == Hit::Corner;
    const QPointF start = m_pressXf.map(handleSource(h));
    const QPointF target = v->snapPoint(start + (e.pos - m_pressPos));
    const QPointF d = target - start;
    enum class Op { Scale, Distort, MoveEdge, SkewEdge, Perspective } op = Op::Scale;
    switch (m_mode) {
    case Mode::Free:
        if (e.ctrl() && e.alt() && e.shift() && corner) op = Op::Perspective;
        else if (e.ctrl() && corner) op = Op::Distort;
        else if (e.ctrl()) op = e.shift() ? Op::SkewEdge : Op::MoveEdge;
        break;
    case Mode::Skew: op = corner ? Op::Distort : Op::SkewEdge; break;
    case Mode::Distort: op = corner ? Op::Distort : Op::MoveEdge; break;
    case Mode::Perspective: op = corner ? Op::Perspective : Op::SkewEdge; break;
    default: break;
    }
    QPolygonF q = m_pressQuad;
    const int c0 = corner ? h / 2 : (h - 1) / 2;
    const int c1 = corner ? c0 : ((h + 1) / 2) % 4;
    switch (op) {
    case Op::Scale: {
        // Scale in the layer's own frame, about the opposite handle (or the reference point with Alt).
        const QPointF sm = m_pressXf.inverted().map(target);
        const QPointF hs = handleSource(h);
        const QPointF a = e.alt() ? m_pivot : handleSource((h + 4) % 8);
        auto ratio = [](double num, double den) { return std::fabs(den) > 1e-9 ? num / den : 1.0; };
        double sx = 1, sy = 1;
        if (corner) {
            if (m_link != e.shift()) {
                const QPointF dh = hs - a;
                const double len2 = dh.x() * dh.x() + dh.y() * dh.y();
                sx = sy = len2 > 1e-12 ? QPointF::dotProduct(sm - a, dh) / len2 : 1.0;
            } else {
                sx = ratio(sm.x() - a.x(), hs.x() - a.x());
                sy = ratio(sm.y() - a.y(), hs.y() - a.y());
            }
        } else if (h == 1 || h == 5) {
            sy = ratio(sm.y() - a.y(), hs.y() - a.y());
            if (e.shift()) sx = sy;
        } else {
            sx = ratio(sm.x() - a.x(), hs.x() - a.x());
            if (e.shift()) sy = sx;
        }
        const QTransform s = QTransform::fromTranslate(-a.x(), -a.y()) * QTransform::fromScale(sx, sy)
            * QTransform::fromTranslate(a.x(), a.y());
        q = (s * m_pressXf).map(Xform::rectQuad(srcRectF()));
        break;
    }
    case Op::Distort: q[c0] += d; break;
    case Op::MoveEdge:
        q[c0] += d;
        q[c1] += d;
        break;
    case Op::SkewEdge: {
        const QPointF dir = unit(m_pressQuad[c1] - m_pressQuad[c0]);
        const QPointF along = dir * QPointF::dotProduct(d, dir);
        q[c0] += along;
        q[c1] += along;
        break;
    }
    case Op::Perspective: {
        // Move this corner and mirror its neighbour, keeping the shape symmetric.
        const int hn = c0 ^ 1, vn = 3 - c0;
        const QPointF eh = unit(m_pressQuad[hn] - m_pressQuad[c0]), ev = unit(m_pressQuad[vn] - m_pressQuad[c0]);
        const double ph = QPointF::dotProduct(d, eh), pv = QPointF::dotProduct(d, ev);
        if (std::fabs(ph) >= std::fabs(pv)) {
            q[c0] += eh * ph;
            q[hn] -= eh * ph;
        } else {
            q[c0] += ev * pv;
            q[vn] -= ev * pv;
        }
        break;
    }
    }
    acceptQuad(q);
}

void FreeTransformTool::dragWarp(CanvasView*, const ToolEvent& e)
{
    const QPointF d = e.pos - m_pressPos;
    m_patch = m_pressPatch;
    if (m_drag.hit == Hit::WarpPoint) {
        const int k = m_drag.index, row = k / 4, col = k % 4;
        m_patch.pts[k] += d;
        // Corner points carry their tangent handles with them.
        if ((row == 0 || row == 3) && (col == 0 || col == 3)) {
            m_patch.at(row, col == 0 ? 1 : 2) += d;
            m_patch.at(row == 0 ? 1 : 2, col) += d;
        }
        return;
    }
    // Dragging the surface: the smallest change to the control points that moves the
    // point under the pointer by exactly the drag distance. The corners stay pinned.
    double w[16], sum = 0;
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) {
            const bool corner = (r == 0 || r == 3) && (c == 0 || c == 3);
            w[r * 4 + c] = corner ? 0.0 : Xform::bernstein(r, m_warpV) * Xform::bernstein(c, m_warpU);
            sum += w[r * 4 + c] * w[r * 4 + c];
        }
    if (sum < 1e-12) return;
    for (int k = 0; k < 16; ++k) m_patch.pts[k] += d * (w[k] / sum);
}

void FreeTransformTool::mouseMove(CanvasView* v, const ToolEvent& e)
{
    if (!isActive() || v->document() != m_doc) return;
    if (m_drag.hit == Hit::None) {
        const HitResult h = hitTest(v, e.viewPos);
        if (h.hit != m_hover.hit || h.index != m_hover.index) {
            m_hover = h;
            v->viewport()->setCursor(cursor(v, e.mods));
        }
        return;
    }
    switch (m_drag.hit) {
    case Hit::Move: {
        QPointF d = e.pos - m_pressPos;
        if (e.shift()) {
            if (std::fabs(d.x()) > std::fabs(d.y())) d.setY(0);
            else d.setX(0);
        }
        d += v->snapRectOffset(m_pressQuad.boundingRect().translated(d));
        d = QPointF(std::round(d.x()), std::round(d.y()));
        m_quad = m_pressQuad.translated(d);
        break;
    }
    case Hit::Rotate: {
        const QPointF p = m_pressXf.map(m_pivot);
        double a = deg(std::atan2(e.pos.y() - p.y(), e.pos.x() - p.x()) - std::atan2(m_pressPos.y() - p.y(), m_pressPos.x() - p.x()));
        if (e.shift()) a = std::round(a / 15.0) * 15.0;
        const QTransform r = QTransform::fromTranslate(-p.x(), -p.y()) * QTransform().rotate(a) * QTransform::fromTranslate(p.x(), p.y());
        acceptQuad(r.map(m_pressQuad));
        break;
    }
    case Hit::Corner:
    case Hit::Edge: dragHandle(v, e); break;
    case Hit::Pivot: {
        // The reference point snaps to the handles and the centre.
        QPointF target = v->snapPoint(e.pos);
        for (int h = 0; h <= 8; ++h) {
            const QPointF hp = h < 8 ? m_pressXf.map(handleSource(h)) : m_pressXf.map(srcRectF().center());
            if (QLineF(v->canvasToView(hp), e.viewPos).length() <= 8) target = hp;
        }
        m_pivot = m_pressXf.inverted().map(target);
        break;
    }
    case Hit::WarpPoint:
    case Hit::WarpSurface: dragWarp(v, e); break;
    case Hit::None: break;
    }
    syncParams();
    updateFields();
    emit overlayChanged();
}

void FreeTransformTool::mouseRelease(CanvasView* v, const ToolEvent&)
{
    if (!isActive() || m_drag.hit == Hit::None) return;
    m_drag = HitResult();
    if (m_previewing) render();
    syncParams();
    updateFields();
    v->viewport()->setCursor(cursor(v, QGuiApplication::queryKeyboardModifiers()));
    emit overlayChanged();
}

void FreeTransformTool::mouseDoubleClick(CanvasView* v, const ToolEvent& e)
{
    const Hit h = hitTest(v, e.viewPos).hit;
    if (h == Hit::Move || h == Hit::WarpSurface) commit(v);
}

bool FreeTransformTool::keyPress(CanvasView*, QKeyEvent* e)
{
    if (!isActive()) return false;
    QPointF d;
    switch (e->key()) {
    case Qt::Key_Left: d = QPointF(-1, 0); break;
    case Qt::Key_Right: d = QPointF(1, 0); break;
    case Qt::Key_Up: d = QPointF(0, -1); break;
    case Qt::Key_Down: d = QPointF(0, 1); break;
    default: return false;
    }
    if (e->modifiers() & Qt::ShiftModifier) d *= 10;
    if (m_warp) {
        for (QPointF& p : m_patch.pts) p += d;
    } else {
        m_quad.translate(d);
    }
    render();
    syncParams();
    updateFields();
    emit overlayChanged();
    return true;
}

void FreeTransformTool::paintOverlay(QPainter& p, CanvasView* v)
{
    if (!isActive() || v->document() != m_doc) return;
    const double s = v->scale();
    const QPointF o = v->canvasToView(QPointF(0, 0));
    const QTransform toView(s, 0, 0, s, o.x(), o.y());

    if (m_previewing && !m_selectionOnly && !m_src.isNull()) {
        const int idx = m_doc->indexOfId(m_layerId);
        const Layer& l = m_doc->layerAt(idx == -1 ? m_doc->activeIndex() : idx);
        p.save();
        p.setOpacity(idx == Document::kQuickMaskIndex ? 1.0 : l.opacity * l.fill);
        if (m_warp) {
            Xform::Patch vp;
            for (int k = 0; k < 16; ++k) vp.pts[k] = toView.map(m_patch.pts[k]);
            QRect r;
            const QImage img = Xform::warped(m_src, vp, Xform::Interp::Bilinear, v->viewport()->rect(), &r);
            if (!img.isNull()) p.drawImage(r.topLeft(), img);
        } else {
            p.setTransform(srcToDest() * toView);
            p.setRenderHint(QPainter::SmoothPixmapTransform, true);
            p.drawImage(QPointF(0, 0), m_src);
        }
        p.restore();
    }

    p.setRenderHint(QPainter::Antialiasing, true);
    const QPen dark(QColor(40, 40, 40), 1);
    const QPen light(QColor(255, 255, 255, 200), 1);
    auto handle = [&](const QPointF& c, bool filled) {
        p.setPen(dark);
        p.setBrush(filled ? QBrush(Qt::white) : QBrush(Qt::NoBrush));
        p.drawRect(QRectF(c.x() - 3.5, c.y() - 3.5, 7, 7));
    };
    if (m_warp) {
        // The patch outline, its thirds, and the control points with their tangent lines.
        p.setBrush(Qt::NoBrush);
        for (int i = 0; i <= 3; ++i) {
            QPolygonF a, b;
            for (int k = 0; k <= 24; ++k) {
                a << toView.map(m_patch.eval(k / 24.0, i / 3.0));
                b << toView.map(m_patch.eval(i / 3.0, k / 24.0));
            }
            p.setPen(i == 0 || i == 3 ? dark : QPen(QColor(40, 40, 40, 150), 1));
            p.drawPolyline(a);
            p.drawPolyline(b);
        }
        p.setPen(QPen(QColor(40, 40, 40, 170), 1, Qt::DashLine));
        for (int r : {0, 3})
            for (int c : {0, 3}) {
                const QPointF corner = toView.map(m_patch.at(r, c));
                p.drawLine(corner, toView.map(m_patch.at(r, c == 0 ? 1 : 2)));
                p.drawLine(corner, toView.map(m_patch.at(r == 0 ? 1 : 2, c)));
            }
        for (int k = 0; k < 16; ++k) {
            const int r = k / 4, c = k % 4;
            const bool corner = (r == 0 || r == 3) && (c == 0 || c == 3);
            const QPointF pt = toView.map(m_patch.pts[k]);
            if (corner) {
                handle(pt, true);
            } else {
                p.setPen(dark);
                p.setBrush(Qt::white);
                p.drawEllipse(pt, 3, 3);
            }
        }
        return;
    }
    QPolygonF vq = toView.map(m_quad);
    p.setBrush(Qt::NoBrush);
    p.setPen(light);
    p.drawPolygon(vq.translated(1, 1));
    p.setPen(dark);
    p.drawPolygon(vq);
    for (int h = 0; h < 8; ++h) handle(toView.map(handlePos(h)), true);
    if (m_showPivot) {
        const QPointF c = toView.map(xf().map(m_pivot));
        p.setPen(QPen(QColor(40, 40, 40), 1));
        p.setBrush(QColor(255, 255, 255, 180));
        p.drawEllipse(c, 4.5, 4.5);
        p.drawLine(c - QPointF(7, 0), c + QPointF(7, 0));
        p.drawLine(c - QPointF(0, 7), c + QPointF(0, 7));
    }
}
