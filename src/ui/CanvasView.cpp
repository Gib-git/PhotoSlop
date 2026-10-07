#include "ui/CanvasView.h"

#include "app/Theme.h"
#include "core/Document.h"
#include "tools/Tool.h"
#include "tools/ToolManager.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QNativeGestureEvent>
#include <QPainter>
#include <QScrollBar>
#include <QTabletEvent>
#include <QWheelEvent>
#include <cmath>
#include <algorithm>
#include <iterator>

namespace {

const double kZoomSteps[] = {0.01,   0.02, 0.03, 0.04, 0.05, 0.0625, 0.0833, 0.125, 0.1667,
                             0.25,   0.3333, 0.5, 0.6667, 1.0, 2.0,  3.0,   4.0,   5.0,
                             6.0,    7.0,  8.0,  12.0, 16.0, 24.0, 32.0,  64.0,  128.0};
constexpr double kMinZoom = 0.01;
constexpr double kMaxZoom = 128.0;

const QBrush& checkerBrush()
{
    static QBrush brush = [] {
        QPixmap pm(16, 16);
        pm.fill(Qt::white);
        QPainter p(&pm);
        p.fillRect(0, 0, 8, 8, QColor(0xcc, 0xcc, 0xcc));
        p.fillRect(8, 8, 8, 8, QColor(0xcc, 0xcc, 0xcc));
        return QBrush(pm);
    }();
    return brush;
}

QBrush antsBrush(int phase)
{
    static QPixmap pm = [] {
        QImage img(8, 8, QImage::Format_ARGB32);
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 8; ++x)
                img.setPixel(x, y, ((x + y) % 8) < 4 ? 0xff000000 : 0xffffffff);
        return QPixmap::fromImage(img);
    }();
    QBrush b(pm);
    b.setTransform(QTransform::fromTranslate(phase, 0));
    return b;
}

} // namespace

CanvasView::CanvasView(Document* doc, ToolManager* tools, QWidget* parent)
    : QAbstractScrollArea(parent)
    , m_doc(doc)
    , m_tools(tools)
{
    setFrameShape(QFrame::NoFrame);
    setFocusPolicy(Qt::StrongFocus);
    viewport()->setMouseTracking(true);
    viewport()->setAttribute(Qt::WA_OpaquePaintEvent);
    viewport()->setAttribute(Qt::WA_TabletTracking);
    viewport()->grabGesture(Qt::PinchGesture);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    horizontalScrollBar()->setSingleStep(20);
    verticalScrollBar()->setSingleStep(20);

    connect(doc, &Document::imageChanged, this, &CanvasView::onImageChanged);
    connect(doc, &Document::sizeChanged, this, [this] {
        updateScrollBars();
        viewport()->update();
        emit viewChanged();
    });
    connect(doc, &Document::selectionChanged, this, [this] {
        viewport()->update();
        if (m_doc->hasSelection()) m_antsTimer.start();
        else m_antsTimer.stop();
    });

    m_antsTimer.setInterval(120);
    connect(&m_antsTimer, &QTimer::timeout, this, [this] {
        m_antsPhase = (m_antsPhase + 1) % 8;
        QRectF r = selectionViewRect();
        if (!r.isEmpty()) viewport()->update(r.toAlignedRect().adjusted(-2, -2, 2, 2));
    });
}

double CanvasView::scale() const { return m_zoom / viewport()->devicePixelRatioF(); }

QPointF CanvasView::origin() const
{
    const double s = scale();
    const double cw = m_doc->width() * s, ch = m_doc->height() * s;
    const QSize vs = viewport()->size();
    double x = cw <= vs.width() ? (vs.width() - cw) / 2.0 : -horizontalScrollBar()->value();
    double y = ch <= vs.height() ? (vs.height() - ch) / 2.0 : -verticalScrollBar()->value();
    // Snap to device pixels so 100% zoom stays crisp.
    const double dpr = viewport()->devicePixelRatioF();
    return QPointF(std::round(x * dpr) / dpr, std::round(y * dpr) / dpr);
}

QPointF CanvasView::viewToCanvas(const QPointF& p) const { return (p - origin()) / scale(); }

QPointF CanvasView::canvasToView(const QPointF& p) const { return p * scale() + origin(); }

QRectF CanvasView::canvasToView(const QRectF& r) const
{
    return QRectF(canvasToView(r.topLeft()), canvasToView(r.bottomRight()));
}

QRectF CanvasView::visibleCanvasRect() const
{
    QRectF v(viewToCanvas(QPointF(0, 0)), viewToCanvas(QPointF(viewport()->width(), viewport()->height())));
    return v & QRectF(m_doc->bounds());
}

void CanvasView::updateScrollBars()
{
    const double s = scale();
    const QSize vs = viewport()->size();
    const int cw = int(std::ceil(m_doc->width() * s)), ch = int(std::ceil(m_doc->height() * s));
    horizontalScrollBar()->setRange(0, std::max(0, cw - vs.width()));
    horizontalScrollBar()->setPageStep(vs.width());
    verticalScrollBar()->setRange(0, std::max(0, ch - vs.height()));
    verticalScrollBar()->setPageStep(vs.height());
}

void CanvasView::setZoom(double zoom, const QPointF& anchor)
{
    m_userZoomed = true;
    applyZoom(zoom, anchor);
}

void CanvasView::applyZoom(double zoom, const QPointF& anchorIn)
{
    zoom = std::clamp(zoom, kMinZoom, kMaxZoom);
    QPointF anchor = anchorIn.x() < 0 ? QPointF(viewport()->width() / 2.0, viewport()->height() / 2.0) : anchorIn;
    QPointF canvasPt = viewToCanvas(anchor);
    m_zoom = zoom;
    updateScrollBars();
    const double s = scale();
    horizontalScrollBar()->setValue(int(std::round(canvasPt.x() * s - anchor.x())));
    verticalScrollBar()->setValue(int(std::round(canvasPt.y() * s - anchor.y())));
    viewport()->update();
    updateCursor();
    emit zoomChanged(m_zoom);
    emit viewChanged();
}

void CanvasView::zoomIn(const QPointF& anchor)
{
    for (double z : kZoomSteps)
        if (z > m_zoom * 1.001) return setZoom(z, anchor);
}

void CanvasView::zoomOut(const QPointF& anchor)
{
    for (int i = int(std::size(kZoomSteps)) - 1; i >= 0; --i)
        if (kZoomSteps[i] < m_zoom * 0.999) return setZoom(kZoomSteps[i], anchor);
}

void CanvasView::fitOnScreen()
{
    const double dpr = viewport()->devicePixelRatioF();
    const QSize vs = viewport()->size();
    double z = std::min(double(vs.width()) / m_doc->width(), double(vs.height()) / m_doc->height()) * dpr;
    setZoom(z);
}

void CanvasView::fillScreen()
{
    const double dpr = viewport()->devicePixelRatioF();
    const QSize vs = viewport()->size();
    double z = std::max(double(vs.width()) / m_doc->width(), double(vs.height()) / m_doc->height()) * dpr;
    setZoom(z);
}

void CanvasView::actualPixels() { setZoom(1.0); }

void CanvasView::panBy(const QPointF& d)
{
    horizontalScrollBar()->setValue(horizontalScrollBar()->value() - int(std::round(d.x())));
    verticalScrollBar()->setValue(verticalScrollBar()->value() - int(std::round(d.y())));
}

void CanvasView::centerOn(const QPointF& canvasPt)
{
    const double s = scale();
    horizontalScrollBar()->setValue(int(canvasPt.x() * s - viewport()->width() / 2.0));
    verticalScrollBar()->setValue(int(canvasPt.y() * s - viewport()->height() / 2.0));
}

void CanvasView::setShowPixelGrid(bool on)
{
    m_pixelGrid = on;
    viewport()->update();
}

void CanvasView::setShowSelectionEdges(bool on)
{
    m_showEdges = on;
    viewport()->update();
}

void CanvasView::scrollContentsBy(int, int)
{
    viewport()->update();
    emit viewChanged();
}

bool CanvasView::initialFit()
{
    // Until the user zooms, keep applying the default: 100% when the image fits,
    // otherwise fit on screen snapped to a preset step (Photoshop behaviour).
    if (m_userZoomed || !isVisible() || viewport()->width() <= 50 || viewport()->height() <= 50) return false;
    const double dpr = viewport()->devicePixelRatioF();
    const QSize vs = viewport()->size() * dpr;
    double z = 1.0;
    if (m_doc->width() > vs.width() || m_doc->height() > vs.height()) {
        z = std::min(double(vs.width()) / m_doc->width(), double(vs.height()) / m_doc->height());
        for (int i = int(std::size(kZoomSteps)) - 1; i >= 0; --i)
            if (kZoomSteps[i] <= z) {
                if (z / kZoomSteps[i] < 1.15) z = kZoomSteps[i];
                break;
            }
    }
    applyZoom(z, QPointF(-1, -1));
    return true;
}

void CanvasView::resizeEvent(QResizeEvent* e)
{
    QAbstractScrollArea::resizeEvent(e);
    if (initialFit()) return;
    updateScrollBars();
    emit viewChanged();
}

void CanvasView::showEvent(QShowEvent* e)
{
    QAbstractScrollArea::showEvent(e);
    QTimer::singleShot(0, this, [this] { initialFit(); });
}

void CanvasView::onImageChanged(const QRect& r)
{
    QRectF vr = canvasToView(QRectF(r));
    viewport()->update(vr.toAlignedRect().adjusted(-2, -2, 2, 2));
}

QRectF CanvasView::selectionViewRect() const
{
    if (!m_doc->hasSelection()) return QRectF();
    return canvasToView(QRectF(m_doc->selectionBounds())) & QRectF(viewport()->rect());
}

// ---------------- Painting ----------------

void CanvasView::paintEvent(QPaintEvent* e)
{
    QPainter p(viewport());
    const QRect clip = e->rect();
    p.fillRect(clip, Theme::kPasteboard);

    const QRectF canvasView = canvasToView(QRectF(m_doc->bounds()));
    const QRectF visible = canvasView & QRectF(clip);
    if (!visible.isEmpty()) {
        p.setBrushOrigin(canvasView.topLeft());
        p.fillRect(visible, checkerBrush());

        // Source rectangle in canvas pixels covering the exposed area.
        QRectF srcF(viewToCanvas(visible.topLeft()), viewToCanvas(visible.bottomRight()));
        QRect src = srcF.toAlignedRect() & m_doc->bounds();
        if (!src.isEmpty()) {
            int level = 0;
            if (m_zoom < 1.0) level = int(std::floor(std::log2(1.0 / m_zoom) + 1e-6));
            level = std::clamp(level, 0, m_doc->pyramidLevelCount() - 1);
            const QImage& img = m_doc->pyramidLevel(level);
            const double f = std::pow(2.0, level);
            const QRectF levelSrc(src.x() / f, src.y() / f, src.width() / f, src.height() / f);
            p.setRenderHint(QPainter::SmoothPixmapTransform, m_zoom < 1.0 && std::fabs(m_zoom * f - 1.0) > 1e-6);
            p.drawImage(canvasToView(QRectF(src)), img, levelSrc);
        }

        // Pixel grid at high zoom (Photoshop shows it above 500%).
        if (m_pixelGrid && m_zoom >= 6.0) {
            QRect vis = (QRectF(viewToCanvas(visible.topLeft()), viewToCanvas(visible.bottomRight()))
                             .toAlignedRect() & m_doc->bounds());
            p.setPen(QPen(QColor(128, 128, 128, 90), 0));
            QVector<QLineF> lines;
            for (int x = vis.left(); x <= vis.right() + 1; ++x) {
                double vx = canvasToView(QPointF(x, 0)).x();
                lines.append(QLineF(vx, visible.top(), vx, visible.bottom()));
            }
            for (int y = vis.top(); y <= vis.bottom() + 1; ++y) {
                double vy = canvasToView(QPointF(0, y)).y();
                lines.append(QLineF(visible.left(), vy, visible.right(), vy));
            }
            p.drawLines(lines);
        }
    }

    // Marching ants.
    if (m_showEdges && m_doc->hasSelection()) {
        const auto& edges = m_doc->selectionEdges();
        const double s = scale();
        const QPointF o = origin();
        QVector<QLineF> lines;
        lines.reserve(edges.size());
        const QRectF bounds = QRectF(clip).adjusted(-1, -1, 1, 1);
        for (const QLine& l : edges) {
            QLineF v(l.x1() * s + o.x(), l.y1() * s + o.y(), l.x2() * s + o.x(), l.y2() * s + o.y());
            if (std::max(v.x1(), v.x2()) < bounds.left() || std::min(v.x1(), v.x2()) > bounds.right()
                || std::max(v.y1(), v.y2()) < bounds.top() || std::min(v.y1(), v.y2()) > bounds.bottom())
                continue;
            lines.append(v);
        }
        p.setRenderHint(QPainter::Antialiasing, false);
        QPen pen(antsBrush(m_antsPhase), 1.0);
        pen.setCosmetic(true);
        p.setPen(pen);
        p.drawLines(lines);
    }

    // Tool overlay (marquee preview, crop box, gradient line...).
    if (Tool* t = m_tools->current(); t && m_tools->activeView() == this) {
        p.save();
        t->paintOverlay(p, this);
        p.restore();

        const double outline = t->brushOutlineSize() * scale();
        if (m_cursorInside && outline >= 6.0) {
            p.setRenderHint(QPainter::Antialiasing, true);
            p.setCompositionMode(QPainter::CompositionMode_Difference);
            p.setPen(QPen(Qt::white, 1.0));
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(m_lastViewPos, outline / 2.0, outline / 2.0);
        }
    }
}

// ---------------- Input ----------------

ToolEvent CanvasView::makeEvent(const QPointF& viewPos, Qt::KeyboardModifiers mods,
                                Qt::MouseButton button, Qt::MouseButtons buttons, qreal pressure,
                                bool tablet) const
{
    ToolEvent ev;
    ev.viewPos = viewPos;
    ev.pos = viewToCanvas(viewPos);
    ev.mods = mods;
    ev.button = button;
    ev.buttons = buttons;
    ev.pressure = pressure;
    ev.tablet = tablet;
    return ev;
}

void CanvasView::updateCursorArea(const QPointF& oldPos, const QPointF& newPos)
{
    Tool* t = m_tools->current();
    double r = t ? t->brushOutlineSize() * scale() / 2.0 + 3.0 : 0.0;
    if (r <= 3.0) return;
    viewport()->update(QRectF(oldPos.x() - r, oldPos.y() - r, 2 * r, 2 * r).toAlignedRect());
    viewport()->update(QRectF(newPos.x() - r, newPos.y() - r, 2 * r, 2 * r).toAlignedRect());
}

void CanvasView::handlePress(const ToolEvent& ev)
{
    m_tools->setActiveView(this);
    m_pressTool = m_tools->current();
    m_tools->setBusy(true);
    if (m_pressTool) m_pressTool->mousePress(this, ev);
}

void CanvasView::handleMove(const ToolEvent& ev)
{
    QPointF old = m_lastViewPos;
    m_lastViewPos = ev.viewPos;
    m_lastCanvasPos = ev.pos;
    m_cursorInside = true;
    updateCursorArea(old, ev.viewPos);
    emit cursorMoved(ev.pos, m_doc->bounds().contains(ev.pixel()));
    Tool* t = m_pressTool ? m_pressTool.data() : m_tools->current();
    if (t && m_tools->activeView() == this) t->mouseMove(this, ev);
}

void CanvasView::handleRelease(const ToolEvent& ev)
{
    m_tools->setBusy(false);
    if (m_pressTool) m_pressTool->mouseRelease(this, ev);
    m_pressTool = nullptr;
    updateCursor();
}

void CanvasView::mousePressEvent(QMouseEvent* e)
{
    setFocus(Qt::MouseFocusReason);
    if (e->button() == Qt::MiddleButton) {
        m_panning = true;
        m_panLast = e->position();
        viewport()->setCursor(Qt::ClosedHandCursor);
        return;
    }
    if (e->button() != Qt::LeftButton || m_pressTool) return;
    handlePress(makeEvent(e->position(), e->modifiers(), e->button(), e->buttons(), 1.0, false));
}

void CanvasView::mouseMoveEvent(QMouseEvent* e)
{
    if (m_panning) {
        panBy(e->position() - m_panLast);
        m_panLast = e->position();
        return;
    }
    handleMove(makeEvent(e->position(), e->modifiers(), Qt::NoButton, e->buttons(), 1.0, false));
}

void CanvasView::mouseReleaseEvent(QMouseEvent* e)
{
    if (e->button() == Qt::MiddleButton && m_panning) {
        m_panning = false;
        updateCursor();
        return;
    }
    if (e->button() != Qt::LeftButton || !m_pressTool) return;
    handleRelease(makeEvent(e->position(), e->modifiers(), e->button(), e->buttons(), 1.0, false));
}

void CanvasView::mouseDoubleClickEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton) return;
    if (Tool* t = m_tools->current())
        t->mouseDoubleClick(this, makeEvent(e->position(), e->modifiers(), e->button(), e->buttons(), 1.0, false));
}

void CanvasView::wheelEvent(QWheelEvent* e)
{
    if (e->modifiers() & Qt::AltModifier) {
        // Alt/Option + wheel zooms around the cursor.
        const int d = e->angleDelta().y() ? e->angleDelta().y() : e->angleDelta().x();
        if (d > 0) zoomIn(e->position());
        else if (d < 0) zoomOut(e->position());
        return;
    }
    if (e->modifiers() & Qt::ControlModifier) {
        // Ctrl/Cmd + wheel scrolls horizontally.
        QPoint px = e->pixelDelta().isNull() ? e->angleDelta() / 2 : e->pixelDelta();
        panBy(QPointF(px.y(), 0));
        return;
    }
    if (!e->pixelDelta().isNull()) {
        panBy(QPointF(e->pixelDelta()));
        return;
    }
    QAbstractScrollArea::wheelEvent(e);
}

void CanvasView::keyPressEvent(QKeyEvent* e)
{
    Tool* t = m_tools->current();
    if (t) {
        if (e->key() == Qt::Key_Escape && t->cancel(this)) return;
        if ((e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) && t->commit(this)) return;
        if (t->keyPress(this, e)) return;
    }
    if (e->key() == Qt::Key_Shift || e->key() == Qt::Key_Alt || e->key() == Qt::Key_Control)
        updateCursor();
    QAbstractScrollArea::keyPressEvent(e);
}

void CanvasView::keyReleaseEvent(QKeyEvent* e)
{
    if (e->key() == Qt::Key_Shift || e->key() == Qt::Key_Alt || e->key() == Qt::Key_Control)
        updateCursor();
    QAbstractScrollArea::keyReleaseEvent(e);
}

bool CanvasView::eventFilter(QObject* obj, QEvent* e) { return QAbstractScrollArea::eventFilter(obj, e); }

bool CanvasView::viewportEvent(QEvent* e)
{
    switch (e->type()) {
    case QEvent::TabletPress:
    case QEvent::TabletMove:
    case QEvent::TabletRelease: {
        auto* te = static_cast<QTabletEvent*>(e);
        const qreal pressure = te->pointerType() == QPointingDevice::PointerType::Eraser ? 1.0 : te->pressure();
        ToolEvent ev = makeEvent(te->position(), te->modifiers(),
                                 e->type() == QEvent::TabletMove ? Qt::NoButton : Qt::LeftButton,
                                 te->buttons(), pressure, true);
        if (e->type() == QEvent::TabletPress) {
            setFocus(Qt::MouseFocusReason);
            if (te->button() != Qt::LeftButton || m_pressTool) break;
            handlePress(ev);
        } else if (e->type() == QEvent::TabletMove) {
            handleMove(ev);
        } else if (m_pressTool) {
            handleRelease(ev);
        }
        e->accept();
        return true;
    }
    case QEvent::NativeGesture: {
        auto* ge = static_cast<QNativeGestureEvent*>(e);
        if (ge->gestureType() == Qt::ZoomNativeGesture) {
            setZoom(m_zoom * (1.0 + ge->value()), ge->position());
            return true;
        }
        if (ge->gestureType() == Qt::SmartZoomNativeGesture) {
            if (m_zoom < 1.0) actualPixels();
            else fitOnScreen();
            return true;
        }
        break;
    }
    case QEvent::Enter:
        m_cursorInside = true;
        updateCursor();
        break;
    case QEvent::Leave:
        m_cursorInside = false;
        updateCursorArea(m_lastViewPos, m_lastViewPos);
        emit cursorMoved(m_lastCanvasPos, false);
        break;
    default:
        break;
    }
    return QAbstractScrollArea::viewportEvent(e);
}

void CanvasView::updateCursor()
{
    Tool* t = m_tools->current();
    if (!t) {
        viewport()->setCursor(Qt::ArrowCursor);
        return;
    }
    const double outline = t->brushOutlineSize() * scale();
    if (outline >= 6.0) viewport()->setCursor(Qt::BlankCursor);
    else if (outline > 0.0) viewport()->setCursor(Qt::CrossCursor);
    else viewport()->setCursor(t->cursor(this, QGuiApplication::queryKeyboardModifiers()));
    viewport()->update();
}
