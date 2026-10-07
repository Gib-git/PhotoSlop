#pragma once

#include <QToolButton>
#include <QWidget>

class ColorState;
class QTimer;
class ToolManager;

// One toolbox slot; shows the group's last-used tool and a flyout of the others.
class ToolGroupButton : public QToolButton {
    Q_OBJECT
public:
    ToolGroupButton(ToolManager* manager, int group, QWidget* parent = nullptr);
    void refresh();

protected:
    void paintEvent(QPaintEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void contextMenuEvent(QContextMenuEvent* e) override;

private:
    void showFlyout();
    ToolManager* m_manager;
    int m_group;
    QTimer* m_holdTimer;
    bool m_flyoutShown = false;
};

// Foreground / background colour squares with swap and default-colours buttons.
class ColorSwatchWidget : public QWidget {
    Q_OBJECT
public:
    explicit ColorSwatchWidget(ColorState* colors, QWidget* parent = nullptr);
    QSize sizeHint() const override { return QSize(44, 44); }

protected:
    void paintEvent(QPaintEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;

private:
    QRect fgRect() const { return QRect(5, 5, 22, 22); }
    QRect bgRect() const { return QRect(17, 17, 22, 22); }
    QRect swapRect() const { return QRect(29, 3, 13, 13); }
    QRect defaultRect() const { return QRect(3, 30, 12, 12); }
    ColorState* m_colors;
};

class ToolBox : public QWidget {
    Q_OBJECT
public:
    ToolBox(ToolManager* manager, ColorState* colors, QWidget* parent = nullptr);

signals:
    void screenModeRequested();

private:
    ToolManager* m_manager;
};
