#pragma once

#include <QDialog>
#include <QKeySequence>
#include <QList>
#include <QString>

class QComboBox;
class QKeySequenceEdit;
class QLabel;
class QLineEdit;
class QTreeWidget;
class QTreeWidgetItem;

// Edit > Keyboard Shortcuts (Ctrl+Alt+Shift+K). Lists the menu commands and tools with their
// shortcuts and lets them be changed. Giving a command a key another command uses takes it
// from that command, as in Photoshop. Nothing changes until OK.
class ShortcutsDialog : public QDialog {
    Q_OBJECT
public:
    struct Entry {
        QString id;
        QString category; // "Application Menus" or "Tools"
        QString group;    // "File", "Edit"... or "Tools"
        QString name;     // "Open Recent > Clear Recent File List"
        QList<QKeySequence> keys;
        QList<QKeySequence> defaults;
        bool editable = true;
    };

    ShortcutsDialog(const QList<Entry>& entries, QWidget* parent = nullptr);

    QList<Entry> entries() const { return m_entries; }
    // Sets a command's shortcuts, taking each key from any command that had it. Returns the
    // commands that lost a key.
    QStringList setShortcuts(const QString& id, const QList<QKeySequence>& keys);
    int indexOf(const QString& id) const;
    // Every command back to its default shortcuts.
    void resetAll();
    // The shortcut list as an HTML page (Summarize...).
    QString summaryHtml() const;

private:
    void rebuild();
    void syncEditor();
    void commitEditor(bool add);
    QString pathOf(const Entry& e) const { return e.group + QStringLiteral(" > ") + e.name; }

    QList<Entry> m_entries;
    QComboBox* m_category;
    QLineEdit* m_search;
    QTreeWidget* m_tree;
    QKeySequenceEdit* m_edit;
    QLabel* m_message;
    QString m_current;
};
