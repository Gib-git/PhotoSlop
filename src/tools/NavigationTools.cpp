#include "tools/NavigationTools.h"

#include "tools/ToolManager.h"
#include "ui/CanvasView.h"
#include "ui/Widgets.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QHBoxLayout>
#include <QPainter>
#include <QPushButton>
#include <cmath>
#include <algorithm>
#include <iterator>

void addViewButtons(ToolManager* manager, QWidget* container)
{
    auto* lay = qobject_cast<QHBoxLayout*>(container->layout());
    auto add = [&](const QString& text, auto fn) {
        auto* b = new QPushButton(text, container);
        b->setStyleSheet(QStringLiteral("QPushButton { min-width: 0; padding: 2px 10px; }"));
        QObject::connect(b, &QPushButton::clicked, manager, [manager, fn] {
            if (CanvasView* v = manager->activeView()) fn(v);
        });
        lay->addWidget(b);
    };
    add(QStringLiteral("100%"), [](CanvasView* v) { v->actualPixels(); });
    add(QStringLiteral("Fit Screen"), [](CanvasView* v) { v->fitOnScreen(); });
    add(QStringLiteral("Fill Screen"), [](CanvasView* v) { v->fillScreen(); });
}

// ---------------- Hand ----------------

QWidget* HandTool::createOptions(QWidget* parent)
{
    auto* w = new QWidget(parent);
    auto* lay = new QHBoxLayout(w);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(6);
    auto* all = new QCheckBox(QStringLiteral("Scroll All Windows"), w);
    all->setEnabled(false);
    lay->addWidget(all);
    addViewButtons(m_manager, w);
    lay->addStretch();
    return w;
}

void HandTool::mousePress(CanvasView* v, const ToolEvent& e)
{
    m_dragging = true;
    m_last = e.viewPos;
    v->viewport()->setCursor(Qt::ClosedHandCursor);
}

void HandTool::mouseMove(CanvasView* v, const ToolEvent& e)
{
    if (!m_dragging) return;
    v->panBy(e.viewPos - m_last);
    m_last = e.viewPos;
}

void HandTool::mouseRelease(CanvasView* v, const ToolEvent&)
{
    m_dragging = false;
    v->viewport()->setCursor(Qt::OpenHandCursor);
}

QCursor HandTool::cursor(CanvasView*, Qt::KeyboardModifiers) const
{
    return m_dragging ? Qt::ClosedHandCursor : Qt::OpenHandCursor;
}

void HandTool::toolButtonDoubleClicked()
{
    if (CanvasView* v = m_manager->activeView()) v->fitOnScreen();
}

// ---------------- Zoom ----------------

QWidget* ZoomTool::createOptions(QWidget* parent)
{
    auto* w = new QWidget(parent);
    auto* lay = new QHBoxLayout(w);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(6);
    auto* in = makeIconButton(QStringLiteral("zoom-in"), QStringLiteral("Zoom In"), w, true);
    auto* out = makeIconButton(QStringLiteral("zoom-out"), QStringLiteral("Zoom Out"), w, true);
    auto* grp = new QButtonGroup(w);
    grp->addButton(in);
    grp->addButton(out);
    in->setChecked(true);
    connect(out, &QToolButton::toggled, this, [this](bool on) { m_zoomOutMode = on; });
    lay->addWidget(in);
    lay->addWidget(out);
    lay->addWidget(makeVSeparator(w));
    for (const char* label : {"Resize Windows to Fit", "Zoom All Windows"}) {
        auto* cb = new QCheckBox(QString::fromLatin1(label), w);
        cb->setEnabled(false);
        lay->addWidget(cb);
    }
    auto* scrubby = new QCheckBox(QStringLiteral("Scrubby Zoom"), w);
    scrubby->setChecked(m_scrubby);
    connect(scrubby, &QCheckBox::toggled, this, [this](bool on) { m_scrubby = on; });
    lay->addWidget(scrubby);
    lay->addWidget(makeVSeparator(w));
    addViewButtons(m_manager, w);
    lay->addStretch();
    return w;
}

bool ZoomTool::zoomingOut(Qt::KeyboardModifiers mods) const
{
    return m_zoomOutMode != bool(mods & Qt::AltModifier);
}

void ZoomTool::mousePress(CanvasView* v, const ToolEvent& e)
{
    m_pressed = true;
    m_moved = false;
    m_startView = m_curView = e.viewPos;
    m_startZoom = v->zoom();
}

void ZoomTool::mouseMove(CanvasView* v, const ToolEvent& e)
{
    if (!m_pressed) return;
    m_curView = e.viewPos;
    if ((m_curView - m_startView).manhattanLength() > 3) m_moved = true;
    if (!m_moved) return;
    if (m_scrubby) {
        const double dx = m_curView.x() - m_startView.x();
        v->setZoom(m_startZoom * std::pow(2.0, dx / 120.0), m_startView);
    } else {
        emit overlayChanged();
    }
}

void ZoomTool::mouseRelease(CanvasView* v, const ToolEvent& e)
{
    if (!m_pressed) return;
    m_pressed = false;
    if (!m_moved) {
        if (zoomingOut(e.mods)) v->zoomOut(e.viewPos);
        else v->zoomIn(e.viewPos);
        return;
    }
    if (!m_scrubby) {
        QRectF r = QRectF(m_startView, m_curView).normalized();
        if (r.width() > 4 && r.height() > 4) {
            QPointF c0 = v->viewToCanvas(r.topLeft()), c1 = v->viewToCanvas(r.bottomRight());
            const double dpr = v->viewport()->devicePixelRatioF();
            double z = std::min(v->viewport()->width() / (c1.x() - c0.x()),
                                v->viewport()->height() / (c1.y() - c0.y())) * dpr;
            v->setZoom(z);
            v->centerOn((c0 + c1) / 2.0);
        }
        emit overlayChanged();
    }
}

void ZoomTool::paintOverlay(QPainter& p, CanvasView*)
{
    if (!m_pressed || m_scrubby || !m_moved) return;
    p.setPen(QPen(Qt::white, 1, Qt::DashLine));
    p.setBrush(Qt::NoBrush);
    p.drawRect(QRectF(m_startView, m_curView).normalized());
}

QCursor ZoomTool::cursor(CanvasView*, Qt::KeyboardModifiers mods) const
{
    return makeIconCursor(zoomingOut(mods) ? QStringLiteral("zoom-out") : QStringLiteral("zoom-in"),
                          QPoint(10, 10));
}

void ZoomTool::toolButtonDoubleClicked()
{
    if (CanvasView* v = m_manager->activeView()) v->actualPixels();
}
