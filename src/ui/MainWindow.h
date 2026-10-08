#pragma once

#include <QElapsedTimer>
#include <QHash>
#include <QKeySequence>
#include <QMainWindow>
#include <QPointer>
#include <functional>

#include "core/Adjustments.h"
#include "core/Filters.h"
#include "ui/ViewOptions.h"

class AdjustmentsPanel;
struct LayerStyle;
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
class ParamDialog;
class PreviewDialog;
class PanelStrip;
class PropertiesPanel;
class QDockWidget;
class QMenu;
class QStackedWidget;
class QTabWidget;
class QUndoCommand;
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
    // Layer > Layer Style (page is a LayerStyleDialog::Page).
    void layerStyleDialog(int page);
    // Adds an adjustment layer and opens its settings; Cancel removes it again.
    void newAdjustmentLayer(Adjust::Kind kind);
    // Opens the settings of the adjustment layer at `index`; true when OK was pressed.
    bool editAdjustmentLayer(int index);
    void startTransform(bool selectionOnly, int mode);
    void quickTransform(const std::function<void()>& apply);
    void newGuideDialog();
    QAction* viewToggle(const QString& id, const QString& text, const QList<QKeySequence>& keys, bool ViewOptions::*field);
    void syncViewActions();
    // Adjustments and filters
    using SpecRecipe = std::function<Filters::Spec()>;
    using ParamBuilder = std::function<Filters::Spec(const QHash<QString, double>&)>;
    // Runs an adjustment or filter dialog; on OK the change can be faded. True when applied.
    bool execPreview(PreviewDialog& dlg);
    // Before a pixel command: offers to rasterize a text or shape layer, alerts when the
    // target cannot change. True to go ahead.
    bool prepareTarget(const QString& command);
    // Applies a spec at once. With a recipe, it becomes the Last Filter.
    void applySpec(const Filters::Spec& spec, const SpecRecipe& recipe = SpecRecipe());
    // A dialog of sliders and options. Filters remember their values and become the Last
    // Filter; adjustments start from defaults unless Alt is held.
    void paramDialog(const QString& title, bool spreads, bool isFilter, const std::function<void(ParamDialog&)>& setup,
                     const ParamBuilder& build);
    void setLastFilter(const QString& name, const SpecRecipe& recipe);
    void autoAdjust(Adjust::AutoMode mode, const QString& name);
    void rememberFade(const QString& name, const Filters::Applied& applied);
    bool canFade();
    void fadeDialog();
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
    AdjustmentsPanel* m_adjustmentsPanel = nullptr;
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

    SpecRecipe m_lastFilter;
    std::shared_ptr<const LayerStyle> m_copiedStyle;
    struct FadeState {
        QPointer<Document> doc;
        int index = -1;
        const QUndoCommand* command = nullptr;
        QString name;
        Filters::Applied applied;
    } m_fade;

    QElapsedTimer m_digitTimer;
    int m_pendingDigit = -1;
    bool m_tempFromKey = false;
};
