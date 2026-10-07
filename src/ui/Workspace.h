#pragma once

#include <QFrame>
#include <QIcon>
#include <QList>
#include <QStringList>
#include <QWidget>

class QAction;
class QLabel;
class QListWidget;
class QStackedWidget;
class QToolButton;

inline const QString kSlogan = QStringLiteral("Its not a bug, it's a feature");

// Rendered logo artwork (resources/logo.svg) at the given width.
QPixmap logoPixmap(int width, qreal dpr);

// Shown when no documents are open, like Photoshop's Home screen.
class HomeScreen : public QWidget {
    Q_OBJECT
public:
    explicit HomeScreen(QWidget* parent = nullptr);
    void setRecentFiles(const QStringList& files);

signals:
    void newRequested();
    void openRequested();
    void recentRequested(const QString& path);

private:
    QListWidget* m_recent;
};

// The narrow column of panel icons (History, Navigator, Info...) that opens
// panels as flyouts next to it — Photoshop's collapsed panel dock.
class PanelStrip : public QWidget {
    Q_OBJECT
public:
    explicit PanelStrip(QWidget* flyoutHost, QWidget* parent = nullptr);
    // Returns a checkable action that toggles the panel (for the Window menu).
    QAction* addPanel(const QIcon& icon, const QString& title, QWidget* panel, const QSize& size);
    void hideFlyout();

protected:
    bool eventFilter(QObject* obj, QEvent* e) override;

private:
    void toggle(int index);
    void place();

    struct Entry {
        QToolButton* button;
        QAction* action;
        QWidget* panel;
        QString title;
        QSize size;
    };
    QWidget* m_host;
    QFrame* m_flyout;
    QLabel* m_flyoutTitle;
    QStackedWidget* m_stack;
    QList<Entry> m_entries;
    int m_current = -1;
};
