#include "tools/VectorTools.h"

#include "core/ColorState.h"
#include "core/DocumentOps.h"
#include "core/LayerStyle.h"
#include "core/LayerTree.h"
#include "tools/ToolManager.h"
#include "ui/CanvasView.h"
#include "ui/Widgets.h"

#include <QCheckBox>
#include <QComboBox>
#include <QApplication>
#include <QFontComboBox>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QSpinBox>
#include <QToolButton>
#include <QtMath>
#include <cmath>
#include <algorithm>

namespace {

QSpinBox* spinField(int min, int max, int value, const QString& suffix, QWidget* parent)
{
    auto* s = new QSpinBox(parent);
    s->setRange(min, max);
    s->setValue(value);
    s->setSuffix(suffix);
    s->setButtonSymbols(QAbstractSpinBox::NoButtons);
    s->setFixedWidth(58);
    return s;
}

QString firstLine(const QString& text) { return text.section(QLatin1Char('\n'), 0, 0).left(40); }

} // namespace

// ---------------- TypeTool ----------------

TypeTool::TypeTool(ToolManager* m)
    : Tool(m)
{
    m_blink.setInterval(530);
    connect(&m_blink, &QTimer::timeout, this, [this] {
        m_caretOn = !m_caretOn;
        emit overlayChanged();
    });
}

QWidget* TypeTool::createOptions(QWidget* parent)
{
    auto* w = new QWidget(parent);
    auto* lay = new QHBoxLayout(w);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(6);

    m_fontBox = new QFontComboBox(w);
    m_fontBox->setMaximumWidth(190);
    m_fontBox->setToolTip(QStringLiteral("Set the font family"));
    if (m_family.isEmpty()) {
        // A plain sans serif to start with, rather than the alphabetically first font.
        m_family = QApplication::font().family();
        for (const char* f : {"Helvetica Neue", "Helvetica", "Arial", "Segoe UI", "DejaVu Sans", "Liberation Sans"})
            if (QFontDatabase::hasFamily(QString::fromLatin1(f))) {
                m_family = QString::fromLatin1(f);
                break;
            }
    }
    m_fontBox->setCurrentFont(QFont(m_family));
    m_family = m_fontBox->currentFont().family();
    connect(m_fontBox, &QFontComboBox::currentFontChanged, this, [this](const QFont& f) {
        m_family = f.family();
        optionsChanged();
    });
    lay->addWidget(m_fontBox);

    m_styleBox = new QComboBox(w);
    m_styleBox->addItems({QStringLiteral("Regular"), QStringLiteral("Bold"), QStringLiteral("Italic"), QStringLiteral("Bold Italic")});
    m_styleBox->setToolTip(QStringLiteral("Set the font style"));
    connect(m_styleBox, &QComboBox::currentIndexChanged, this, [this](int i) {
        m_bold = i == 1 || i == 3;
        m_italic = i >= 2;
        optionsChanged();
    });
    lay->addWidget(m_styleBox);

    m_sizeBox = spinField(1, 1296, m_size, QStringLiteral(" px"), w);
    m_sizeBox->setToolTip(QStringLiteral("Set the font size"));
    connect(m_sizeBox, &QSpinBox::valueChanged, this, [this](int v) {
        m_size = v;
        optionsChanged();
    });
    lay->addWidget(m_sizeBox);

    m_aaBox = new QComboBox(w);
    m_aaBox->addItems({QStringLiteral("None"), QStringLiteral("Smooth")});
    m_aaBox->setCurrentIndex(m_antialias ? 1 : 0);
    m_aaBox->setToolTip(QStringLiteral("Set the anti-aliasing method"));
    connect(m_aaBox, &QComboBox::currentIndexChanged, this, [this](int i) {
        m_antialias = i == 1;
        optionsChanged();
    });
    lay->addWidget(m_aaBox);
    lay->addWidget(makeVSeparator(w));

    const char* icons[3] = {"align-left", "align-center", "align-right"};
    const char* tips[3] = {"Left align text", "Center text", "Right align text"};
    for (int i = 0; i < 3; ++i) {
        m_alignButtons[i] = makeIconButton(QString::fromLatin1(icons[i]), QString::fromLatin1(tips[i]), w, true);
        m_alignButtons[i]->setAutoExclusive(true);
        m_alignButtons[i]->setChecked(int(m_align) == i);
        connect(m_alignButtons[i], &QToolButton::clicked, this, [this, i] {
            m_align = TextData::Align(i);
            optionsChanged();
        });
        lay->addWidget(m_alignButtons[i]);
    }
    lay->addWidget(makeVSeparator(w));

    m_colorButton = new ColorButton(QStringLiteral("Text Color"), w);
    m_colorButton->setColor(colors()->foreground());
    m_colorButton->setToolTip(QStringLiteral("Set the text color"));
    connect(m_colorButton, &ColorButton::colorChanged, this, [this] { optionsChanged(); });
    lay->addWidget(m_colorButton);
    lay->addStretch();

    m_cancelButton = makeIconButton(QStringLiteral("cancel"), QStringLiteral("Cancel any current edits"), w);
    m_commitButton = makeIconButton(QStringLiteral("check"), QStringLiteral("Commit any current edits"), w);
    connect(m_cancelButton, &QToolButton::clicked, this, [this] { cancel(m_view); });
    connect(m_commitButton, &QToolButton::clicked, this, [this] { commit(m_view); });
    lay->addWidget(m_cancelButton);
    lay->addWidget(m_commitButton);
    setEditingUi(isEditing());
    return w;
}

void TypeTool::setEditingUi(bool on)
{
    if (m_cancelButton) m_cancelButton->setVisible(on);
    if (m_commitButton) m_commitButton->setVisible(on);
}

TextData TypeTool::fromOptions() const
{
    TextData t;
    t.family = m_family;
    t.size = m_size;
    t.bold = m_bold;
    t.italic = m_italic;
    t.antialias = m_antialias;
    t.align = m_align;
    t.color = m_colorButton ? m_colorButton->color() : colors()->foreground();
    return t;
}

void TypeTool::syncOptions(const TextData& t)
{
    m_syncing = true;
    m_family = t.family;
    m_size = t.size;
    m_bold = t.bold;
    m_italic = t.italic;
    m_antialias = t.antialias;
    m_align = t.align;
    if (m_fontBox && !t.family.isEmpty()) m_fontBox->setCurrentFont(QFont(t.family));
    if (m_styleBox) m_styleBox->setCurrentIndex((t.bold ? 1 : 0) + (t.italic ? 2 : 0));
    if (m_sizeBox) m_sizeBox->setValue(t.size);
    if (m_aaBox) m_aaBox->setCurrentIndex(t.antialias ? 1 : 0);
    if (m_alignButtons[int(t.align)]) m_alignButtons[int(t.align)]->setChecked(true);
    if (m_colorButton) m_colorButton->setColor(t.color);
    m_syncing = false;
}

void TypeTool::optionsChanged()
{
    if (m_syncing) return;
    const TextData o = fromOptions();
    auto copyStyle = [&](TextData& t) {
        t.family = o.family;
        t.size = o.size;
        t.bold = o.bold;
        t.italic = o.italic;
        t.antialias = o.antialias;
        t.align = o.align;
        t.color = o.color;
    };
    if (isEditing()) {
        copyStyle(m_text);
        apply();
        if (m_view) m_view->setFocus();
        return;
    }
    // With a type layer selected, the options restyle it.
    CanvasView* v = m_manager->activeView();
    Document* doc = v ? v->document() : nullptr;
    if (!doc || !doc->activeLayer() || doc->activeLayer()->kind != LayerKind::Text) return;
    TextData t = *doc->activeLayer()->text;
    copyStyle(t);
    Ops::setText(doc, doc->activeIndex(), t, QStringLiteral("Edit Type Layer"), 2001);
}

bool TypeTool::beginEdit(CanvasView* v, int index, const QPointF& pos)
{
    if (!v || isEditing()) return false;
    Document* doc = v->document();
    if (doc->inQuickMask()) {
        alert(QStringLiteral("Could not use the type tool because Quick Mask mode is on."));
        return false;
    }
    m_before = doc->state();
    if (index >= 0) {
        const Layer& l = doc->layerAt(index);
        if (l.kind != LayerKind::Text || !l.text) return false;
        m_text = *l.text;
        m_layerId = l.id;
        m_newLayer = false;
        doc->setActiveIndex(index);
        syncOptions(m_text);
        m_caret = m_text.indexAt(pos);
    } else {
        m_text = fromOptions();
        m_text.color = colors()->foreground();
        if (m_colorButton) m_colorButton->setColor(m_text.color);
        m_text.position = pos;
        Layer l = Layer::create(doc->nextLayerName());
        l.kind = LayerKind::Text;
        l.text = std::make_shared<const TextData>(m_text);
        m_layerId = l.id;
        m_newLayer = true;
        Ops::insertLayerRaw(doc, l);
        doc->notifyLayersChanged();
        m_caret = 0;
    }
    m_doc = doc;
    m_view = v;
    m_caretOn = true;
    m_blink.start();
    setEditingUi(true);
    m_manager->enterModal(this);
    emit overlayChanged();
    return true;
}

void TypeTool::apply()
{
    if (!isEditing()) return;
    const int idx = m_doc->indexOfId(m_layerId);
    if (idx < 0) return;
    Layer& l = m_doc->layerRef(idx);
    const int m = l.hasStyle() ? l.style->margin() : 0;
    const QRect old = l.rect().adjusted(-m, -m, m, m);
    l.text = std::make_shared<const TextData>(m_text);
    Vector::rasterize(l);
    m_doc->notifyLayerPixels(idx, old | l.rect().adjusted(-m, -m, m, m));
    m_caretOn = true;
    m_blink.start();
    emit overlayChanged();
}

void TypeTool::insertText(const QString& text)
{
    if (!isEditing() || text.isEmpty()) return;
    m_text.text.insert(m_caret, text);
    m_caret += int(text.size());
    apply();
}

void TypeTool::setCaret(int index)
{
    m_caret = std::clamp(index, 0, int(m_text.text.size()));
    m_caretOn = true;
    emit overlayChanged();
}

void TypeTool::mousePress(CanvasView* v, const ToolEvent& e)
{
    Document* doc = v->document();
    if (isEditing()) {
        if (m_doc == doc && m_text.canvasBounds().containsPoint(e.pos, Qt::OddEvenFill)) {
            m_caret = m_text.indexAt(e.pos);
            m_caretOn = true;
            m_blink.start();
            emit overlayChanged();
            return;
        }
        commit(v);
    }
    // Clicking a type layer edits it.
    const QList<Layer>& layers = doc->layers();
    for (int i = int(layers.size()) - 1; i >= 0; --i) {
        const Layer& l = layers[i];
        if (l.kind == LayerKind::Text && l.text && Tree::effectivelyVisible(layers, i)
            && l.text->canvasBounds().containsPoint(e.pos, Qt::OddEvenFill)) {
            beginEdit(v, i, e.pos);
            return;
        }
    }
    beginEdit(v, -1, e.pos);
}

bool TypeTool::wantsKey(const QKeyEvent* e) const
{
    if (!isEditing()) return false;
    const Qt::KeyboardModifiers mods = e->modifiers() & ~Qt::KeypadModifier;
    switch (e->key()) {
    case Qt::Key_Return:
    case Qt::Key_Enter:
        return true;
    case Qt::Key_Backspace:
    case Qt::Key_Delete:
    case Qt::Key_Left:
    case Qt::Key_Right:
    case Qt::Key_Up:
    case Qt::Key_Down:
    case Qt::Key_Home:
    case Qt::Key_End:
    case Qt::Key_Escape:
        return !(mods & (Qt::ControlModifier | Qt::MetaModifier));
    default:
        break;
    }
    if (mods & (Qt::ControlModifier | Qt::MetaModifier)) return false;
    const QString t = e->text();
    return !t.isEmpty() && t.at(0).isPrint();
}

bool TypeTool::keyPress(CanvasView* v, QKeyEvent* e)
{
    if (!isEditing()) return false;
    const Qt::KeyboardModifiers mods = e->modifiers() & ~Qt::KeypadModifier;
    const QStringList lines = m_text.lines();
    // Line and column of the caret.
    int line = 0, start = 0;
    for (; line < lines.size() - 1; ++line) {
        if (m_caret <= start + lines[line].size()) break;
        start += int(lines[line].size()) + 1;
    }
    const int column = m_caret - start;
    switch (e->key()) {
    case Qt::Key_Escape:
        cancel(v);
        return true;
    case Qt::Key_Enter:
        commit(v);
        return true;
    case Qt::Key_Return:
        if (mods & (Qt::ControlModifier | Qt::MetaModifier)) commit(v);
        else insertText(QStringLiteral("\n"));
        return true;
    case Qt::Key_Backspace:
        if (m_caret > 0) {
            m_text.text.remove(m_caret - 1, 1);
            --m_caret;
            apply();
        }
        return true;
    case Qt::Key_Delete:
        if (m_caret < m_text.text.size()) {
            m_text.text.remove(m_caret, 1);
            apply();
        }
        return true;
    case Qt::Key_Left: m_caret = std::max(0, m_caret - 1); break;
    case Qt::Key_Right: m_caret = std::min(int(m_text.text.size()), m_caret + 1); break;
    case Qt::Key_Home: m_caret = start; break;
    case Qt::Key_End: m_caret = start + int(lines[line].size()); break;
    case Qt::Key_Up:
    case Qt::Key_Down: {
        const int target = line + (e->key() == Qt::Key_Up ? -1 : 1);
        if (target < 0 || target >= lines.size()) break;
        int s = 0;
        for (int i = 0; i < target; ++i) s += int(lines[i].size()) + 1;
        m_caret = s + std::min(column, int(lines[target].size()));
        break;
    }
    default:
        insertText(e->text());
        return true;
    }
    m_caretOn = true;
    m_blink.start();
    emit overlayChanged();
    return true;
}

bool TypeTool::commit(CanvasView*)
{
    if (!isEditing()) return false;
    Document* doc = m_doc;
    const DocState before = m_before;
    const int idx = doc->indexOfId(m_layerId);
    const bool fresh = m_newLayer;
    m_blink.stop();
    m_doc = nullptr;
    m_view = nullptr;
    m_before = DocState();
    setEditingUi(false);
    const bool unchanged = !fresh && idx >= 0 && *doc->layerAt(idx).text == [&] {
        for (const Layer& l : before.layers)
            if (l.id == m_layerId && l.text) return *l.text;
        return TextData();
    }();
    if (idx < 0 || m_text.text.trimmed().isEmpty() || unchanged) {
        // Empty type layers are discarded, as in Photoshop.
        doc->restoreState(before);
    } else {
        Layer& l = doc->layerRef(idx);
        l.name = firstLine(m_text.text);
        doc->pushSnapshot(fresh ? QStringLiteral("Type Layer") : QStringLiteral("Edit Type Layer"), before);
    }
    m_manager->exitModal();
    emit overlayChanged();
    return true;
}

bool TypeTool::cancel(CanvasView*)
{
    if (!isEditing()) return false;
    Document* doc = m_doc;
    const DocState before = m_before;
    m_blink.stop();
    m_doc = nullptr;
    m_view = nullptr;
    m_before = DocState();
    setEditingUi(false);
    doc->restoreState(before);
    m_manager->exitModal();
    emit overlayChanged();
    return true;
}

void TypeTool::deactivated(CanvasView* v)
{
    if (isEditing()) commit(v);
}

void TypeTool::paintOverlay(QPainter& p, CanvasView* v)
{
    if (!isEditing() || m_doc != v->document()) return;
    p.setRenderHint(QPainter::Antialiasing, false);
    // The text's bounding box, then the caret.
    QPolygonF box;
    for (const QPointF& pt : m_text.canvasBounds()) box << v->canvasToView(pt);
    QPen pen(QColor(0x14, 0x73, 0xe6), 1, Qt::DotLine);
    pen.setCosmetic(true);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.drawPolygon(box);
    if (m_caretOn) {
        const QLineF c = m_text.caretLine(m_caret);
        p.setCompositionMode(QPainter::CompositionMode_Difference);
        p.setPen(QPen(Qt::white, 1.5));
        p.drawLine(QLineF(v->canvasToView(c.p1()), v->canvasToView(c.p2())));
    }
}

QCursor TypeTool::cursor(CanvasView*, Qt::KeyboardModifiers) const { return Qt::IBeamCursor; }

// ---------------- ShapeTool ----------------

ShapeTool::Settings& ShapeTool::settings()
{
    static Settings s;
    return s;
}

ShapeTool::ShapeTool(ToolManager* m, Kind kind)
    : Tool(m)
    , m_kind(kind)
{
}

QString ShapeTool::id() const
{
    switch (m_kind) {
    case Kind::Rectangle: return QStringLiteral("rectangle");
    case Kind::Ellipse: return QStringLiteral("ellipse");
    case Kind::Polygon: return QStringLiteral("polygon");
    case Kind::Line: return QStringLiteral("line");
    }
    return {};
}

QString ShapeTool::name() const
{
    switch (m_kind) {
    case Kind::Rectangle: return QStringLiteral("Rectangle Tool");
    case Kind::Ellipse: return QStringLiteral("Ellipse Tool");
    case Kind::Polygon: return QStringLiteral("Polygon Tool");
    case Kind::Line: return QStringLiteral("Line Tool");
    }
    return {};
}

QString ShapeTool::iconName() const
{
    switch (m_kind) {
    case Kind::Rectangle: return QStringLiteral("tool-rectangle");
    case Kind::Ellipse: return QStringLiteral("tool-ellipse");
    case Kind::Polygon: return QStringLiteral("tool-polygon");
    case Kind::Line: return QStringLiteral("tool-line");
    }
    return {};
}

QWidget* ShapeTool::createOptions(QWidget* parent)
{
    Settings& s = settings();
    if (!s.fillColor.isValid()) s.fillColor = colors()->foreground();
    auto* w = new QWidget(parent);
    auto* lay = new QHBoxLayout(w);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(6);
    auto* mode = new QComboBox(w);
    mode->addItem(QStringLiteral("Shape"));
    mode->setToolTip(QStringLiteral("Pick tool mode"));
    lay->addWidget(mode);
    lay->addWidget(makeVSeparator(w));

    lay->addWidget(new QLabel(QStringLiteral("Fill:"), w));
    m_fillButton = new ColorButton(QStringLiteral("Shape Fill Color"), w);
    m_fillButton->setOptional(true);
    m_fillButton->setToolTip(QStringLiteral("Set shape fill color (Shift-click for none)"));
    lay->addWidget(m_fillButton);
    lay->addWidget(new QLabel(QStringLiteral("Stroke:"), w));
    m_strokeButton = new ColorButton(QStringLiteral("Shape Stroke Color"), w);
    m_strokeButton->setOptional(true);
    m_strokeButton->setToolTip(QStringLiteral("Set shape stroke color (Shift-click for none)"));
    lay->addWidget(m_strokeButton);
    auto* width = spinField(0, 288, s.strokeWidth, QStringLiteral(" px"), w);
    width->setToolTip(QStringLiteral("Set shape stroke width"));
    lay->addWidget(width);

    auto sync = [this] {
        const Settings& st = settings();
        m_fillButton->setColor(st.fillColor);
        m_fillButton->setNone(!st.fill);
        m_strokeButton->setColor(st.strokeColor);
        m_strokeButton->setNone(!st.stroke);
    };
    sync();
    connect(m_fillButton, &ColorButton::colorChanged, this, [this](const QColor& c) {
        settings().fillColor = c;
        settings().fill = true;
        restyleActive();
    });
    connect(m_fillButton, &ColorButton::noneChanged, this, [this](bool none) {
        settings().fill = !none;
        restyleActive();
    });
    connect(m_strokeButton, &ColorButton::colorChanged, this, [this](const QColor& c) {
        settings().strokeColor = c;
        settings().stroke = true;
        restyleActive();
    });
    connect(m_strokeButton, &ColorButton::noneChanged, this, [this](bool none) {
        settings().stroke = !none;
        restyleActive();
    });
    connect(width, &QSpinBox::valueChanged, this, [this](int v) {
        settings().strokeWidth = v;
        restyleActive();
    });
    // Other shape tools change the shared settings; refresh when this one comes back.
    connect(m_manager, &ToolManager::currentChanged, w, [sync, width, this](Tool* t) {
        if (t != this) return;
        sync();
        QSignalBlocker b(width);
        width->setValue(settings().strokeWidth);
    });

    lay->addWidget(makeVSeparator(w));
    switch (m_kind) {
    case Kind::Rectangle: {
        lay->addWidget(new QLabel(QStringLiteral("Radius:"), w));
        auto* r = spinField(0, 2000, s.radius, QStringLiteral(" px"), w);
        r->setToolTip(QStringLiteral("Set radius of rounded corners"));
        connect(r, &QSpinBox::valueChanged, this, [](int v) { settings().radius = v; });
        lay->addWidget(r);
        break;
    }
    case Kind::Polygon: {
        lay->addWidget(new QLabel(QStringLiteral("Sides:"), w));
        auto* n = spinField(3, 100, s.sides, QString(), w);
        connect(n, &QSpinBox::valueChanged, this, [](int v) { settings().sides = v; });
        lay->addWidget(n);
        auto* star = new QCheckBox(QStringLiteral("Star"), w);
        star->setChecked(s.star);
        connect(star, &QCheckBox::toggled, this, [](bool on) { settings().star = on; });
        lay->addWidget(star);
        break;
    }
    case Kind::Line: {
        lay->addWidget(new QLabel(QStringLiteral("Weight:"), w));
        auto* wt = spinField(1, 1000, s.weight, QStringLiteral(" px"), w);
        connect(wt, &QSpinBox::valueChanged, this, [](int v) { settings().weight = v; });
        lay->addWidget(wt);
        break;
    }
    case Kind::Ellipse: break;
    }
    lay->addStretch();
    return w;
}

ShapeData ShapeTool::style() const
{
    const Settings& s = settings();
    ShapeData d;
    d.fillEnabled = s.fill;
    d.fillColor = s.fillColor.isValid() ? s.fillColor : colors()->foreground();
    d.strokeEnabled = s.stroke;
    d.strokeColor = s.strokeColor;
    d.strokeWidth = s.strokeWidth;
    if (m_kind == Kind::Line && !d.fillEnabled && !d.strokeEnabled) d.fillEnabled = true;
    return d;
}

void ShapeTool::restyleActive()
{
    CanvasView* v = m_manager->activeView();
    Document* doc = v ? v->document() : nullptr;
    if (!doc || !doc->activeLayer() || doc->activeLayer()->kind != LayerKind::Shape) return;
    ShapeData d = *doc->activeLayer()->shape;
    const ShapeData st = style();
    d.fillEnabled = st.fillEnabled;
    d.fillColor = st.fillColor;
    d.strokeEnabled = st.strokeEnabled;
    d.strokeColor = st.strokeColor;
    d.strokeWidth = st.strokeWidth;
    Ops::setShape(doc, doc->activeIndex(), d, QStringLiteral("Edit Shape"), 2002);
}

QPainterPath ShapeTool::outline(QPointF a, QPointF b, Qt::KeyboardModifiers mods) const
{
    const Settings& s = settings();
    if (m_kind == Kind::Line) {
        if (mods & Qt::ShiftModifier) {
            // Constrain to 45° steps.
            const QPointF d = b - a;
            const double ang = std::round(std::atan2(d.y(), d.x()) / (M_PI / 4)) * (M_PI / 4);
            const double len = std::hypot(d.x(), d.y());
            b = a + QPointF(std::cos(ang) * len, std::sin(ang) * len);
        }
        return Vector::linePath(a, b, s.weight);
    }
    QPointF d = b - a;
    if (mods & Qt::ShiftModifier) {
        const double side = std::max(std::fabs(d.x()), std::fabs(d.y()));
        d = QPointF(d.x() < 0 ? -side : side, d.y() < 0 ? -side : side);
    }
    QRectF r = (mods & Qt::AltModifier) ? QRectF(a - d, a + d) : QRectF(a, a + d);
    r = r.normalized();
    switch (m_kind) {
    case Kind::Rectangle: return Vector::rectanglePath(r, s.radius);
    case Kind::Ellipse: return Vector::ellipsePath(r);
    case Kind::Polygon: return Vector::polygonPath(r, s.sides, s.star);
    case Kind::Line: break;
    }
    return QPainterPath();
}

void ShapeTool::mousePress(CanvasView* v, const ToolEvent& e)
{
    m_dragging = true;
    m_start = m_end = v->snapPoint(e.pos);
    m_mods = e.mods;
    emit overlayChanged();
}

void ShapeTool::mouseMove(CanvasView* v, const ToolEvent& e)
{
    if (!m_dragging) return;
    m_end = v->snapPoint(e.pos);
    m_mods = e.mods;
    emit overlayChanged();
}

void ShapeTool::mouseRelease(CanvasView* v, const ToolEvent& e)
{
    if (!m_dragging) return;
    m_dragging = false;
    m_end = v->snapPoint(e.pos);
    m_mods = e.mods;
    emit overlayChanged();
    const QPainterPath path = outline(m_start, m_end, m_mods);
    const QRectF b = path.boundingRect();
    if (b.width() < 1.0 && b.height() < 1.0) return;
    ShapeData d = style();
    d.path = path;
    const QString base = name().section(QLatin1Char(' '), 0, 0);
    Ops::newShapeLayer(v->document(), d, base);
}

void ShapeTool::paintOverlay(QPainter& p, CanvasView* v)
{
    if (!m_dragging) return;
    const QPainterPath path = outline(m_start, m_end, m_mods);
    QTransform t;
    const QPointF o = v->canvasToView(QPointF(0, 0));
    t.translate(o.x(), o.y());
    t.scale(v->scale(), v->scale());
    p.setRenderHint(QPainter::Antialiasing);
    QPen pen(QColor(0x14, 0x73, 0xe6), 1);
    pen.setCosmetic(true);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.drawPath(t.map(path));
}

QCursor ShapeTool::cursor(CanvasView*, Qt::KeyboardModifiers) const { return Qt::CrossCursor; }
