#include "app/Theme.h"

#include "app/Preferences.h"

#include <QApplication>
#include <QHash>
#include <QPainter>
#include <QPalette>
#include <QProxyStyle>
#include <QStyleFactory>
#include <QStyleOption>
#include <algorithm>
#include <iterator>

namespace Theme {

namespace {

class PhotoSlopStyle : public QProxyStyle {
public:
    PhotoSlopStyle() : QProxyStyle(QStyleFactory::create(QStringLiteral("Fusion"))) {}

    QPixmap generatedIconPixmap(QIcon::Mode mode, const QPixmap& pixmap,
                                const QStyleOption* opt) const override
    {
        if (mode == QIcon::Disabled) {
            // Light icons on a dark UI: fade them rather than greying them out.
            QPixmap out(pixmap.size());
            out.setDevicePixelRatio(pixmap.devicePixelRatio());
            out.fill(Qt::transparent);
            QPainter p(&out);
            p.setOpacity(0.3);
            p.drawPixmap(0, 0, pixmap);
            return out;
        }
        return QProxyStyle::generatedIconPixmap(mode, pixmap, opt);
    }

    int pixelMetric(PixelMetric metric, const QStyleOption* option,
                    const QWidget* widget) const override
    {
        switch (metric) {
        case PM_SmallIconSize: return 16;
        case PM_ToolBarIconSize: return 18;
        case PM_DockWidgetSeparatorExtent: return 2;
        case PM_ToolBarItemSpacing: return 2;
        default: return QProxyStyle::pixelMetric(metric, option, widget);
        }
    }

    int styleHint(StyleHint hint, const QStyleOption* option, const QWidget* widget,
                  QStyleHintReturn* returnData) const override
    {
        if (hint == SH_ToolButton_PopupDelay) return 300;
        if (hint == SH_Slider_AbsoluteSetButtons) return Qt::LeftButton;
        return QProxyStyle::styleHint(hint, option, widget, returnData);
    }
};

QString styleSheet()
{
    return QStringLiteral(R"(
QMainWindow, QDialog { background: #323232; }
QMainWindow::separator { background: #1e1e1e; width: 2px; height: 2px; }
QWidget { color: #d6d6d6; }
QToolTip { background: #464646; color: #e8e8e8; border: 1px solid #1e1e1e; padding: 3px 6px; }

QMenuBar { background: #3a3a3a; border-bottom: 1px solid #222; }
QMenuBar::item { padding: 4px 9px; background: transparent; }
QMenuBar::item:selected { background: #535353; }
QMenu { background: #424242; border: 1px solid #1e1e1e; padding: 4px 0; }
QMenu::item { padding: 4px 28px 4px 24px; }
QMenu::item:selected { background: #1473e6; color: white; }
QMenu::item:disabled { color: #7a7a7a; }
QMenu::separator { height: 1px; background: #5a5a5a; margin: 4px 0; }
QMenu::indicator { width: 12px; height: 12px; left: 6px; }

QToolBar { background: #323232; border: none; spacing: 2px; padding: 2px; }
QToolBar#OptionsBar { border-bottom: 1px solid #1e1e1e; padding: 3px 6px; }
QToolBar#ToolsBar { border-right: 1px solid #1e1e1e; padding: 0; }
QToolBar::separator { background: #4a4a4a; width: 1px; height: 1px; margin: 4px 5px; }

QToolButton { background: transparent; border: 1px solid transparent; border-radius: 2px; padding: 2px; }
QToolButton:hover { background: #474747; }
QToolButton:checked { background: #262626; border-color: #1e1e1e; }
QToolButton:pressed { background: #262626; }
QToolButton:disabled { color: #6a6a6a; }

QDockWidget { titlebar-close-icon: none; titlebar-normal-icon: none; }
QDockWidget > QWidget { background: #323232; }

QTabBar { background: #282828; }
QTabBar::tab { background: #282828; color: #9a9a9a; padding: 5px 12px; border: none; border-right: 1px solid #1e1e1e; }
QTabBar::tab:selected { background: #323232; color: #f0f0f0; }
QTabBar::tab:hover:!selected { color: #d0d0d0; }
QTabBar::close-button { image: none; subcontrol-position: right; }

QTabWidget#DocumentTabs::pane { border: none; background: #282828; }
QTabWidget#DocumentTabs > QTabBar { background: #282828; }
QTabWidget#DocumentTabs > QTabBar::tab { background: #323232; color: #a0a0a0; padding: 5px 14px; border-right: 1px solid #1e1e1e; border-bottom: 1px solid #1e1e1e; min-width: 80px; }
QTabWidget#DocumentTabs > QTabBar::tab:selected { background: #424242; color: #f0f0f0; border-bottom: 1px solid #424242; }

QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox, QPlainTextEdit {
    background: #454545; border: 1px solid #2a2a2a; border-radius: 2px; padding: 2px 4px;
    selection-background-color: #1473e6; min-height: 16px;
}
QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus { border-color: #1473e6; }
QLineEdit:disabled, QSpinBox:disabled, QComboBox:disabled { color: #707070; background: #3a3a3a; }
QSpinBox::up-button, QSpinBox::down-button, QDoubleSpinBox::up-button, QDoubleSpinBox::down-button { width: 0; border: none; }
QComboBox { padding-right: 16px; }
QComboBox::drop-down { border: none; width: 16px; }
QComboBox::down-arrow { image: none; border-left: 4px solid transparent; border-right: 4px solid transparent; border-top: 5px solid #bdbdbd; width: 0; height: 0; margin-right: 6px; }
QComboBox QAbstractItemView { background: #424242; border: 1px solid #1e1e1e; selection-background-color: #1473e6; outline: none; }

QPushButton { background: #535353; border: 1px solid #262626; border-radius: 3px; padding: 4px 14px; min-width: 60px; }
QPushButton:hover { background: #5e5e5e; }
QPushButton:pressed { background: #444; }
QPushButton:default { background: #1473e6; border-color: #0d5bbd; color: white; }
QPushButton:default:hover { background: #2680eb; }
QPushButton:disabled { color: #777; background: #444; }

QCheckBox, QRadioButton { spacing: 6px; }
QCheckBox::indicator, QRadioButton::indicator { width: 12px; height: 12px; background: #454545; border: 1px solid #222; }
QRadioButton::indicator { border-radius: 7px; }
QCheckBox::indicator:checked { background: #1473e6; border-color: #0d5bbd; image: none; }
QRadioButton::indicator:checked { background: qradialgradient(cx:0.5, cy:0.5, radius:0.5, fx:0.5, fy:0.5, stop:0 white, stop:0.35 white, stop:0.45 #1473e6, stop:1 #1473e6); }

QSlider::groove:horizontal { height: 3px; background: #1e1e1e; border-radius: 1px; }
QSlider::handle:horizontal { background: #d6d6d6; width: 10px; height: 10px; margin: -4px 0; border-radius: 5px; }
QSlider::sub-page:horizontal { background: #8a8a8a; }

QScrollBar:vertical { background: #2d2d2d; width: 10px; margin: 0; }
QScrollBar:horizontal { background: #2d2d2d; height: 10px; margin: 0; }
QScrollBar::handle { background: #5a5a5a; border-radius: 4px; min-height: 20px; min-width: 20px; margin: 1px; }
QScrollBar::handle:hover { background: #6a6a6a; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: none; }

QListView, QTreeView, QUndoView { background: #323232; border: none; outline: none; }
QListView::item:selected, QUndoView::item:selected { background: #4b4b4b; color: white; }

QGroupBox { border: 1px solid #4a4a4a; border-radius: 3px; margin-top: 10px; padding-top: 6px; }
QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 3px; }

QStatusBar, QWidget#DocStatusBar { background: #323232; color: #b0b0b0; border-top: 1px solid #1e1e1e; }
QLabel:disabled { color: #707070; }
)");
}

} // namespace

void apply(QApplication& app)
{
    app.setStyle(new PhotoSlopStyle);

    QPalette pal;
    pal.setColor(QPalette::Window, kPanel);
    pal.setColor(QPalette::WindowText, kText);
    pal.setColor(QPalette::Base, kField);
    pal.setColor(QPalette::AlternateBase, kPanel);
    pal.setColor(QPalette::Text, kText);
    pal.setColor(QPalette::Button, QColor(0x53, 0x53, 0x53));
    pal.setColor(QPalette::ButtonText, kText);
    pal.setColor(QPalette::Highlight, kAccent);
    pal.setColor(QPalette::HighlightedText, Qt::white);
    pal.setColor(QPalette::ToolTipBase, QColor(0x46, 0x46, 0x46));
    pal.setColor(QPalette::ToolTipText, kText);
    pal.setColor(QPalette::PlaceholderText, kTextDim);
    pal.setColor(QPalette::Mid, QColor(0x1e, 0x1e, 0x1e));
    pal.setColor(QPalette::Dark, QColor(0x1e, 0x1e, 0x1e));
    pal.setColor(QPalette::Light, QColor(0x5a, 0x5a, 0x5a));
    pal.setColor(QPalette::Disabled, QPalette::Text, QColor(0x70, 0x70, 0x70));
    pal.setColor(QPalette::Disabled, QPalette::WindowText, QColor(0x70, 0x70, 0x70));
    pal.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(0x70, 0x70, 0x70));
    app.setPalette(pal);

    QFont f = app.font();
    f.setPixelSize(Preferences::instance().uiFontPixels());
    app.setFont(f);

    app.setStyleSheet(styleSheet());
}

QIcon icon(const QString& name)
{
    static QHash<QString, QIcon> cache;
    auto it = cache.find(name);
    if (it != cache.end()) return *it;
    QIcon ic(QStringLiteral(":/icons/%1.svg").arg(name));
    cache.insert(name, ic);
    return ic;
}

} // namespace Theme
