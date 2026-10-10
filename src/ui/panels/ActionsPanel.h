#pragma once

#include <QSet>
#include <QWidget>

class ActionsModel;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;

// Window > Actions (Alt+F9): sets, their actions and each action's steps. The left columns
// switch a step on or off and choose whether it stops at its dialog; the buttons along the
// bottom stop, record, play and add or delete.
class ActionsPanel : public QWidget {
    Q_OBJECT
public:
    struct Target {
        int set = -1;
        int action = -1;
        int step = -1;
    };

    explicit ActionsPanel(ActionsModel* model, QWidget* parent = nullptr);
    Target selected() const;
    void select(int set, int action, int step = -1);
    // New Action: asks for a name, adds the action to the selected set and starts recording.
    void newAction(const QString& name = QString());
    void newSet(const QString& name = QString());
    void deleteSelected(bool confirm = true);

signals:
    void playRequested(int set, int action, int fromStep);
    void recordRequested(int set, int action);
    void stopRequested();

private:
    void rebuild();
    void updateButtons();
    QTreeWidgetItem* itemFor(int set, int action, int step) const;

    ActionsModel* m_model;
    QTreeWidget* m_tree;
    QToolButton* m_stop;
    QToolButton* m_record;
    QToolButton* m_play;
    QToolButton* m_newSet;
    QToolButton* m_newAction;
    QToolButton* m_delete;
    QSet<QString> m_expanded;
    bool m_rebuilding = false;
};
