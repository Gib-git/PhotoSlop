#include "ui/dialogs/ShortcutsDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStandardPaths>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {
QString keysText(const QList<QKeySequence>& keys)
{
    QStringList out;
    for (const QKeySequence& k : keys)
        if (!k.isEmpty()) out << k.toString(QKeySequence::NativeText);
    return out.join(QStringLiteral(", "));
}
} // namespace

ShortcutsDialog::ShortcutsDialog(const QList<Entry>& entries, QWidget* parent)
    : QDialog(parent)
    , m_entries(entries)
{
    setWindowTitle(QStringLiteral("Keyboard Shortcuts"));
    resize(680, 640);
    auto* lay = new QVBoxLayout(this);

    auto* top = new QHBoxLayout;
    top->addWidget(new QLabel(QStringLiteral("Shortcuts For:"), this));
    m_category = new QComboBox(this);
    m_category->addItems({QStringLiteral("Application Menus"), QStringLiteral("Tools")});
    top->addWidget(m_category);
    top->addStretch();
    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(QStringLiteral("Search"));
    m_search->setClearButtonEnabled(true);
    m_search->setFixedWidth(200);
    top->addWidget(m_search);
    lay->addLayout(top);

    m_tree = new QTreeWidget(this);
    m_tree->setColumnCount(2);
    m_tree->setHeaderLabels({QStringLiteral("Command"), QStringLiteral("Shortcut")});
    m_tree->header()->setStretchLastSection(true);
    m_tree->setColumnWidth(0, 380);
    m_tree->setUniformRowHeights(true);
    lay->addWidget(m_tree, 1);

    auto* editRow = new QHBoxLayout;
    editRow->addWidget(new QLabel(QStringLiteral("Shortcut:"), this));
    m_edit = new QKeySequenceEdit(this);
    m_edit->setMaximumSequenceLength(1);
    m_edit->setClearButtonEnabled(true);
    editRow->addWidget(m_edit, 1);
    auto* add = new QPushButton(QStringLiteral("Add Shortcut"), this);
    auto* del = new QPushButton(QStringLiteral("Delete Shortcut"), this);
    auto* def = new QPushButton(QStringLiteral("Use Default"), this);
    editRow->addWidget(add);
    editRow->addWidget(del);
    editRow->addWidget(def);
    lay->addLayout(editRow);

    m_message = new QLabel(this);
    m_message->setWordWrap(true);
    m_message->setStyleSheet(QStringLiteral("color: #f0c040;"));
    lay->addWidget(m_message);

    auto* bottom = new QHBoxLayout;
    auto* summarize = new QPushButton(QStringLiteral("Summarize..."), this);
    auto* resetAllButton = new QPushButton(QStringLiteral("Reset All to Defaults"), this);
    bottom->addWidget(summarize);
    bottom->addWidget(resetAllButton);
    bottom->addStretch();
    auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    bottom->addWidget(box);
    lay->addLayout(bottom);

    connect(box, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_category, &QComboBox::currentIndexChanged, this, &ShortcutsDialog::rebuild);
    connect(m_search, &QLineEdit::textChanged, this, &ShortcutsDialog::rebuild);
    connect(m_tree, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* item) {
        m_current = item ? item->data(0, Qt::UserRole).toString() : QString();
        syncEditor();
    });
    connect(m_edit, &QKeySequenceEdit::editingFinished, this, [this] { commitEditor(false); });
    connect(add, &QPushButton::clicked, this, [this] { commitEditor(true); });
    connect(del, &QPushButton::clicked, this, [this] {
        const int i = indexOf(m_current);
        if (i < 0 || !m_entries[i].editable) return;
        QList<QKeySequence> keys = m_entries[i].keys;
        if (!keys.isEmpty()) keys.removeLast();
        setShortcuts(m_current, keys);
    });
    connect(def, &QPushButton::clicked, this, [this] {
        const int i = indexOf(m_current);
        if (i >= 0) setShortcuts(m_current, m_entries[i].defaults);
    });
    connect(resetAllButton, &QPushButton::clicked, this, &ShortcutsDialog::resetAll);
    connect(summarize, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getSaveFileName(
            this, QStringLiteral("Summarize"),
            QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + QStringLiteral("/PhotoSlop Shortcuts.htm"),
            QStringLiteral("HTML (*.htm *.html)"));
        if (path.isEmpty()) return;
        QFile f(path);
        if (f.open(QIODevice::WriteOnly)) f.write(summaryHtml().toUtf8());
    });
    rebuild();
}

int ShortcutsDialog::indexOf(const QString& id) const
{
    for (int i = 0; i < m_entries.size(); ++i)
        if (m_entries[i].id == id) return i;
    return -1;
}

void ShortcutsDialog::rebuild()
{
    const QString category = m_category->currentText();
    const QString filter = m_search->text().trimmed();
    m_tree->clear();
    QHash<QString, QTreeWidgetItem*> groups;
    QTreeWidgetItem* toSelect = nullptr;
    for (const Entry& e : std::as_const(m_entries)) {
        if (e.category != category) continue;
        const QString keys = keysText(e.keys);
        if (!filter.isEmpty() && !e.name.contains(filter, Qt::CaseInsensitive) && !keys.contains(filter, Qt::CaseInsensitive))
            continue;
        QTreeWidgetItem*& g = groups[e.group];
        if (!g) {
            g = new QTreeWidgetItem(m_tree, {e.group});
            g->setFlags(Qt::ItemIsEnabled);
            g->setExpanded(true);
        }
        auto* item = new QTreeWidgetItem(g, {e.name, keys});
        item->setData(0, Qt::UserRole, e.id);
        if (!e.editable) item->setForeground(0, QColor(0x80, 0x80, 0x80));
        if (e.id == m_current) toSelect = item;
    }
    if (!filter.isEmpty() || groups.size() < 3)
        for (QTreeWidgetItem* g : std::as_const(groups)) g->setExpanded(true);
    if (toSelect) m_tree->setCurrentItem(toSelect);
    syncEditor();
}

void ShortcutsDialog::syncEditor()
{
    const int i = indexOf(m_current);
    m_edit->setEnabled(i >= 0 && m_entries[i].editable);
    m_edit->setKeySequence(i >= 0 && !m_entries[i].keys.isEmpty() ? m_entries[i].keys.first() : QKeySequence());
}

void ShortcutsDialog::commitEditor(bool add)
{
    const int i = indexOf(m_current);
    if (i < 0 || !m_entries[i].editable) return;
    const QKeySequence seq = m_edit->keySequence();
    QList<QKeySequence> keys = m_entries[i].keys;
    if (add) {
        if (seq.isEmpty() || keys.contains(seq)) return;
        keys.append(seq);
    } else if (seq.isEmpty()) {
        if (!keys.isEmpty()) keys.removeFirst();
    } else if (keys.isEmpty()) {
        keys.append(seq);
    } else {
        if (keys.first() == seq) return;
        keys[0] = seq;
    }
    setShortcuts(m_current, keys);
}

QStringList ShortcutsDialog::setShortcuts(const QString& id, const QList<QKeySequence>& requested)
{
    const int i = indexOf(id);
    if (i < 0) return {};
    QList<QKeySequence> keys;
    for (const QKeySequence& k : requested)
        if (!k.isEmpty() && !keys.contains(k)) keys.append(k);
    QStringList losers;
    for (int j = 0; j < m_entries.size(); ++j) {
        if (j == i) continue;
        Entry& other = m_entries[j];
        bool lost = false;
        for (const QKeySequence& k : keys) lost = other.keys.removeAll(k) > 0 || lost;
        if (lost) losers << pathOf(other);
    }
    m_entries[i].keys = keys;
    if (losers.isEmpty()) {
        m_message->clear();
    } else {
        m_message->setText(QStringLiteral("%1 was in use and has been removed from %2.")
                               .arg(keysText(keys), losers.join(QStringLiteral(", "))));
    }
    m_current = id;
    rebuild();
    return losers;
}

void ShortcutsDialog::resetAll()
{
    for (Entry& e : m_entries) e.keys = e.defaults;
    m_message->clear();
    rebuild();
}

QString ShortcutsDialog::summaryHtml() const
{
    QString html = QStringLiteral("<!DOCTYPE html><html><head><meta charset=\"utf-8\"><title>PhotoSlop Keyboard Shortcuts</title>"
                                  "<style>body{font-family:sans-serif} td{padding:2px 12px} h2{margin-top:24px}</style>"
                                  "</head><body><h1>PhotoSlop Keyboard Shortcuts</h1>");
    QString group;
    for (const Entry& e : m_entries) {
        if (e.group != group) {
            if (!group.isEmpty()) html += QStringLiteral("</table>");
            group = e.group;
            html += QStringLiteral("<h2>%1</h2><table>").arg(group.toHtmlEscaped());
        }
        html += QStringLiteral("<tr><td>%1</td><td>%2</td></tr>").arg(e.name.toHtmlEscaped(), keysText(e.keys).toHtmlEscaped());
    }
    if (!group.isEmpty()) html += QStringLiteral("</table>");
    return html + QStringLiteral("</body></html>");
}
