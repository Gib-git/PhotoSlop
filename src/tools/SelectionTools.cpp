#include "tools/SelectionTools.h"

#include "core/DocumentOps.h"
#include "core/ImageOps.h"
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
#include <limits>
#include <queue>
#include <algorithm>
#include <iterator>

namespace {
constexpr double kPi = 3.14159265358979323846;
}

// ---------------- SelectionTool ----------------

void SelectionTool::addSelectionOptions(QHBoxLayout* lay, QWidget* parent, bool antialiasOption,
                                        bool featherOption, bool subsetOps)
{
    auto* grp = new QButtonGroup(parent);
    m_modeButtons = grp;
    const struct { const char* icon; const char* tip; Sel::Op op; } modes[] = {
        {"sel-new", "New selection", Sel::Op::Replace},
        {"sel-add", "Add to selection", Sel::Op::Add},
        {"sel-sub", "Subtract from selection", Sel::Op::Subtract},
        {"sel-int", "Intersect with selection", Sel::Op::Intersect},
    };
    for (const auto& m : modes) {
        if (subsetOps && m.op == Sel::Op::Intersect) continue;
        auto* b = makeIconButton(QString::fromLatin1(m.icon), QString::fromLatin1(m.tip), parent, true);
        grp->addButton(b, int(m.op));
        if (m.op == m_mode) b->setChecked(true);
        const Sel::Op op = m.op;
        connect(b, &QToolButton::toggled, this, [this, op](bool on) {
            if (on) m_mode = op;
        });
        lay->addWidget(b);
    }
    lay->addWidget(makeVSeparator(parent));

    if (featherOption) {
        auto* featherSpin = new QSpinBox(parent);
        featherSpin->setRange(0, 1000);
        featherSpin->setSuffix(QStringLiteral(" px"));
        featherSpin->setButtonSymbols(QAbstractSpinBox::NoButtons);
        featherSpin->setFixedWidth(56);
        featherSpin->setValue(int(m_feather));
        connect(featherSpin, &QSpinBox::valueChanged, this, [this](int v) { m_feather = v; });
        lay->addWidget(new ScrubbyLabel(QStringLiteral("Feather:"), featherSpin, parent));
        lay->addWidget(featherSpin);
    }

    if (antialiasOption) {
        auto* aa = new QCheckBox(QStringLiteral("Anti-alias"), parent);
        aa->setChecked(m_antialias);
        connect(aa, &QCheckBox::toggled, this, [this](bool on) { m_antialias = on; });
        lay->addWidget(aa);
    } else if (featherOption) {
        // The rectangular marquee shows Anti-alias greyed out, like Photoshop.
        auto* aa = new QCheckBox(QStringLiteral("Anti-alias"), parent);
        aa->setChecked(true);
        aa->setEnabled(false);
        lay->addWidget(aa);
    }
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

void SelectionTool::applyMask(CanvasView* v, const QImage& mask, Sel::Op op, const QString& undoText)
{
    Document* doc = v->document();
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
    m_start = v->snapPoint(QPointF(std::round(e.pos.x()), std::round(e.pos.y())));
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
    m_current = v->snapPoint(QPointF(std::round(e.pos.x()), std::round(e.pos.y())));
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
    m_current = v->snapPoint(QPointF(std::round(e.pos.x()), std::round(e.pos.y())));
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

// ---------------- MagneticLassoTool ----------------

namespace {

QPointF centre(const QPoint& p) { return QPointF(p.x() + 0.5, p.y() + 0.5); }

int luminance(QRgb p) { return (qRed(p) * 11 + qGreen(p) * 16 + qBlue(p) * 5) / 32; }

} // namespace

QWidget* MagneticLassoTool::createOptions(QWidget* parent)
{
    auto* w = new QWidget(parent);
    auto* lay = new QHBoxLayout(w);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(6);
    addSelectionOptions(lay, w, true);
    lay->addWidget(makeVSeparator(w));
    auto field = [&](const QString& label, int min, int max, const QString& suffix, int* target) {
        auto* f = new ValueField(label, min, max, suffix, false, w);
        f->setValue(*target);
        f->setFieldWidth(44);
        connect(f, &ValueField::valueChanged, this, [target](int v) { *target = v; });
        lay->addWidget(f);
    };
    field(QStringLiteral("Width:"), 1, 256, QStringLiteral(" px"), &m_width);
    field(QStringLiteral("Contrast:"), 1, 100, QStringLiteral("%"), &m_contrast);
    field(QStringLiteral("Frequency:"), 0, 100, QString(), &m_frequency);
    lay->addWidget(makeVSeparator(w));
    auto* mask = new QPushButton(QStringLiteral("Select and Mask..."), w);
    mask->setEnabled(false);
    lay->addWidget(mask);
    lay->addStretch();
    return w;
}

void MagneticLassoTool::prepare(Document* doc)
{
    // Sobel edge strength of the composite's luminance.
    const QImage& img = doc->composite();
    m_size = img.size();
    const int w = m_size.width(), h = m_size.height();
    std::vector<uchar> lum(size_t(w) * h);
    for (int y = 0; y < h; ++y) {
        const QRgb* row = reinterpret_cast<const QRgb*>(img.constScanLine(y));
        for (int x = 0; x < w; ++x) lum[size_t(y) * w + x] = uchar(luminance(row[x]));
    }
    m_edges.assign(size_t(w) * h, 0);
    auto L = [&](int x, int y) { return int(lum[size_t(std::clamp(y, 0, h - 1)) * w + std::clamp(x, 0, w - 1)]); };
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const int gx = L(x + 1, y - 1) + 2 * L(x + 1, y) + L(x + 1, y + 1) - L(x - 1, y - 1) - 2 * L(x - 1, y) - L(x - 1, y + 1);
            const int gy = L(x - 1, y + 1) + 2 * L(x, y + 1) + L(x + 1, y + 1) - L(x - 1, y - 1) - 2 * L(x, y - 1) - L(x + 1, y - 1);
            m_edges[size_t(y) * w + x] = uchar(std::min(255.0, std::hypot(gx, gy) / 2.0));
        }
}

QPoint MagneticLassoTool::snapToEdge(const QPoint& p) const
{
    const int w = m_size.width(), h = m_size.height();
    const QPoint c(std::clamp(p.x(), 0, w - 1), std::clamp(p.y(), 0, h - 1));
    if (m_edges.empty()) return c;
    const int threshold = m_contrast * 255 / 100;
    QPoint best = c;
    int bestStrength = threshold - 1;
    int bestDist = 0;
    const int r = m_width;
    for (int dy = -r; dy <= r; ++dy)
        for (int dx = -r; dx <= r; ++dx) {
            const int d2 = dx * dx + dy * dy;
            if (d2 > r * r) continue;
            const int x = c.x() + dx, y = c.y() + dy;
            if (x < 0 || y < 0 || x >= w || y >= h) continue;
            const int g = m_edges[size_t(y) * w + x];
            if (g > bestStrength || (g == bestStrength && d2 < bestDist)) {
                bestStrength = g;
                bestDist = d2;
                best = QPoint(x, y);
            }
        }
    return best;
}

std::vector<QPoint> MagneticLassoTool::liveWire(const QPoint& a, const QPoint& b) const
{
    std::vector<QPoint> straight;
    auto line = [&] {
        const int n = std::max(std::abs(b.x() - a.x()), std::abs(b.y() - a.y()));
        for (int i = 0; i <= n; ++i) {
            const double t = n ? double(i) / n : 0.0;
            straight.emplace_back(int(std::lround(a.x() + (b.x() - a.x()) * t)), int(std::lround(a.y() + (b.y() - a.y()) * t)));
        }
        return straight;
    };
    if (m_edges.empty()) return line();
    const int pad = std::max(8, m_width);
    const QRect win = QRect(a, b).normalized().adjusted(-pad, -pad, pad, pad) & QRect(QPoint(), m_size);
    if (!win.contains(a) || !win.contains(b) || qint64(win.width()) * win.height() > 600 * 600) return line();

    // Dijkstra over the window; strong edges are cheap to follow.
    const int ww = win.width(), wh = win.height();
    const int threshold = m_contrast * 255 / 100;
    auto cost = [&](int x, int y) {
        int g = m_edges[size_t(y) * m_size.width() + x];
        if (g < threshold) g = 0;
        return (255 - g) / 255.0 + 0.08;
    };
    std::vector<double> dist(size_t(ww) * wh, std::numeric_limits<double>::max());
    std::vector<int> prev(size_t(ww) * wh, -1);
    auto idx = [&](int x, int y) { return (y - win.top()) * ww + (x - win.left()); };
    using Node = std::pair<double, int>;
    std::priority_queue<Node, std::vector<Node>, std::greater<Node>> queue;
    dist[idx(a.x(), a.y())] = 0;
    queue.emplace(0.0, idx(a.x(), a.y()));
    const int target = idx(b.x(), b.y());
    while (!queue.empty()) {
        const auto [d, i] = queue.top();
        queue.pop();
        if (d > dist[i]) continue;
        if (i == target) break;
        const int x = win.left() + i % ww, y = win.top() + i / ww;
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
                if (!dx && !dy) continue;
                const int nx = x + dx, ny = y + dy;
                if (!win.contains(nx, ny)) continue;
                const double nd = d + cost(nx, ny) * ((dx && dy) ? 1.41421356 : 1.0);
                const int ni = idx(nx, ny);
                if (nd < dist[ni]) {
                    dist[ni] = nd;
                    prev[ni] = i;
                    queue.emplace(nd, ni);
                }
            }
    }
    if (prev[target] < 0 && target != idx(a.x(), a.y())) return line();
    std::vector<QPoint> path;
    for (int i = target; i >= 0; i = prev[i]) path.emplace_back(win.left() + i % ww, win.top() + i / ww);
    std::reverse(path.begin(), path.end());
    return path;
}

void MagneticLassoTool::updateLive(const QPoint& target)
{
    if (m_anchors.isEmpty()) return;
    const QPoint snapped = snapToEdge(target);
    m_live = liveWire(m_anchors.last(), snapped);
    // Lay anchors automatically; higher Frequency places them closer together.
    const size_t spacing = size_t(8 + (100 - m_frequency) * 0.8);
    for (int guard = 0; guard < 8 && m_live.size() > spacing + 1; ++guard) {
        for (size_t i = 1; i <= spacing; ++i) m_points.append(centre(m_live[i]));
        m_anchors.append(m_live[spacing]);
        m_live = liveWire(m_anchors.last(), snapped);
    }
}

void MagneticLassoTool::addAnchor(const QPoint& p)
{
    updateLive(p);
    for (size_t i = 1; i < m_live.size(); ++i) m_points.append(centre(m_live[i]));
    if (!m_live.empty() && m_live.back() != m_anchors.last()) m_anchors.append(m_live.back());
    m_live.clear();
}

void MagneticLassoTool::mousePress(CanvasView* v, const ToolEvent& e)
{
    Document* doc = v->document();
    if (!m_active) {
        if (opFor(e.mods) == Sel::Op::Replace && beginMoveSelection(v, e)) return;
        prepare(doc);
        m_active = true;
        m_op = opFor(e.mods);
        const QPoint start = snapToEdge(e.pixel());
        m_anchors = {start};
        m_points = {centre(start)};
        m_live.clear();
        emit overlayChanged();
        return;
    }
    // Clicking the starting point closes the selection.
    if (m_anchors.size() > 2 && QLineF(v->canvasToView(m_points.first()), e.viewPos).length() < 7) {
        finish(v, false);
        return;
    }
    addAnchor(e.pixel());
    emit overlayChanged();
}

void MagneticLassoTool::mouseMove(CanvasView* v, const ToolEvent& e)
{
    if (m_movingSelection) {
        updateMoveSelection(v, e);
        return;
    }
    if (!m_active) return;
    updateLive(e.pixel());
    emit overlayChanged();
}

void MagneticLassoTool::mouseRelease(CanvasView* v, const ToolEvent&)
{
    if (m_movingSelection) endMoveSelection(v);
}

void MagneticLassoTool::mouseDoubleClick(CanvasView* v, const ToolEvent&)
{
    if (m_active) finish(v, true);
}

void MagneticLassoTool::finish(CanvasView* v, bool closeMagnetically)
{
    if (m_movingSelection) endMoveSelection(v);
    if (!m_active) return;
    if (closeMagnetically && m_anchors.size() > 1) {
        const auto back = liveWire(m_anchors.last(), m_anchors.first());
        for (size_t i = 1; i < back.size(); ++i) m_points.append(centre(back[i]));
    }
    QPolygonF pts = m_points;
    m_active = false;
    m_points.clear();
    m_anchors.clear();
    m_live.clear();
    m_edges.clear();
    emit overlayChanged();
    if (pts.size() < 3) {
        if (m_op == Sel::Op::Replace) Ops::deselect(v->document());
        return;
    }
    QPainterPath path;
    path.addPolygon(pts);
    path.closeSubpath();
    applyShape(v, path, m_op, QStringLiteral("Magnetic Lasso"));
}

bool MagneticLassoTool::keyPress(CanvasView* v, QKeyEvent* e)
{
    if (m_active && (e->key() == Qt::Key_Backspace || e->key() == Qt::Key_Delete)) {
        if (m_anchors.size() <= 1) {
            cancel(v);
            return true;
        }
        // Drop the last anchor and the path that led to it.
        m_anchors.removeLast();
        const QPointF a = centre(m_anchors.last());
        while (m_points.size() > 1 && m_points.last() != a) m_points.removeLast();
        updateLive(v->lastCanvasPos().toPoint());
        emit overlayChanged();
        return true;
    }
    return !m_active && nudgeSelection(v, e);
}

bool MagneticLassoTool::commit(CanvasView* v)
{
    if (!m_active) return false;
    finish(v, true);
    return true;
}

bool MagneticLassoTool::cancel(CanvasView*)
{
    if (!m_active) return false;
    m_active = false;
    m_points.clear();
    m_anchors.clear();
    m_live.clear();
    emit overlayChanged();
    return true;
}

void MagneticLassoTool::deactivated(CanvasView* v) { cancel(v); }

void MagneticLassoTool::paintOverlay(QPainter& p, CanvasView* v)
{
    if (!m_active || m_points.isEmpty()) return;
    QPolygonF view;
    for (const QPointF& pt : m_points) view.append(v->canvasToView(pt));
    for (size_t i = 1; i < m_live.size(); ++i) view.append(v->canvasToView(centre(m_live[i])));
    QPainterPath path;
    path.addPolygon(view);
    drawOutline(p, path);
    p.setPen(QPen(Qt::black, 1));
    p.setBrush(Qt::white);
    for (const QPoint& a : m_anchors) {
        const QPointF c = v->canvasToView(centre(a));
        p.drawRect(QRectF(c.x() - 2.5, c.y() - 2.5, 5, 5));
    }
}

// ---------------- MagicWandTool ----------------

QWidget* MagicWandTool::createOptions(QWidget* parent)
{
    auto* w = new QWidget(parent);
    auto* lay = new QHBoxLayout(w);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(6);
    addSelectionOptions(lay, w, false, false);
    lay->addWidget(new QLabel(QStringLiteral("Sample Size:"), w));
    auto* size = new QComboBox(w);
    for (int n : {1, 3, 5, 11, 31, 51, 101})
        size->addItem(n == 1 ? QStringLiteral("Point Sample") : QStringLiteral("%1 by %1 Average").arg(n), n);
    connect(size, &QComboBox::currentIndexChanged, this, [this, size](int) { m_sampleSize = size->currentData().toInt(); });
    lay->addWidget(size);
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
    addCheck(QStringLiteral("Sample All Layers"), &m_allLayers);
    lay->addWidget(makeVSeparator(w));
    auto* mask = new QPushButton(QStringLiteral("Select and Mask..."), w);
    mask->setEnabled(false);
    lay->addWidget(mask);
    lay->addStretch();
    return w;
}

QCursor MagicWandTool::cursor(CanvasView*, Qt::KeyboardModifiers) const
{
    return makeIconCursor(QStringLiteral("tool-magic-wand"), QPoint(15, 6));
}

void MagicWandTool::mousePress(CanvasView* v, const ToolEvent& e)
{
    Document* doc = v->document();
    const QPoint px = e.pixel();
    if (!doc->bounds().contains(px)) return;
    const Layer* l = doc->editLayer();
    if (!l) return;
    const QImage sample = m_allLayers ? doc->composite() : l->toCanvasImage(doc->size());
    QImage mask = ImageOps::floodMask(sample, px, m_tolerance, m_contiguous, m_antialias, m_sampleSize);
    applyMask(v, mask, opFor(e.mods), QStringLiteral("Magic Wand"));
}

// ---------------- QuickSelectionTool ----------------

QWidget* QuickSelectionTool::createOptions(QWidget* parent)
{
    auto* w = new QWidget(parent);
    auto* lay = new QHBoxLayout(w);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(6);
    addSelectionOptions(lay, w, false, false, true);
    auto* size = new QSpinBox(w);
    size->setRange(1, 5000);
    size->setSuffix(QStringLiteral(" px"));
    size->setButtonSymbols(QAbstractSpinBox::NoButtons);
    size->setFixedWidth(60);
    size->setValue(m_size);
    m_sizeField = size;
    connect(size, &QSpinBox::valueChanged, this, [this](int v) {
        m_size = v;
        emit overlayChanged();
    });
    lay->addWidget(new ScrubbyLabel(QStringLiteral("Size:"), size, w));
    lay->addWidget(size);
    lay->addWidget(makeVSeparator(w));
    auto addCheck = [&](const QString& text, bool* target) {
        auto* cb = new QCheckBox(text, w);
        cb->setChecked(*target);
        connect(cb, &QCheckBox::toggled, this, [target](bool on) { *target = on; });
        lay->addWidget(cb);
    };
    addCheck(QStringLiteral("Sample All Layers"), &m_allLayers);
    addCheck(QStringLiteral("Auto-Enhance"), &m_autoEnhance);
    lay->addWidget(makeVSeparator(w));
    auto* mask = new QPushButton(QStringLiteral("Select and Mask..."), w);
    mask->setEnabled(false);
    lay->addWidget(mask);
    lay->addStretch();
    return w;
}

void QuickSelectionTool::adjustSize(int direction)
{
    const int step = m_size < 10 ? 1 : (m_size < 50 ? 5 : (m_size < 100 ? 10 : 25));
    m_size = std::clamp(m_size + direction * step, 1, 5000);
    if (m_sizeField) m_sizeField->setValue(m_size);
    if (CanvasView* v = m_manager->activeView()) v->updateCursor();
}

void QuickSelectionTool::dab(const QPointF& c)
{
    // Segment from the colours under the brush: grow into similar pixels nearby, without
    // crossing sharp colour steps.
    const QSize size = m_sample.size();
    const double r = std::max(0.5, m_size / 2.0);
    const QRect disk = QRectF(c.x() - r, c.y() - r, 2 * r, 2 * r).toAlignedRect() & QRect(QPoint(), size);
    if (disk.isEmpty()) return;
    auto px = [&](int x, int y) { return reinterpret_cast<const QRgb*>(m_sample.constScanLine(y))[x]; };
    auto inDisk = [&](int x, int y) { return std::hypot(x + 0.5 - c.x(), y + 0.5 - c.y()) <= r; };
    double sum[4] = {0, 0, 0, 0}, sq[4] = {0, 0, 0, 0};
    int n = 0;
    for (int y = disk.top(); y <= disk.bottom(); ++y)
        for (int x = disk.left(); x <= disk.right(); ++x) {
            if (!inDisk(x, y)) continue;
            const QRgb p = px(x, y);
            const int ch[4] = {qRed(p), qGreen(p), qBlue(p), qAlpha(p)};
            for (int i = 0; i < 4; ++i) {
                sum[i] += ch[i];
                sq[i] += double(ch[i]) * ch[i];
            }
            ++n;
        }
    if (!n) return;
    int mean[4];
    double spread = 0;
    for (int i = 0; i < 4; ++i) {
        mean[i] = int(sum[i] / n);
        spread += std::sqrt(std::max(0.0, sq[i] / n - (sum[i] / n) * (sum[i] / n)));
    }
    const int tol = std::clamp(int(spread / 2 + 18), 18, 64);
    const int step = tol * 2 / 3;
    auto diff = [](QRgb a, const int* b) {
        return std::max({std::abs(qRed(a) - b[0]), std::abs(qGreen(a) - b[1]), std::abs(qBlue(a) - b[2]), std::abs(qAlpha(a) - b[3])});
    };
    auto diff2 = [](QRgb a, QRgb b) {
        return std::max({std::abs(qRed(a) - qRed(b)), std::abs(qGreen(a) - qGreen(b)), std::abs(qBlue(a) - qBlue(b)),
                         std::abs(qAlpha(a) - qAlpha(b))});
    };
    const double reach = std::max(3 * r, r + 24);
    const QRect win = QRectF(c.x() - reach, c.y() - reach, 2 * reach, 2 * reach).toAlignedRect() & QRect(QPoint(), size);
    std::vector<uchar> seen(size_t(win.width()) * win.height(), 0);
    auto at = [&](int x, int y) -> uchar& { return seen[size_t(y - win.top()) * win.width() + (x - win.left())]; };
    std::vector<QPoint> stack;
    for (int y = disk.top(); y <= disk.bottom(); ++y)
        for (int x = disk.left(); x <= disk.right(); ++x)
            if (inDisk(x, y)) {
                at(x, y) = 1;
                stack.emplace_back(x, y);
            }
    while (!stack.empty()) {
        const QPoint p = stack.back();
        stack.pop_back();
        const QRgb pc = px(p.x(), p.y());
        const QPoint nbrs[4] = {{p.x() - 1, p.y()}, {p.x() + 1, p.y()}, {p.x(), p.y() - 1}, {p.x(), p.y() + 1}};
        for (const QPoint& q : nbrs) {
            if (!win.contains(q) || at(q.x(), q.y())) continue;
            const QRgb qc = px(q.x(), q.y());
            if (diff(qc, mean) > tol || diff2(qc, pc) > step) continue;
            at(q.x(), q.y()) = 1;
            stack.push_back(q);
        }
    }
    for (int y = win.top(); y <= win.bottom(); ++y) {
        uchar* d = m_stroke.scanLine(y);
        for (int x = win.left(); x <= win.right(); ++x)
            if (at(x, y)) d[x] = 255;
    }
}

void QuickSelectionTool::preview(Document* doc, bool force)
{
    if (!force && m_previewTimer.isValid() && m_previewTimer.elapsed() < 40) return;
    m_previewTimer.start();
    doc->setSelectionRaw(Sel::combine(m_base, m_stroke, m_op));
}

void QuickSelectionTool::mousePress(CanvasView* v, const ToolEvent& e)
{
    Document* doc = v->document();
    const Layer* l = doc->editLayer();
    if (!l) return;
    m_op = e.alt() ? Sel::Op::Subtract : (e.shift() ? Sel::Op::Add : m_mode);
    m_before = doc->state();
    m_base = doc->selection();
    m_sample = m_allLayers ? doc->composite() : l->toCanvasImage(doc->size());
    m_stroke = Sel::empty(doc->size());
    m_painting = true;
    m_last = e.pos;
    dab(e.pos);
    preview(doc, true);
}

void QuickSelectionTool::mouseMove(CanvasView* v, const ToolEvent& e)
{
    if (!m_painting) return;
    const double spacing = std::max(1.0, m_size / 4.0);
    const double len = QLineF(m_last, e.pos).length();
    if (len < spacing) return;
    const int steps = int(len / spacing);
    for (int i = 1; i <= steps; ++i) dab(m_last + (e.pos - m_last) * (double(i) / steps));
    m_last = e.pos;
    preview(v->document(), false);
}

void QuickSelectionTool::mouseRelease(CanvasView* v, const ToolEvent&)
{
    if (!m_painting) return;
    m_painting = false;
    Document* doc = v->document();
    QImage stroke = m_stroke;
    if (m_autoEnhance) {
        // Smooth jagged edges and soften them slightly.
        stroke = Sel::smoothed(stroke, 1, true);
        if (!stroke.isNull()) Sel::feather(stroke, 1.0);
    }
    doc->setSelectionRaw(Sel::combine(m_base, stroke.isNull() ? Sel::empty(doc->size()) : stroke, m_op));
    const bool changed = doc->selection().cacheKey() != m_before.selection.cacheKey()
        || doc->selection().isNull() != m_before.selection.isNull();
    if (changed) doc->pushSnapshot(QStringLiteral("Quick Selection"), m_before);
    m_sample = m_stroke = m_base = QImage();
    m_before = DocState();
    // After the first stroke, Photoshop switches to Add to Selection.
    if (m_mode == Sel::Op::Replace) {
        m_mode = Sel::Op::Add;
        if (m_modeButtons)
            if (auto* b = m_modeButtons->button(int(Sel::Op::Add))) b->setChecked(true);
    }
}
