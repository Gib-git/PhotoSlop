#include "ui/panels/Panels.h"

#include "app/Theme.h"
#include "core/Adjustments.h"
#include "core/ColorState.h"
#include "core/ColorModes.h"
#include "core/Document.h"
#include "core/VectorLayers.h"
#include "ui/CanvasView.h"
#include "ui/ToolBox.h"
#include "ui/Widgets.h"
#include "tools/Tool.h"

#include <QAction>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHelpEvent>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
#include <QTimer>
#include <QToolTip>
#include <QUndoGroup>
#include <QUndoStack>
#include <QUndoView>
#include <QVBoxLayout>
#include <cmath>
#include <algorithm>
#include <iterator>

// ---------------- ChannelSlider ----------------

ChannelSlider::ChannelSlider(QWidget* parent)
    : QWidget(parent)
{
    setFixedHeight(16);
    setMinimumWidth(100);
}

void ChannelSlider::setGradient(const QColor& from, const QColor& to)
{
    m_from = from;
    m_to = to;
    QWidget::update();
}

void ChannelSlider::setValue(int v)
{
    m_value = std::clamp(v, 0, 255);
    QWidget::update();
}

void ChannelSlider::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    QRect bar(4, 1, width() - 8, 8);
    QLinearGradient g(bar.topLeft(), bar.topRight());
    g.setColorAt(0, m_from);
    g.setColorAt(1, m_to);
    p.fillRect(bar, g);
    p.setPen(QColor(0x1e, 0x1e, 0x1e));
    p.drawRect(bar.adjusted(0, 0, -1, -1));
    const double x = bar.left() + m_value / 255.0 * (bar.width() - 1);
    p.setRenderHint(QPainter::Antialiasing);
    p.setBrush(QColor(0xe0, 0xe0, 0xe0));
    p.setPen(QColor(0x1e, 0x1e, 0x1e));
    p.drawPolygon(QPolygonF{QPointF(x, 9), QPointF(x - 4, 15), QPointF(x + 4, 15)});
}

void ChannelSlider::pick(double x)
{
    const double t = (x - 4) / std::max(1, width() - 9);
    setValue(int(std::round(std::clamp(t, 0.0, 1.0) * 255)));
    emit valueChanged(m_value);
}

void ChannelSlider::mousePressEvent(QMouseEvent* e) { pick(e->position().x()); }
void ChannelSlider::mouseMoveEvent(QMouseEvent* e) { pick(e->position().x()); }

// ---------------- ColorPanel ----------------

namespace {

QImage spectrumRamp(const QSize& s)
{
    QImage img(s, QImage::Format_RGB32);
    for (int y = 0; y < s.height(); ++y) {
        auto* row = reinterpret_cast<QRgb*>(img.scanLine(y));
        const double t = double(y) / std::max(1, s.height() - 1);
        for (int x = 0; x < s.width(); ++x) {
            QColor c = QColor::fromHsvF(float(double(x) / s.width()), 1.f, 1.f);
            // Top fades to white, bottom fades to black.
            double r = c.redF(), g = c.greenF(), b = c.blueF();
            if (t < 0.5) {
                double k = 1.0 - t * 2;
                r += (1 - r) * k, g += (1 - g) * k, b += (1 - b) * k;
            } else {
                double k = (t - 0.5) * 2;
                r *= 1 - k, g *= 1 - k, b *= 1 - k;
            }
            row[x] = qRgb(int(r * 255), int(g * 255), int(b * 255));
        }
    }
    return img;
}

} // namespace

ColorPanel::ColorPanel(ColorState* colors, QWidget* parent)
    : QWidget(parent)
    , m_colors(colors)
{
    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    auto* swatches = new ColorSwatchWidget(colors, this);
    root->addWidget(swatches, 0, Qt::AlignTop);
    auto* right = new QVBoxLayout;
    auto* grid = new QGridLayout;
    grid->setVerticalSpacing(2);
    const char* labels[] = {"R", "G", "B"};
    for (int i = 0; i < 3; ++i) {
        m_sliders[i] = new ChannelSlider(this);
        m_spins[i] = new QSpinBox(this);
        m_spins[i]->setRange(0, 255);
        m_spins[i]->setButtonSymbols(QAbstractSpinBox::NoButtons);
        m_spins[i]->setFixedWidth(40);
        grid->addWidget(new QLabel(QString::fromLatin1(labels[i]), this), i, 0);
        grid->addWidget(m_sliders[i], i, 1);
        grid->addWidget(m_spins[i], i, 2);
        auto apply = [this, i](int v) {
            if (m_syncing) return;
            QColor c = m_editBackground ? m_colors->background() : m_colors->foreground();
            int rgb[3] = {c.red(), c.green(), c.blue()};
            rgb[i] = v;
            c = QColor(rgb[0], rgb[1], rgb[2]);
            if (m_editBackground) m_colors->setBackground(c);
            else m_colors->setForeground(c);
        };
        connect(m_sliders[i], &ChannelSlider::valueChanged, this, apply);
        connect(m_spins[i], &QSpinBox::valueChanged, this, apply);
    }
    right->addLayout(grid);
    m_ramp = new QLabel(this);
    m_ramp->setFixedHeight(40);
    m_ramp->setMinimumWidth(120);
    m_ramp->setScaledContents(true);
    m_ramp->setPixmap(QPixmap::fromImage(spectrumRamp(QSize(256, 40))));
    m_ramp->setCursor(makeIconCursor(QStringLiteral("tool-eyedropper"), QPoint(4, 19)));
    m_ramp->installEventFilter(this);
    right->addWidget(m_ramp);
    right->addStretch();
    root->addLayout(right, 1);
    connect(colors, &ColorState::changed, this, &ColorPanel::sync);
    sync();
}

void ColorPanel::sync()
{
    m_syncing = true;
    const QColor c = m_editBackground ? m_colors->background() : m_colors->foreground();
    const int v[3] = {c.red(), c.green(), c.blue()};
    for (int i = 0; i < 3; ++i) {
        QColor from = c, to = c;
        if (i == 0) from.setRed(0), to.setRed(255);
        if (i == 1) from.setGreen(0), to.setGreen(255);
        if (i == 2) from.setBlue(0), to.setBlue(255);
        m_sliders[i]->setGradient(from, to);
        m_sliders[i]->setValue(v[i]);
        m_spins[i]->setValue(v[i]);
    }
    m_syncing = false;
}

bool ColorPanel::eventFilter(QObject* obj, QEvent* e)
{
    if (obj == m_ramp && (e->type() == QEvent::MouseButtonPress || e->type() == QEvent::MouseMove)) {
        auto* me = static_cast<QMouseEvent*>(e);
        if (e->type() == QEvent::MouseMove && !(me->buttons() & Qt::LeftButton)) return false;
        const QImage img = spectrumRamp(QSize(256, 40));
        const int x = std::clamp(int(me->position().x() * 256 / m_ramp->width()), 0, 255);
        const int y = std::clamp(int(me->position().y() * 40 / m_ramp->height()), 0, 39);
        const QColor c = img.pixelColor(x, y);
        if (me->modifiers() & Qt::AltModifier) m_colors->setBackground(c);
        else m_colors->setForeground(c);
        return true;
    }
    return QWidget::eventFilter(obj, e);
}

// ---------------- SwatchesPanel ----------------

namespace {

QList<QColor> defaultSwatches()
{
    QList<QColor> out;
    for (int i = 0; i <= 10; ++i) out << QColor::fromRgbF(1 - i / 10.f, 1 - i / 10.f, 1 - i / 10.f);
    const int hues[] = {0, 30, 50, 60, 90, 120, 160, 180, 200, 220, 240, 270, 300, 330};
    for (double val : {1.0, 0.75, 0.5}) {
        for (int h : hues) out << QColor::fromHsvF(h / 360.f, 1.f, float(val));
    }
    for (int h : hues) out << QColor::fromHsvF(h / 360.f, 0.45f, 1.f);
    for (int h : hues) out << QColor::fromHsvF(h / 360.f, 0.6f, 0.35f);
    return out;
}

constexpr int kCell = 18;

} // namespace

SwatchesPanel::SwatchesPanel(ColorState* colors, QWidget* parent)
    : QWidget(parent)
    , m_colors(colors)
{
    setMouseTracking(true);
    QSettings settings;
    const QStringList saved = settings.value(QStringLiteral("swatches")).toStringList();
    if (saved.isEmpty()) m_swatches = defaultSwatches();
    else
        for (const QString& s : saved) m_swatches << QColor(s);

    auto* add = makeIconButton(QStringLiteral("new-layer"), QStringLiteral("Create new swatch of foreground color"), this, false, 20);
    connect(add, &QToolButton::clicked, this, [this] { addSwatch(m_colors->foreground()); });
    add->move(0, 0);
    add->setObjectName(QStringLiteral("addSwatch"));
    setMinimumHeight(120);
}

void SwatchesPanel::addSwatch(const QColor& c)
{
    m_swatches.prepend(c);
    save();
    QWidget::update();
}

void SwatchesPanel::save()
{
    QStringList out;
    for (const QColor& c : m_swatches) out << c.name();
    QSettings().setValue(QStringLiteral("swatches"), out);
}

QRect SwatchesPanel::cellRect(int i) const
{
    return QRect(8 + (i % m_columns) * (kCell + 2), 8 + (i / m_columns) * (kCell + 2), kCell, kCell);
}

int SwatchesPanel::indexAt(const QPoint& p) const
{
    for (int i = 0; i < m_swatches.size(); ++i)
        if (cellRect(i).contains(p)) return i;
    return -1;
}

void SwatchesPanel::resizeEvent(QResizeEvent*)
{
    m_columns = std::max(4, (width() - 16) / (kCell + 2));
    if (auto* add = findChild<QToolButton*>(QStringLiteral("addSwatch"))) add->move(width() - 28, height() - 26);
}

void SwatchesPanel::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    for (int i = 0; i < m_swatches.size(); ++i) {
        QRect r = cellRect(i);
        p.fillRect(r, m_swatches[i]);
        p.setPen(QColor(0x1e, 0x1e, 0x1e));
        p.drawRect(r.adjusted(0, 0, -1, -1));
    }
}

void SwatchesPanel::mousePressEvent(QMouseEvent* e)
{
    const int i = indexAt(e->position().toPoint());
    if (i < 0) return;
    if (e->modifiers() & Qt::AltModifier) {
        // Alt-click deletes a swatch, as in Photoshop.
        m_swatches.removeAt(i);
        save();
        QWidget::update();
    } else if (e->modifiers() & Qt::ControlModifier) {
        m_colors->setBackground(m_swatches[i]);
    } else {
        m_colors->setForeground(m_swatches[i]);
    }
}

void SwatchesPanel::mouseMoveEvent(QMouseEvent* e)
{
    setCursor(indexAt(e->position().toPoint()) >= 0 ? makeIconCursor(QStringLiteral("tool-eyedropper"), QPoint(4, 19)) : Qt::ArrowCursor);
}

bool SwatchesPanel::event(QEvent* e)
{
    if (e->type() == QEvent::ToolTip) {
        auto* he = static_cast<QHelpEvent*>(e);
        const int i = indexAt(he->pos());
        if (i >= 0) QToolTip::showText(he->globalPos(), m_swatches[i].name().toUpper(), this);
        else QToolTip::hideText();
        return true;
    }
    return QWidget::event(e);
}

// ---------------- NavigatorPanel ----------------

NavigatorPanel::NavigatorPanel(QWidget* parent)
    : QWidget(parent)
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 6);
    m_canvas = new QWidget(this);
    m_canvas->setMinimumHeight(150);
    m_canvas->installEventFilter(this);
    m_canvas->setCursor(Qt::OpenHandCursor);
    root->addWidget(m_canvas, 1);
    auto* row = new QHBoxLayout;
    m_zoomField = new QSpinBox(this);
    m_zoomField->setRange(1, 12800);
    m_zoomField->setSuffix(QStringLiteral("%"));
    m_zoomField->setButtonSymbols(QAbstractSpinBox::NoButtons);
    m_zoomField->setFixedWidth(60);
    m_zoomField->setKeyboardTracking(false);
    auto* small = makeIconButton(QStringLiteral("mountain-small"), QStringLiteral("Zoom Out"), this, false, 20);
    auto* large = makeIconButton(QStringLiteral("mountain-large"), QStringLiteral("Zoom In"), this, false, 20);
    m_slider = new QSlider(Qt::Horizontal, this);
    m_slider->setRange(0, 1000);
    row->addWidget(m_zoomField);
    row->addWidget(small);
    row->addWidget(m_slider, 1);
    row->addWidget(large);
    root->addLayout(row);

    connect(small, &QToolButton::clicked, this, [this] { if (m_view) m_view->zoomOut(); });
    connect(large, &QToolButton::clicked, this, [this] { if (m_view) m_view->zoomIn(); });
    connect(m_zoomField, &QSpinBox::valueChanged, this, [this](int v) {
        if (!m_syncing && m_view) m_view->setZoom(v / 100.0);
    });
    connect(m_slider, &QSlider::valueChanged, this, [this](int v) {
        // Logarithmic: 1% .. 3200%.
        if (!m_syncing && m_view) m_view->setZoom(0.01 * std::pow(3200.0, v / 1000.0));
    });
    setView(nullptr);
}

void NavigatorPanel::setView(CanvasView* view)
{
    if (m_view) disconnect(m_view, nullptr, this, nullptr);
    if (m_view && m_view->document()) disconnect(m_view->document(), nullptr, this, nullptr);
    m_view = view;
    if (view) {
        connect(view, &CanvasView::viewChanged, m_canvas, qOverload<>(&QWidget::update));
        connect(view, &CanvasView::zoomChanged, this, &NavigatorPanel::syncZoom);
        connect(view->document(), &Document::imageChanged, m_canvas, qOverload<>(&QWidget::update));
    }
    m_zoomField->setEnabled(view);
    m_slider->setEnabled(view);
    syncZoom();
    m_canvas->update();
}

void NavigatorPanel::syncZoom()
{
    if (!m_view) return;
    m_syncing = true;
    m_zoomField->setValue(int(std::round(m_view->zoom() * 100)));
    m_slider->setValue(int(std::round(std::log(m_view->zoom() / 0.01) / std::log(3200.0) * 1000)));
    m_syncing = false;
}

QRectF NavigatorPanel::imageRect() const
{
    if (!m_view) return QRectF();
    Document* doc = m_view->document();
    const QRectF area = QRectF(m_canvas->rect()).adjusted(4, 4, -4, -4);
    const double s = std::min(area.width() / doc->width(), area.height() / doc->height());
    QRectF r(0, 0, doc->width() * s, doc->height() * s);
    r.moveCenter(area.center());
    return r;
}

bool NavigatorPanel::eventFilter(QObject* obj, QEvent* e)
{
    if (obj != m_canvas) return QWidget::eventFilter(obj, e);
    if (e->type() == QEvent::Paint) {
        QPainter p(m_canvas);
        p.fillRect(m_canvas->rect(), Theme::kPanelDark);
        if (!m_view) return true;
        Document* doc = m_view->document();
        const QRectF r = imageRect();
        const double s = r.width() / doc->width();
        int level = std::clamp(int(std::floor(std::log2(1.0 / std::max(1e-6, s * m_canvas->devicePixelRatioF())))), 0,
                               doc->pyramidLevelCount() - 1);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.fillRect(r, Qt::white);
        p.drawImage(r, doc->pyramidLevel(level));
        // Red view rectangle.
        const QRectF vis = m_view->visibleCanvasRect();
        QRectF vr(r.left() + vis.left() * s, r.top() + vis.top() * s, vis.width() * s, vis.height() * s);
        p.setPen(QPen(QColor(0xe0, 0x30, 0x30), 2));
        p.setBrush(Qt::NoBrush);
        p.drawRect(vr.adjusted(1, 1, -1, -1));
        return true;
    }
    if ((e->type() == QEvent::MouseButtonPress || e->type() == QEvent::MouseMove) && m_view) {
        auto* me = static_cast<QMouseEvent*>(e);
        if (e->type() == QEvent::MouseMove && !(me->buttons() & Qt::LeftButton)) return false;
        const QRectF r = imageRect();
        const double s = r.width() / m_view->document()->width();
        m_view->centerOn((me->position() - r.topLeft()) / s);
        return true;
    }
    return QWidget::eventFilter(obj, e);
}

// ---------------- InfoPanel ----------------

InfoPanel::InfoPanel(QWidget* parent)
    : QWidget(parent)
{
    auto* grid = new QGridLayout(this);
    grid->setContentsMargins(10, 8, 10, 8);
    grid->setHorizontalSpacing(16);
    m_rgb = new QLabel(this);
    m_xy = new QLabel(this);
    m_wh = new QLabel(this);
    m_doc = new QLabel(this);
    m_second = new QLabel(this);
    for (QLabel* l : {m_rgb, m_second, m_xy, m_wh, m_doc}) {
        l->setTextFormat(Qt::RichText);
        l->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    }
    grid->addWidget(m_rgb, 0, 0);
    grid->addWidget(m_second, 0, 1);
    grid->addWidget(m_xy, 1, 0);
    grid->addWidget(m_wh, 1, 1);
    grid->addWidget(m_doc, 2, 0, 1, 2);
    grid->setRowStretch(3, 1);
    showAt(QPointF(), false);
}

void InfoPanel::setView(CanvasView* view)
{
    if (m_view) disconnect(m_view, nullptr, this, nullptr);
    m_view = view;
    if (view) connect(view, &CanvasView::cursorMoved, this, &InfoPanel::showAt);
    showAt(QPointF(), false);
}

namespace {

// One Info panel readout: the colour's values in `mode`, or blanks.
QString readout(ColorMode mode, const QColor* c)
{
    QStringList labels, values;
    switch (mode) {
    case ColorMode::Grayscale: labels = {QStringLiteral("K:")}; break;
    case ColorMode::CMYK: labels = {QStringLiteral("C:"), QStringLiteral("M:"), QStringLiteral("Y:"), QStringLiteral("K:")}; break;
    case ColorMode::Lab: labels = {QStringLiteral("L:"), QStringLiteral("a:"), QStringLiteral("b:")}; break;
    default: labels = {QStringLiteral("R:"), QStringLiteral("G:"), QStringLiteral("B:")}; break;
    }
    if (c) {
        switch (mode) {
        case ColorMode::Grayscale:
            values << QStringLiteral("%1%").arg(std::lround((255 - ColorModes::gray(c->red(), c->green(), c->blue())) * 100.0 / 255.0));
            break;
        case ColorMode::CMYK:
            for (double v : ColorModes::rgbToCmyk(c->red(), c->green(), c->blue())) values << QStringLiteral("%1%").arg(std::lround(v * 100.0));
            break;
        case ColorMode::Lab:
            for (double v : ColorModes::rgbToLab(c->red(), c->green(), c->blue())) values << QString::number(std::lround(v));
            break;
        default: values << QString::number(c->red()) << QString::number(c->green()) << QString::number(c->blue()); break;
        }
    }
    QString rows;
    for (int i = 0; i < labels.size(); ++i)
        rows += QStringLiteral("<tr><td>%1&nbsp;</td><td>%2</td></tr>").arg(labels[i], i < values.size() ? values[i] : QStringLiteral(" "));
    return QStringLiteral("<table>%1</table>").arg(rows);
}

} // namespace

void InfoPanel::showAt(const QPointF& pos, bool inside)
{
    QString x = QStringLiteral(" "), y = QStringLiteral(" ");
    QColor c;
    const bool valid = m_view && inside;
    // The first readout follows the document's mode; the second is CMYK, as in Photoshop.
    const ColorMode mode = m_view ? m_view->document()->colorMode() : ColorMode::RGB;
    if (valid) {
        const QPoint px(int(std::floor(pos.x())), int(std::floor(pos.y())));
        c = QColor::fromRgba(qUnpremultiply(m_view->document()->compositePixel(px)));
        x = QString::number(px.x());
        y = QString::number(px.y());
    }
    m_rgb->setText(readout(mode, valid ? &c : nullptr));
    m_second->setText(readout(mode == ColorMode::CMYK ? ColorMode::RGB : ColorMode::CMYK, valid ? &c : nullptr));
    m_xy->setText(QStringLiteral("<table><tr><td>X:&nbsp;</td><td>%1</td></tr><tr><td>Y:&nbsp;</td><td>%2</td></tr></table>").arg(x, y));
    QString w = QStringLiteral(" "), h = QStringLiteral(" ");
    if (m_view && m_view->document()->hasSelection()) {
        const QRect sb = m_view->document()->selectionBounds();
        w = QString::number(sb.width());
        h = QString::number(sb.height());
    }
    m_wh->setText(QStringLiteral("<table><tr><td>W:&nbsp;</td><td>%1</td></tr><tr><td>H:&nbsp;</td><td>%2</td></tr></table>").arg(w, h));
    if (m_view) {
        Document* d = m_view->document();
        m_doc->setText(QStringLiteral("Doc: %1 px × %2 px (%3 ppi)").arg(d->width()).arg(d->height()).arg(d->dpi()));
    } else {
        m_doc->clear();
    }
}

// ---------------- HistoryPanel ----------------

HistoryPanel::HistoryPanel(QUndoGroup* group, QWidget* parent)
    : QWidget(parent)
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    m_view = new QUndoView(group, this);
    m_view->setEmptyLabel(QStringLiteral("Open"));
    m_view->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_view, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        const QModelIndex idx = m_view->indexAt(pos);
        if (!m_doc || !idx.isValid()) return;
        QMenu menu(this);
        QAction* a = menu.addAction(QStringLiteral("Set Source for History Brush"));
        a->setCheckable(true);
        a->setChecked(idx.row() == m_source);
        if (menu.exec(m_view->viewport()->mapToGlobal(pos)) == a) setHistoryBrushSource(idx.row());
    });
    root->addWidget(m_view);
}

void HistoryPanel::setDocument(Document* doc)
{
    if (doc != m_doc) m_source = 0;
    m_doc = doc;
    m_view->setEmptyLabel(doc && doc->filePath().isEmpty() ? QStringLiteral("New") : QStringLiteral("Open"));
}

void HistoryPanel::setHistoryBrushSource(int state)
{
    if (!m_doc) return;
    m_source = state;
    m_doc->setHistorySource(m_doc->stateAt(state));
}

// ---------------- PropertiesPanel ----------------

PropertiesPanel::PropertiesPanel(QWidget* parent)
    : QWidget(parent)
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(10, 8, 10, 8);
    m_title = new QLabel(this);
    m_title->setStyleSheet(QStringLiteral("font-weight: bold;"));
    m_body = new QLabel(this);
    m_body->setTextFormat(Qt::RichText);
    m_body->setWordWrap(true);
    root->addWidget(m_title);
    root->addWidget(m_body);
    m_buttons = new QWidget(this);
    auto* column = new QVBoxLayout(m_buttons);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(4);
    root->addWidget(m_buttons);
    root->addStretch();
    refresh();
}

void PropertiesPanel::setActionLookup(std::function<QAction*(const QString&)> lookup)
{
    m_lookup = std::move(lookup);
    m_buttonIds.clear();
    refresh();
}

void PropertiesPanel::setButtons(const QStringList& ids)
{
    if (ids == m_buttonIds) return;
    m_buttonIds = ids;
    auto* column = static_cast<QVBoxLayout*>(m_buttons->layout());
    while (QLayoutItem* it = column->takeAt(0)) {
        delete it->widget();
        delete it;
    }
    for (const QString& id : ids) {
        QAction* a = m_lookup ? m_lookup(id) : nullptr;
        if (!a) continue;
        auto* b = new QPushButton(m_buttons);
        QString t = a->text();
        t.remove(QLatin1Char('&'));
        b->setText(t);
        b->setStyleSheet(QStringLiteral("QPushButton { min-width: 0; padding: 3px 8px; }"));
        b->setEnabled(a->isEnabled());
        connect(b, &QPushButton::clicked, this, [a] { a->trigger(); });
        // Follow the action's state (and toggled names such as Disable/Enable Layer Mask).
        connect(a, &QAction::changed, b, [a, b] {
            QString text = a->text();
            text.remove(QLatin1Char('&'));
            b->setText(text);
            b->setEnabled(a->isEnabled());
        });
        column->addWidget(b, 0, Qt::AlignLeft);
    }
}

void PropertiesPanel::setDocument(Document* doc)
{
    if (m_doc) disconnect(m_doc, nullptr, this, nullptr);
    m_doc = doc;
    if (doc) {
        connect(doc, &Document::layersChanged, this, &PropertiesPanel::refresh);
        connect(doc, &Document::activeLayerChanged, this, &PropertiesPanel::refresh);
        connect(doc, &Document::editTargetChanged, this, &PropertiesPanel::refresh);
        connect(doc, &Document::sizeChanged, this, &PropertiesPanel::refresh);
    }
    refresh();
}

void PropertiesPanel::refresh()
{
    if (!m_doc) {
        m_title->setText(QStringLiteral("No Properties"));
        m_body->clear();
        setButtons({});
        return;
    }
    const Layer* l = m_doc->activeLayer();
    const QString canvas = QStringLiteral("<p><b>Canvas</b></p><table cellspacing=4>"
                                          "<tr><td>W:</td><td>%1 px</td><td>&nbsp;&nbsp;H:</td><td>%2 px</td></tr>"
                                          "<tr><td>Resolution:</td><td colspan=3>%3 Pixels/Inch</td></tr>"
                                          "<tr><td>Mode:</td><td colspan=3>RGB Color, 8 Bits/Channel</td></tr></table>")
                               .arg(m_doc->width())
                               .arg(m_doc->height())
                               .arg(m_doc->dpi());
    auto transform = [](const QRect& r) {
        return QStringLiteral("<p><b>Transform</b></p><table cellspacing=4>"
                              "<tr><td>W:</td><td>%1 px</td><td>&nbsp;&nbsp;H:</td><td>%2 px</td></tr>"
                              "<tr><td>X:</td><td>%3 px</td><td>&nbsp;&nbsp;Y:</td><td>%4 px</td></tr></table>")
            .arg(r.width())
            .arg(r.height())
            .arg(r.x())
            .arg(r.y());
    };
    if (!l || l->isBackground) {
        m_title->setText(QStringLiteral("Document"));
        m_body->setText(canvas);
        setButtons({QStringLiteral("image.imageSize"), QStringLiteral("image.canvasSize")});
        return;
    }
    if (m_doc->editingMask()) {
        m_title->setText(QStringLiteral("Masks"));
        m_body->setText(QStringLiteral("<p><b>Layer Mask</b></p><p>Paint black to hide and white to reveal. "
                                       "Click the layer thumbnail in the Layers panel to edit the pixels again.</p>"));
        setButtons({QStringLiteral("layer.maskToggle"), QStringLiteral("layer.maskApply"), QStringLiteral("layer.maskDelete"),
                    QStringLiteral("layer.maskLoadSelection")});
        return;
    }
    switch (l->kind) {
    case LayerKind::Adjustment:
        m_title->setText(l->adjustment ? l->adjustment->name() : QStringLiteral("Adjustment"));
        m_body->setText(QStringLiteral("<p>Adjustment layer: it changes the layers below without altering their pixels. "
                                       "Paint on its mask to limit where it applies.</p>"));
        setButtons({QStringLiteral("layer.contentOptions"), QStringLiteral("layer.clipping")});
        return;
    case LayerKind::Group:
        m_title->setText(QStringLiteral("Group"));
        m_body->setText(QStringLiteral("<p>Blend mode: %1</p>").arg(Blend::name(l->mode)));
        setButtons({QStringLiteral("layer.ungroup"), QStringLiteral("layer.mergeDown")});
        return;
    case LayerKind::Text: {
        const TextData& t = *l->text;
        m_title->setText(QStringLiteral("Type Layer"));
        m_body->setText(transform(l->rect())
                        + QStringLiteral("<p><b>Character</b></p><p>%1, %2 px%3<br>Color: %4</p>")
                              .arg(t.font().family())
                              .arg(t.size)
                              .arg(t.bold || t.italic ? QStringLiteral(", ") + QString(t.bold ? QStringLiteral("Bold ") : QString())
                                                            + QString(t.italic ? QStringLiteral("Italic") : QString())
                                                      : QString())
                              .arg(t.color.name().toUpper()));
        setButtons({QStringLiteral("type.rasterize"), QStringLiteral("layer.styleBlending")});
        return;
    }
    case LayerKind::Shape: {
        const ShapeData& s = *l->shape;
        m_title->setText(QStringLiteral("Live Shape Properties"));
        m_body->setText(transform(s.path.boundingRect().toAlignedRect())
                        + QStringLiteral("<p><b>Appearance</b></p><p>Fill: %1<br>Stroke: %2</p>")
                              .arg(s.fillEnabled ? s.fillColor.name().toUpper() : QStringLiteral("None"))
                              .arg(s.strokeEnabled ? QStringLiteral("%1, %2 px").arg(s.strokeColor.name().toUpper()).arg(s.strokeWidth)
                                                   : QStringLiteral("None")));
        setButtons({QStringLiteral("layer.rasterizeShape"), QStringLiteral("layer.styleBlending")});
        return;
    }
    case LayerKind::Pixel: break;
    }
    m_title->setText(QStringLiteral("Pixel Layer"));
    m_body->setText(transform(l->rect()));
    setButtons({QStringLiteral("layer.styleBlending"), QStringLiteral("layer.maskRevealAll")});
}

// ---------------- ChannelsPanel ----------------

ChannelsPanel::ChannelsPanel(QWidget* parent)
    : QWidget(parent)
    , m_timer(new QTimer(this))
{
    m_timer->setSingleShot(true);
    m_timer->setInterval(400);
    connect(m_timer, &QTimer::timeout, this, &ChannelsPanel::rebuild);
}

void ChannelsPanel::setDocument(Document* doc)
{
    if (m_doc) disconnect(m_doc, nullptr, this, nullptr);
    m_doc = doc;
    if (doc) {
        connect(doc, &Document::imageChanged, this, [this] { m_timer->start(); });
        connect(doc, &Document::colorModeChanged, this, &ChannelsPanel::rebuild);
    }
    rebuild();
}

void ChannelsPanel::rebuild()
{
    m_thumbs.clear();
    m_names.clear();
    if (m_doc) {
        const ColorMode mode = m_doc->colorMode();
        m_names = ColorModes::channelNames(mode);
        const int level = std::max(0, m_doc->pyramidLevelCount() - 1);
        QImage src = m_doc->pyramidLevel(level).scaled(32, 32, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        QImage flat(src.size(), QImage::Format_RGB32);
        flat.fill(Qt::white);
        QPainter(&flat).drawImage(0, 0, src);
        // Grayscale documents have a single channel, shown without a composite row.
        const int n = ColorModes::channelCount(mode);
        if (n > 1) m_thumbs << flat;
        QList<QImage> planes;
        for (int c = 0; c < n; ++c) planes << QImage(flat.size(), QImage::Format_Grayscale8);
        int v[4];
        for (int y = 0; y < flat.height(); ++y) {
            auto* s = reinterpret_cast<const QRgb*>(flat.constScanLine(y));
            for (int x = 0; x < flat.width(); ++x) {
                ColorModes::toChannels(mode, s[x], v);
                for (int c = 0; c < n; ++c)
                    // Ink plates show ink as dark, like a printed separation.
                    planes[c].scanLine(y)[x] = uchar(mode == ColorMode::CMYK ? 255 - v[c] : v[c]);
            }
        }
        m_thumbs += planes;
        if (n == 1) m_names = m_names.mid(0, 1);
    }
    QWidget::update();
}

void ChannelsPanel::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.fillRect(rect(), Theme::kPanel);
    if (!m_doc) return;
#ifdef Q_OS_MACOS
    const QString mod = QStringLiteral("⌘");
#else
    const QString mod = QStringLiteral("Ctrl+");
#endif
    for (int i = 0; i < m_thumbs.size() && i < m_names.size(); ++i) {
        QRect row(0, i * 42, width(), 42);
        p.fillRect(row, i == 0 ? Theme::kRowSelected : Theme::kPanel);
        p.setPen(QColor(0x26, 0x26, 0x26));
        p.drawLine(row.bottomLeft(), row.bottomRight());
        Theme::icon(QStringLiteral("eye")).paint(&p, QRect(6, row.top() + 13, 16, 16));
        QRect thumb(34, row.top() + 5, 32, 32);
        QRect tr(QPoint(), m_thumbs[i].size());
        tr.moveCenter(thumb.center());
        p.drawImage(tr, m_thumbs[i]);
        p.setPen(Theme::kText);
        p.drawText(QRect(76, row.top(), width() - 140, 42), Qt::AlignVCenter, m_names[i]);
        p.setPen(Theme::kTextDim);
        p.drawText(QRect(width() - 70, row.top(), 62, 42), Qt::AlignVCenter | Qt::AlignRight, mod + QString::number(i + 2));
    }
}

// ---------------- Placeholders ----------------

PlaceholderPanel::PlaceholderPanel(const QString& text, QWidget* parent)
    : QWidget(parent)
{
    auto* lay = new QVBoxLayout(this);
    auto* label = new QLabel(text, this);
    label->setAlignment(Qt::AlignCenter);
    label->setWordWrap(true);
    label->setStyleSheet(QStringLiteral("color: #8e8e8e;"));
    lay->addWidget(label);
}

AdjustmentsPanel::AdjustmentsPanel(QWidget* parent)
    : QWidget(parent)
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(10, 8, 10, 8);
    root->addWidget(new QLabel(QStringLiteral("Add an adjustment"), this));
    auto* grid = new QGridLayout;
    grid->setSpacing(2);
    const struct { const char* name; const char* action; } items[] = {
        {"Brightness/Contrast", "layer.newAdjustment.brightnessContrast"}, {"Levels", "layer.newAdjustment.levels"},
        {"Curves", "layer.newAdjustment.curves"}, {"Exposure", nullptr}, {"Vibrance", nullptr},
        {"Hue/Saturation", "layer.newAdjustment.hueSaturation"}, {"Color Balance", "layer.newAdjustment.colorBalance"},
        {"Black & White", "layer.newAdjustment.blackWhite"}, {"Photo Filter", nullptr}, {"Channel Mixer", nullptr},
        {"Color Lookup", nullptr}, {"Invert", "layer.newAdjustment.invert"}, {"Posterize", "layer.newAdjustment.posterize"},
        {"Threshold", "layer.newAdjustment.threshold"}, {"Gradient Map", nullptr}, {"Selective Color", nullptr},
    };
    int i = 0;
    for (const auto& it : items) {
        auto* b = makeIconButton(QStringLiteral("adjustment"), QString::fromLatin1(it.name), this, false, 26);
        b->setEnabled(false);
        if (it.action) m_buttons.append({b, QString::fromLatin1(it.action)});
        grid->addWidget(b, i / 8, i % 8);
        ++i;
    }
    root->addLayout(grid);
    root->addStretch();
}

void AdjustmentsPanel::setActionLookup(std::function<QAction*(const QString&)> lookup)
{
    for (const auto& [button, id] : m_buttons) {
        QAction* a = lookup(id);
        if (!a) continue;
        connect(button, &QToolButton::clicked, a, &QAction::trigger);
        connect(a, &QAction::changed, button, [button, a] { button->setEnabled(a->isEnabled()); });
        button->setEnabled(a->isEnabled());
    }
}
