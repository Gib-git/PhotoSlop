#include "ui/Workspace.h"

#include "app/Theme.h"

#include <QAction>
#include <QEvent>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QPushButton>
#include <QStackedWidget>
#include <QSvgRenderer>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <iterator>

QPixmap logoPixmap(int width, qreal dpr)
{
    QSvgRenderer svg(QStringLiteral(":/logo.svg"));
    const QSizeF def = svg.defaultSize();
    const QSize size(width, int(width * def.height() / def.width()));
    QPixmap pm(size * dpr);
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    svg.render(&p, QRectF(QPointF(0, 0), QSizeF(size)));
    return pm;
}

// ---------------- HomeScreen ----------------

HomeScreen::HomeScreen(QWidget* parent)
    : QWidget(parent)
{
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, QColor(0x26, 0x26, 0x26));
    setPalette(pal);

    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(40, 40, 40, 40);
    root->setSpacing(40);

    auto* left = new QVBoxLayout;
    auto* newBtn = new QPushButton(QStringLiteral("New file"), this);
    newBtn->setDefault(true);
    auto* openBtn = new QPushButton(QStringLiteral("Open"), this);
    for (QPushButton* b : {newBtn, openBtn}) {
        b->setMinimumWidth(150);
        b->setMinimumHeight(32);
        left->addWidget(b);
    }
    left->addStretch();
    root->addLayout(left);

    auto* center = new QVBoxLayout;
    auto* card = new QLabel(this);
    card->setAlignment(Qt::AlignCenter);
    card->setPixmap(logoPixmap(260, devicePixelRatioF()));
    card->setStyleSheet(QStringLiteral("background: white; border-radius: 12px; padding: 18px;"));
    card->setFixedWidth(320);
    auto* slogan = new QLabel(kSlogan, this);
    slogan->setAlignment(Qt::AlignCenter);
    slogan->setStyleSheet(QStringLiteral("color: #bdbdbd; font-size: 15px; font-style: italic; margin-top: 10px;"));
    auto* welcome = new QLabel(QStringLiteral("Welcome to PhotoSlop"), this);
    welcome->setStyleSheet(QStringLiteral("color: #f0f0f0; font-size: 26px; font-weight: 300; margin-bottom: 14px;"));
    welcome->setAlignment(Qt::AlignCenter);
    center->addStretch();
    center->addWidget(welcome);
    center->addWidget(card, 0, Qt::AlignHCenter);
    center->addWidget(slogan);
    auto* recentTitle = new QLabel(QStringLiteral("Recent"), this);
    recentTitle->setStyleSheet(QStringLiteral("color: #d0d0d0; font-size: 15px; margin-top: 24px;"));
    center->addWidget(recentTitle);
    m_recent = new QListWidget(this);
    m_recent->setMaximumHeight(180);
    m_recent->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_recent->setTextElideMode(Qt::ElideMiddle);
    m_recent->setStyleSheet(QStringLiteral("QListWidget { background: #2b2b2b; border-radius: 4px; padding: 4px; }"
                                           "QListWidget::item { padding: 5px; } QListWidget::item:hover { background: #3a3a3a; }"));
    center->addWidget(m_recent);
    center->addStretch();
    root->addLayout(center, 1);
    root->addSpacing(150);

    connect(newBtn, &QPushButton::clicked, this, &HomeScreen::newRequested);
    connect(openBtn, &QPushButton::clicked, this, &HomeScreen::openRequested);
    connect(m_recent, &QListWidget::itemActivated, this, [this](QListWidgetItem* item) {
        emit recentRequested(item->data(Qt::UserRole).toString());
    });
    connect(m_recent, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        emit recentRequested(item->data(Qt::UserRole).toString());
    });
}

void HomeScreen::setRecentFiles(const QStringList& files)
{
    m_recent->clear();
    for (const QString& f : files) {
        auto* item = new QListWidgetItem(QStringLiteral("%1    —    %2").arg(QFileInfo(f).fileName(), QFileInfo(f).absolutePath()), m_recent);
        item->setData(Qt::UserRole, f);
        item->setToolTip(f);
    }
    if (files.isEmpty()) {
        auto* item = new QListWidgetItem(QStringLiteral("No recent files"), m_recent);
        item->setFlags(Qt::NoItemFlags);
    }
}

// ---------------- PanelStrip ----------------

PanelStrip::PanelStrip(QWidget* flyoutHost, QWidget* parent)
    : QWidget(parent)
    , m_host(flyoutHost)
{
    setFixedWidth(36);
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(2, 6, 2, 6);
    lay->setSpacing(2);
    lay->addStretch();

    m_flyout = new QFrame(flyoutHost);
    m_flyout->setObjectName(QStringLiteral("Flyout"));
    m_flyout->setStyleSheet(QStringLiteral("QFrame#Flyout { background: #323232; border: 1px solid #1a1a1a; }"));
    auto* fl = new QVBoxLayout(m_flyout);
    fl->setContentsMargins(0, 0, 0, 0);
    fl->setSpacing(0);
    auto* header = new QWidget(m_flyout);
    header->setStyleSheet(QStringLiteral("background: #282828;"));
    auto* hl = new QHBoxLayout(header);
    hl->setContentsMargins(10, 4, 4, 4);
    m_flyoutTitle = new QLabel(header);
    m_flyoutTitle->setStyleSheet(QStringLiteral("color: #f0f0f0;"));
    auto* collapse = new QToolButton(header);
    collapse->setIcon(Theme::icon(QStringLiteral("collapse")));
    collapse->setToolTip(QStringLiteral("Collapse to Icons"));
    collapse->setAutoRaise(true);
    connect(collapse, &QToolButton::clicked, this, &PanelStrip::hideFlyout);
    hl->addWidget(m_flyoutTitle);
    hl->addStretch();
    hl->addWidget(collapse);
    fl->addWidget(header);
    m_stack = new QStackedWidget(m_flyout);
    fl->addWidget(m_stack, 1);
    m_flyout->hide();
    flyoutHost->installEventFilter(this);
}

QAction* PanelStrip::addPanel(const QIcon& icon, const QString& title, QWidget* panel, const QSize& size)
{
    auto* b = new QToolButton(this);
    b->setIcon(icon);
    b->setIconSize(QSize(20, 20));
    b->setFixedSize(32, 30);
    b->setCheckable(true);
    b->setToolTip(title);
    auto* lay = static_cast<QVBoxLayout*>(layout());
    lay->insertWidget(lay->count() - 1, b);
    auto* action = new QAction(title, this);
    action->setCheckable(true);
    m_stack->addWidget(panel);
    const int index = int(m_entries.size());
    m_entries.append({b, action, panel, title, size});
    connect(b, &QToolButton::clicked, this, [this, index] { toggle(index); });
    connect(action, &QAction::triggered, this, [this, index] { toggle(index); });
    return action;
}

void PanelStrip::toggle(int index)
{
    if (m_flyout->isVisible() && m_current == index) {
        hideFlyout();
        return;
    }
    m_current = index;
    for (int i = 0; i < m_entries.size(); ++i) {
        m_entries[i].button->setChecked(i == index);
        m_entries[i].action->setChecked(i == index);
    }
    m_stack->setCurrentWidget(m_entries[index].panel);
    m_flyoutTitle->setText(m_entries[index].title);
    place();
    m_flyout->show();
    m_flyout->raise();
}

void PanelStrip::hideFlyout()
{
    m_flyout->hide();
    for (auto& e : m_entries) {
        e.button->setChecked(false);
        e.action->setChecked(false);
    }
}

void PanelStrip::place()
{
    if (m_current < 0) return;
    const Entry& e = m_entries[m_current];
    const QPoint topRight = mapTo(m_host, QPoint(0, e.button->y()));
    const int h = std::min(e.size.height(), m_host->height() - topRight.y() - 30);
    m_flyout->setGeometry(topRight.x() - e.size.width() - 2, topRight.y(), e.size.width(), std::max(120, h));
}

bool PanelStrip::eventFilter(QObject* obj, QEvent* e)
{
    if (obj == m_host && e->type() == QEvent::Resize && m_flyout->isVisible()) place();
    return QWidget::eventFilter(obj, e);
}
