#include "ui/ToolBox.h"

#include "app/Theme.h"
#include "core/ColorState.h"
#include "tools/Tool.h"
#include "tools/ToolManager.h"
#include "ui/dialogs/ColorPickerDialog.h"

#include <QContextMenuEvent>
#include <QFrame>
#include <QMenu>
#include <QPainter>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <iterator>

// ---------------- ToolGroupButton ----------------

ToolGroupButton::ToolGroupButton(ToolManager* manager, int group, QWidget* parent)
    : QToolButton(parent)
    , m_manager(manager)
    , m_group(group)
    , m_holdTimer(new QTimer(this))
{
    setCheckable(true);
    setAutoRaise(true);
    setFixedSize(34, 30);
    setIconSize(QSize(20, 20));
    m_holdTimer->setSingleShot(true);
    m_holdTimer->setInterval(350);
    connect(m_holdTimer, &QTimer::timeout, this, [this] {
        m_flyoutShown = true;
        showFlyout();
    });
    connect(manager, &ToolManager::currentChanged, this, &ToolGroupButton::refresh);
    connect(manager, &ToolManager::groupToolChanged, this, [this](int g) {
        if (g == m_group) refresh();
    });
    refresh();
}

void ToolGroupButton::refresh()
{
    const auto& grp = m_manager->groups()[m_group];
    Tool* shown = grp.last;
    if (grp.tools.contains(m_manager->current())) shown = m_manager->current();
    setIcon(Theme::icon(shown->iconName()));
    setToolTip(QStringLiteral("%1 (%2)").arg(shown->name(), shown->shortcut()));
    setChecked(grp.tools.contains(m_manager->current()));
}

void ToolGroupButton::paintEvent(QPaintEvent* e)
{
    QToolButton::paintEvent(e);
    if (m_manager->groups()[m_group].tools.size() < 2) return;
    // Small flyout triangle in the bottom-right corner.
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0xb0, 0xb0, 0xb0));
    const QPointF br(width() - 4, height() - 4);
    p.drawPolygon(QPolygonF{br, br - QPointF(4, 0), br - QPointF(0, 4)});
}

void ToolGroupButton::mousePressEvent(QMouseEvent* e)
{
    m_flyoutShown = false;
    if (e->button() == Qt::LeftButton && m_manager->groups()[m_group].tools.size() > 1) m_holdTimer->start();
    QToolButton::mousePressEvent(e);
}

void ToolGroupButton::mouseReleaseEvent(QMouseEvent* e)
{
    m_holdTimer->stop();
    QToolButton::mouseReleaseEvent(e);
    if (e->button() == Qt::LeftButton && !m_flyoutShown && rect().contains(e->position().toPoint()))
        m_manager->select(m_manager->groups()[m_group].last);
    refresh();
}

void ToolGroupButton::mouseDoubleClickEvent(QMouseEvent*)
{
    if (Tool* t = m_manager->current()) t->toolButtonDoubleClicked();
}

void ToolGroupButton::contextMenuEvent(QContextMenuEvent*) { showFlyout(); }

void ToolGroupButton::showFlyout()
{
    const auto& grp = m_manager->groups()[m_group];
    if (grp.tools.size() < 2) return;
    QMenu menu(this);
    for (Tool* t : grp.tools) {
        QAction* a = menu.addAction(Theme::icon(t->iconName()), t->name() + QLatin1Char('\t') + t->shortcut());
        a->setCheckable(true);
        a->setChecked(t == grp.last);
        connect(a, &QAction::triggered, this, [this, t] { m_manager->select(t); });
    }
    menu.exec(mapToGlobal(QPoint(width(), 0)));
    setDown(false);
    refresh();
}

// ---------------- ColorSwatchWidget ----------------

ColorSwatchWidget::ColorSwatchWidget(ColorState* colors, QWidget* parent)
    : QWidget(parent)
    , m_colors(colors)
{
    setFixedSize(44, 44);
    setToolTip(QStringLiteral("Set foreground / background color"));
    connect(colors, &ColorState::changed, this, qOverload<>(&QWidget::update));
}

void ColorSwatchWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    auto swatch = [&](const QRect& r, const QColor& c) {
        p.fillRect(r, c);
        p.setPen(QColor(0x1e, 0x1e, 0x1e));
        p.drawRect(r.adjusted(0, 0, -1, -1));
        p.setPen(QColor(0xd6, 0xd6, 0xd6));
        p.drawRect(r.adjusted(1, 1, -2, -2));
    };
    swatch(bgRect(), m_colors->background());
    swatch(fgRect(), m_colors->foreground());
    Theme::icon(QStringLiteral("swap")).paint(&p, swapRect());
    Theme::icon(QStringLiteral("default-colors")).paint(&p, defaultRect());
}

void ColorSwatchWidget::mousePressEvent(QMouseEvent* e)
{
    const QPoint pt = e->position().toPoint();
    if (swapRect().contains(pt)) {
        m_colors->swap();
    } else if (defaultRect().contains(pt)) {
        m_colors->reset();
    } else if (fgRect().contains(pt)) {
        QColor c = ColorPickerDialog::getColor(m_colors->foreground(), QStringLiteral("Color Picker (Foreground Color)"), window());
        if (c.isValid()) m_colors->setForeground(c);
    } else if (bgRect().contains(pt)) {
        QColor c = ColorPickerDialog::getColor(m_colors->background(), QStringLiteral("Color Picker (Background Color)"), window());
        if (c.isValid()) m_colors->setBackground(c);
    }
}

// ---------------- ToolBox ----------------

ToolBox::ToolBox(ToolManager* manager, ColorState* colors, QWidget* parent)
    : QWidget(parent)
    , m_manager(manager)
{
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(2, 0, 2, 6);
    lay->setSpacing(1);

    // Header strip (Photoshop's collapse handle).
    auto* header = new QFrame(this);
    header->setFixedHeight(10);
    header->setStyleSheet(QStringLiteral("background: #282828;"));
    lay->addWidget(header);
    lay->addSpacing(4);

    // Separators after these group indices mirror Photoshop's toolbar sections.
    const QList<int> separatorsAfter = {0, 4, 7};
    const auto& groups = manager->groups();
    for (int g = 0; g < groups.size(); ++g) {
        lay->addWidget(new ToolGroupButton(manager, g, this), 0, Qt::AlignHCenter);
        if (separatorsAfter.contains(g)) {
            auto* sep = new QFrame(this);
            sep->setFixedHeight(1);
            sep->setStyleSheet(QStringLiteral("background: #464646; margin: 0 6px;"));
            lay->addSpacing(3);
            lay->addWidget(sep);
            lay->addSpacing(3);
        }
    }
    lay->addSpacing(8);
    lay->addWidget(new ColorSwatchWidget(colors, this), 0, Qt::AlignHCenter);
    lay->addSpacing(6);

    auto* quickMask = new QToolButton(this);
    quickMask->setIcon(Theme::icon(QStringLiteral("quickmask")));
    quickMask->setToolTip(QStringLiteral("Edit in Quick Mask Mode (Q)"));
    quickMask->setFixedSize(34, 28);
    quickMask->setIconSize(QSize(18, 18));
    quickMask->setEnabled(false);
    lay->addWidget(quickMask, 0, Qt::AlignHCenter);

    auto* screen = new QToolButton(this);
    screen->setIcon(Theme::icon(QStringLiteral("screenmode")));
    screen->setToolTip(QStringLiteral("Change Screen Mode (F)"));
    screen->setFixedSize(34, 28);
    screen->setIconSize(QSize(18, 18));
    connect(screen, &QToolButton::clicked, this, &ToolBox::screenModeRequested);
    lay->addWidget(screen, 0, Qt::AlignHCenter);
    lay->addStretch();
}
