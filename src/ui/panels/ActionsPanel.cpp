#include "ui/panels/ActionsPanel.h"

#include "app/Theme.h"
#include "ui/ActionsModel.h"
#include "ui/Widgets.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QMessageBox>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {
enum Column { kEnabled, kDialog, kName };
constexpr int kSetRole = Qt::UserRole;
constexpr int kActionRole = Qt::UserRole + 1;
constexpr int kStepRole = Qt::UserRole + 2;

QString key(int set, int action) { return QStringLiteral("%1/%2").arg(set).arg(action); }
} // namespace

ActionsPanel::ActionsPanel(ActionsModel* model, QWidget* parent)
    : QWidget(parent)
    , m_model(model)
{
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);
    m_tree = new QTreeWidget(this);
    m_tree->setColumnCount(3);
    m_tree->setHeaderHidden(true);
    m_tree->setTreePosition(kName);
    m_tree->header()->setStretchLastSection(true);
    m_tree->setColumnWidth(kEnabled, 26);
    m_tree->setColumnWidth(kDialog, 26);
    m_tree->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    m_tree->setIconSize(QSize(16, 16));
    lay->addWidget(m_tree, 1);

    auto* bar = new QWidget(this);
    auto* bl = new QHBoxLayout(bar);
    bl->setContentsMargins(6, 3, 6, 3);
    bl->setSpacing(2);
    m_stop = makeIconButton(QStringLiteral("action-stop"), QStringLiteral("Stop playing/recording"), bar);
    m_record = makeIconButton(QStringLiteral("action-record"), QStringLiteral("Begin recording"), bar);
    m_play = makeIconButton(QStringLiteral("action-play"), QStringLiteral("Play selection"), bar);
    m_newSet = makeIconButton(QStringLiteral("folder"), QStringLiteral("Create new set"), bar);
    m_newAction = makeIconButton(QStringLiteral("new-layer"), QStringLiteral("Create new action"), bar);
    m_delete = makeIconButton(QStringLiteral("trash"), QStringLiteral("Delete"), bar);
    bl->addStretch();
    for (QToolButton* b : {m_stop, m_record, m_play, m_newSet, m_newAction, m_delete}) bl->addWidget(b);
    lay->addWidget(bar);

    connect(m_model, &ActionsModel::changed, this, &ActionsPanel::rebuild);
    connect(m_model, &ActionsModel::recordingChanged, this, [this](bool on) {
        m_record->setIcon(Theme::icon(on ? QStringLiteral("action-recording") : QStringLiteral("action-record")));
        updateButtons();
    });
    connect(m_tree, &QTreeWidget::currentItemChanged, this, &ActionsPanel::updateButtons);
    connect(m_tree, &QTreeWidget::itemExpanded, this, [this](QTreeWidgetItem* it) {
        m_expanded.insert(key(it->data(kName, kSetRole).toInt(), it->data(kName, kActionRole).toInt()));
    });
    connect(m_tree, &QTreeWidget::itemCollapsed, this, [this](QTreeWidgetItem* it) {
        m_expanded.remove(key(it->data(kName, kSetRole).toInt(), it->data(kName, kActionRole).toInt()));
    });
    connect(m_tree, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* it, int column) {
        if (m_rebuilding) return;
        const int s = it->data(kName, kSetRole).toInt(), a = it->data(kName, kActionRole).toInt(), st = it->data(kName, kStepRole).toInt();
        auto& sets = m_model->sets();
        if (s < 0 || s >= sets.size()) return;
        if (column == kName) {
            const QString text = it->text(kName).trimmed();
            if (text.isEmpty()) return;
            if (a < 0) sets[s].name = text;
            else if (RecordedAction* ra = m_model->actionAt(s, a); ra && st < 0) ra->name = text;
        } else if (RecordedAction* ra = m_model->actionAt(s, a)) {
            const bool on = it->checkState(column) == Qt::Checked;
            if (st < 0 && column == kEnabled) ra->enabled = on;
            else if (st >= 0 && st < ra->steps.size()) {
                if (column == kEnabled) ra->steps[st].enabled = on;
                else if (ra->steps[st].hasDialog) ra->steps[st].showDialog = on;
            }
        }
        m_model->notify();
    });
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* it, int) {
        // Double-clicking a step plays the action from there, as in Photoshop.
        const int st = it->data(kName, kStepRole).toInt();
        if (st >= 0) emit playRequested(it->data(kName, kSetRole).toInt(), it->data(kName, kActionRole).toInt(), st);
    });
    connect(m_stop, &QToolButton::clicked, this, &ActionsPanel::stopRequested);
    connect(m_record, &QToolButton::clicked, this, [this] {
        const Target t = selected();
        if (t.action >= 0) emit recordRequested(t.set, t.action);
    });
    connect(m_play, &QToolButton::clicked, this, [this] {
        const Target t = selected();
        if (t.action >= 0) emit playRequested(t.set, t.action, std::max(0, t.step));
    });
    connect(m_newSet, &QToolButton::clicked, this, [this] { newSet(); });
    connect(m_newAction, &QToolButton::clicked, this, [this] { newAction(); });
    connect(m_delete, &QToolButton::clicked, this, [this] { deleteSelected(); });

    m_expanded.insert(key(0, -1));
    rebuild();
}

ActionsPanel::Target ActionsPanel::selected() const
{
    Target t;
    if (QTreeWidgetItem* it = m_tree->currentItem()) {
        t.set = it->data(kName, kSetRole).toInt();
        t.action = it->data(kName, kActionRole).toInt();
        t.step = it->data(kName, kStepRole).toInt();
    }
    return t;
}

QTreeWidgetItem* ActionsPanel::itemFor(int set, int action, int step) const
{
    for (QTreeWidgetItemIterator it(m_tree); *it; ++it)
        if ((*it)->data(kName, kSetRole).toInt() == set && (*it)->data(kName, kActionRole).toInt() == action
            && (*it)->data(kName, kStepRole).toInt() == step)
            return *it;
    return nullptr;
}

void ActionsPanel::select(int set, int action, int step)
{
    if (action >= 0) m_expanded.insert(key(set, -1));
    if (QTreeWidgetItem* it = itemFor(set, action, step)) {
        for (QTreeWidgetItem* p = it->parent(); p; p = p->parent()) p->setExpanded(true);
        m_tree->setCurrentItem(it);
    }
}

void ActionsPanel::rebuild()
{
    const Target keep = selected();
    m_rebuilding = true;
    m_tree->clear();
    const auto& sets = m_model->sets();
    for (int s = 0; s < sets.size(); ++s) {
        auto* si = new QTreeWidgetItem(m_tree);
        si->setText(kName, sets[s].name);
        si->setIcon(kName, Theme::icon(QStringLiteral("folder")));
        si->setFlags(si->flags() | Qt::ItemIsEditable);
        si->setData(kName, kSetRole, s);
        si->setData(kName, kActionRole, -1);
        si->setData(kName, kStepRole, -1);
        for (int a = 0; a < sets[s].actions.size(); ++a) {
            const RecordedAction& ra = sets[s].actions[a];
            auto* ai = new QTreeWidgetItem(si);
            ai->setText(kName, ra.name);
            ai->setFlags(ai->flags() | Qt::ItemIsEditable | Qt::ItemIsUserCheckable);
            ai->setCheckState(kEnabled, ra.enabled ? Qt::Checked : Qt::Unchecked);
            ai->setData(kName, kSetRole, s);
            ai->setData(kName, kActionRole, a);
            ai->setData(kName, kStepRole, -1);
            if (m_model->recordingSet() == s && m_model->recordingAction() == a)
                ai->setIcon(kName, Theme::icon(QStringLiteral("action-recording")));
            for (int st = 0; st < ra.steps.size(); ++st) {
                const ActionStep& step = ra.steps[st];
                auto* ti = new QTreeWidgetItem(ai);
                ti->setText(kName, step.title);
                ti->setToolTip(kName, step.detail());
                ti->setFlags((ti->flags() | Qt::ItemIsUserCheckable) & ~Qt::ItemIsEditable);
                ti->setCheckState(kEnabled, step.enabled ? Qt::Checked : Qt::Unchecked);
                if (step.hasDialog) {
                    ti->setCheckState(kDialog, step.showDialog ? Qt::Checked : Qt::Unchecked);
                    ti->setToolTip(kDialog, QStringLiteral("Toggle dialog on/off"));
                }
                ti->setData(kName, kSetRole, s);
                ti->setData(kName, kActionRole, a);
                ti->setData(kName, kStepRole, st);
                if (!step.detail().isEmpty()) {
                    auto* di = new QTreeWidgetItem(ti);
                    di->setText(kName, step.detail());
                    di->setFlags(Qt::ItemIsEnabled);
                    di->setForeground(kName, Theme::kTextDim);
                    di->setData(kName, kSetRole, s);
                    di->setData(kName, kActionRole, a);
                    di->setData(kName, kStepRole, st);
                }
            }
            ai->setExpanded(m_expanded.contains(key(s, a)));
        }
        si->setExpanded(m_expanded.contains(key(s, -1)));
    }
    m_rebuilding = false;
    if (keep.set >= 0) {
        QTreeWidgetItem* it = itemFor(keep.set, keep.action, keep.step);
        if (!it) it = itemFor(keep.set, keep.action, -1);
        if (it) m_tree->setCurrentItem(it);
    }
    updateButtons();
}

void ActionsPanel::updateButtons()
{
    const Target t = selected();
    const bool recording = m_model->isRecording();
    m_stop->setEnabled(recording);
    m_record->setEnabled(!recording && t.action >= 0);
    m_play->setEnabled(!recording && t.action >= 0);
    m_newAction->setEnabled(!recording);
    // Deleting while recording would shift the action being recorded.
    m_delete->setEnabled(t.set >= 0 && !recording);
}

void ActionsPanel::newSet(const QString& presetName)
{
    QString name = presetName;
    if (name.isEmpty()) {
        bool ok = false;
        name = QInputDialog::getText(this, QStringLiteral("New Set"), QStringLiteral("Name:"), QLineEdit::Normal,
                                     QStringLiteral("Set %1").arg(m_model->sets().size() + 1), &ok)
                   .trimmed();
        if (!ok || name.isEmpty()) return;
    }
    const int s = m_model->addSet(name);
    m_expanded.insert(key(s, -1));
    rebuild();
    select(s, -1);
}

void ActionsPanel::newAction(const QString& presetName)
{
    int set = selected().set;
    if (set < 0) set = m_model->sets().isEmpty() ? -1 : 0;
    if (set < 0) set = m_model->addSet(QStringLiteral("Set 1"));
    QString name = presetName;
    if (name.isEmpty()) {
        bool ok = false;
        name = QInputDialog::getText(this, QStringLiteral("New Action"), QStringLiteral("Name:"), QLineEdit::Normal,
                                     QStringLiteral("Action %1").arg(m_model->sets()[set].actions.size() + 1), &ok)
                   .trimmed();
        if (!ok || name.isEmpty()) return;
    }
    const int a = m_model->addAction(set, name);
    m_expanded.insert(key(set, -1));
    m_expanded.insert(key(set, a));
    rebuild();
    select(set, a);
    emit recordRequested(set, a);
}

void ActionsPanel::deleteSelected(bool confirm)
{
    const Target t = selected();
    if (t.set < 0 || m_model->isRecording()) return;
    if (confirm) {
        const QString what = t.step >= 0 ? QStringLiteral("this step") : t.action >= 0 ? QStringLiteral("this action") : QStringLiteral("this set");
        if (QMessageBox::question(this, QStringLiteral("PhotoSlop"), QStringLiteral("Delete %1?").arg(what)) != QMessageBox::Yes) return;
    }
    auto& sets = m_model->sets();
    if (t.step >= 0) {
        if (RecordedAction* a = m_model->actionAt(t.set, t.action); a && t.step < a->steps.size()) a->steps.removeAt(t.step);
    } else if (t.action >= 0) {
        sets[t.set].actions.removeAt(t.action);
    } else {
        sets.removeAt(t.set);
    }
    m_model->notify();
}
