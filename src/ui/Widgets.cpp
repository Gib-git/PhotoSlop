#include "ui/Widgets.h"

#include "app/Theme.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QSignalBlocker>
#include <QSlider>
#include <QStandardItemModel>
#include <algorithm>
#include <iterator>

// ---------------- ScrubbyLabel ----------------

ScrubbyLabel::ScrubbyLabel(const QString& text, QSpinBox* target, QWidget* parent)
    : QLabel(text, parent)
    , m_target(target)
{
    setCursor(Qt::SizeHorCursor);
}

void ScrubbyLabel::mousePressEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton || !m_target) return;
    m_dragging = true;
    m_startX = int(e->globalPosition().x());
    m_startValue = m_target->value();
}

void ScrubbyLabel::mouseMoveEvent(QMouseEvent* e)
{
    if (!m_dragging) return;
    int dx = int(e->globalPosition().x()) - m_startX;
    // Shift scrubs ten times faster, like Photoshop.
    int step = (e->modifiers() & Qt::ShiftModifier) ? 10 : 1;
    m_target->setValue(m_startValue + dx * step);
}

void ScrubbyLabel::mouseReleaseEvent(QMouseEvent*) { m_dragging = false; }

// ---------------- ValueField ----------------

ValueField::ValueField(const QString& label, int min, int max, const QString& suffix,
                       bool popupSlider, QWidget* parent)
    : QWidget(parent)
{
    auto* lay = new QHBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(4);
    m_spin = new QSpinBox(this);
    m_spin->setRange(min, max);
    m_spin->setSuffix(suffix);
    m_spin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    m_spin->setAlignment(Qt::AlignRight);
    m_spin->setFixedWidth(52);
    m_spin->setKeyboardTracking(false);
    if (!label.isEmpty()) lay->addWidget(new ScrubbyLabel(label, m_spin, this));
    lay->addWidget(m_spin);
    if (popupSlider) {
        m_drop = new QToolButton(this);
        m_drop->setArrowType(Qt::DownArrow);
        m_drop->setFixedSize(14, 20);
        m_drop->setStyleSheet(QStringLiteral("QToolButton { border: none; }"));
        connect(m_drop, &QToolButton::clicked, this, &ValueField::showPopup);
        lay->addWidget(m_drop);
        lay->setSpacing(1);
    }
    connect(m_spin, &QSpinBox::valueChanged, this, &ValueField::valueChanged);
}

int ValueField::value() const { return m_spin->value(); }

void ValueField::setValue(int v)
{
    QSignalBlocker b(m_spin);
    m_spin->setValue(v);
}

void ValueField::setFieldWidth(int w) { m_spin->setFixedWidth(w); }

void ValueField::showPopup()
{
    auto* popup = new QFrame(this, Qt::Popup);
    popup->setAttribute(Qt::WA_DeleteOnClose);
    popup->setStyleSheet(QStringLiteral("QFrame { background: #424242; border: 1px solid #1e1e1e; }"));
    auto* lay = new QHBoxLayout(popup);
    lay->setContentsMargins(8, 6, 8, 6);
    auto* slider = new QSlider(Qt::Horizontal, popup);
    slider->setRange(m_spin->minimum(), m_spin->maximum());
    slider->setValue(m_spin->value());
    slider->setFixedWidth(150);
    lay->addWidget(slider);
    connect(slider, &QSlider::valueChanged, m_spin, &QSpinBox::setValue);
    popup->adjustSize();
    popup->move(m_spin->mapToGlobal(QPoint(0, m_spin->height())));
    popup->show();
}

// ---------------- BlendModeCombo ----------------

BlendModeCombo::BlendModeCombo(QWidget* parent, bool)
    : QComboBox(parent)
{
    const auto groups = Blend::groups();
    for (int g = 0; g < groups.size(); ++g) {
        if (g > 0) insertSeparator(count());
        for (BlendMode m : groups[g]) addItem(Blend::name(m), int(m));
    }
    setMaxVisibleItems(40);
    setSizeAdjustPolicy(QComboBox::AdjustToContents);
    connect(this, &QComboBox::currentIndexChanged, this, [this] { emit modeChanged(mode()); });
}

BlendMode BlendModeCombo::mode() const
{
    QVariant v = currentData();
    return v.isValid() ? BlendMode(v.toInt()) : BlendMode::Normal;
}

void BlendModeCombo::setMode(BlendMode mode)
{
    QSignalBlocker b(this);
    setCurrentIndex(findData(int(mode)));
}

// ---------------- helpers ----------------

QToolButton* makeIconButton(const QString& icon, const QString& tooltip, QWidget* parent,
                            bool checkable, int size)
{
    auto* b = new QToolButton(parent);
    b->setIcon(Theme::icon(icon));
    b->setToolTip(tooltip);
    b->setCheckable(checkable);
    b->setAutoRaise(true);
    b->setFixedSize(size, size);
    b->setIconSize(QSize(size - 6, size - 6));
    return b;
}

QWidget* makeVSeparator(QWidget* parent)
{
    auto* f = new QFrame(parent);
    f->setFrameShape(QFrame::VLine);
    f->setFixedWidth(1);
    f->setFixedHeight(20);
    f->setStyleSheet(QStringLiteral("background: #4a4a4a; border: none;"));
    return f;
}
