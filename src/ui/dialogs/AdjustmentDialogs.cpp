#include "ui/dialogs/AdjustmentDialogs.h"

#include "app/Theme.h"
#include "core/Compositor.h"
#include "core/Document.h"
#include "core/DocumentOps.h"
#include "core/LayerTree.h"
#include "ui/Widgets.h"

#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDeadlineTimer>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QRadioButton>
#include <QSlider>
#include <QSpinBox>
#include <QThread>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <cmath>

namespace {

constexpr int kBarMargin = 6; // where 0 sits in HistogramView and LevelsBar

const QStringList kChannelNames = {QStringLiteral("RGB"), QStringLiteral("Red"), QStringLiteral("Green"), QStringLiteral("Blue")};

QColor channelColor(int channel)
{
    switch (channel) {
    case 1: return QColor(230, 80, 80);
    case 2: return QColor(90, 200, 90);
    case 3: return QColor(90, 140, 240);
    default: return Theme::kText;
    }
}

QSpinBox* intField(int min, int max, int value, QWidget* parent)
{
    auto* s = new QSpinBox(parent);
    s->setRange(min, max);
    s->setValue(value);
    s->setButtonSymbols(QAbstractSpinBox::NoButtons);
    s->setAlignment(Qt::AlignRight);
    s->setFixedWidth(52);
    return s;
}

QComboBox* channelCombo(QWidget* parent)
{
    auto* c = new QComboBox(parent);
    c->addItems(kChannelNames);
    return c;
}

QHBoxLayout* labelled(const QString& label, QWidget* field, QWidget* parent)
{
    auto* row = new QHBoxLayout;
    row->addWidget(new QLabel(label, parent));
    row->addWidget(field);
    row->addStretch();
    return row;
}

} // namespace

// ---------------- HistogramView ----------------

HistogramView::HistogramView(QWidget* parent)
    : QWidget(parent)
{
    setFixedSize(sizeHint());
}

void HistogramView::setData(const std::array<quint32, 256>& bins, const QColor& color)
{
    m_bins = bins;
    m_color = color;
    update();
}

void HistogramView::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    const QRect area(kBarMargin, 1, 256, height() - 2);
    p.setPen(Theme::kBorder);
    p.drawRect(area.adjusted(-1, -1, 0, 0)); // outside the bars, so bins 0 and 255 show
    p.fillRect(area, Theme::kPanelDark);
    // Scale to a high percentile so one huge spike does not flatten everything else.
    std::array<quint32, 256> sorted = m_bins;
    std::sort(sorted.begin(), sorted.end());
    const double top = std::max<quint32>(1, std::max(sorted[250], sorted[255] / 4));
    p.setPen(m_color.isValid() ? m_color.darker(110) : Theme::kText);
    for (int i = 0; i < 256; ++i) {
        if (!m_bins[i]) continue;
        const int h = std::min(area.height(), int(std::ceil(m_bins[i] / top * (area.height() - 2))));
        p.drawLine(area.left() + i, area.bottom(), area.left() + i, area.bottom() - h + 1);
    }
}

// ---------------- LevelsBar ----------------

LevelsBar::LevelsBar(int handles, QWidget* parent)
    : QWidget(parent)
{
    setFixedSize(sizeHint());
    for (int i = 0; i < handles; ++i) m_values << (handles == 1 ? 128.0 : 255.0 * i / (handles - 1));
}

void LevelsBar::setValues(const QList<double>& values)
{
    m_values = values;
    update();
}

int LevelsBar::xOf(double value) const { return kBarMargin + int(std::lround(value)); }

double LevelsBar::valueAt(int x) const { return std::clamp(double(x - kBarMargin), 0.0, 255.0); }

void LevelsBar::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRect strip(kBarMargin, 0, 256, 9);
    QLinearGradient g(strip.topLeft(), strip.topRight());
    g.setColorAt(0, Qt::black);
    g.setColorAt(1, Qt::white);
    p.fillRect(strip, g);
    const int n = int(m_values.size());
    for (int i = 0; i < n; ++i) {
        // Black, grey and white handles for Levels; a white one for Threshold.
        QColor fill = n == 1 ? Qt::white : (i == 0 ? Qt::black : (i == n - 1 ? Qt::white : QColor(128, 128, 128)));
        const int x = xOf(m_values[i]);
        QPainterPath tri;
        tri.moveTo(x, 10);
        tri.lineTo(x - 5, 20);
        tri.lineTo(x + 5, 20);
        tri.closeSubpath();
        p.setPen(QPen(fill.lightness() < 100 ? QColor(150, 150, 150) : QColor(30, 30, 30), 1));
        p.setBrush(fill);
        p.drawPath(tri);
    }
}

void LevelsBar::mousePressEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton) return;
    const int x = int(e->position().x());
    int best = -1, bestDist = 9;
    for (int i = 0; i < m_values.size(); ++i) {
        const int d = std::abs(xOf(m_values[i]) - x);
        // On a tie, take the handle that can move towards the click.
        if (d < bestDist || (d == bestDist && best >= 0 && x > xOf(m_values[i]))) {
            best = i;
            bestDist = d;
        }
    }
    m_drag = best;
    if (m_drag >= 0) emit moved(m_drag, valueAt(x));
}

void LevelsBar::mouseMoveEvent(QMouseEvent* e)
{
    if (m_drag >= 0) emit moved(m_drag, valueAt(int(e->position().x())));
}

void LevelsBar::mouseReleaseEvent(QMouseEvent*) { m_drag = -1; }

// ---------------- CurveEditor ----------------

CurveEditor::CurveEditor(QWidget* parent)
    : QWidget(parent)
    , m_color(Theme::kText)
{
    setFixedSize(sizeHint());
    setFocusPolicy(Qt::ClickFocus);
    setMouseTracking(false);
}

void CurveEditor::setPoints(const Adjust::CurvePoints& points)
{
    m_points = points;
    if (m_selected >= m_points.size()) m_selected = -1;
    update();
}

void CurveEditor::setHistogram(const std::array<quint32, 256>& bins)
{
    m_hist = bins;
    update();
}

void CurveEditor::setCurveColor(const QColor& c)
{
    m_color = c;
    update();
}

QRectF CurveEditor::graph() const { return QRectF(5, 5, width() - 10, height() - 10); }

QPointF CurveEditor::toWidget(const QPointF& v) const
{
    const QRectF g = graph();
    return QPointF(g.left() + v.x() / 255.0 * g.width(), g.bottom() - v.y() / 255.0 * g.height());
}

QPointF CurveEditor::toValue(const QPointF& w) const
{
    const QRectF g = graph();
    return QPointF((w.x() - g.left()) / g.width() * 255.0, (g.bottom() - w.y()) / g.height() * 255.0);
}

void CurveEditor::setSelectedPoint(const QPointF& value)
{
    if (m_selected < 0) return;
    moveSelected(value, false);
}

void CurveEditor::moveSelected(QPointF v, bool fromMouse)
{
    const int i = m_selected;
    const int n = int(m_points.size());
    // A point stays between its neighbours.
    const double lo = i > 0 ? m_points[i - 1].x() + 1 : 0;
    const double hi = i < n - 1 ? m_points[i + 1].x() - 1 : 255;
    QPointF p(std::clamp(std::round(v.x()), lo, hi), std::clamp(std::round(v.y()), 0.0, 255.0));
    if (p == m_points[i]) return;
    m_points[i] = p;
    update();
    emit pointsChanged();
    if (!fromMouse) return;
    emit selectionChanged(); // the fields follow the point
}

void CurveEditor::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    const QRectF g = graph();
    p.fillRect(rect(), Theme::kPanel);
    p.fillRect(g, Theme::kPanelDark);
    // Histogram behind the grid.
    const quint32 top = std::max<quint32>(1, *std::max_element(m_hist.begin(), m_hist.end()));
    p.setPen(QColor(0x4a, 0x4a, 0x4a));
    for (int i = 0; i < 256; ++i) {
        if (!m_hist[i]) continue;
        const double x = g.left() + (i + 0.5) / 256.0 * g.width();
        p.drawLine(QPointF(x, g.bottom()), QPointF(x, g.bottom() - double(m_hist[i]) / top * g.height() * 0.9));
    }
    p.setPen(QColor(0x3e, 0x3e, 0x3e));
    for (int i = 1; i < 4; ++i) {
        p.drawLine(QPointF(g.left() + g.width() * i / 4, g.top()), QPointF(g.left() + g.width() * i / 4, g.bottom()));
        p.drawLine(QPointF(g.left(), g.top() + g.height() * i / 4), QPointF(g.right(), g.top() + g.height() * i / 4));
    }
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QColor(0x5a, 0x5a, 0x5a));
    p.drawLine(g.bottomLeft(), g.topRight());
    const Adjust::Lut lut = Adjust::curveLut(m_points);
    QPainterPath path;
    for (int i = 0; i < 256; ++i) {
        const QPointF w = toWidget(QPointF(i, lut[i]));
        if (i == 0) path.moveTo(w);
        else path.lineTo(w);
    }
    p.setPen(QPen(m_color, 1.5));
    p.setBrush(Qt::NoBrush);
    p.drawPath(path);
    p.setRenderHint(QPainter::Antialiasing, false);
    for (int i = 0; i < m_points.size(); ++i) {
        const QPointF w = toWidget(m_points[i]);
        const QRectF box(w.x() - 3, w.y() - 3, 6, 6);
        p.setPen(m_color);
        p.setBrush(i == m_selected ? QBrush(m_color) : QBrush(Theme::kPanelDark));
        p.drawRect(box);
    }
    p.setPen(Theme::kBorder);
    p.setBrush(Qt::NoBrush);
    p.drawRect(g);
}

void CurveEditor::mousePressEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton) return;
    const QPointF pos = e->position();
    int hit = -1;
    double best = 7.0;
    for (int i = 0; i < m_points.size(); ++i) {
        const double d = QLineF(toWidget(m_points[i]), pos).length();
        if (d <= best) {
            best = d;
            hit = i;
        }
    }
    bool added = false;
    if (hit < 0 && graph().adjusted(-4, -4, 4, 4).contains(pos)) {
        const QPointF v(std::clamp(std::round(toValue(pos).x()), 0.0, 255.0), std::clamp(std::round(toValue(pos).y()), 0.0, 255.0));
        int at = 0;
        while (at < m_points.size() && m_points[at].x() < v.x()) ++at;
        if (at < m_points.size() && m_points[at].x() == v.x()) {
            hit = at;
        } else {
            m_points.insert(at, v);
            hit = at;
            added = true;
        }
    }
    m_selected = hit;
    m_dragging = hit >= 0;
    m_removed = false;
    update();
    if (added) emit pointsChanged();
    emit selectionChanged();
}

void CurveEditor::mouseMoveEvent(QMouseEvent* e)
{
    if (!m_dragging || m_selected < 0) return;
    // Dragging a point well off the graph removes it, as long as two points remain.
    if (!graph().adjusted(-14, -14, 14, 14).contains(e->position()) && m_points.size() > 2) {
        m_points.removeAt(m_selected);
        m_selected = -1;
        m_dragging = false;
        m_removed = true;
        update();
        emit pointsChanged();
        emit selectionChanged();
        return;
    }
    moveSelected(toValue(e->position()), true);
}

void CurveEditor::mouseReleaseEvent(QMouseEvent*) { m_dragging = false; }

void CurveEditor::keyPressEvent(QKeyEvent* e)
{
    if ((e->key() == Qt::Key_Delete || e->key() == Qt::Key_Backspace) && m_selected >= 0 && m_points.size() > 2) {
        m_points.removeAt(m_selected);
        m_selected = -1;
        update();
        emit pointsChanged();
        emit selectionChanged();
        return;
    }
    QWidget::keyPressEvent(e);
}

// ---------------- SliderField ----------------

SliderField::SliderField(const QString& label, double min, double max, double value, int decimals,
                         const QString& unit, double sliderMax, QWidget* parent)
    : QWidget(parent)
    , m_scale(std::pow(10.0, decimals))
    , m_sliderMax(sliderMax > 0 ? sliderMax : max)
{
    auto* grid = new QGridLayout(this);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(6);
    grid->setVerticalSpacing(2);
    grid->addWidget(new QLabel(label, this), 0, 0);
    m_spin = new QDoubleSpinBox(this);
    m_spin->setDecimals(decimals);
    m_spin->setRange(min, max);
    m_spin->setValue(value);
    m_spin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    m_spin->setAlignment(Qt::AlignRight);
    m_spin->setFixedWidth(64);
    m_spin->setKeyboardTracking(false);
    grid->addWidget(m_spin, 0, 2);
    auto* unitLabel = new QLabel(unit, this);
    unitLabel->setMinimumWidth(unit.isEmpty() ? 0 : 36);
    grid->addWidget(unitLabel, 0, 3);
    grid->setColumnStretch(1, 1);
    m_slider = new QSlider(Qt::Horizontal, this);
    m_slider->setRange(int(std::lround(min * m_scale)), int(std::lround(m_sliderMax * m_scale)));
    m_slider->setValue(int(std::lround(value * m_scale)));
    grid->addWidget(m_slider, 1, 0, 1, 4);

    connect(m_slider, &QSlider::valueChanged, this, [this](int v) {
        QSignalBlocker b(m_spin);
        m_spin->setValue(v / m_scale);
        emit valueChanged(m_spin->value());
    });
    connect(m_spin, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        QSignalBlocker b(m_slider);
        m_slider->setValue(int(std::lround(std::min(v, m_sliderMax) * m_scale)));
        emit valueChanged(v);
    });
}

double SliderField::value() const { return m_spin->value(); }

void SliderField::setValue(double v)
{
    QSignalBlocker b1(m_spin), b2(m_slider);
    m_spin->setValue(v);
    m_slider->setValue(int(std::lround(std::min(v, m_sliderMax) * m_scale)));
}

void SliderField::setRange(double min, double max)
{
    QSignalBlocker b1(m_spin), b2(m_slider);
    m_sliderMax = max;
    m_spin->setRange(min, max);
    m_slider->setRange(int(std::lround(min * m_scale)), int(std::lround(max * m_scale)));
}

// ---------------- PreviewDialog ----------------

PreviewDialog::PreviewDialog(Document* doc, const QString& title, bool spreads, QWidget* parent, const QRect& target,
                             int adjustmentLayer)
    : QDialog(parent)
{
    setWindowTitle(title);
    if (adjustmentLayer >= 0 && adjustmentLayer < doc->layerCount()
        && doc->layerAt(adjustmentLayer).kind == LayerKind::Adjustment) {
        m_doc = doc;
        m_layer = adjustmentLayer;
        m_layerBefore = doc->layerAt(adjustmentLayer).adjustment;
    } else {
        m_session = std::make_unique<Filters::Session>(doc, title, spreads, target);
        if (!m_session->isValid()) {
            m_error = m_session->error();
            m_session.reset();
        }
    }

    auto* root = new QHBoxLayout(this);
    root->setSpacing(14);
    m_content = new QVBoxLayout;
    m_content->setSpacing(8);
    root->addLayout(m_content, 1);
    m_buttons = new QVBoxLayout;
    m_buttons->setSpacing(6);
    root->addLayout(m_buttons);
    auto* ok = new QPushButton(QStringLiteral("OK"), this);
    ok->setDefault(true);
    auto* cancel = new QPushButton(QStringLiteral("Cancel"), this);
    m_preview = new QCheckBox(QStringLiteral("Preview"), this);
    m_preview->setChecked(true);
    m_buttons->addWidget(ok);
    m_buttons->addWidget(cancel);
    m_buttons->addSpacing(8);
    m_buttons->addWidget(m_preview);
    m_buttons->addStretch();
    connect(ok, &QPushButton::clicked, this, &QDialog::accept);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);

    m_timer.setSingleShot(true);
    m_timer.setInterval(10);
    connect(&m_timer, &QTimer::timeout, this, &PreviewDialog::render);
    connect(&m_watcher, &QFutureWatcher<QImage>::finished, this, &PreviewDialog::finished);
    connect(m_preview, &QCheckBox::toggled, this, [this](bool on) {
        if (on) {
            settingsChanged();
            return;
        }
        stopWorker(false); // its queued finished() sees the cancel flag
        m_stale = true;
        if (m_session) m_session->showOriginal();
        if (m_layer >= 0) showLayerSettings(m_layerBefore);
    });
}

PreviewDialog::~PreviewDialog()
{
    if (m_session || m_layer >= 0 || m_watcher.isRunning()) finish(false);
}

void PreviewDialog::showLayerSettings(const std::shared_ptr<const Adjust::LayerSettings>& s)
{
    if (!m_doc || m_layer < 0 || m_layer >= m_doc->layerCount() || !s) return;
    m_doc->layerRef(m_layer).adjustment = s;
    m_doc->invalidate();
}

Adjust::Histogram PreviewDialog::sourceHistogram() const
{
    if (m_session) return Adjust::histogram(m_session->originalTarget(), m_session->selectionTarget());
    if (!m_doc || m_layer < 0) return Adjust::Histogram();
    // Everything below the adjustment layer.
    QList<Layer> layers = m_doc->layers();
    for (int i = 0; i < layers.size(); ++i)
        if (i >= m_layer && !Tree::isInside(layers, m_layer, i)) layers[i].visible = false;
    return Adjust::histogram(Compositor::flatten(layers, m_doc->bounds()));
}

QPushButton* PreviewDialog::addButton(const QString& text)
{
    auto* b = new QPushButton(text, this);
    b->setAutoDefault(false);
    m_buttons->insertWidget(2, b); // under OK and Cancel
    return b;
}

bool PreviewDialog::previewEnabled() const { return m_preview->isChecked(); }

void PreviewDialog::setPreviewEnabled(bool on) { m_preview->setChecked(on); }

void PreviewDialog::settingsChanged()
{
    m_stale = true;
    if (m_rendering) m_cancel->store(true);
    if (m_preview->isChecked()) m_timer.start();
}

void PreviewDialog::render()
{
    if (m_layer >= 0) {
        // Adjustment layers re-render straight away.
        if (!m_preview->isChecked()) return;
        m_stale = false;
        showLayerSettings(layerSettings());
        return;
    }
    if (!m_session || !m_preview->isChecked()) return;
    if (m_rendering) {
        // The job's finished() starts the next one.
        m_cancel->store(true);
        return;
    }
    m_stale = false;
    m_rendering = true;
    m_cancel = std::make_shared<Filters::CancelFlag>(false);
    std::shared_ptr<Filters::CancelFlag> cancel = m_cancel;
    const Filters::Session::Input input = m_session->input();
    const Filters::Spec s = spec();
    m_watcher.setFuture(QtConcurrent::run([input, s, cancel] { return Filters::Session::render(input, s, cancel.get()); }));
}

void PreviewDialog::finished()
{
    m_rendering = false;
    if (!m_session) return;
    const QImage result = m_watcher.result();
    if (!result.isNull() && !m_cancel->load() && m_preview->isChecked()) m_session->show(result);
    if (m_stale && m_preview->isChecked()) m_timer.start();
}

void PreviewDialog::stopWorker(bool keepCurrent)
{
    m_timer.stop();
    if (!m_watcher.isRunning()) return;
    if (!keepCurrent || m_stale) m_cancel->store(true);
    m_watcher.waitForFinished();
}

void PreviewDialog::waitForPreview()
{
    QDeadlineTimer deadline(20000);
    while ((m_session || m_layer >= 0) && m_preview->isChecked() && (m_stale || m_timer.isActive() || m_rendering)
           && !deadline.hasExpired()) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        QThread::msleep(1);
    }
}

void PreviewDialog::finish(bool commit)
{
    // Late results must not reach a committed or restored layer.
    disconnect(&m_watcher, nullptr, this, nullptr);
    stopWorker(commit);
    if (m_layer >= 0) {
        const int layer = m_layer;
        m_layer = -1;
        if (!m_doc || layer >= m_doc->layerCount()) return;
        const auto settings = layerSettings();
        m_doc->layerRef(layer).adjustment = m_layerBefore;
        m_doc->invalidate();
        if (commit && settings) Ops::setAdjustment(m_doc, layer, settings);
        return;
    }
    if (!m_session) return;
    if (commit) {
        const Filters::Spec s = spec();
        QImage result;
        if (!m_stale && m_preview->isChecked() && m_watcher.future().resultCount() > 0 && !m_cancel->load())
            result = m_watcher.result();
        if (result.isNull()) {
            QApplication::setOverrideCursor(Qt::WaitCursor);
            result = Filters::Session::render(m_session->input(), s);
            QApplication::restoreOverrideCursor();
        }
        m_session->show(result);
        m_session->commit(s.name);
        m_applied = {m_session->layerId(), m_session->target(), m_session->originalTarget(), result};
    }
    m_session.reset(); // uncommitted: restores the original pixels
}

void PreviewDialog::accept()
{
    finish(true);
    QDialog::accept();
}

void PreviewDialog::reject()
{
    finish(false);
    QDialog::reject();
}

// ---------------- ParamDialog ----------------

QHash<QString, ParamDialog::Values> ParamDialog::s_last;

ParamDialog::ParamDialog(Document* doc, const QString& title, bool spreads, Builder builder, QWidget* parent,
                         int adjustmentLayer)
    : PreviewDialog(doc, title, spreads, parent, QRect(), adjustmentLayer)
    , m_title(title)
    , m_builder(std::move(builder))
{
    setMinimumWidth(380);
}

SliderField* ParamDialog::addSlider(const QString& key, const QString& label, double min, double max, double value,
                                    int decimals, const QString& unit, double sliderMax)
{
    auto* f = new SliderField(label, min, max, value, decimals, unit, sliderMax, this);
    content()->addWidget(f);
    m_values.insert(key, value);
    m_setters.insert(key, [f](double v) { f->setValue(v); });
    connect(f, &SliderField::valueChanged, this, [this, key](double v) {
        m_values[key] = v;
        settingsChanged();
    });
    return f;
}

QCheckBox* ParamDialog::addCheck(const QString& key, const QString& label, bool value)
{
    auto* c = new QCheckBox(label, this);
    c->setChecked(value);
    content()->addWidget(c);
    m_values.insert(key, value ? 1 : 0);
    m_setters.insert(key, [c](double v) {
        QSignalBlocker b(c);
        c->setChecked(v != 0);
    });
    connect(c, &QCheckBox::toggled, this, [this, key](bool on) {
        m_values[key] = on ? 1 : 0;
        settingsChanged();
    });
    return c;
}

void ParamDialog::addChoice(const QString& key, const QString& title, const QStringList& options, int value)
{
    auto* box = new QGroupBox(title, this);
    auto* lay = new QVBoxLayout(box);
    auto* group = new QButtonGroup(box);
    for (int i = 0; i < options.size(); ++i) {
        auto* r = new QRadioButton(options[i], box);
        r->setChecked(i == value);
        group->addButton(r, i);
        lay->addWidget(r);
    }
    content()->addWidget(box);
    m_values.insert(key, value);
    m_setters.insert(key, [group](double v) {
        QSignalBlocker b(group);
        if (QAbstractButton* btn = group->button(int(v))) btn->setChecked(true);
    });
    connect(group, &QButtonGroup::idToggled, this, [this, key](int id, bool on) {
        if (!on) return;
        m_values[key] = id;
        settingsChanged();
    });
}

void ParamDialog::setValue(const QString& key, double v)
{
    if (auto it = m_setters.find(key); it != m_setters.end()) (*it)(v);
    m_values[key] = v;
    settingsChanged();
}

void ParamDialog::useLastValues()
{
    const auto it = s_last.constFind(m_title);
    if (it == s_last.constEnd()) return;
    for (auto v = it->constBegin(); v != it->constEnd(); ++v) {
        if (!m_setters.contains(v.key())) continue; // hidden values (seeds) stay fresh
        m_setters[v.key()](v.value());
        m_values[v.key()] = v.value();
    }
    settingsChanged();
}

std::shared_ptr<const Adjust::LayerSettings> ParamDialog::layerSettings() const
{
    return m_layerBuilder ? m_layerBuilder(m_values).finalized() : nullptr;
}

void ParamDialog::accept()
{
    s_last.insert(m_title, m_values);
    PreviewDialog::accept();
}

// ---------------- LevelsDialog ----------------

Adjust::Levels LevelsDialog::s_last;

LevelsDialog::LevelsDialog(Document* doc, bool useLast, QWidget* parent, int adjustmentLayer)
    : PreviewDialog(doc, QStringLiteral("Levels"), false, parent, QRect(), adjustmentLayer)
{
    m_hist = sourceHistogram();
    if (useLast) m_levels = s_last;

    m_channelBox = channelCombo(this);
    content()->addLayout(labelled(QStringLiteral("Channel:"), m_channelBox, this));
    content()->addWidget(new QLabel(QStringLiteral("Input Levels:"), this));
    m_histView = new HistogramView(this);
    content()->addWidget(m_histView);
    m_input = new LevelsBar(3, this);
    content()->addWidget(m_input);
    auto* inRow = new QHBoxLayout;
    m_inBlack = intField(0, 253, 0, this);
    m_gamma = new QDoubleSpinBox(this);
    m_gamma->setRange(0.01, 9.99);
    m_gamma->setDecimals(2);
    m_gamma->setSingleStep(0.01);
    m_gamma->setButtonSymbols(QAbstractSpinBox::NoButtons);
    m_gamma->setAlignment(Qt::AlignCenter);
    m_gamma->setFixedWidth(52);
    m_inWhite = intField(2, 255, 255, this);
    inRow->addWidget(m_inBlack);
    inRow->addStretch();
    inRow->addWidget(m_gamma);
    inRow->addStretch();
    inRow->addWidget(m_inWhite);
    content()->addLayout(inRow);
    content()->addSpacing(6);
    content()->addWidget(new QLabel(QStringLiteral("Output Levels:"), this));
    m_output = new LevelsBar(2, this);
    content()->addWidget(m_output);
    auto* outRow = new QHBoxLayout;
    m_outBlack = intField(0, 255, 0, this);
    m_outWhite = intField(0, 255, 255, this);
    outRow->addWidget(m_outBlack);
    outRow->addStretch();
    outRow->addWidget(m_outWhite);
    content()->addLayout(outRow);
    content()->addStretch();
    connect(addButton(QStringLiteral("Auto")), &QPushButton::clicked, this, &LevelsDialog::autoLevels);

    connect(m_channelBox, &QComboBox::currentIndexChanged, this, &LevelsDialog::setChannel);
    connect(m_input, &LevelsBar::moved, this, &LevelsDialog::inputMoved);
    connect(m_output, &LevelsBar::moved, this, [this](int h, double v) {
        auto& c = m_levels.channels[m_channel];
        (h == 0 ? c.outBlack : c.outWhite) = int(std::lround(v));
        sync();
        settingsChanged();
    });
    auto field = [this](QSpinBox* box, auto apply) {
        connect(box, &QSpinBox::valueChanged, this, [this, apply](int v) {
            apply(m_levels.channels[m_channel], v);
            sync();
            settingsChanged();
        });
    };
    field(m_inBlack, [](Adjust::LevelsChannel& c, int v) { c.inBlack = std::min(v, c.inWhite - 2); });
    field(m_inWhite, [](Adjust::LevelsChannel& c, int v) { c.inWhite = std::max(v, c.inBlack + 2); });
    field(m_outBlack, [](Adjust::LevelsChannel& c, int v) { c.outBlack = v; });
    field(m_outWhite, [](Adjust::LevelsChannel& c, int v) { c.outWhite = v; });
    connect(m_gamma, &QDoubleSpinBox::valueChanged, this, [this](double g) {
        m_levels.channels[m_channel].gamma = g;
        sync();
        settingsChanged();
    });
    sync();
    settingsChanged();
}

void LevelsDialog::inputMoved(int handle, double value)
{
    auto& c = m_levels.channels[m_channel];
    const int v = int(std::lround(value));
    if (handle == 0) {
        c.inBlack = std::clamp(v, 0, c.inWhite - 2);
    } else if (handle == 2) {
        c.inWhite = std::clamp(v, c.inBlack + 2, 255);
    } else {
        // The grey handle sits where the output is mid-grey: black + (white - black) * 0.5^gamma.
        const double x = std::clamp(value, c.inBlack + 1.0, c.inWhite - 1.0);
        const double frac = (x - c.inBlack) / double(c.inWhite - c.inBlack);
        c.gamma = std::clamp(std::round(std::log(frac) / std::log(0.5) * 100.0) / 100.0, 0.01, 9.99);
    }
    sync();
    settingsChanged();
}

void LevelsDialog::sync()
{
    const auto& c = m_levels.channels[m_channel];
    QSignalBlocker b0(m_channelBox), b1(m_inBlack), b2(m_gamma), b3(m_inWhite), b4(m_outBlack), b5(m_outWhite);
    m_channelBox->setCurrentIndex(m_channel);
    m_histView->setData(m_hist.channels[m_channel], channelColor(m_channel));
    const double mid = c.inBlack + (c.inWhite - c.inBlack) * std::pow(0.5, c.gamma);
    m_input->setValues({double(c.inBlack), mid, double(c.inWhite)});
    m_output->setValues({double(c.outBlack), double(c.outWhite)});
    m_inBlack->setValue(c.inBlack);
    m_gamma->setValue(c.gamma);
    m_inWhite->setValue(c.inWhite);
    m_outBlack->setValue(c.outBlack);
    m_outWhite->setValue(c.outWhite);
}

void LevelsDialog::setLevels(const Adjust::Levels& levels)
{
    m_levels = levels;
    sync();
    settingsChanged();
}

void LevelsDialog::setChannel(int channel)
{
    m_channel = std::clamp(channel, 0, 3);
    sync();
}

void LevelsDialog::autoLevels() { setLevels(Adjust::autoLevels(m_hist, Adjust::AutoMode::Tone)); }

Filters::Spec LevelsDialog::spec() const { return Adjust::spec(QStringLiteral("Levels"), Adjust::levelsMap(m_levels)); }

void LevelsDialog::accept()
{
    s_last = m_levels;
    PreviewDialog::accept();
}

// ---------------- CurvesDialog ----------------

Adjust::Curves CurvesDialog::s_last;

CurvesDialog::CurvesDialog(Document* doc, bool useLast, QWidget* parent, int adjustmentLayer)
    : PreviewDialog(doc, QStringLiteral("Curves"), false, parent, QRect(), adjustmentLayer)
{
    m_hist = sourceHistogram();
    if (useLast) m_curves = s_last;

    m_channelBox = channelCombo(this);
    content()->addLayout(labelled(QStringLiteral("Channel:"), m_channelBox, this));
    m_editor = new CurveEditor(this);
    content()->addWidget(m_editor);
    auto* fields = new QHBoxLayout;
    m_inField = intField(0, 255, 0, this);
    m_outField = intField(0, 255, 0, this);
    fields->addWidget(new QLabel(QStringLiteral("Input:"), this));
    fields->addWidget(m_inField);
    fields->addSpacing(12);
    fields->addWidget(new QLabel(QStringLiteral("Output:"), this));
    fields->addWidget(m_outField);
    fields->addStretch();
    content()->addLayout(fields);
    content()->addStretch();
    connect(addButton(QStringLiteral("Auto")), &QPushButton::clicked, this, [this] {
        // Stretch each colour channel, as Auto Tone does.
        const Adjust::Levels lv = Adjust::autoLevels(m_hist, Adjust::AutoMode::Tone);
        Adjust::Curves c;
        for (int ch = 1; ch <= 3; ++ch)
            c.channels[ch] = {QPointF(lv.channels[ch].inBlack, 0), QPointF(lv.channels[ch].inWhite, 255)};
        setCurves(c);
    });

    connect(m_channelBox, &QComboBox::currentIndexChanged, this, &CurvesDialog::setChannel);
    connect(m_editor, &CurveEditor::pointsChanged, this, [this] {
        m_curves.channels[m_channel] = m_editor->points();
        syncFields();
        settingsChanged();
    });
    connect(m_editor, &CurveEditor::selectionChanged, this, &CurvesDialog::syncFields);
    auto fieldChanged = [this] { m_editor->setSelectedPoint(QPointF(m_inField->value(), m_outField->value())); };
    connect(m_inField, &QSpinBox::valueChanged, this, fieldChanged);
    connect(m_outField, &QSpinBox::valueChanged, this, fieldChanged);
    sync();
    settingsChanged();
}

void CurvesDialog::sync()
{
    QSignalBlocker b(m_channelBox);
    m_channelBox->setCurrentIndex(m_channel);
    m_editor->setPoints(m_curves.channels[m_channel]);
    m_editor->setHistogram(m_hist.channels[m_channel]);
    m_editor->setCurveColor(channelColor(m_channel));
    syncFields();
}

void CurvesDialog::syncFields()
{
    const int sel = m_editor->selected();
    QSignalBlocker b1(m_inField), b2(m_outField);
    m_inField->setEnabled(sel >= 0);
    m_outField->setEnabled(sel >= 0);
    if (sel < 0) {
        m_inField->clear();
        m_outField->clear();
        return;
    }
    const QPointF p = m_editor->points()[sel];
    m_inField->setValue(int(p.x()));
    m_outField->setValue(int(p.y()));
}

void CurvesDialog::setCurves(const Adjust::Curves& curves)
{
    m_curves = curves;
    sync();
    settingsChanged();
}

void CurvesDialog::setChannel(int channel)
{
    m_channel = std::clamp(channel, 0, 3);
    sync();
}

Filters::Spec CurvesDialog::spec() const { return Adjust::spec(QStringLiteral("Curves"), Adjust::curvesMap(m_curves)); }

void CurvesDialog::accept()
{
    s_last = m_curves;
    PreviewDialog::accept();
}

// ---------------- HueSaturationDialog ----------------

// The two spectrum bars under Hue/Saturation: input colours above, adjusted colours below.
class SpectrumBars : public QWidget {
public:
    explicit SpectrumBars(QWidget* parent)
        : QWidget(parent)
    {
        setFixedHeight(26);
        setMinimumWidth(268);
    }
    void setMap(const Adjust::PixelMap& map)
    {
        m_out.resize(360);
        for (int h = 0; h < 360; ++h) {
            QRgb c = Adjust::hslToRgb(h, 1.0, 0.5);
            map(&c, 1);
            m_out[h] = c;
        }
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        const int w = width();
        for (int x = 0; x < w; ++x) {
            const int h = x * 360 / w;
            p.setPen(QColor::fromRgb(Adjust::hslToRgb(h, 1.0, 0.5)));
            p.drawLine(x, 0, x, 10);
            if (h < int(m_out.size())) p.setPen(QColor::fromRgb(m_out[h]));
            p.drawLine(x, 14, x, 24);
        }
    }

private:
    std::vector<QRgb> m_out;
};

Adjust::HueSaturation HueSaturationDialog::s_last;

HueSaturationDialog::HueSaturationDialog(Document* doc, bool useLast, QWidget* parent, int adjustmentLayer)
    : PreviewDialog(doc, QStringLiteral("Hue/Saturation"), false, parent, QRect(), adjustmentLayer)
{
    if (useLast) m_hs = s_last;
    setMinimumWidth(400);
    m_rangeBox = new QComboBox(this);
    m_rangeBox->addItems({QStringLiteral("Master"), QStringLiteral("Reds"), QStringLiteral("Yellows"), QStringLiteral("Greens"),
                          QStringLiteral("Cyans"), QStringLiteral("Blues"), QStringLiteral("Magentas")});
    content()->addWidget(m_rangeBox);
    m_hue = new SliderField(QStringLiteral("Hue:"), -180, 180, 0, 0, QString(), 0, this);
    m_sat = new SliderField(QStringLiteral("Saturation:"), -100, 100, 0, 0, QString(), 0, this);
    m_light = new SliderField(QStringLiteral("Lightness:"), -100, 100, 0, 0, QString(), 0, this);
    content()->addWidget(m_hue);
    content()->addWidget(m_sat);
    content()->addWidget(m_light);
    m_bars = new SpectrumBars(this);
    content()->addSpacing(6);
    content()->addWidget(m_bars);
    m_colorize = new QCheckBox(QStringLiteral("Colorize"), this);
    content()->addWidget(m_colorize);
    content()->addStretch();

    connect(m_rangeBox, &QComboBox::currentIndexChanged, this, &HueSaturationDialog::setRange);
    for (SliderField* f : {m_hue, m_sat, m_light}) connect(f, &SliderField::valueChanged, this, &HueSaturationDialog::changed);
    connect(m_colorize, &QCheckBox::toggled, this, [this](bool on) {
        m_hs.colorize = on;
        m_range = 0;
        // Colorize starts from a light red tint, as in Photoshop with the default colours.
        m_hs.ranges[0] = on ? Adjust::HueSaturation::Range{0, 25, 0} : Adjust::HueSaturation::Range{};
        sync();
        settingsChanged();
    });
    sync();
    settingsChanged();
}

void HueSaturationDialog::changed()
{
    m_hs.ranges[m_range] = {int(m_hue->value()), int(m_sat->value()), int(m_light->value())};
    m_bars->setMap(Adjust::hueSaturationMap(m_hs));
    settingsChanged();
}

void HueSaturationDialog::sync()
{
    QSignalBlocker b0(m_rangeBox), b1(m_colorize);
    m_rangeBox->setCurrentIndex(m_range);
    m_rangeBox->setEnabled(!m_hs.colorize);
    m_colorize->setChecked(m_hs.colorize);
    m_hue->setRange(m_hs.colorize ? 0 : -180, m_hs.colorize ? 360 : 180);
    m_sat->setRange(m_hs.colorize ? 0 : -100, 100);
    const auto& r = m_hs.ranges[m_range];
    m_hue->setValue(r.hue);
    m_sat->setValue(r.saturation);
    m_light->setValue(r.lightness);
    m_bars->setMap(Adjust::hueSaturationMap(m_hs));
}

void HueSaturationDialog::setSettings(const Adjust::HueSaturation& hs)
{
    m_hs = hs;
    if (m_hs.colorize) m_range = 0;
    sync();
    settingsChanged();
}

void HueSaturationDialog::setRange(int range)
{
    m_range = m_hs.colorize ? 0 : std::clamp(range, 0, 6);
    sync();
}

Filters::Spec HueSaturationDialog::spec() const
{
    return Adjust::spec(QStringLiteral("Hue/Saturation"), Adjust::hueSaturationMap(m_hs));
}

void HueSaturationDialog::accept()
{
    s_last = m_hs;
    PreviewDialog::accept();
}

// ---------------- ColorBalanceDialog ----------------

Adjust::ColorBalance ColorBalanceDialog::s_last;

ColorBalanceDialog::ColorBalanceDialog(Document* doc, bool useLast, QWidget* parent, int adjustmentLayer)
    : PreviewDialog(doc, QStringLiteral("Color Balance"), false, parent, QRect(), adjustmentLayer)
{
    if (useLast) m_cb = s_last;
    setMinimumWidth(420);
    auto* balance = new QGroupBox(QStringLiteral("Color Balance"), this);
    auto* grid = new QGridLayout(balance);
    auto* levelsRow = new QHBoxLayout;
    levelsRow->addWidget(new QLabel(QStringLiteral("Color Levels:"), balance));
    static const char* ends[3][2] = {{"Cyan", "Red"}, {"Magenta", "Green"}, {"Yellow", "Blue"}};
    for (int a = 0; a < 3; ++a) {
        m_fields[a] = intField(-100, 100, 0, balance);
        levelsRow->addWidget(m_fields[a]);
        m_sliders[a] = new QSlider(Qt::Horizontal, balance);
        m_sliders[a]->setRange(-100, 100);
        grid->addWidget(new QLabel(QString::fromLatin1(ends[a][0]), balance), a + 1, 0, Qt::AlignRight);
        grid->addWidget(m_sliders[a], a + 1, 1);
        grid->addWidget(new QLabel(QString::fromLatin1(ends[a][1]), balance), a + 1, 2);
        auto set = [this, a](int v) {
            m_cb.values[m_tone][a] = v;
            sync();
            settingsChanged();
        };
        connect(m_sliders[a], &QSlider::valueChanged, this, set);
        connect(m_fields[a], &QSpinBox::valueChanged, this, set);
    }
    levelsRow->addStretch();
    grid->addLayout(levelsRow, 0, 0, 1, 3);
    grid->setColumnStretch(1, 1);
    content()->addWidget(balance);

    auto* tone = new QGroupBox(QStringLiteral("Tone Balance"), this);
    auto* tl = new QVBoxLayout(tone);
    auto* radios = new QHBoxLayout;
    auto* group = new QButtonGroup(tone);
    const char* toneNames[3] = {"Shadows", "Midtones", "Highlights"};
    for (int t = 0; t < 3; ++t) {
        m_tones[t] = new QRadioButton(QString::fromLatin1(toneNames[t]), tone);
        group->addButton(m_tones[t], t);
        radios->addWidget(m_tones[t]);
    }
    radios->addStretch();
    tl->addLayout(radios);
    m_preserve = new QCheckBox(QStringLiteral("Preserve Luminosity"), tone);
    tl->addWidget(m_preserve);
    content()->addWidget(tone);
    content()->addStretch();

    connect(group, &QButtonGroup::idToggled, this, [this](int id, bool on) {
        if (on) setTone(id);
    });
    connect(m_preserve, &QCheckBox::toggled, this, [this](bool on) {
        m_cb.preserveLuminosity = on;
        settingsChanged();
    });
    sync();
    settingsChanged();
}

void ColorBalanceDialog::sync()
{
    QSignalBlocker b(m_preserve);
    m_preserve->setChecked(m_cb.preserveLuminosity);
    {
        QSignalBlocker bt(m_tones[m_tone]);
        m_tones[m_tone]->setChecked(true);
    }
    for (int a = 0; a < 3; ++a) {
        QSignalBlocker b1(m_sliders[a]), b2(m_fields[a]);
        m_sliders[a]->setValue(m_cb.values[m_tone][a]);
        m_fields[a]->setValue(m_cb.values[m_tone][a]);
    }
}

void ColorBalanceDialog::setSettings(const Adjust::ColorBalance& cb)
{
    m_cb = cb;
    sync();
    settingsChanged();
}

void ColorBalanceDialog::setTone(int tone)
{
    m_tone = std::clamp(tone, 0, 2);
    sync();
}

Filters::Spec ColorBalanceDialog::spec() const
{
    return Adjust::spec(QStringLiteral("Color Balance"), Adjust::colorBalanceMap(m_cb));
}

void ColorBalanceDialog::accept()
{
    s_last = m_cb;
    PreviewDialog::accept();
}

// ---------------- ThresholdDialog ----------------

int ThresholdDialog::s_last = 128;

ThresholdDialog::ThresholdDialog(Document* doc, QWidget* parent, int adjustmentLayer)
    : PreviewDialog(doc, QStringLiteral("Threshold"), false, parent, QRect(), adjustmentLayer)
{
    m_level = intField(1, 255, s_last, this);
    content()->addLayout(labelled(QStringLiteral("Threshold Level:"), m_level, this));
    auto* hist = new HistogramView(this);
    hist->setData(sourceHistogram().channels[0], Theme::kText);
    content()->addWidget(hist);
    m_bar = new LevelsBar(1, this);
    m_bar->setValues({double(s_last)});
    content()->addWidget(m_bar);
    content()->addStretch();
    connect(m_bar, &LevelsBar::moved, this, [this](int, double v) { setLevel(int(std::lround(v))); });
    connect(m_level, &QSpinBox::valueChanged, this, [this](int v) { setLevel(v); });
    settingsChanged();
}

int ThresholdDialog::level() const { return m_level->value(); }

void ThresholdDialog::setLevel(int level)
{
    level = std::clamp(level, 1, 255);
    QSignalBlocker b(m_level);
    m_level->setValue(level);
    m_bar->setValues({double(level)});
    settingsChanged();
}

Filters::Spec ThresholdDialog::spec() const
{
    return Adjust::spec(QStringLiteral("Threshold"), Adjust::thresholdMap(m_level->value()));
}

void ThresholdDialog::accept()
{
    s_last = m_level->value();
    PreviewDialog::accept();
}

// ---------------- FadeDialog ----------------

FadeDialog::FadeDialog(Document* doc, const QString& name, const Filters::Applied& faded, QWidget* parent)
    : PreviewDialog(doc, QStringLiteral("Fade"), true, parent, faded.target)
    , m_name(name)
    , m_before(faded.before)
{
    // The faded command already limited itself to the selection.
    if (session()) session()->ignoreSelection();
    setMinimumWidth(360);
    m_opacity = new SliderField(QStringLiteral("Opacity:"), 0, 100, 100, 0, QStringLiteral("%"), 0, this);
    content()->addWidget(m_opacity);
    m_mode = new BlendModeCombo(this);
    content()->addLayout(labelled(QStringLiteral("Mode:"), m_mode, this));
    content()->addStretch();
    connect(m_opacity, &SliderField::valueChanged, this, [this] { settingsChanged(); });
    connect(m_mode, &BlendModeCombo::modeChanged, this, [this] { settingsChanged(); });
    settingsChanged();
}

void FadeDialog::setOpacity(int percent)
{
    m_opacity->setValue(percent);
    settingsChanged();
}

Filters::Spec FadeDialog::spec() const
{
    return Filters::fadeSpec(QStringLiteral("Fade ") + m_name, m_before, int(m_mode->mode()), m_opacity->value() / 100.0);
}

// ---------------- Adjustment layer settings ----------------

std::shared_ptr<const Adjust::LayerSettings> LevelsDialog::layerSettings() const
{
    Adjust::LayerSettings s;
    s.kind = Adjust::Kind::Levels;
    s.levels = m_levels;
    return s.finalized();
}

std::shared_ptr<const Adjust::LayerSettings> CurvesDialog::layerSettings() const
{
    Adjust::LayerSettings s;
    s.kind = Adjust::Kind::Curves;
    s.curves = m_curves;
    return s.finalized();
}

std::shared_ptr<const Adjust::LayerSettings> HueSaturationDialog::layerSettings() const
{
    Adjust::LayerSettings s;
    s.kind = Adjust::Kind::HueSaturation;
    s.hueSaturation = m_hs;
    return s.finalized();
}

std::shared_ptr<const Adjust::LayerSettings> ColorBalanceDialog::layerSettings() const
{
    Adjust::LayerSettings s;
    s.kind = Adjust::Kind::ColorBalance;
    s.colorBalance = m_cb;
    return s.finalized();
}

std::shared_ptr<const Adjust::LayerSettings> ThresholdDialog::layerSettings() const
{
    Adjust::LayerSettings s;
    s.kind = Adjust::Kind::Threshold;
    s.thresholdLevel = level();
    return s.finalized();
}
