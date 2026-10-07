#include "tools/ToolManager.h"

#include "tools/Tool.h"
#include "ui/CanvasView.h"
#include <algorithm>
#include <iterator>

ToolManager::ToolManager(ColorState* colors, QObject* parent)
    : QObject(parent)
    , m_colors(colors)
{
}

void ToolManager::addTool(Tool* tool, int group)
{
    if (group >= 0) {
        while (m_groups.size() <= group) m_groups.append(Group{});
        m_groups[group].tools.append(tool);
        if (!m_groups[group].last) m_groups[group].last = tool;
    }
    m_tools.append(tool);
    connect(tool, &Tool::overlayChanged, this, [this, tool] {
        if (m_view && tool == m_current) m_view->viewport()->update();
    });
}

int ToolManager::groupOf(Tool* tool) const
{
    for (int i = 0; i < m_groups.size(); ++i)
        if (m_groups[i].tools.contains(tool)) return i;
    return -1;
}

Tool* ToolManager::tool(const QString& id) const
{
    for (Tool* t : m_tools)
        if (t->id() == id) return t;
    return nullptr;
}

void ToolManager::activate(Tool* tool)
{
    if (!tool || tool == m_current) return;
    if (m_current && m_view) m_current->deactivated(m_view);
    m_current = tool;
    if (m_view) m_current->activated(m_view);
    emit currentChanged(tool);
}

void ToolManager::select(const QString& id) { select(tool(id)); }

void ToolManager::select(Tool* t)
{
    if (!t || m_busy) return;
    if (m_modal && t != m_modal) commitModal();
    m_beforeTemporary = nullptr;
    int g = groupOf(t);
    if (g >= 0 && m_groups[g].last != t) {
        m_groups[g].last = t;
        emit groupToolChanged(g);
    }
    activate(t);
}

bool ToolManager::selectByShortcut(QChar key, bool cycle)
{
    key = key.toUpper();
    for (int g = 0; g < m_groups.size(); ++g) {
        Group& grp = m_groups[g];
        if (grp.tools.isEmpty() || grp.tools.first()->shortcut() != key) continue;
        Tool* next = grp.last;
        if (cycle && grp.tools.contains(m_current)) {
            int i = int(grp.tools.indexOf(m_current));
            next = grp.tools[(i + 1) % grp.tools.size()];
        }
        select(next);
        return true;
    }
    return false;
}

void ToolManager::enterModal(Tool* tool)
{
    if (!tool || m_modal) return;
    m_beforeModal = m_beforeTemporary ? m_beforeTemporary : m_current;
    m_beforeTemporary = nullptr;
    m_modal = tool;
    activate(tool);
}

void ToolManager::exitModal()
{
    if (!m_modal) return;
    Tool* back = m_beforeModal;
    m_modal = m_beforeModal = nullptr;
    m_beforeTemporary = nullptr;
    activate(back);
}

void ToolManager::commitModal()
{
    if (m_modal) m_modal->commit(m_view);
    exitModal(); // in case the tool had nothing to commit
}

void ToolManager::pushTemporary(const QString& id)
{
    Tool* t = tool(id);
    if (!t || m_busy || m_beforeTemporary || t == m_current) return;
    m_beforeTemporary = m_current;
    activate(t);
}

void ToolManager::popTemporary()
{
    if (!m_beforeTemporary || m_busy) return;
    Tool* back = m_beforeTemporary;
    m_beforeTemporary = nullptr;
    activate(back);
}

void ToolManager::setActiveView(CanvasView* view)
{
    if (view == m_view) return;
    if (m_modal) commitModal();
    if (m_view && m_current) m_current->deactivated(m_view);
    m_view = view;
    if (m_view && m_current) m_current->activated(m_view);
}
