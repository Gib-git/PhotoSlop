#pragma once

#include <QList>
#include <QObject>
#include <QPointer>

#include "ui/CanvasView.h"

class ColorState;
class Tool;

// Owns all tools, the current selection, tool groups (toolbox flyouts) and
// temporary tool switching (Space = Hand, Alt = Eyedropper, Ctrl = Move).
class ToolManager : public QObject {
    Q_OBJECT
public:
    struct Group {
        QList<Tool*> tools;
        Tool* last = nullptr;
    };

    explicit ToolManager(ColorState* colors, QObject* parent = nullptr);

    // Adds a tool; tools added with the same group index share a toolbox button.
    void addTool(Tool* tool, int group);
    const QList<Group>& groups() const { return m_groups; }
    int groupOf(Tool* tool) const;
    Tool* tool(const QString& id) const;
    Tool* current() const { return m_current; }
    ColorState* colors() const { return m_colors; }

    void select(const QString& id);
    void select(Tool* tool);
    // Letter shortcut: selects the group's last used tool, or cycles with `cycle`.
    bool selectByShortcut(QChar key, bool cycle);

    void pushTemporary(const QString& id);
    void popTemporary();
    bool hasTemporary() const { return m_beforeTemporary != nullptr; }

    void setActiveView(CanvasView* view);
    CanvasView* activeView() const { return m_view; }
    // True while a mouse button is held on the canvas (blocks tool switching).
    void setBusy(bool busy) { m_busy = busy; }
    bool isBusy() const { return m_busy; }

signals:
    void currentChanged(Tool* tool);
    void groupToolChanged(int group);
    void alertRequested(const QString& message);

private:
    void activate(Tool* tool);

    ColorState* m_colors;
    QList<Group> m_groups;
    QList<Tool*> m_tools;
    Tool* m_current = nullptr;
    Tool* m_beforeTemporary = nullptr;
    QPointer<CanvasView> m_view;
    bool m_busy = false;
};
