#include "tools/TransformTools.h"

#include "core/ColorState.h"
#include "core/Compositor.h"
#include "core/DocumentOps.h"
#include "core/ImageOps.h"
#include "core/LayerTree.h"
#include "core/Selection.h"
#include "tools/ToolManager.h"
#include "ui/CanvasView.h"
#include "ui/Widgets.h"

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

// ---------------- Move ----------------

QWidget* MoveTool::createOptions(QWidget* parent)
{
    auto* w = new QWidget(parent);
    auto* lay = new QHBoxLayout(w);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(6);
    auto* autoSel = new QCheckBox(QStringLiteral("Auto-Select:"), w);
    connect(autoSel, &QCheckBox::toggled, this, [this](bool on) { m_autoSelect = on; });
    lay->addWidget(autoSel);
    auto* target = new QComboBox(w);
    target->addItems({QStringLiteral("Layer"), QStringLiteral("Group")});
    connect(target, &QComboBox::currentIndexChanged, this, [this](int i) { m_autoSelectGroup = i == 1; });
    lay->addWidget(target);
    lay->addWidget(makeVSeparator(w));
    auto* transform = new QCheckBox(QStringLiteral("Show Transform Controls"), w);
    transform->setEnabled(false);
    lay->addWidget(transform);
    lay->addStretch();
    return w;
}

QCursor MoveTool::cursor(CanvasView*, Qt::KeyboardModifiers) const
{
    return makeIconCursor(QStringLiteral("tool-move"), QPoint(11, 11));
}

bool MoveTool::begin(Document* doc)
{
    Layer* active = doc->activeLayer();
    if (!active) return false;
    m_floating = doc->hasSelection();
    if (m_floating) {
        if (!m_manager->preparePixelEdit(doc, QStringLiteral("Could not use the move tool"))) return false;
    } else if (active->positionLocked()) {
        alert(QStringLiteral("Could not use the move tool because the layer is locked."));
        return false;
    }
    m_before = doc->state();
    m_delta = QPoint();
    if (m_floating) {
        // Lift the selected pixels (of the layer or its mask) into a floating image.
        m_index = doc->editIndex();
        Layer* l = &doc->layerRef(m_index);
        if (!l->isBackground) l->ensureCovers(doc->bounds());
        const QRect r = doc->selectionBounds();
        m_float = QImage(r.size(), QImage::Format_ARGB32_Premultiplied);
        m_float.fill(Qt::transparent);
        const QImage& sel = doc->selection();
        const QRect src = r & l->rect();
        for (int y = src.top(); y <= src.bottom(); ++y) {
            auto* d = reinterpret_cast<uint32_t*>(m_float.scanLine(y - r.top())) + (src.left() - r.left());
            auto* s = reinterpret_cast<const uint32_t*>(l->image.constScanLine(y - l->offset.y())) + (src.left() - l->offset.x());
            const uchar* m = sel.constScanLine(y) + src.left();
            for (int i = 0; i < src.width(); ++i) d[i] = Blend::byteMul(s[i], m[i]);
        }
        m_floatOrigin = r.topLeft();
        if (l->isBackground)
            ImageOps::fillColor(*l, r, ImageOps::premultiplied(colors()->background()), BlendMode::Normal, 1.f, sel, false);
        else
            ImageOps::clearPixels(*l, r, sel);
        m_cleared = l->image;
        m_prevRect = QRect();
        m_selOrig = sel;
        m_snapRect = r;
        moveTo(doc, QPoint());
    } else {
        // The whole layer moves, with a group's contents and linked masks.
        m_index = doc->activeIndex();
        m_subStart = Tree::subtreeStart(doc->layers(), m_index);
        m_origLayers = doc->layers().mid(m_subStart, m_index - m_subStart + 1);
        m_prevExtent = Compositor::extent(doc->layers(), m_index, doc->bounds());
        QRect content;
        for (const Layer& l : m_origLayers) {
            Layer probe = l;
            probe.trimToContent();
            content |= probe.rect();
        }
        m_snapRect = content;
    }
    m_active = true;
    return true;
}

void MoveTool::moveTo(Document* doc, const QPoint& delta)
{
    if (!m_floating) {
        for (int i = 0; i < m_origLayers.size(); ++i) {
            Layer& l = doc->layerRef(m_subStart + i);
            l = m_origLayers[i];
            l.translate(delta);
        }
        const QRect ext = Compositor::extent(doc->layers(), m_index, doc->bounds());
        doc->notifyLayerPixels(m_index, m_prevExtent | ext);
        m_prevExtent = ext;
        m_delta = delta;
        return;
    }
    Layer* l = &doc->layerRef(m_index);
    const int idx = m_index;
    // Restore the previously covered area, then stamp the floating pixels.
    const QRect lr = l->rect();
    QRect restore = m_prevRect & lr;
    for (int y = restore.top(); y <= restore.bottom(); ++y)
        memcpy(l->image.scanLine(y - lr.top()) + (restore.left() - lr.left()) * 4,
               m_cleared.constScanLine(y - lr.top()) + (restore.left() - lr.left()) * 4, size_t(restore.width()) * 4);
    const QRect target = QRect(m_floatOrigin + delta, m_float.size()) & lr;
    for (int y = target.top(); y <= target.bottom(); ++y) {
        auto* d = reinterpret_cast<uint32_t*>(l->image.scanLine(y - lr.top())) + (target.left() - lr.left());
        auto* s = reinterpret_cast<const uint32_t*>(m_float.constScanLine(y - m_floatOrigin.y() - delta.y()))
            + (target.left() - m_floatOrigin.x() - delta.x());
        Blend::compositeRow(d, s, target.width(), BlendMode::Normal, 1.f);
    }
    doc->notifyLayerPixels(idx, m_prevRect | target);
    m_prevRect = target;
    if (delta != m_delta || delta.isNull())
        doc->setSelectionRaw(delta.isNull() ? m_selOrig : Sel::translated(m_selOrig, delta));
    m_delta = delta;
}

void MoveTool::finish(Document* doc, const QString& text)
{
    m_active = false;
    if (m_delta.isNull()) {
        doc->restoreState(m_before);
    } else {
        doc->pushSnapshot(text, m_before);
    }
    m_float = QImage();
    m_cleared = QImage();
    m_selOrig = QImage();
    m_origLayers.clear();
    m_before = DocState();
}

void MoveTool::mousePress(CanvasView* v, const ToolEvent& e)
{
    Document* doc = v->document();
    if (m_autoSelect) {
        // The top visible layer with pixels under the pointer (or its outermost group).
        const QPoint px = e.pixel();
        const QList<Layer>& layers = doc->layers();
        for (int i = doc->layerCount() - 1; i >= 0; --i) {
            const Layer& l = layers[i];
            if (l.isGroup() || l.kind == LayerKind::Adjustment || !Tree::effectivelyVisible(layers, i)) continue;
            if (qAlpha(l.pixelAt(px)) == 0 || l.maskAt(px) == 0) continue;
            int pick = i;
            if (m_autoSelectGroup)
                for (int p = Tree::parentIndex(layers, i); p >= 0; p = Tree::parentIndex(layers, p)) pick = p;
            doc->setActiveIndex(pick);
            break;
        }
    }
    if (!begin(doc)) return;
    m_start = e.pos;
}

void MoveTool::mouseMove(CanvasView* v, const ToolEvent& e)
{
    if (!m_active) return;
    QPointF d = e.pos - m_start;
    if (e.shift()) {
        // Constrain to horizontal / vertical.
        if (std::fabs(d.x()) > std::fabs(d.y())) d.setY(0);
        else d.setX(0);
    }
    if (!m_snapRect.isEmpty()) d += v->snapRectOffset(QRectF(m_snapRect).translated(d));
    QPoint delta(int(std::round(d.x())), int(std::round(d.y())));
    if (delta != m_delta) moveTo(v->document(), delta);
}

void MoveTool::mouseRelease(CanvasView* v, const ToolEvent&)
{
    if (!m_active) return;
    finish(v->document(), QStringLiteral("Move"));
}

bool MoveTool::keyPress(CanvasView* v, QKeyEvent* e)
{
    QPoint d;
    switch (e->key()) {
    case Qt::Key_Left: d = QPoint(-1, 0); break;
    case Qt::Key_Right: d = QPoint(1, 0); break;
    case Qt::Key_Up: d = QPoint(0, -1); break;
    case Qt::Key_Down: d = QPoint(0, 1); break;
    default: return false;
    }
    if (m_active) return true;
    if (e->modifiers() & Qt::ShiftModifier) d *= 10;
    Document* doc = v->document();
    if (!begin(doc)) return true;
    moveTo(doc, d);
    finish(doc, QStringLiteral("Nudge"));
    return true;
}

// ---------------- Crop ----------------

QWidget* CropTool::createOptions(QWidget* parent)
{
    auto* w = new QWidget(parent);
    auto* lay = new QHBoxLayout(w);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(6);
    auto* preset = new QComboBox(w);
    const struct { const char* name; double w, h; } presets[] = {
        {"Ratio", 0, 0},          {"Original Ratio", -1, -1}, {"1 : 1 (Square)", 1, 1},
        {"4 : 5 (8 : 10)", 4, 5}, {"5 : 7", 5, 7},            {"2 : 3 (4 : 6)", 2, 3},
        {"16 : 9", 16, 9},
    };
    for (const auto& p : presets) preset->addItem(QString::fromLatin1(p.name), QPointF(p.w, p.h));
    lay->addWidget(preset);
    auto* ws = new QDoubleSpinBox(w);
    auto* hs = new QDoubleSpinBox(w);
    for (QDoubleSpinBox* s : {ws, hs}) {
        s->setRange(0, 100000);
        s->setDecimals(0);
        s->setSpecialValueText(QStringLiteral(" "));
        s->setButtonSymbols(QAbstractSpinBox::NoButtons);
        s->setFixedWidth(48);
    }
    auto* swap = new QPushButton(QStringLiteral("⇄"), w);
    swap->setStyleSheet(QStringLiteral("QPushButton { min-width: 0; padding: 2px 6px; }"));
    auto* clear = new QPushButton(QStringLiteral("Clear"), w);
    clear->setStyleSheet(QStringLiteral("QPushButton { min-width: 0; padding: 2px 10px; }"));
    lay->addWidget(ws);
    lay->addWidget(swap);
    lay->addWidget(hs);
    lay->addWidget(clear);
    auto apply = [this, ws, hs] {
        m_ratioW = ws->value();
        m_ratioH = hs->value();
    };
    connect(ws, &QDoubleSpinBox::valueChanged, this, apply);
    connect(hs, &QDoubleSpinBox::valueChanged, this, apply);
    connect(swap, &QPushButton::clicked, this, [ws, hs] {
        double a = ws->value();
        ws->setValue(hs->value());
        hs->setValue(a);
    });
    connect(clear, &QPushButton::clicked, this, [ws, hs, preset] {
        preset->setCurrentIndex(0);
        ws->setValue(0);
        hs->setValue(0);
    });
    connect(preset, &QComboBox::currentIndexChanged, this, [this, preset, ws, hs] {
        QPointF r = preset->currentData().toPointF();
        if (r.x() < 0 && m_doc) r = QPointF(m_doc->width(), m_doc->height());
        ws->setValue(r.x());
        hs->setValue(r.y());
    });
    lay->addWidget(makeVSeparator(w));
    auto* straighten = new QPushButton(QStringLiteral("Straighten"), w);
    straighten->setEnabled(false);
    lay->addWidget(straighten);
    auto* overlay = new QComboBox(w);
    overlay->addItems({QStringLiteral("Rule of Thirds"), QStringLiteral("Grid"), QStringLiteral("None")});
    connect(overlay, &QComboBox::currentIndexChanged, this, [this](int i) {
        m_overlay = i;
        emit overlayChanged();
    });
    lay->addWidget(overlay);
    lay->addWidget(makeVSeparator(w));
    auto* del = new QCheckBox(QStringLiteral("Delete Cropped Pixels"), w);
    del->setChecked(m_deletePixels);
    connect(del, &QCheckBox::toggled, this, [this](bool on) { m_deletePixels = on; });
    lay->addWidget(del);
    auto* ca = new QCheckBox(QStringLiteral("Content-Aware"), w);
    ca->setEnabled(false);
    lay->addWidget(ca);
    lay->addStretch();
    auto* cancelBtn = makeIconButton(QStringLiteral("cancel"), QStringLiteral("Cancel current crop operation (Esc)"), w);
    auto* commitBtn = makeIconButton(QStringLiteral("check"), QStringLiteral("Commit current crop operation (Enter)"), w);
    connect(cancelBtn, &QToolButton::clicked, this, [this] {
        if (CanvasView* v = m_manager->activeView()) cancel(v);
    });
    connect(commitBtn, &QToolButton::clicked, this, [this] {
        if (CanvasView* v = m_manager->activeView()) commit(v);
    });
    lay->addWidget(cancelBtn);
    lay->addWidget(commitBtn);
    return w;
}

double CropTool::ratio() const
{
    return (m_ratioW > 0 && m_ratioH > 0) ? m_ratioW / m_ratioH : 0.0;
}

void CropTool::reset(CanvasView* v)
{
    m_doc = v ? v->document() : nullptr;
    m_rect = m_doc ? QRectF(m_doc->bounds()) : QRectF();
    m_drag = None;
    emit overlayChanged();
}

void CropTool::activated(CanvasView* v) { reset(v); }

void CropTool::deactivated(CanvasView*)
{
    m_doc = nullptr;
    m_rect = QRectF();
}

CropTool::Handle CropTool::hitTest(CanvasView* v, const QPointF& p) const
{
    const QRectF r = v->canvasToView(m_rect);
    const QPointF pts[8] = {r.topLeft(), QPointF(r.center().x(), r.top()), r.topRight(),
                            QPointF(r.right(), r.center().y()), r.bottomRight(),
                            QPointF(r.center().x(), r.bottom()), r.bottomLeft(),
                            QPointF(r.left(), r.center().y())};
    for (int i = 0; i < 8; ++i)
        if (QLineF(pts[i], p).length() <= 8) return Handle(i);
    return r.contains(p) ? Inside : Outside;
}

void CropTool::mousePress(CanvasView* v, const ToolEvent& e)
{
    if (m_doc != v->document()) reset(v);
    m_drag = hitTest(v, e.viewPos);
    // Dragging inside the untouched default box draws a new crop area, as in Photoshop.
    if (m_drag == Inside && m_rect == QRectF(v->document()->bounds())) m_drag = Outside;
    m_pressPos = v->snapPoint(e.pos);
    m_pressRect = m_rect;
    if (m_drag == Outside) m_rect = QRectF(m_pressPos, QSizeF(0, 0));
}

void CropTool::mouseMove(CanvasView* v, const ToolEvent& e)
{
    if (m_drag == None) {
        // Hover feedback.
        switch (hitTest(v, e.viewPos)) {
        case TL: case BR: v->viewport()->setCursor(Qt::SizeFDiagCursor); break;
        case TR: case BL: v->viewport()->setCursor(Qt::SizeBDiagCursor); break;
        case T: case B: v->viewport()->setCursor(Qt::SizeVerCursor); break;
        case L: case R: v->viewport()->setCursor(Qt::SizeHorCursor); break;
        case Inside: v->viewport()->setCursor(Qt::SizeAllCursor); break;
        default: v->viewport()->setCursor(cursor(v, e.mods)); break;
        }
        return;
    }
    const QPointF p = v->snapPoint(QPointF(std::round(e.pos.x()), std::round(e.pos.y())));
    QPointF d = e.pos - m_pressPos;
    if (m_drag == Inside) d += v->snapRectOffset(m_pressRect.translated(d));
    double fixed = ratio();
    if (!fixed && e.shift() && m_pressRect.height() > 0) fixed = m_pressRect.width() / m_pressRect.height();
    QRectF r = m_pressRect;
    switch (m_drag) {
    case Inside:
        r.translate(std::round(d.x()), std::round(d.y()));
        break;
    case Outside: {
        QPointF a = m_pressPos;
        QPointF b = p;
        if (fixed > 0) {
            double w = std::fabs(b.x() - a.x());
            b.setY(a.y() + std::copysign(w / fixed, b.y() - a.y()));
        }
        r = QRectF(a, b).normalized();
        break;
    }
    default: {
        if (m_drag == TL || m_drag == L || m_drag == BL) r.setLeft(p.x());
        if (m_drag == TR || m_drag == R || m_drag == BR) r.setRight(p.x());
        if (m_drag == TL || m_drag == T || m_drag == TR) r.setTop(p.y());
        if (m_drag == BL || m_drag == B || m_drag == BR) r.setBottom(p.y());
        r = r.normalized();
        if (fixed > 0) {
            if (m_drag == T || m_drag == B) r.setWidth(r.height() * fixed);
            else r.setHeight(r.width() / fixed);
        }
        break;
    }
    }
    m_rect = r;
    emit overlayChanged();
}

void CropTool::mouseRelease(CanvasView* v, const ToolEvent&)
{
    if (m_drag == Outside && (m_rect.width() < 1 || m_rect.height() < 1)) m_rect = m_pressRect;
    m_drag = None;
    if (m_rect.isEmpty()) reset(v);
    emit overlayChanged();
}

void CropTool::mouseDoubleClick(CanvasView* v, const ToolEvent& e)
{
    if (hitTest(v, e.viewPos) == Inside) commit(v);
}

bool CropTool::commit(CanvasView* v)
{
    Document* doc = v->document();
    if (m_doc != doc || m_rect.isEmpty()) return false;
    QRect r = m_rect.toAlignedRect();
    if (r != doc->bounds()) Ops::crop(doc, r, m_deletePixels);
    reset(v);
    return true;
}

bool CropTool::cancel(CanvasView* v)
{
    reset(v);
    return true;
}

QCursor CropTool::cursor(CanvasView*, Qt::KeyboardModifiers) const
{
    return makeIconCursor(QStringLiteral("tool-crop"), QPoint(7, 7));
}

void CropTool::paintOverlay(QPainter& p, CanvasView* v)
{
    if (m_doc != v->document() || m_rect.isEmpty()) return;
    const QRectF r = v->canvasToView(m_rect);
    const QRectF all = QRectF(v->viewport()->rect());
    // Shade outside the crop box.
    QPainterPath outside;
    outside.addRect(all);
    QPainterPath inside;
    inside.addRect(r);
    p.fillPath(outside.subtracted(inside), QColor(0, 0, 0, 120));

    p.setRenderHint(QPainter::Antialiasing, false);
    // Overlay guides.
    p.setPen(QPen(QColor(255, 255, 255, 110), 1));
    if (m_overlay == 0) {
        for (int i = 1; i < 3; ++i) {
            p.drawLine(QPointF(r.left() + r.width() * i / 3, r.top()), QPointF(r.left() + r.width() * i / 3, r.bottom()));
            p.drawLine(QPointF(r.left(), r.top() + r.height() * i / 3), QPointF(r.right(), r.top() + r.height() * i / 3));
        }
    } else if (m_overlay == 1) {
        for (int i = 1; i < 8; ++i) {
            p.drawLine(QPointF(r.left() + r.width() * i / 8, r.top()), QPointF(r.left() + r.width() * i / 8, r.bottom()));
            p.drawLine(QPointF(r.left(), r.top() + r.height() * i / 8), QPointF(r.right(), r.top() + r.height() * i / 8));
        }
    }
    p.setPen(QPen(QColor(230, 230, 230), 1));
    p.setBrush(Qt::NoBrush);
    p.drawRect(r);

    // Thick corner and edge handles.
    p.setPen(QPen(Qt::white, 3, Qt::SolidLine, Qt::SquareCap));
    const double c = std::min(16.0, std::min(r.width(), r.height()) / 3);
    auto corner = [&](QPointF pt, double dx, double dy) {
        p.drawLine(pt, pt + QPointF(dx * c, 0));
        p.drawLine(pt, pt + QPointF(0, dy * c));
    };
    corner(r.topLeft(), 1, 1);
    corner(r.topRight(), -1, 1);
    corner(r.bottomLeft(), 1, -1);
    corner(r.bottomRight(), -1, -1);
    const double h = c / 2;
    p.drawLine(QPointF(r.center().x() - h, r.top()), QPointF(r.center().x() + h, r.top()));
    p.drawLine(QPointF(r.center().x() - h, r.bottom()), QPointF(r.center().x() + h, r.bottom()));
    p.drawLine(QPointF(r.left(), r.center().y() - h), QPointF(r.left(), r.center().y() + h));
    p.drawLine(QPointF(r.right(), r.center().y() - h), QPointF(r.right(), r.center().y() + h));
}
