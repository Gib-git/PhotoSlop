#include "tools/PaintTools.h"

#include "core/ColorState.h"
#include "core/Commands.h"
#include "core/Document.h"
#include "core/ImageOps.h"
#include "tools/ToolManager.h"
#include "ui/CanvasView.h"
#include "ui/Widgets.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QSlider>
#include <QVBoxLayout>
#include <cmath>
#include <algorithm>
#include <iterator>

// ---------------- BrushPickerButton ----------------

BrushPickerButton::BrushPickerButton(QWidget* parent)
    : QToolButton(parent)
{
    setFixedSize(52, 26);
    setToolTip(QStringLiteral("Open the Brush Preset picker"));
    connect(this, &QToolButton::clicked, this, &BrushPickerButton::showPicker);
}

void BrushPickerButton::setBrush(int size, int hardness)
{
    m_size = size;
    m_hardness = hardness;
    update();
}

void BrushPickerButton::paintEvent(QPaintEvent* e)
{
    QToolButton::paintEvent(e);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QPointF c(13, height() / 2.0);
    const double r = 8.0;
    QRadialGradient g(c, r);
    const double h = m_hardness / 100.0;
    g.setColorAt(0, QColor(230, 230, 230));
    g.setColorAt(std::min(0.99, h), QColor(230, 230, 230));
    g.setColorAt(1, QColor(230, 230, 230, h > 0.98 ? 255 : 0));
    p.setPen(Qt::NoPen);
    p.setBrush(g);
    p.drawEllipse(c, r, r);
    p.setPen(QColor(214, 214, 214));
    QFont f = font();
    f.setPixelSize(10);
    p.setFont(f);
    p.drawText(QRectF(24, 0, width() - 30, height()), Qt::AlignVCenter | Qt::AlignLeft,
               QString::number(m_size));
    // Drop-down arrow.
    QPolygonF tri{QPointF(width() - 9, height() / 2.0 - 2), QPointF(width() - 3, height() / 2.0 - 2),
                  QPointF(width() - 6, height() / 2.0 + 2)};
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(190, 190, 190));
    p.drawPolygon(tri);
}

void BrushPickerButton::showPicker()
{
    auto* popup = new QFrame(this, Qt::Popup);
    popup->setAttribute(Qt::WA_DeleteOnClose);
    popup->setStyleSheet(QStringLiteral("QFrame#picker { background: #424242; border: 1px solid #1e1e1e; }"));
    popup->setObjectName(QStringLiteral("picker"));
    auto* lay = new QVBoxLayout(popup);
    lay->setContentsMargins(10, 10, 10, 10);
    auto* grid = new QGridLayout;
    auto* sizeSlider = new QSlider(Qt::Horizontal, popup);
    sizeSlider->setRange(1, 1000);
    sizeSlider->setValue(m_size);
    auto* sizeSpin = new QSpinBox(popup);
    sizeSpin->setRange(1, 5000);
    sizeSpin->setSuffix(QStringLiteral(" px"));
    sizeSpin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    sizeSpin->setValue(m_size);
    auto* hardSlider = new QSlider(Qt::Horizontal, popup);
    hardSlider->setRange(0, 100);
    hardSlider->setValue(m_hardness);
    auto* hardSpin = new QSpinBox(popup);
    hardSpin->setRange(0, 100);
    hardSpin->setSuffix(QStringLiteral("%"));
    hardSpin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    hardSpin->setValue(m_hardness);
    grid->addWidget(new QLabel(QStringLiteral("Size:")), 0, 0);
    grid->addWidget(sizeSpin, 0, 2);
    grid->addWidget(sizeSlider, 1, 0, 1, 3);
    grid->addWidget(new QLabel(QStringLiteral("Hardness:")), 2, 0);
    grid->addWidget(hardSpin, 2, 2);
    grid->addWidget(hardSlider, 3, 0, 1, 3);
    lay->addLayout(grid);

    auto emitBrush = [this, sizeSpin, hardSpin] {
        setBrush(sizeSpin->value(), hardSpin->value());
        emit brushChosen(sizeSpin->value(), hardSpin->value());
    };
    connect(sizeSlider, &QSlider::valueChanged, sizeSpin, &QSpinBox::setValue);
    connect(sizeSpin, &QSpinBox::valueChanged, popup, [=] {
        QSignalBlocker b(sizeSlider);
        sizeSlider->setValue(sizeSpin->value());
        emitBrush();
    });
    connect(hardSlider, &QSlider::valueChanged, hardSpin, &QSpinBox::setValue);
    connect(hardSpin, &QSpinBox::valueChanged, popup, [=] {
        QSignalBlocker b(hardSlider);
        hardSlider->setValue(hardSpin->value());
        emitBrush();
    });

    // Preset tips: soft and hard round brushes.
    auto* presets = new QGridLayout;
    presets->setSpacing(2);
    const struct { int size; int hard; } tips[] = {
        {1, 100}, {3, 100}, {5, 100}, {9, 100}, {13, 100}, {19, 100}, {5, 0},  {9, 0},
        {13, 0},  {17, 0},  {21, 0},  {27, 0},  {35, 0},   {45, 0},   {65, 0}, {100, 0},
        {200, 0}, {300, 0}, {14, 100}, {24, 100}, {27, 100}, {39, 100}, {46, 100}, {59, 100}};
    int i = 0;
    for (const auto& t : tips) {
        auto* b = new QToolButton(popup);
        b->setFixedSize(40, 40);
        const int size = t.size, hard = t.hard;
        QPixmap pm(36, 36);
        pm.fill(Qt::transparent);
        {
            QPainter p(&pm);
            p.setRenderHint(QPainter::Antialiasing);
            double r = std::clamp(size / 4.0 + 2, 1.5, 12.0);
            QRadialGradient g(QPointF(18, 14), r);
            g.setColorAt(0, Qt::white);
            g.setColorAt(hard ? 0.98 : 0.0, Qt::white);
            g.setColorAt(1, QColor(255, 255, 255, hard ? 255 : 0));
            p.setPen(Qt::NoPen);
            p.setBrush(g);
            p.drawEllipse(QPointF(18, 14), r, r);
            p.setPen(QColor(200, 200, 200));
            QFont f = p.font();
            f.setPixelSize(9);
            p.setFont(f);
            p.drawText(QRect(0, 26, 36, 10), Qt::AlignCenter, QString::number(size));
        }
        b->setIcon(QIcon(pm));
        b->setIconSize(QSize(36, 36));
        b->setToolTip(QStringLiteral("%1 Round %2").arg(hard ? QStringLiteral("Hard") : QStringLiteral("Soft")).arg(size));
        connect(b, &QToolButton::clicked, popup, [=] {
            sizeSpin->setValue(size);
            hardSpin->setValue(hard);
        });
        presets->addWidget(b, i / 8, i % 8);
        ++i;
    }
    lay->addLayout(presets);
    popup->adjustSize();
    popup->move(mapToGlobal(QPoint(0, height())));
    popup->show();
}

// ---------------- BrushTool ----------------

BrushTool::~BrushTool() = default;

BrushTool::BrushTool(ToolManager* m, Kind kind)
    : Tool(m)
    , m_kind(kind)
{
    if (kind == Kind::Pencil) m_size = 1;
}

QString BrushTool::id() const
{
    switch (m_kind) {
    case Kind::Brush: return QStringLiteral("brush");
    case Kind::Pencil: return QStringLiteral("pencil");
    case Kind::Eraser: return QStringLiteral("eraser");
    }
    return {};
}

QString BrushTool::name() const
{
    switch (m_kind) {
    case Kind::Brush: return QStringLiteral("Brush Tool");
    case Kind::Pencil: return QStringLiteral("Pencil Tool");
    case Kind::Eraser: return QStringLiteral("Eraser Tool");
    }
    return {};
}

QString BrushTool::iconName() const
{
    switch (m_kind) {
    case Kind::Brush: return QStringLiteral("tool-brush");
    case Kind::Pencil: return QStringLiteral("tool-pencil");
    case Kind::Eraser: return QStringLiteral("tool-eraser");
    }
    return {};
}

bool BrushTool::pencilTip() const
{
    return m_kind == Kind::Pencil || (m_kind == Kind::Eraser && m_eraserPencil);
}

QWidget* BrushTool::createOptions(QWidget* parent)
{
    auto* w = new QWidget(parent);
    auto* lay = new QHBoxLayout(w);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(6);

    m_pickerButton = new BrushPickerButton(w);
    m_pickerButton->setBrush(m_size, m_hardness);
    connect(m_pickerButton, &BrushPickerButton::brushChosen, this, [this](int size, int hard) {
        m_size = size;
        m_hardness = hard;
        if (CanvasView* v = m_manager->activeView()) v->updateCursor();
    });
    lay->addWidget(m_pickerButton);
    auto* settings = makeIconButton(QStringLiteral("brush-settings"), QStringLiteral("Toggle the Brush Settings panel"), w);
    settings->setEnabled(false);
    lay->addWidget(settings);
    lay->addWidget(makeVSeparator(w));

    lay->addWidget(new QLabel(QStringLiteral("Mode:"), w));
    if (m_kind == Kind::Eraser) {
        auto* mode = new QComboBox(w);
        mode->addItems({QStringLiteral("Brush"), QStringLiteral("Pencil")});
        connect(mode, &QComboBox::currentIndexChanged, this, [this](int i) { m_eraserPencil = i == 1; });
        lay->addWidget(mode);
    } else {
        m_modeCombo = new BlendModeCombo(w);
        connect(m_modeCombo, &BlendModeCombo::modeChanged, this, [this](BlendMode m) { m_mode = m; });
        lay->addWidget(m_modeCombo);
    }
    lay->addWidget(makeVSeparator(w));

    m_opacityField = new ValueField(QStringLiteral("Opacity:"), 1, 100, QStringLiteral("%"), true, w);
    m_opacityField->setValue(m_opacity);
    connect(m_opacityField, &ValueField::valueChanged, this, [this](int v) { m_opacity = v; });
    lay->addWidget(m_opacityField);
    auto* pOpacity = makeIconButton(QStringLiteral("pressure-opacity"),
                                    QStringLiteral("Always use Pressure for Opacity"), w, true);
    connect(pOpacity, &QToolButton::toggled, this, [this](bool on) { m_pressureOpacity = on; });
    lay->addWidget(pOpacity);

    if (m_kind != Kind::Pencil) {
        lay->addWidget(makeVSeparator(w));
        m_flowField = new ValueField(QStringLiteral("Flow:"), 1, 100, QStringLiteral("%"), true, w);
        m_flowField->setValue(m_flow);
        connect(m_flowField, &ValueField::valueChanged, this, [this](int v) { m_flow = v; });
        lay->addWidget(m_flowField);
        auto* air = makeIconButton(QStringLiteral("airbrush"), QStringLiteral("Enable airbrush-style build-up effects"), w, true);
        air->setEnabled(false);
        lay->addWidget(air);
    }
    if (m_kind == Kind::Brush) {
        lay->addWidget(makeVSeparator(w));
        auto* smoothing = new ValueField(QStringLiteral("Smoothing:"), 0, 100, QStringLiteral("%"), true, w);
        smoothing->setValue(10);
        smoothing->setEnabled(false);
        lay->addWidget(smoothing);
    }
    if (m_kind == Kind::Pencil) {
        auto* autoErase = new QCheckBox(QStringLiteral("Auto Erase"), w);
        autoErase->setEnabled(false);
        lay->addWidget(autoErase);
    }
    if (m_kind == Kind::Eraser) {
        auto* history = new QCheckBox(QStringLiteral("Erase to History"), w);
        history->setEnabled(false);
        lay->addWidget(history);
    }
    lay->addWidget(makeVSeparator(w));
    auto* pSize = makeIconButton(QStringLiteral("pressure-size"),
                                 QStringLiteral("Always use Pressure for Size"), w, true);
    connect(pSize, &QToolButton::toggled, this, [this](bool on) { m_pressureSize = on; });
    lay->addWidget(pSize);
    lay->addStretch();

    connect(this, &Tool::optionsChanged, this, &BrushTool::syncOptions);
    return w;
}

void BrushTool::syncOptions()
{
    if (m_pickerButton) m_pickerButton->setBrush(m_size, m_hardness);
    if (m_opacityField) m_opacityField->setValue(m_opacity);
    if (m_flowField) m_flowField->setValue(m_flow);
}

bool BrushTool::setOpacityPercent(int v)
{
    m_opacity = std::clamp(v, 1, 100);
    emit optionsChanged();
    return true;
}

void BrushTool::adjustSize(int dir)
{
    // Photoshop-like bracket steps.
    const int s = m_size;
    int step;
    if (dir > 0) step = s < 10 ? 1 : s < 100 ? 10 : s < 200 ? 25 : s < 500 ? 50 : 100;
    else step = s <= 10 ? 1 : s <= 100 ? 10 : s <= 200 ? 25 : s <= 500 ? 50 : 100;
    m_size = std::clamp(s + dir * step, 1, 5000);
    emit optionsChanged();
    if (CanvasView* v = m_manager->activeView()) v->updateCursor();
}

void BrushTool::adjustHardness(int dir)
{
    if (pencilTip()) return;
    m_hardness = std::clamp(m_hardness + dir * 25, 0, 100);
    emit optionsChanged();
}

bool BrushTool::begin(CanvasView* v, const ToolEvent& e)
{
    Document* doc = v->document();
    Layer* l = doc->activeLayer();
    if (!l) return false;
    const QString toolName = name().section(QLatin1Char(' '), 0, 0).toLower();
    if (!l->visible) {
        alert(QStringLiteral("Could not use the %1 tool because the target layer is hidden.").arg(toolName));
        return false;
    }
    if (l->pixelsLocked()) {
        alert(QStringLiteral("Could not use the %1 tool because the layer is locked.").arg(toolName));
        return false;
    }
    m_erase = m_kind == Kind::Eraser && !l->isBackground && !l->lockTransparency;
    m_preserveAlpha = l->lockTransparency && !l->isBackground;
    const bool paintsBackgroundColor = m_kind == Kind::Eraser && !m_erase;
    m_color = ImageOps::premultiplied(paintsBackgroundColor ? colors()->background() : colors()->foreground());

    m_edit = std::make_unique<PixelEdit>(doc, doc->activeIndex(), (m_erase || m_preserveAlpha) ? QRect() : doc->bounds());
    m_maskRect = m_edit->layerRect() & doc->bounds();
    if (m_maskRect.isEmpty()) {
        m_edit->cancel();
        m_edit.reset();
        return false;
    }
    m_mask.assign(size_t(m_maskRect.width()) * size_t(m_maskRect.height()), 0);
    m_selection = doc->selection();
    m_pending = QRect();
    Q_UNUSED(e);
    return true;
}

void BrushTool::mousePress(CanvasView* v, const ToolEvent& e)
{
    if (!begin(v, e)) return;
    Document* doc = v->document();
    const double pressure = e.pressure;
    if (e.shift() && m_hasLastStrokeEnd && m_lastDoc == doc) {
        // Shift-click draws a straight line from the last stroke.
        m_last = m_lastStrokeEnd;
        m_lastPressure = pressure;
        dab(m_last, pressure);
        m_carry = std::max(pencilTip() ? 1.0 : 0.5, m_size * m_spacing / 100.0);
        strokeTo(e.pos, pressure);
    } else {
        m_last = e.pos;
        m_lastPressure = pressure;
        dab(e.pos, pressure);
        m_carry = std::max(pencilTip() ? 1.0 : 0.5, m_size * m_spacing / 100.0);
        flushToLayer();
    }
}

void BrushTool::mouseMove(CanvasView*, const ToolEvent& e)
{
    if (!m_edit) return;
    strokeTo(e.pos, e.pressure);
}

void BrushTool::mouseRelease(CanvasView* v, const ToolEvent& e)
{
    if (!m_edit) return;
    if (e.tablet) strokeTo(e.pos, m_lastPressure);
    m_lastStrokeEnd = m_last;
    m_hasLastStrokeEnd = true;
    m_lastDoc = v->document();
    end();
}

void BrushTool::deactivated(CanvasView*)
{
    if (m_edit) end();
}

void BrushTool::end()
{
    flushToLayer();
    QString text;
    switch (m_kind) {
    case Kind::Brush: text = QStringLiteral("Brush Tool"); break;
    case Kind::Pencil: text = QStringLiteral("Pencil"); break;
    case Kind::Eraser: text = QStringLiteral("Eraser"); break;
    }
    m_edit->commit(text);
    m_edit.reset();
    m_mask.clear();
    m_mask.shrink_to_fit();
    m_selection = QImage();
}

void BrushTool::strokeTo(const QPointF& p, double pressure)
{
    const QPointF d = p - m_last;
    const double dist = std::hypot(d.x(), d.y());
    if (dist <= 0.0) return;
    double t = m_carry;
    while (t <= dist) {
        const double f = t / dist;
        const double pr = m_lastPressure + (pressure - m_lastPressure) * f;
        dab(m_last + d * f, pr);
        const double diameter = m_size * (m_pressureSize ? std::max(0.05, pr) : 1.0);
        t += std::max(pencilTip() ? 1.0 : 0.5, diameter * m_spacing / 100.0);
    }
    m_carry = t - dist;
    m_last = p;
    m_lastPressure = pressure;
    flushToLayer();
}

void BrushTool::dab(const QPointF& c, double pressure)
{
    const double pr = std::clamp(pressure, 0.0, 1.0);
    double r = m_size / 2.0 * (m_pressureSize ? std::max(0.05, pr) : 1.0);
    const double flow = (pencilTip() ? 1.0 : m_flow / 100.0) * (m_pressureOpacity ? pr : 1.0);
    if (flow <= 0.0) return;
    const bool pencil = pencilTip();

    QRect box;
    if (pencil && m_size <= 1) {
        box = QRect(int(std::floor(c.x())), int(std::floor(c.y())), 1, 1);
    } else {
        r = std::max(r, 0.5);
        box = QRect(QPoint(int(std::floor(c.x() - r - 1)), int(std::floor(c.y() - r - 1))),
                    QPoint(int(std::ceil(c.x() + r + 1)), int(std::ceil(c.y() + r + 1))));
    }
    box &= m_maskRect;
    if (box.isEmpty()) return;

    const double h = pencil ? 1.0 : m_hardness / 100.0;
    const double inner = r * h;
    const int stride = m_maskRect.width();
    for (int y = box.top(); y <= box.bottom(); ++y) {
        uint16_t* row = m_mask.data() + size_t(y - m_maskRect.top()) * size_t(stride) - m_maskRect.left();
        const double dy = y + 0.5 - c.y();
        for (int x = box.left(); x <= box.right(); ++x) {
            double a;
            if (pencil) {
                if (m_size <= 1) a = 1.0;
                else {
                    const double dx = x + 0.5 - c.x();
                    a = (dx * dx + dy * dy <= r * r) ? 1.0 : 0.0;
                }
            } else {
                const double dx = x + 0.5 - c.x();
                const double d = std::sqrt(dx * dx + dy * dy);
                const double edge = std::clamp(r - d + 0.5, 0.0, 1.0);
                if (h >= 0.99 || d <= inner) {
                    a = edge;
                } else if (d >= r) {
                    a = 0.0;
                } else {
                    const double t = (d - inner) / (r - inner);
                    const double s = 1.0 - t * t;
                    a = std::min(edge, s * s);
                }
            }
            if (a <= 0.0) continue;
            uint16_t& m = row[x];
            m = uint16_t(m + (65535 - m) * (a * flow));
        }
    }
    m_pending |= box;
}

void BrushTool::flushToLayer()
{
    if (!m_edit || m_pending.isEmpty()) return;
    const QRect r = m_pending & m_maskRect;
    m_pending = QRect();
    Layer& layer = m_edit->layer();
    const QImage& base = m_edit->original();
    const QPoint off = layer.offset;
    const uint32_t opacity = uint32_t(m_opacity * 255 / 100);
    const int n = r.width();
    std::vector<uint8_t> cov(static_cast<size_t>(n));
    std::vector<uint32_t> src(static_cast<size_t>(n), m_color);

    for (int y = r.top(); y <= r.bottom(); ++y) {
        const uint16_t* mrow = m_mask.data() + size_t(y - m_maskRect.top()) * size_t(m_maskRect.width())
            + (r.left() - m_maskRect.left());
        const uchar* sel = m_selection.isNull() ? nullptr : m_selection.constScanLine(y) + r.left();
        for (int i = 0; i < n; ++i) {
            uint32_t v = (uint32_t(mrow[i]) * opacity + 32767) / 65535;
            if (sel) v = (v * sel[i] + 127) / 255;
            cov[size_t(i)] = uint8_t(v);
        }
        auto* dst = reinterpret_cast<uint32_t*>(layer.image.scanLine(y - off.y())) + (r.left() - off.x());
        auto* orig = reinterpret_cast<const uint32_t*>(base.constScanLine(y - off.y())) + (r.left() - off.x());
        memcpy(dst, orig, size_t(n) * 4);
        if (m_erase) {
            for (int i = 0; i < n; ++i)
                if (cov[size_t(i)]) dst[i] = Blend::byteMul(dst[i], 255 - cov[size_t(i)]);
        } else if (m_preserveAlpha) {
            Blend::compositeRowPreserveAlpha(dst, src.data(), n, m_mode, 1.f, cov.data(), r.left(), y);
        } else {
            Blend::compositeRow(dst, src.data(), n, m_kind == Kind::Eraser ? BlendMode::Normal : m_mode,
                                1.f, cov.data(), r.left(), y);
        }
    }
    m_edit->markDirty(r);
}
