#pragma once

#include <QElapsedTimer>
#include <QHash>
#include <QKeySequence>
#include <QMainWindow>
#include <QPointer>
#include <functional>

#include "ui/ViewOptions.h"

class CanvasView;
class ChannelsPanel;
class ColorState;
class Document;
class DocumentPage;
class FreeTransformTool;
class HistoryPanel;
class HomeScreen;
class InfoPanel;
class LayersPanel;
class NavigatorPanel;
class PanelStrip;
class PropertiesPanel;
class QDockWidget;
class QMenu;
class QStackedWidget;
class QTabWidget;
class QToolBar;
class QToolButton;
class QUndoGroup;
class SwatchesPanel;
class Tool;
class ToolBox;
class ToolManager;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    void openFiles(const QStringList& paths);
    QAction* action(const QString& id) const { return m_actions.value(id); }

protected:
    void closeEvent(QCloseEvent* e) override;
    bool eventFilter(QObject* obj, QEvent* e) override;
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dropEvent(QDropEvent* e) override;

private:
    // Setup
    void createTools();
    void createToolBars();
    void createDocks();
    void createMenus();
    void createHiddenShortcuts();
    QAction* makeAction(const QString& id, const QString& text, const QList<QKeySequence>& keys,
                        std::function<void()> fn, bool needsDocument = true);
    QAction* stub(QMenu* menu, const QString& text, const QKeySequence& key = QKeySequence());
    QDockWidget* makeDock(const QString& title, const QString& objectName, QWidget* content);

    // Documents
    DocumentPage* currentPage() const;
    Document* currentDoc() const;
    CanvasView* currentView() const;
    void addDocument(Document* doc);
    bool closeDocument(int index);
    bool closeAll();
    bool saveDocument(Document* doc, bool saveAs, bool asCopy = false);
    void newDocument();
    void openDialog();
    void exportAs();
    void quickExportPng();
    void revert();
    void addRecent(const QString& path);
    void refreshRecent();
    void onCurrentChanged();
    void updateTabTitles();
    void updateActions();
    void rebuildWindowMenuDocs();

    // Commands
    void alert(const QString& message);
    void copy(bool merged);
    void cut();
    void paste(bool inPlace);
    void fillDialog();
    void quickFill(bool background, bool preserve);
    void clearOrDelete();
    void newLayerDialog();
    void duplicateLayerDialog();
    void layerFromBackground();
    void imageSizeDialog();
    void canvasSizeDialog();
    void featherDialog();
    void modifySelectionDialog(int how);
    void toggleQuickMask();
    void startTransform(bool selectionOnly, int mode);
    void quickTransform(const std::function<void()>& apply);
    void newGuideDialog();
    QAction* viewToggle(const QString& id, const QString& text, const QList<QKeySequence>& keys, bool ViewOptions::*field);
    void syncViewActions();
    void applyPixelFilter(const QString& name, const std::function<QRgb(QRgb)>& fn);
    void showShortcuts();
    void showAbout();
    void toggleAllPanels(bool docksOnly);
    void cycleScreenMode();
    void setScreenMode(int mode);
    void handleDigit(int digit);
    bool isTypingTarget() const;

    ColorState* m_colors;
    ToolManager* m_tools;
    QUndoGroup* m_undoGroup;
    ViewOptions* m_viewOptions;
    FreeTransformTool* m_transform = nullptr;
    ToolBox* m_toolBox = nullptr;
    QHash<QString, bool ViewOptions::*> m_viewToggles;

    QStackedWidget* m_central = nullptr;
    HomeScreen* m_home = nullptr;
    QTabWidget* m_tabs = nullptr;
    QToolBar* m_toolsBar = nullptr;
    QToolBar* m_optionsBar = nullptr;
    QStackedWidget* m_optionsStack = nullptr;
    QToolButton* m_toolPreset = nullptr;
    QHash<Tool*, QWidget*> m_optionWidgets;

    LayersPanel* m_layersPanel = nullptr;
    PropertiesPanel* m_propertiesPanel = nullptr;
    ChannelsPanel* m_channelsPanel = nullptr;
    HistoryPanel* m_historyPanel = nullptr;
    NavigatorPanel* m_navigatorPanel = nullptr;
    InfoPanel* m_infoPanel = nullptr;
    SwatchesPanel* m_swatchesPanel = nullptr;
    PanelStrip* m_strip = nullptr;
    QList<QDockWidget*> m_docks;

    QHash<QString, QAction*> m_actions;
    QList<QAction*> m_docActions;
    QMenu* m_recentMenu = nullptr;
    QMenu* m_windowMenu = nullptr;
    QList<QAction*> m_windowDocActions;
    QByteArray m_defaultState;

    int m_untitled = 0;
    int m_screenMode = 0;
    bool m_wasMaximized = false;
    bool m_panelsHidden = false;
    QList<QWidget*> m_hiddenByTab;

    QPoint m_clipPos;
    QSize m_clipSize;
    bool m_clipValid = false;
    bool m_settingClipboard = false;

    QElapsedTimer m_digitTimer;
    int m_pendingDigit = -1;
    bool m_tempFromKey = false;
};
