#include "tools/SelectionTools.h"

#include "core/DocumentOps.h"
#include "tools/ToolManager.h"
#include "ui/CanvasView.h"
#include "ui/Widgets.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <cmath>
#include <algorithm>
#include <iterator>

namespace {
constexpr double kPi = 3.14159265358979323846;
}

// ---------------- SelectionTool ----------------

void SelectionTool::addSelectionOptions(QHBoxLayout* lay, QWidget* parent, bool antialiasOption)
{
    auto* grp = new QButtonGroup(parent);
    const struct { const char* icon; const char* tip; Sel::Op op; } modes[] = {
        {"sel-new", "New selection", Sel::Op::Replace},
        {"sel-add", "Add to selection", Sel::Op::Add},
        {"sel-sub", "Subtract from selection", Sel::Op::Subtract},
        {"sel-int", "Intersect with selection", Sel::Op::Intersect},
    };
    for (const auto& m : modes) {
        auto* b = makeIconButton(QString::fromLatin1(m.icon), QString::fromLatin1(m.tip), parent, true);
        grp->addButton(b);
        if (m.op == m_mode) b->setChecked(true);
        const Sel::Op op = m.op;
        connect(b, &QToolButton::toggled, this, [this, op](bool on) {
            if (on) m_mode = op;
        });
        lay->addWidget(b);
    }
    lay->addWidget(makeVSeparator(parent));

    auto* featherSpin = new QSpinBox(parent);
    featherSpin->setRange(0, 1000);
    featherSpin->setSuffix(QStringLiteral(" px"));
    featherSpin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    featherSpin->setFixedWidth(56);
    featherSpin->setValue(int(m_feather));
    connect(featherSpin, &QSpinBox::valueChanged, this, [this](int v) { m_feather = v; });
    lay->addWidget(new ScrubbyLabel(QStringLiteral("Feather:"), featherSpin, parent));
    lay->addWidget(featherSpin);

    auto* aa = new QCheckBox(QStringLiteral("Anti-alias"), parent);
    aa->setChecked(m_antialias);
    aa->setEnabled(antialiasOption);
    connect(aa, &QCheckBox::toggled, this, [this](bool on) { m_antialias = on; });
    lay->addWidget(aa);
}

Sel::Op SelectionTool::opFor(Qt::KeyboardModifiers mods) const
{
    const bool shift = mods & Qt::ShiftModifier, alt = mods & Qt::AltModifier;
    if (shift && alt) return Sel::Op::Intersect;
    if (shift) return Sel::Op::Add;
    if (alt) return Sel::Op::Subtract;
    return m_mode;
}

void SelectionTool::applyShape(CanvasView* v, const QPainterPath& path, Sel::Op op,
                               const QString& undoText)
{
    Document* doc = v->document();
    QImage mask = Sel::pathMask(doc->size(), path, m_antialias);
    if (m_feather > 0) Sel::feather(mask, m_feather);
    QImage result = Sel::combine(doc->selection(), mask, op);
    if (result.isNull() && !doc->hasSelection()) return;
    doc->changeSelection(result, undoText);
}

bool SelectionTool::beginMoveSelection(CanvasView* v, const ToolEvent& e)
{
    Document* doc = v->document();
    if (!doc->hasSelection() || e.mods & (Qt::ShiftModifier | Qt::AltModifier)) return false;
    const QPoint px = e.pixel();
    if (!doc->bounds().contains(px) || doc->selection().constScanLine(px.y())[px.x()] < 128) return false;
    m_movingSelection = true;
    m_moveStart = e.pos;
    m_moveOriginal = doc->selection();
    m_moveBefore = doc->state();
    return true;
}

void SelectionTool::updateMoveSelection(CanvasView* v, const ToolEvent& e)
{
    QPointF d = e.pos - m_moveStart;
    QPoint delta(int(std::round(d.x())), int(std::round(d.y())));
    v->document()->setSelectionRaw(delta.isNull() ? m_moveOriginal : Sel::translated(m_moveOriginal, delta));
}

void SelectionTool::endMoveSelection(CanvasView* v)
{
    m_movingSelection = false;
    Document* doc = v->document();
    if (doc->selection().cacheKey() != m_moveOriginal.cacheKey())
        doc->pushSnapshot(QStringLiteral("Move Selection"), m_moveBefore);
    m_moveOriginal = QImage();
    m_moveBefore = DocState();
}

bool SelectionTool::nudgeSelection(CanvasView* v, QKeyEvent* e)
{
    Document* doc = v->document();
    if (!doc->hasSelection()) return false;
    QPoint d;
    switch (e->key()) {
    case Qt::Key_Left: d = QPoint(-1, 0); break;
    case Qt::Key_Right: d = QPoint(1, 0); break;
    case Qt::Key_Up: d = QPoint(0, -1); break;
    case Qt::Key_Down: d = QPoint(0, 1); break;
    default: return false;
    }
    if (e->modifiers() & Qt::ShiftModifier) d *= 10;
    doc->changeSelection(Sel::translated(doc->selection(), d), QStringLiteral("Nudge Selection"));
    return true;
}

void SelectionTool::drawOutline(QPainter& p, const QPainterPath& viewPath)
{
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(Qt::white, 1));
    p.drawPath(viewPath);
    p.setPen(QPen(Qt::black, 1, Qt::DashLine));
    p.drawPath(viewPath);
}

// ---------------- MarqueeTool ----------------

QWidget* MarqueeTool::createOptions(QWidget* parent)
{
    auto* w = new QWidget(parent);
    auto* lay = new QHBoxLayout(w);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(6);
    addSelectionOptions(lay, w, m_elliptical);
    lay->addWidget(makeVSeparator(w));

    lay->addWidget(new QLabel(QStringLiteral("Style:"), w));
    auto* style = new QComboBox(w);
    style->addItems({QStringLiteral("Normal"), QStringLiteral("Fixed Ratio"), QStringLiteral("Fixed Size")});
    lay->addWidget(style);
    auto* wl = new QLabel(QStringLiteral("Width:"), w);
    auto* ws = new QDoubleSpinBox(w);
    auto* hl = new QLabel(QStringLiteral("Height:"), w);
    auto* hs = new QDoubleSpinBox(w);
    for (QDoubleSpinBox* s : {ws, hs}) {
        s->setRange(0.001, 100000);
        s->setDecimals(0);
        s->setValue(1);
        s->setButtonSymbols(QAbstractSpinBox::NoButtons);
        s->setFixedWidth(56);
        s->setEnabled(false);
    }
    wl->setEnabled(false);
    hl->setEnabled(false);
    lay->addWidget(wl);
    lay->addWidget(ws);
    lay->addWidget(hl);
    lay->addWidget(hs);
    connect(style, &QComboBox::currentIndexChanged, this, [=, this](int i) {
        m_style = Style(i);
        for (QWidget* x : std::initializer_list<QWidget*>{wl, ws, hl, hs}) x->setEnabled(i != 0);
        ws->setSuffix(i == 2 ? QStringLiteral(" px") : QString());
        hs->setSuffix(i == 2 ? QStringLiteral(" px") : QString());
        if (i == 2) {
            ws->setValue(64);
            hs->setValue(64);
        }
    });
    connect(ws, &QDoubleSpinBox::valueChanged, this, [this](double v) { m_fixedW = v; });
    connect(hs, &QDoubleSpinBox::valueChanged, this, [this](double v) { m_fixedH = v; });
    lay->addWidget(makeVSeparator(w));
    auto* mask = new QPushButton(QStringLiteral("Select and Mask..."), w);
    mask->setEnabled(false);
    lay->addWidget(mask);
    lay->addStretch();
    return w;
}

void MarqueeTool::mousePress(CanvasView* v, const ToolEvent& e)
{
    if (m_style != Style::FixedSize && opFor(e.mods) == Sel::Op::Replace && beginMoveSelection(v, e)) return;
    m_drawing = true;
    m_op = opFor(e.mods);
    m_pressMods = e.mods;
    m_mods = e.mods;
    m_start = QPointF(std::round(e.pos.x()), std::round(e.pos.y()));
    m_current = m_start;
    emit overlayChanged();
}

QRectF MarqueeTool::currentRect(Qt::KeyboardModifiers mods) const
{
    if (m_style == Style::FixedSize)
        return QRectF(m_current, QSizeF(m_fixedW, m_fixedH));

    QPointF d = m_current - m_start;
    // Shift/Alt pressed at the start choose the selection mode; pressing them
    // again during the drag constrains / draws from the centre.
    const bool constrain = (mods & Qt::ShiftModifier) && !(m_pressMods & Qt::ShiftModifier);
    const bool fromCenter = (mods & Qt::AltModifier) && !(m_pressMods & Qt::AltModifier);
    if (m_style == Style::FixedRatio) {
        double ratio = m_fixedH / std::max(0.001, m_fixedW);
        double w = std::fabs(d.x());
        d = QPointF(std::copysign(w, d.x()), std::copysign(w * ratio, d.y()));
    } else if (constrain) {
        double s = std::max(std::fabs(d.x()), std::fabs(d.y()));
        d = QPointF(std::copysign(s, d.x()), std::copysign(s, d.y()));
    }
    if (fromCenter) return QRectF(m_start - d, m_start + d).normalized();
    return QRectF(m_start, m_start + d).normalized();
}

QPainterPath MarqueeTool::shapePath(const QRectF& r) const
{
    QPainterPath path;
    if (m_elliptical) path.addEllipse(r);
    else path.addRect(r);
    return path;
}

void MarqueeTool::mouseMove(CanvasView* v, const ToolEvent& e)
{
    if (m_movingSelection) {
        updateMoveSelection(v, e);
        return;
    }
    if (!m_drawing) return;
    m_current = QPointF(std::round(e.pos.x()), std::round(e.pos.y()));
    m_mods = e.mods;
    emit overlayChanged();
}

void MarqueeTool::mouseRelease(CanvasView* v, const ToolEvent& e)
{
    if (m_movingSelection) {
        endMoveSelection(v);
        return;
    }
    if (!m_drawing) return;
    m_drawing = false;
    m_current = QPointF(std::round(e.pos.x()), std::round(e.pos.y()));
    QRectF r = currentRect(e.mods);
    emit overlayChanged();
    if (r.width() < 1 || r.height() < 1) {
        // A simple click deselects.
        if (m_op == Sel::Op::Replace) Ops::deselect(v->document());
        return;
    }
    applyShape(v, shapePath(r), m_op,
               m_elliptical ? QStringLiteral("Elliptical Marquee") : QStringLiteral("Rectangular Marquee"));
}

bool MarqueeTool::keyPress(CanvasView* v, QKeyEvent* e)
{
    if (e->key() == Qt::Key_Shift || e->key() == Qt::Key_Alt) {
        m_mods = e->modifiers();
        emit overlayChanged();
        return false;
    }
    return nudgeSelection(v, e);
}

void MarqueeTool::paintOverlay(QPainter& p, CanvasView* v)
{
    if (!m_drawing) return;
    QRectF r = currentRect(QGuiApplication::queryKeyboardModifiers());
    QPainterPath path = shapePath(v->canvasToView(r));
    drawOutline(p, path);
}

// ---------------- LassoTool ----------------

QWidget* LassoTool::createOptions(QWidget* parent)
{
    auto* w = new QWidget(parent);
    auto* lay = new QHBoxLayout(w);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(6);
    addSelectionOptions(lay, w, true);
    lay->addWidget(makeVSeparator(w));
    auto* mask = new QPushButton(QStringLiteral("Select and Mask..."), w);
    mask->setEnabled(false);
    lay->addWidget(mask);
    lay->addStretch();
    return w;
}

void LassoTool::mousePress(CanvasView* v, const ToolEvent& e)
{
    if (m_polygonal) {
        if (!m_active) {
            m_active = true;
            m_op = opFor(e.mods);
            m_points = {e.pos};
        } else {
            // Clicking near the start point closes the polygon.
            QPointF startView = v->canvasToView(m_points.first());
            if (m_points.size() > 2 && QLineF(startView, e.viewPos).length() < 7) {
                finish(v);
                return;
            }
            QPointF p = e.pos;
            if (e.shift()) {
                // Constrain to 45° increments.
                QPointF d = p - m_points.last();
                double ang = std::round(std::atan2(d.y(), d.x()) / (kPi / 4)) * (kPi / 4);
                double len = std::hypot(d.x(), d.y());
                p = m_points.last() + QPointF(std::cos(ang) * len, std::sin(ang) * len);
            }
            m_points.append(p);
        }
        m_hover = e.pos;
        emit overlayChanged();
        return;
    }
    if (opFor(e.mods) == Sel::Op::Replace && beginMoveSelection(v, e)) return;
    m_active = true;
    m_op = opFor(e.mods);
    m_points = {e.pos};
}

void LassoTool::mouseMove(CanvasView* v, const ToolEvent& e)
{
    if (m_movingSelection) {
        updateMoveSelection(v, e);
        return;
    }
    if (!m_active) return;
    if (m_polygonal) {
        m_hover = e.pos;
    } else {
        if (QLineF(m_points.last(), e.pos).length() * v->scale() < 1.0) return;
        m_points.append(e.pos);
    }
    emit overlayChanged();
}

void LassoTool::mouseRelease(CanvasView* v, const ToolEvent&)
{
    if (m_movingSelection) {
        endMoveSelection(v);
        return;
    }
    if (!m_polygonal && m_active) finish(v);
}

void LassoTool::mouseDoubleClick(CanvasView* v, const ToolEvent&)
{
    if (m_polygonal && m_active) finish(v);
}

void LassoTool::finish(CanvasView* v)
{
    m_active = false;
    QPolygonF pts = m_points;
    m_points.clear();
    emit overlayChanged();
    if (pts.size() < 3) {
        if (m_op == Sel::Op::Replace) Ops::deselect(v->document());
        return;
    }
    QPainterPath path;
    path.addPolygon(pts);
    path.closeSubpath();
    applyShape(v, path, m_op, m_polygonal ? QStringLiteral("Polygonal Lasso") : QStringLiteral("Lasso"));
}

bool LassoTool::keyPress(CanvasView* v, QKeyEvent* e)
{
    if (m_polygonal && m_active && (e->key() == Qt::Key_Backspace || e->key() == Qt::Key_Delete)) {
        if (m_points.size() > 1) {
            m_points.removeLast();
            emit overlayChanged();
        } else {
            cancel(v);
        }
        return true;
    }
    return !m_active && nudgeSelection(v, e);
}

bool LassoTool::commit(CanvasView* v)
{
    if (!m_active) return false;
    finish(v);
    return true;
}

bool LassoTool::cancel(CanvasView*)
{
    if (!m_active) return false;
    m_active = false;
    m_points.clear();
    emit overlayChanged();
    return true;
}

void LassoTool::deactivated(CanvasView* v)
{
    if (m_polygonal) cancel(v);
}

void LassoTool::paintOverlay(QPainter& p, CanvasView* v)
{
    if (!m_active || m_points.isEmpty()) return;
    QPolygonF view;
    for (const QPointF& pt : m_points) view.append(v->canvasToView(pt));
    if (m_polygonal) view.append(v->canvasToView(m_hover));
    QPainterPath path;
    path.addPolygon(view);
    drawOutline(p, path);
}
