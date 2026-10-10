#include "ui/CanvasView.h"

#include "app/Preferences.h"
#include "app/Theme.h"
#include "core/Document.h"
#include "tools/Tool.h"
#include "tools/ToolManager.h"
#include "ui/GpuViewport.h"
#include "ui/ViewOptions.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QNativeGestureEvent>
#include <QPainter>
#include <QScrollBar>
#include <QTabletEvent>
#include <QWheelEvent>
#include <cmath>
#include <limits>
#include <algorithm>
#include <iterator>

namespace {

const double kZoomSteps[] = {0.01,   0.02, 0.03, 0.04, 0.05, 0.0625, 0.0833, 0.125, 0.1667,
                             0.25,   0.3333, 0.5, 0.6667, 1.0, 2.0,  3.0,   4.0,   5.0,
                             6.0,    7.0,  8.0,  12.0, 16.0, 24.0, 32.0,  64.0,  128.0};
constexpr double kMinZoom = 0.01;
constexpr double kMaxZoom = 128.0;

// The transparency checkerboard, as set in Preferences > Transparency.
QBrush checkerBrush()
{
    const Preferences& prefs = Preferences::instance();
    const int n = prefs.checkerPixels();
    if (n == 0) return QBrush(Qt::white);
    QPixmap pm(2 * n, 2 * n);
    pm.fill(prefs.checkerLight());
    QPainter p(&pm);
    p.fillRect(0, 0, n, n, prefs.checkerDark());
    p.fillRect(n, n, n, n, prefs.checkerDark());
    return QBrush(pm);
}

// A one-pixel line at any zoom. Zero-width pens draw nothing on the OpenGL paint engine.
QPen hairline(const QColor& color, Qt::PenStyle style = Qt::SolidLine)
{
    QPen pen(color, 1.0, style);
    pen.setCosmetic(true);
    return pen;
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

CanvasView::CanvasView(Document* doc, ToolManager* tools, ViewOptions* options, QWidget* parent)
    : QAbstractScrollArea(parent)
    , m_doc(doc)
    , m_tools(tools)
    , m_opts(options)
{
    setFrameShape(QFrame::NoFrame);
    setFocusPolicy(Qt::StrongFocus);
    prepareViewport();
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

    // Connected through lambdas: the viewport widget changes when the GPU setting does.
    auto repaint = [this] { viewport()->update(); };
    connect(doc, &Document::guidesChanged, this, repaint);
    connect(doc, &Document::quickMaskChanged, this, repaint);
    connect(options, &ViewOptions::changed, this, repaint);
    connect(&Preferences::instance(), &Preferences::changed, this, [this] {
        m_checker = checkerBrush();
        applyGpuPreference();
        updateCursor();
    });
    m_checker = checkerBrush();
    applyGpuPreference();

    m_antsTimer.setInterval(120);
    connect(&m_antsTimer, &QTimer::timeout, this, [this] {
        m_antsPhase = (m_antsPhase + 1) % 8;
        QRectF r = selectionViewRect();
        if (!r.isEmpty()) viewport()->update(r.toAlignedRect().adjusted(-2, -2, 2, 2));
    });
}

void CanvasView::prepareViewport()
{
    viewport()->setMouseTracking(true);
    viewport()->setAttribute(Qt::WA_OpaquePaintEvent);
    viewport()->setAttribute(Qt::WA_TabletTracking);
    viewport()->grabGesture(Qt::PinchGesture);
}

void CanvasView::applyGpuPreference()
{
    const bool want = Preferences::instance().useGraphicsProcessor && Gpu::available();
    if (want == bool(m_gpu)) return;
    // QAbstractScrollArea deletes the old viewport.
    QWidget* vp = nullptr;
    if (want) {
        m_gpu = new GpuViewport(m_doc);
        m_gpu->setPainter([this](QPainter& p) { paintCanvas(p, viewport()->rect()); });
        vp = m_gpu;
    } else {
        m_gpu = nullptr;
        vp = new QWidget;
    }
    setViewport(vp);
    prepareViewport();
    updateCursor();
    viewport()->update();
}

void CanvasView::setContrastPen(QPainter& p, qreal width) const
{
    if (!m_gpu) {
        // Inverts whatever is underneath, so the line shows on any colour.
        p.setCompositionMode(QPainter::CompositionMode_Difference);
        p.setPen(QPen(Qt::white, width));
        return;
    }
    // OpenGL has no Difference blending without an extension: alternate black and white instead.
    static const QBrush pattern = [] {
        QImage img(4, 4, QImage::Format_ARGB32);
        for (int y = 0; y < 4; ++y)
            for (int x = 0; x < 4; ++x) img.setPixel(x, y, ((x + y) & 2) ? 0xff000000 : 0xffffffff);
        return QBrush(img);
    }();
    p.setPen(QPen(pattern, std::max<qreal>(width, 1.0)));
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

void CanvasView::setSelectionEdgesSuppressed(bool on)
{
    if (on == m_suppressEdges) return;
    m_suppressEdges = on;
    viewport()->update();
}

// ---------------- Snapping ----------------

double CanvasView::gridStep() const
{
    const double major = std::max(2.0, m_opts->gridSpacing(m_doc->dpi(), m_doc->width()));
    return major / std::max(1, m_opts->gridSubdivisions);
}

double CanvasView::snapValue(double v, bool xAxis, bool includeGuides, double* distance) const
{
    // Photoshop snaps within a few screen pixels.
    double best = 6.0 / scale();
    double result = v;
    auto consider = [&](double target) {
        const double d = std::fabs(target - v);
        if (d < best) {
            best = d;
            result = target;
        }
    };
    if (m_opts->snap) {
        if (includeGuides && m_opts->snapGuides && m_opts->showsGuides())
            for (const Guide& g : m_doc->guides())
                if ((g.orientation == Qt::Vertical) == xAxis) consider(g.position);
        if (m_opts->snapGrid && m_opts->showsGrid()) {
            const double step = gridStep();
            consider(std::round(v / step) * step);
        }
        if (m_opts->snapBounds) {
            consider(0.0);
            consider(xAxis ? m_doc->width() : m_doc->height());
        }
    }
    if (distance) *distance = result == v ? std::numeric_limits<double>::max() : best;
    return result;
}

QPointF CanvasView::snapPoint(const QPointF& p) const
{
    return QPointF(snapValue(p.x(), true, true, nullptr), snapValue(p.y(), false, true, nullptr));
}

QPointF CanvasView::snapRectOffset(const QRectF& r) const
{
    QPointF offset;
    for (bool xAxis : {true, false}) {
        const double edges[3] = {xAxis ? r.left() : r.top(), xAxis ? r.right() : r.bottom(),
                                 xAxis ? r.center().x() : r.center().y()};
        double bestDist = std::numeric_limits<double>::max(), bestDelta = 0.0;
        for (double e : edges) {
            double d = 0.0;
            const double snapped = snapValue(e, xAxis, true, &d);
            if (d < bestDist) {
                bestDist = d;
                bestDelta = snapped - e;
            }
        }
        if (xAxis) offset.setX(bestDelta);
        else offset.setY(bestDelta);
    }
    return offset;
}

// ---------------- Guides ----------------

int CanvasView::guideAt(const QPointF& viewPos) const
{
    if (!m_opts->showsGuides()) return -1;
    const auto& guides = m_doc->guides();
    for (int i = int(guides.size()) - 1; i >= 0; --i) {
        const Guide& g = guides[i];
        const double v = g.orientation == Qt::Vertical ? canvasToView(QPointF(g.position, 0)).x()
                                                       : canvasToView(QPointF(0, g.position)).y();
        const double cur = g.orientation == Qt::Vertical ? viewPos.x() : viewPos.y();
        if (std::fabs(v - cur) <= 3.0) return i;
    }
    return -1;
}

bool CanvasView::canDragGuides() const
{
    const Tool* t = m_tools->current();
    return t && t->id() == QLatin1String("move") && m_opts->showsGuides() && !m_opts->lockGuides;
}

void CanvasView::beginGuideDrag(Qt::Orientation orientation, int index, bool fromRuler)
{
    m_guideDrag = GuideDrag();
    m_guideDrag.active = true;
    m_guideDrag.fromRuler = fromRuler;
    m_guideDrag.index = index;
    m_guideDrag.orientation = m_guideDrag.startOrientation = orientation;
    if (index >= 0) {
        m_guideDrag.position = m_doc->guides()[index].position;
        m_guideDrag.visible = true;
    }
    if (!m_opts->showsGuides()) {
        // Dragging from a ruler shows guides again, as in Photoshop.
        m_opts->extras = m_opts->guides = true;
        m_opts->notify();
    }
    viewport()->update();
}

void CanvasView::updateGuideDrag(const QPointF& viewPos, Qt::KeyboardModifiers mods)
{
    if (!m_guideDrag.active) return;
    // Alt/Option flips a guide pulled from a ruler.
    m_guideDrag.orientation = m_guideDrag.startOrientation;
    if (m_guideDrag.fromRuler && (mods & Qt::AltModifier))
        m_guideDrag.orientation = m_guideDrag.orientation == Qt::Horizontal ? Qt::Vertical : Qt::Horizontal;
    const QPointF c = viewToCanvas(viewPos);
    const bool xAxis = m_guideDrag.orientation == Qt::Vertical;
    const double raw = std::round(xAxis ? c.x() : c.y());
    m_guideDrag.position = snapValue(raw, xAxis, false, nullptr);
    m_guideDrag.visible = viewport()->rect().contains(viewPos.toPoint());
    viewport()->update();
}

void CanvasView::endGuideDrag(const QPointF& viewPos)
{
    if (!m_guideDrag.active) return;
    const GuideDrag d = m_guideDrag;
    m_guideDrag = GuideDrag();
    viewport()->update();
    QList<Guide> guides = m_doc->guides();
    const bool inside = viewport()->rect().contains(viewPos.toPoint());
    if (d.index >= 0 && d.index < guides.size()) {
        if (!inside) {
            guides.removeAt(d.index);
            m_doc->changeGuides(guides, QStringLiteral("Delete Guide"));
            return;
        }
        guides[d.index] = Guide{d.orientation, d.position};
        m_doc->changeGuides(guides, QStringLiteral("Move Guide"));
        return;
    }
    if (!inside) return;
    guides.append(Guide{d.orientation, d.position});
    m_doc->changeGuides(guides, QStringLiteral("New Guide"));
}

void CanvasView::paintGridAndGuides(QPainter& p, const QRectF& canvasView)
{
    p.save();
    p.setRenderHint(QPainter::Antialiasing, false);
    const QRectF vis = canvasView & QRectF(viewport()->rect());
    if (m_opts->showsGrid() && !vis.isEmpty()) {
        const double step = gridStep();
        const int sub = std::max(1, m_opts->gridSubdivisions);
        const double viewStep = step * scale();
        if (viewStep * sub >= 4.0) {
            const QRectF c(viewToCanvas(vis.topLeft()), viewToCanvas(vis.bottomRight()));
            QVector<QLineF> major, minor;
            const bool showMinor = viewStep >= 5.0;
            for (long i = long(std::floor(c.left() / step)); i * step <= c.right(); ++i) {
                if (i * step < c.left()) continue;
                const double x = canvasToView(QPointF(i * step, 0)).x();
                if (i % sub == 0) major.append(QLineF(x, vis.top(), x, vis.bottom()));
                else if (showMinor) minor.append(QLineF(x, vis.top(), x, vis.bottom()));
            }
            for (long i = long(std::floor(c.top() / step)); i * step <= c.bottom(); ++i) {
                if (i * step < c.top()) continue;
                const double y = canvasToView(QPointF(0, i * step)).y();
                if (i % sub == 0) major.append(QLineF(vis.left(), y, vis.right(), y));
                else if (showMinor) minor.append(QLineF(vis.left(), y, vis.right(), y));
            }
            const Preferences& prefs = Preferences::instance();
            QColor minorColor = prefs.gridColor, majorColor = prefs.gridColor;
            minorColor.setAlpha(110);
            majorColor.setAlpha(190);
            const Qt::PenStyle style = prefs.gridStyle == Preferences::GridStyle::Dots         ? Qt::DotLine
                                       : prefs.gridStyle == Preferences::GridStyle::DashedLines ? Qt::DashLine
                                                                                                : Qt::SolidLine;
            p.setPen(hairline(minorColor, Qt::DotLine));
            p.drawLines(minor);
            p.setPen(hairline(majorColor, style));
            p.drawLines(major);
        }
    }
    // Guides span the whole window, like Photoshop's.
    const QColor guideColor = Preferences::instance().guideColor;
    auto drawGuide = [&](Qt::Orientation o, double pos) {
        if (o == Qt::Vertical) {
            const double x = std::round(canvasToView(QPointF(pos, 0)).x()) + 0.5;
            p.drawLine(QPointF(x, 0), QPointF(x, viewport()->height()));
        } else {
            const double y = std::round(canvasToView(QPointF(0, pos)).y()) + 0.5;
            p.drawLine(QPointF(0, y), QPointF(viewport()->width(), y));
        }
    };
    if (m_opts->showsGuides()) {
        p.setPen(hairline(guideColor));
        const auto& guides = m_doc->guides();
        for (int i = 0; i < guides.size(); ++i)
            if (!(m_guideDrag.active && i == m_guideDrag.index)) drawGuide(guides[i].orientation, guides[i].position);
    }
    if (m_guideDrag.active && m_guideDrag.visible) {
        p.setPen(hairline(guideColor));
        drawGuide(m_guideDrag.orientation, m_guideDrag.position);
    }
    p.restore();
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
    if (m_gpu) m_gpu->invalidate(r);
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
    paintCanvas(p, e->rect());
}

void CanvasView::paintCanvas(QPainter& p, const QRect& clip)
{
    p.fillRect(clip, Theme::kPasteboard);

    const QRectF canvasView = canvasToView(QRectF(m_doc->bounds()));
    const QRectF visible = canvasView & QRectF(clip);
    if (!visible.isEmpty()) {
        p.setBrushOrigin(canvasView.topLeft());
        p.fillRect(visible, m_checker);

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
            const bool smooth = m_zoom < 1.0 && std::fabs(m_zoom * f - 1.0) > 1e-6;
            bool drawn = false;
            if (m_gpu) {
                p.beginNativePainting();
                drawn = m_gpu->drawImage(canvasToView(QRectF(src)), src, level, smooth);
                p.endNativePainting();
            }
            if (!drawn) {
                p.setRenderHint(QPainter::SmoothPixmapTransform, smooth);
                p.drawImage(canvasToView(QRectF(src)), img, levelSrc);
            }
            if (m_doc->inQuickMask()) {
                p.setRenderHint(QPainter::SmoothPixmapTransform, false);
                p.drawImage(canvasToView(QRectF(src)), m_doc->quickMaskOverlay(), QRectF(src));
            }
        }

        // Pixel grid at high zoom (Photoshop shows it above 500%).
        if (m_opts->showsPixelGrid() && m_zoom >= 6.0) {
            QRect vis = (QRectF(viewToCanvas(visible.topLeft()), viewToCanvas(visible.bottomRight()))
                             .toAlignedRect() & m_doc->bounds());
            p.setPen(hairline(QColor(128, 128, 128, 90)));
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

    paintGridAndGuides(p, canvasView);

    // Marching ants.
    if (m_opts->showsSelectionEdges() && !m_suppressEdges && m_doc->hasSelection()) {
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
        const Preferences& prefs = Preferences::instance();
        if (m_cursorInside && outline >= 6.0 && prefs.paintingCursor == Preferences::PaintingCursor::BrushTip) {
            p.setRenderHint(QPainter::Antialiasing, true);
            setContrastPen(p, 1.0);
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(m_lastViewPos, outline / 2.0, outline / 2.0);
            if (prefs.brushCrosshair) {
                p.drawLine(m_lastViewPos - QPointF(4, 0), m_lastViewPos + QPointF(4, 0));
                p.drawLine(m_lastViewPos - QPointF(0, 4), m_lastViewPos + QPointF(0, 4));
            }
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
    if (canDragGuides()) {
        const int g = guideAt(e->position());
        if (g >= 0) {
            m_guideMouse = true;
            beginGuideDrag(m_doc->guides()[g].orientation, g, false);
            return;
        }
    }
    handlePress(makeEvent(e->position(), e->modifiers(), e->button(), e->buttons(), 1.0, false));
}

void CanvasView::mouseMoveEvent(QMouseEvent* e)
{
    if (m_panning) {
        panBy(e->position() - m_panLast);
        m_panLast = e->position();
        return;
    }
    if (m_guideMouse) {
        updateGuideDrag(e->position(), e->modifiers());
        return;
    }
    if (!m_pressTool) {
        // Hovering a guide with the Move tool offers to drag it.
        const int g = canDragGuides() ? guideAt(e->position()) : -1;
        if (g >= 0) {
            viewport()->setCursor(m_doc->guides()[g].orientation == Qt::Vertical ? Qt::SplitHCursor : Qt::SplitVCursor);
            m_hoverGuide = true;
        } else if (m_hoverGuide) {
            m_hoverGuide = false;
            updateCursor();
        }
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
    if (e->button() == Qt::LeftButton && m_guideMouse) {
        m_guideMouse = false;
        endGuideDrag(e->position());
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
    // Preferences > General > Zoom with Scroll Wheel swaps plain and Alt scrolling.
    const bool zoomKey = bool(e->modifiers() & Qt::AltModifier) != Preferences::instance().zoomWithScrollWheel;
    if (zoomKey && !(e->modifiers() & Qt::ControlModifier)) {
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
    case QEvent::Paint:
        // The OpenGL viewport paints itself through paintGL(), which calls paintCanvas().
        if (m_gpu) return false;
        break;
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
    // Preferences > Cursors: painting tools show their tip, a crosshair or the tool icon.
    const Preferences& prefs = Preferences::instance();
    const double outline = t->brushOutlineSize() * scale();
    if (t->brushOutlineSize() > 0.0) {
        if (prefs.paintingCursor == Preferences::PaintingCursor::Standard)
            viewport()->setCursor(t->cursor(this, QGuiApplication::queryKeyboardModifiers()));
        else if (prefs.paintingCursor == Preferences::PaintingCursor::BrushTip && outline >= 6.0)
            viewport()->setCursor(Qt::BlankCursor);
        else
            viewport()->setCursor(Qt::CrossCursor);
    } else if (prefs.otherCursors == Preferences::OtherCursor::Precise && t->id() != QLatin1String("hand")
               && t->id() != QLatin1String("zoom") && t->id() != QLatin1String("move")) {
        viewport()->setCursor(Qt::CrossCursor);
    } else {
        viewport()->setCursor(t->cursor(this, QGuiApplication::queryKeyboardModifiers()));
    }
    viewport()->update();
}
