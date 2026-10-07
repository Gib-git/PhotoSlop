#include "ui/Ruler.h"

#include "app/Theme.h"
#include "core/Document.h"
#include "ui/CanvasView.h"
#include "ui/ViewOptions.h"

#include <QActionGroup>
#include <QContextMenuEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <cmath>
#include <algorithm>
#include <iterator>

Ruler::Ruler(Qt::Orientation orientation, CanvasView* view, QWidget* parent)
    : QWidget(parent)
    , m_orientation(orientation)
    , m_view(view)
{
    if (orientation == Qt::Horizontal) setFixedHeight(kThickness);
    else setFixedWidth(kThickness);
    setCursor(Qt::ArrowCursor);
    connect(view, &CanvasView::viewChanged, this, qOverload<>(&QWidget::update));
    connect(view->document(), &Document::sizeChanged, this, qOverload<>(&QWidget::update));
    connect(view->options(), &ViewOptions::changed, this, qOverload<>(&QWidget::update));
    connect(view, &CanvasView::cursorMoved, this, [this](const QPointF& pos, bool) {
        m_cursor = pos;
        m_cursorInside = true;
        update();
    });
}

QSize Ruler::sizeHint() const { return QSize(kThickness, kThickness); }

double Ruler::pixelsPerUnit() const
{
    const Document* doc = m_view->document();
    switch (m_view->options()->units) {
    case ViewOptions::Units::Pixels: return 1.0;
    case ViewOptions::Units::Inches: return doc->dpi();
    case ViewOptions::Units::Centimeters: return doc->dpi() / 2.54;
    case ViewOptions::Units::Millimeters: return doc->dpi() / 25.4;
    case ViewOptions::Units::Percent:
        return (m_orientation == Qt::Horizontal ? doc->width() : doc->height()) / 100.0;
    }
    return 1.0;
}

QPointF Ruler::toViewport(const QPointF& localPos) const
{
    // The ruler is not an ancestor of the viewport, so map through global coordinates.
    return m_view->viewport()->mapFromGlobal(mapToGlobal(localPos));
}

QPointF Ruler::fromViewport(const QPointF& viewportPos) const
{
    return mapFromGlobal(m_view->viewport()->mapToGlobal(viewportPos));
}

void Ruler::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.fillRect(rect(), Theme::kPanel);
    p.setPen(Theme::kBorder);
    const bool horiz = m_orientation == Qt::Horizontal;
    if (horiz) p.drawLine(0, height() - 1, width(), height() - 1);
    else p.drawLine(width() - 1, 0, width() - 1, height());

    // Where canvas coordinate 0 sits along this ruler, and how far one unit spans.
    const QPointF origin = fromViewport(m_view->canvasToView(QPointF(0, 0)));
    const double unitPx = pixelsPerUnit() * m_view->scale();
    if (unitPx <= 0) return;
    // Major ticks at a "nice" interval at least ~50 screen pixels apart.
    static const double steps[] = {1, 2, 5, 10, 20, 25, 50, 100, 200, 250, 500, 1000, 2000, 5000, 10000, 20000, 50000};
    double major = steps[std::size(steps) - 1];
    for (double s : steps)
        if (s * unitPx >= 50) {
            major = s;
            break;
        }
    if (unitPx > 50) {
        // Zoomed far in on large units: fractional steps.
        major = 1.0;
        while (major / 2 * unitPx >= 50) major /= 2;
    }
    const int minorCount = (major * unitPx >= 100) ? 10 : ((major * unitPx >= 50) ? 5 : 2);
    const double length = horiz ? width() : height();
    const double start = horiz ? origin.x() : origin.y();
    const double first = std::floor(-start / (major * unitPx)) * major;
    QFont f = font();
    f.setPixelSize(9);
    p.setFont(f);
    p.setPen(Theme::kTextDim);
    for (double v = first; (start + v * unitPx) <= length; v += major) {
        for (int i = 0; i < minorCount; ++i) {
            const double pos = start + (v + major * i / minorCount) * unitPx;
            if (pos < 0 || pos > length) continue;
            const int tick = i == 0 ? kThickness : (minorCount == 10 && i == 5 ? 6 : 3);
            const double x = std::round(pos) + 0.5;
            if (horiz) p.drawLine(QPointF(x, kThickness - tick), QPointF(x, kThickness));
            else p.drawLine(QPointF(kThickness - tick, x), QPointF(kThickness, x));
        }
        const double pos = start + v * unitPx;
        const QString label = QString::number(v, 'g', 6);
        if (horiz) {
            p.drawText(QPointF(pos + 3, 9), label);
        } else {
            p.save();
            p.translate(9, pos + 3);
            p.rotate(90);
            p.drawText(QPointF(0, 0), label);
            p.restore();
        }
    }
    // Cursor position marker.
    if (m_cursorInside) {
        const QPointF c = fromViewport(m_view->canvasToView(m_cursor));
        p.setPen(QPen(Theme::kText, 1, Qt::DotLine));
        if (horiz) p.drawLine(QPointF(c.x() + 0.5, 0), QPointF(c.x() + 0.5, kThickness));
        else p.drawLine(QPointF(0, c.y() + 0.5), QPointF(kThickness, c.y() + 0.5));
    }
}

void Ruler::mousePressEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton) return;
    // The top ruler makes horizontal guides, the side ruler vertical ones.
    m_dragging = true;
    m_view->beginGuideDrag(m_orientation, -1, true);
    m_view->updateGuideDrag(toViewport(e->position()), e->modifiers());
}

void Ruler::mouseMoveEvent(QMouseEvent* e)
{
    if (m_dragging) m_view->updateGuideDrag(toViewport(e->position()), e->modifiers());
}

void Ruler::mouseReleaseEvent(QMouseEvent* e)
{
    if (!m_dragging || e->button() != Qt::LeftButton) return;
    m_dragging = false;
    m_view->endGuideDrag(toViewport(e->position()));
}

void Ruler::contextMenuEvent(QContextMenuEvent* e)
{
    QMenu menu(this);
    auto* group = new QActionGroup(&menu);
    const struct { const char* name; ViewOptions::Units u; } units[] = {
        {"Pixels", ViewOptions::Units::Pixels},          {"Inches", ViewOptions::Units::Inches},
        {"Centimeters", ViewOptions::Units::Centimeters}, {"Millimeters", ViewOptions::Units::Millimeters},
        {"Percent", ViewOptions::Units::Percent},
    };
    ViewOptions* opts = m_view->options();
    for (const auto& u : units) {
        QAction* a = menu.addAction(QString::fromLatin1(u.name));
        a->setCheckable(true);
        a->setChecked(opts->units == u.u);
        group->addAction(a);
        const auto unit = u.u;
        connect(a, &QAction::triggered, this, [opts, unit] {
            opts->units = unit;
            opts->notify();
        });
    }
    menu.exec(e->globalPos());
}
