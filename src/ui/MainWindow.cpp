#include "ui/MainWindow.h"

#include "app/Theme.h"
#include "core/ColorState.h"
#include "core/Commands.h"
#include "core/Document.h"
#include "core/DocumentOps.h"
#include "core/ImageOps.h"
#include "core/LayerStyle.h"
#include "core/LayerTree.h"
#include "core/VectorLayers.h"
#include "io/DocumentIO.h"
#include "tools/FillTools.h"
#include "tools/FreeTransformTool.h"
#include "tools/NavigationTools.h"
#include "tools/PaintTools.h"
#include "tools/RetouchTools.h"
#include "tools/SelectionTools.h"
#include "tools/ToolManager.h"
#include "tools/TransformTools.h"
#include "tools/VectorTools.h"
#include "ui/CanvasView.h"
#include "ui/DocumentPage.h"
#include "ui/ToolBox.h"
#include "ui/Workspace.h"
#include "ui/dialogs/AdjustmentDialogs.h"
#include "ui/dialogs/ColorPickerDialog.h"
#include "ui/dialogs/Dialogs.h"
#include "ui/dialogs/LayerStyleDialog.h"
#include "ui/panels/LayersPanel.h"
#include "ui/panels/Panels.h"

#include <QAbstractSpinBox>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QRandomGenerator>
#include <QSettings>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTabBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextEdit>
#include <QToolBar>
#include <QToolButton>
#include <QUndoGroup>
#include <QUndoStack>
#include <QVBoxLayout>
#include <cmath>
#include <algorithm>
#include <iterator>

namespace {

constexpr int kStateVersion = 4;

QList<QKeySequence> keys(std::initializer_list<const char*> list)
{
    QList<QKeySequence> out;
    for (const char* k : list) out << QKeySequence(QString::fromLatin1(k));
    return out;
}

using Values = QHash<QString, double>;

// Dialog recipes shared by Image > Adjustments and the matching adjustment layers.
void setupBrightnessContrast(ParamDialog& d)
{
    d.addSlider(QStringLiteral("brightness"), QStringLiteral("Brightness:"), -150, 150, 0);
    d.addSlider(QStringLiteral("contrast"), QStringLiteral("Contrast:"), -50, 100, 0);
    d.addCheck(QStringLiteral("legacy"), QStringLiteral("Use Legacy"), false);
}

Adjust::LayerSettings brightnessContrastSettings(const Values& v)
{
    Adjust::LayerSettings s;
    s.kind = Adjust::Kind::BrightnessContrast;
    s.brightness = int(v[QStringLiteral("brightness")]);
    s.contrast = int(v[QStringLiteral("contrast")]);
    s.legacy = v[QStringLiteral("legacy")] != 0;
    return s;
}

void setupBlackWhite(ParamDialog& d)
{
    const Adjust::BlackWhite def;
    const char* names[6] = {"Reds:", "Yellows:", "Greens:", "Cyans:", "Blues:", "Magentas:"};
    for (int i = 0; i < 6; ++i)
        d.addSlider(QStringLiteral("w%1").arg(i), QString::fromLatin1(names[i]), -200, 300, def.weights[i], 0, QStringLiteral("%"));
    d.addCheck(QStringLiteral("tint"), QStringLiteral("Tint"), false);
    d.addSlider(QStringLiteral("hue"), QStringLiteral("Hue:"), 0, 360, def.tintHue, 0, QStringLiteral("°"));
    d.addSlider(QStringLiteral("saturation"), QStringLiteral("Saturation:"), 0, 100, def.tintSaturation, 0, QStringLiteral("%"));
}

Adjust::LayerSettings blackWhiteSettings(const Values& v)
{
    Adjust::LayerSettings s;
    s.kind = Adjust::Kind::BlackWhite;
    for (int i = 0; i < 6; ++i) s.blackWhite.weights[i] = int(v[QStringLiteral("w%1").arg(i)]);
    s.blackWhite.tint = v[QStringLiteral("tint")] != 0;
    s.blackWhite.tintHue = int(v[QStringLiteral("hue")]);
    s.blackWhite.tintSaturation = int(v[QStringLiteral("saturation")]);
    return s;
}

void setupPosterize(ParamDialog& d)
{
    d.addSlider(QStringLiteral("levels"), QStringLiteral("Levels:"), 2, 255, 4, 0, QString(), 64);
}

Adjust::LayerSettings posterizeSettings(const Values& v)
{
    Adjust::LayerSettings s;
    s.kind = Adjust::Kind::Posterize;
    s.posterizeLevels = int(v[QStringLiteral("levels")]);
    return s;
}

// The dialog values matching saved settings.
Values valuesOf(const Adjust::LayerSettings& s)
{
    Values v;
    switch (s.kind) {
    case Adjust::Kind::BrightnessContrast:
        v[QStringLiteral("brightness")] = s.brightness;
        v[QStringLiteral("contrast")] = s.contrast;
        v[QStringLiteral("legacy")] = s.legacy;
        break;
    case Adjust::Kind::BlackWhite:
        for (int i = 0; i < 6; ++i) v[QStringLiteral("w%1").arg(i)] = s.blackWhite.weights[i];
        v[QStringLiteral("tint")] = s.blackWhite.tint;
        v[QStringLiteral("hue")] = s.blackWhite.tintHue;
        v[QStringLiteral("saturation")] = s.blackWhite.tintSaturation;
        break;
    case Adjust::Kind::Posterize: v[QStringLiteral("levels")] = s.posterizeLevels; break;
    default: break;
    }
    return v;
}

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , m_colors(new ColorState(this))
    , m_tools(new ToolManager(m_colors, this))
    , m_undoGroup(new QUndoGroup(this))
    , m_viewOptions(new ViewOptions(this))
{
    setWindowTitle(QStringLiteral("PhotoSlop — %1").arg(kSlogan));
    setWindowIcon(Theme::icon(QStringLiteral("app")));
    setAcceptDrops(true);
    setDockNestingEnabled(true);
    setDockOptions(QMainWindow::AnimatedDocks | QMainWindow::AllowTabbedDocks | QMainWindow::AllowNestedDocks
                   | QMainWindow::GroupedDragging);
    setTabPosition(Qt::AllDockWidgetAreas, QTabWidget::North);

    createTools();

    // Central area: Home screen until a document is opened, then document tabs.
    m_central = new QStackedWidget(this);
    m_home = new HomeScreen(m_central);
    m_tabs = new QTabWidget(m_central);
    m_tabs->setObjectName(QStringLiteral("DocumentTabs"));
    m_tabs->setDocumentMode(true);
    m_tabs->setTabsClosable(true);
    m_tabs->setMovable(true);
    m_tabs->tabBar()->setExpanding(false);
    m_tabs->tabBar()->setElideMode(Qt::ElideMiddle);
    m_central->addWidget(m_home);
    m_central->addWidget(m_tabs);
    setCentralWidget(m_central);
    connect(m_home, &HomeScreen::newRequested, this, &MainWindow::newDocument);
    connect(m_home, &HomeScreen::openRequested, this, &MainWindow::openDialog);
    connect(m_home, &HomeScreen::recentRequested, this, [this](const QString& p) { openFiles({p}); });
    connect(m_tabs, &QTabWidget::currentChanged, this, &MainWindow::onCurrentChanged);
    connect(m_tabs, &QTabWidget::tabCloseRequested, this, &MainWindow::closeDocument);

    createToolBars();
    createDocks();
    createMenus();
    createHiddenShortcuts();
    m_adjustmentsPanel->setActionLookup([this](const QString& id) { return action(id); });

    connect(m_tools, &ToolManager::alertRequested, this, &MainWindow::alert);
    connect(m_viewOptions, &ViewOptions::changed, this, &MainWindow::syncViewActions);
    connect(QApplication::clipboard(), &QClipboard::dataChanged, this, [this] {
        if (!m_settingClipboard) m_clipValid = false;
    });
    qApp->installEventFilter(this);

    m_tools->select(QStringLiteral("move"));
    resize(1440, 900);
    m_defaultState = saveState(kStateVersion);
    QSettings s;
    restoreGeometry(s.value(QStringLiteral("geometry")).toByteArray());
    restoreState(s.value(QStringLiteral("windowState")).toByteArray(), kStateVersion);
    refreshRecent();
    onCurrentChanged();
}

MainWindow::~MainWindow()
{
    qApp->removeEventFilter(this);
    // Documents outlive this object's slots during teardown (their undo stacks signal as they clear).
    for (int i = 0; i < m_tabs->count(); ++i) {
        Document* doc = static_cast<DocumentPage*>(m_tabs->widget(i))->document();
        disconnect(doc, nullptr, this, nullptr);
        disconnect(doc->undoStack(), nullptr, this, nullptr);
    }
}

// ---------------- Setup ----------------

void MainWindow::createTools()
{
    using K = BrushTool::Kind;
    // Toolbox slots in Photoshop's order; tools in one slot share a flyout.
    int g = 0;
    m_tools->addTool(new MoveTool(m_tools), g++);
    m_tools->addTool(new MarqueeTool(m_tools, false), g);
    m_tools->addTool(new MarqueeTool(m_tools, true), g++);
    m_tools->addTool(new LassoTool(m_tools, false), g);
    m_tools->addTool(new LassoTool(m_tools, true), g);
    m_tools->addTool(new MagneticLassoTool(m_tools), g++);
    m_tools->addTool(new QuickSelectionTool(m_tools), g);
    m_tools->addTool(new MagicWandTool(m_tools), g++);
    m_tools->addTool(new CropTool(m_tools), g++);
    m_tools->addTool(new EyedropperTool(m_tools), g++);
    m_tools->addTool(new SpotHealingTool(m_tools), g);
    m_tools->addTool(new CloneStampTool(m_tools, true), g++);
    m_tools->addTool(new BrushTool(m_tools, K::Brush), g);
    m_tools->addTool(new BrushTool(m_tools, K::Pencil), g++);
    m_tools->addTool(new CloneStampTool(m_tools), g++);
    m_tools->addTool(new HistoryBrushTool(m_tools), g++);
    m_tools->addTool(new BrushTool(m_tools, K::Eraser), g++);
    m_tools->addTool(new GradientTool(m_tools), g);
    m_tools->addTool(new PaintBucketTool(m_tools), g++);
    m_tools->addTool(new FocusTool(m_tools, false), g);
    m_tools->addTool(new FocusTool(m_tools, true), g);
    m_tools->addTool(new SmudgeTool(m_tools), g++);
    m_tools->addTool(new ToningTool(m_tools, ToningTool::Kind::Dodge), g);
    m_tools->addTool(new ToningTool(m_tools, ToningTool::Kind::Burn), g);
    m_tools->addTool(new ToningTool(m_tools, ToningTool::Kind::Sponge), g++);
    m_tools->addTool(new TypeTool(m_tools), g++);
    m_tools->addTool(new ShapeTool(m_tools, ShapeTool::Kind::Rectangle), g);
    m_tools->addTool(new ShapeTool(m_tools, ShapeTool::Kind::Ellipse), g);
    m_tools->addTool(new ShapeTool(m_tools, ShapeTool::Kind::Polygon), g);
    m_tools->addTool(new ShapeTool(m_tools, ShapeTool::Kind::Line), g++);
    m_tools->addTool(new HandTool(m_tools), g++);
    m_tools->addTool(new ZoomTool(m_tools), g++);
    m_transform = new FreeTransformTool(m_tools);
    m_tools->addTool(m_transform, -1);

    // Painting on a text or shape layer asks to rasterize it first, as Photoshop does.
    m_tools->setRasterizePrompt([this](Document* doc, int index) {
        const bool text = doc->layerAt(index).kind == LayerKind::Text;
        const QString msg = text ? QStringLiteral("This type layer must be rasterized before proceeding. Its text will no longer be editable.")
                                 : QStringLiteral("This shape layer must be rasterized before proceeding. Its vector outline will no longer be editable.");
        QMessageBox box(QMessageBox::Warning, QStringLiteral("PhotoSlop"), msg, QMessageBox::Ok | QMessageBox::Cancel, this);
        box.button(QMessageBox::Ok)->setText(text ? QStringLiteral("Rasterize Type") : QStringLiteral("Rasterize Shape"));
        if (box.exec() != QMessageBox::Ok) return false;
        return Ops::rasterizeLayer(doc, index);
    });
}

void MainWindow::createToolBars()
{
    // Options bar (top): tool preset button + per-tool options.
    m_optionsBar = new QToolBar(QStringLiteral("Options"), this);
    m_optionsBar->setObjectName(QStringLiteral("OptionsBar"));
    m_optionsBar->setMovable(false);
    m_optionsBar->setFloatable(false);
    m_optionsBar->setIconSize(QSize(20, 20));
    m_toolPreset = new QToolButton(m_optionsBar);
    m_toolPreset->setFixedSize(40, 28);
    m_toolPreset->setIconSize(QSize(20, 20));
    m_toolPreset->setPopupMode(QToolButton::InstantPopup);
    m_toolPreset->setToolTip(QStringLiteral("Tool Preset picker"));
    m_optionsBar->addWidget(m_toolPreset);
    m_optionsBar->addSeparator();
    m_optionsStack = new QStackedWidget(m_optionsBar);
    m_optionsStack->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_optionsBar->addWidget(m_optionsStack);
    addToolBar(Qt::TopToolBarArea, m_optionsBar);

    connect(m_tools, &ToolManager::currentChanged, this, [this](Tool* t) {
        QWidget* w = m_optionWidgets.value(t);
        if (!w) {
            w = t->createOptions(m_optionsStack);
            m_optionWidgets.insert(t, w);
            m_optionsStack->addWidget(w);
        }
        m_optionsStack->setCurrentWidget(w);
        m_toolPreset->setIcon(Theme::icon(t->iconName()));
        if (CanvasView* v = currentView()) v->updateCursor();
    });

    // Toolbox (left).
    m_toolsBar = new QToolBar(QStringLiteral("Tools"), this);
    m_toolsBar->setObjectName(QStringLiteral("ToolsBar"));
    m_toolsBar->setMovable(false);
    m_toolsBar->setFloatable(false);
    m_toolsBar->setOrientation(Qt::Vertical);
    m_toolBox = new ToolBox(m_tools, m_colors, m_toolsBar);
    connect(m_toolBox, &ToolBox::screenModeRequested, this, &MainWindow::cycleScreenMode);
    connect(m_toolBox, &ToolBox::quickMaskRequested, this, [this] {
        m_tools->commitModal();
        toggleQuickMask();
    });
    m_toolsBar->addWidget(m_toolBox);
    addToolBar(Qt::LeftToolBarArea, m_toolsBar);
}

QDockWidget* MainWindow::makeDock(const QString& title, const QString& objectName, QWidget* content)
{
    auto* dock = new QDockWidget(title, this);
    dock->setObjectName(objectName);
    dock->setWidget(content);
    dock->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    // Photoshop panels show only their tabs; replace the title bar with a thin grip.
    auto* grip = new QWidget(dock);
    grip->setFixedHeight(4);
    grip->setStyleSheet(QStringLiteral("background: #282828;"));
    dock->setTitleBarWidget(grip);
    m_docks.append(dock);
    return dock;
}

void MainWindow::createDocks()
{
    auto lookup = [this](const QString& id) { return action(id); };

    auto* color = makeDock(QStringLiteral("Color"), QStringLiteral("ColorDock"), new ColorPanel(m_colors));
    m_swatchesPanel = new SwatchesPanel(m_colors);
    auto* swatches = makeDock(QStringLiteral("Swatches"), QStringLiteral("SwatchesDock"), m_swatchesPanel);
    m_propertiesPanel = new PropertiesPanel;
    auto* props = makeDock(QStringLiteral("Properties"), QStringLiteral("PropertiesDock"), m_propertiesPanel);
    m_adjustmentsPanel = new AdjustmentsPanel;
    auto* adjust = makeDock(QStringLiteral("Adjustments"), QStringLiteral("AdjustmentsDock"), m_adjustmentsPanel);
    m_layersPanel = new LayersPanel;
    m_layersPanel->setActionLookup(lookup);
    auto* layers = makeDock(QStringLiteral("Layers"), QStringLiteral("LayersDock"), m_layersPanel);
    m_channelsPanel = new ChannelsPanel;
    auto* channels = makeDock(QStringLiteral("Channels"), QStringLiteral("ChannelsDock"), m_channelsPanel);
    auto* paths = makeDock(QStringLiteral("Paths"), QStringLiteral("PathsDock"),
                           new PlaceholderPanel(QStringLiteral("Paths will appear here once the Pen tool arrives.")));

    // Collapsed icon column (History / Navigator / Info), left of the panels.
    m_strip = new PanelStrip(this);
    auto* stripDock = new QDockWidget(QStringLiteral("Panel Icons"), this);
    stripDock->setObjectName(QStringLiteral("PanelStripDock"));
    stripDock->setWidget(m_strip);
    stripDock->setFeatures(QDockWidget::NoDockWidgetFeatures);
    auto* grip = new QWidget(stripDock);
    grip->setFixedHeight(4);
    grip->setStyleSheet(QStringLiteral("background: #282828;"));
    stripDock->setTitleBarWidget(grip);

    // Split first, then tabify: splitting a tabbed dock would add a tab instead.
    addDockWidget(Qt::RightDockWidgetArea, stripDock);
    addDockWidget(Qt::RightDockWidgetArea, color);
    splitDockWidget(stripDock, color, Qt::Horizontal);
    splitDockWidget(color, props, Qt::Vertical);
    splitDockWidget(props, layers, Qt::Vertical);
    tabifyDockWidget(color, swatches);
    tabifyDockWidget(props, adjust);
    tabifyDockWidget(layers, channels);
    tabifyDockWidget(channels, paths);
    color->raise();
    props->raise();
    layers->raise();
    resizeDocks({color, props, layers}, {170, 190, 420}, Qt::Vertical);
    resizeDocks({color}, {290}, Qt::Horizontal);

    m_historyPanel = new HistoryPanel(m_undoGroup);
    m_navigatorPanel = new NavigatorPanel;
    m_infoPanel = new InfoPanel;
    m_actions.insert(QStringLiteral("window.history"),
                     m_strip->addPanel(Theme::icon(QStringLiteral("history")), QStringLiteral("History"), m_historyPanel, QSize(260, 340)));
    m_actions.insert(QStringLiteral("window.navigator"),
                     m_strip->addPanel(Theme::icon(QStringLiteral("navigator")), QStringLiteral("Navigator"), m_navigatorPanel, QSize(260, 260)));
    m_actions.insert(QStringLiteral("window.info"),
                     m_strip->addPanel(Theme::icon(QStringLiteral("info")), QStringLiteral("Info"), m_infoPanel, QSize(240, 170)));
    action(QStringLiteral("window.info"))->setShortcut(QKeySequence(Qt::Key_F8));
    addAction(action(QStringLiteral("window.info")));
}

QAction* MainWindow::makeAction(const QString& id, const QString& text, const QList<QKeySequence>& ks,
                                std::function<void()> fn, bool needsDocument)
{
    auto* a = new QAction(text, this);
    a->setShortcuts(ks);
    a->setShortcutContext(Qt::WindowShortcut);
    // Most commands apply a pending Free Transform first, as Photoshop does.
    static const QStringList keepsTransform = {QStringLiteral("view."), QStringLiteral("transform."), QStringLiteral("tool."),
                                               QStringLiteral("color."), QStringLiteral("brush."), QStringLiteral("window."),
                                               QStringLiteral("edit.freeTransform"), QStringLiteral("edit.shortcuts")};
    const bool commits = std::none_of(keepsTransform.begin(), keepsTransform.end(), [&](const QString& p) { return id.startsWith(p); });
    connect(a, &QAction::triggered, this, [this, commits, fn = std::move(fn)] {
        if (commits) m_tools->commitModal();
        fn();
    });
    m_actions.insert(id, a);
    if (needsDocument) m_docActions.append(a);
    return a;
}

QAction* MainWindow::stub(QMenu* menu, const QString& text, const QKeySequence& key)
{
    QAction* a = menu->addAction(text);
    // Shown with its Photoshop shortcut for reference, but not bound until implemented.
    if (!key.isEmpty())
        a->setText(text + QLatin1Char('\t') + key.toString(QKeySequence::NativeText));
    a->setEnabled(false);
    return a;
}

void MainWindow::createMenus()
{
    QMenuBar* mb = menuBar();
    auto A = [this](const QString& id) { return action(id); };

    // ---------------- File ----------------
    QMenu* file = mb->addMenu(QStringLiteral("&File"));
    file->addAction(makeAction(QStringLiteral("file.new"), QStringLiteral("&New..."), keys({"Ctrl+N"}), [this] { newDocument(); }, false));
    file->addAction(makeAction(QStringLiteral("file.open"), QStringLiteral("&Open..."), keys({"Ctrl+O"}), [this] { openDialog(); }, false));
    stub(file, QStringLiteral("Browse in Bridge..."), QKeySequence(QStringLiteral("Ctrl+Alt+O")));
    file->addAction(makeAction(QStringLiteral("file.openAs"), QStringLiteral("Open As..."), keys({"Ctrl+Alt+Shift+O"}), [this] { openDialog(); }, false));
    stub(file, QStringLiteral("Open as Smart Object..."));
    m_recentMenu = file->addMenu(QStringLiteral("Open &Recent"));
    file->addSeparator();
    file->addAction(makeAction(QStringLiteral("file.close"), QStringLiteral("&Close"), keys({"Ctrl+W"}), [this] { closeDocument(m_tabs->currentIndex()); }));
    file->addAction(makeAction(QStringLiteral("file.closeAll"), QStringLiteral("Close All"), keys({"Ctrl+Alt+W"}), [this] { closeAll(); }));
    file->addAction(makeAction(QStringLiteral("file.closeOthers"), QStringLiteral("Close Others"), keys({"Ctrl+Alt+P"}), [this] {
        QWidget* keep = m_tabs->currentWidget();
        for (int i = m_tabs->count() - 1; i >= 0; --i)
            if (m_tabs->widget(i) != keep && !closeDocument(i)) return;
    }));
    stub(file, QStringLiteral("Close and Go to Bridge..."), QKeySequence(QStringLiteral("Ctrl+Shift+W")));
    file->addAction(makeAction(QStringLiteral("file.save"), QStringLiteral("&Save"), keys({"Ctrl+S"}), [this] { saveDocument(currentDoc(), false); }));
    file->addAction(makeAction(QStringLiteral("file.saveAs"), QStringLiteral("Save &As..."), keys({"Ctrl+Shift+S"}), [this] { saveDocument(currentDoc(), true); }));
    file->addAction(makeAction(QStringLiteral("file.saveCopy"), QStringLiteral("Save a Copy..."), keys({"Ctrl+Alt+S"}), [this] { saveDocument(currentDoc(), true, true); }));
    file->addAction(makeAction(QStringLiteral("file.revert"), QStringLiteral("Revert"), keys({"F12"}), [this] { revert(); }));
    file->addSeparator();
    QMenu* exportMenu = file->addMenu(QStringLiteral("&Export"));
    exportMenu->addAction(makeAction(QStringLiteral("file.quickExport"), QStringLiteral("Quick Export as PNG"), {}, [this] { quickExportPng(); }));
    exportMenu->addAction(makeAction(QStringLiteral("file.exportAs"), QStringLiteral("Export As..."), keys({"Ctrl+Alt+Shift+W"}), [this] { exportAs(); }));
    exportMenu->addSeparator();
    stub(exportMenu, QStringLiteral("Save for Web (Legacy)..."), QKeySequence(QStringLiteral("Ctrl+Alt+Shift+S")));
    stub(exportMenu, QStringLiteral("Artboards to Files..."));
    stub(exportMenu, QStringLiteral("Layers to Files..."));
    stub(file, QStringLiteral("Generate"));
    stub(file, QStringLiteral("Share..."));
    file->addSeparator();
    stub(file, QStringLiteral("Place Embedded..."));
    stub(file, QStringLiteral("Place Linked..."));
    stub(file, QStringLiteral("Package..."));
    file->addSeparator();
    stub(file, QStringLiteral("Automate"));
    stub(file, QStringLiteral("Scripts"));
    stub(file, QStringLiteral("Import"));
    file->addSeparator();
    stub(file, QStringLiteral("File Info..."), QKeySequence(QStringLiteral("Ctrl+Alt+Shift+I")));
    stub(file, QStringLiteral("Version History"));
    file->addSeparator();
    stub(file, QStringLiteral("Print..."), QKeySequence(QStringLiteral("Ctrl+P")));
    stub(file, QStringLiteral("Print One Copy"), QKeySequence(QStringLiteral("Ctrl+Alt+Shift+P")));
    file->addSeparator();
    QAction* quit = makeAction(QStringLiteral("file.exit"), QStringLiteral("E&xit"), keys({"Ctrl+Q"}), [this] { close(); }, false);
    quit->setMenuRole(QAction::QuitRole);
    file->addAction(quit);

    // ---------------- Edit ----------------
    QMenu* edit = mb->addMenu(QStringLiteral("&Edit"));
    QAction* undo = m_undoGroup->createUndoAction(this, QStringLiteral("Undo"));
    undo->setShortcut(QKeySequence(QStringLiteral("Ctrl+Z")));
    QAction* redo = m_undoGroup->createRedoAction(this, QStringLiteral("Redo"));
    redo->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+Z")));
    // Undo during Free Transform cancels it instead of stepping back through history.
    disconnect(undo, &QAction::triggered, m_undoGroup, nullptr);
    connect(undo, &QAction::triggered, this, [this] {
        // Undo during Free Transform or typing abandons it instead.
        if (Tool* modal = m_tools->modalTool()) modal->cancel(currentView());
        else m_undoGroup->undo();
    });
    disconnect(redo, &QAction::triggered, m_undoGroup, nullptr);
    connect(redo, &QAction::triggered, this, [this] {
        if (!m_tools->modalTool()) m_undoGroup->redo();
    });
    m_actions.insert(QStringLiteral("edit.undo"), undo);
    m_actions.insert(QStringLiteral("edit.redo"), redo);
    edit->addAction(undo);
    edit->addAction(redo);
    edit->addAction(makeAction(QStringLiteral("edit.toggleLast"), QStringLiteral("Toggle Last State"), keys({"Ctrl+Alt+Z"}), [this] {
        QUndoStack* s = m_undoGroup->activeStack();
        if (!s) return;
        // Flip between the current state and the one before it.
        static thread_local bool undone = false;
        if (s->canRedo() && undone) {
            s->redo();
            undone = false;
        } else if (s->canUndo()) {
            s->undo();
            undone = true;
        }
    }));
    edit->addSeparator();
    edit->addAction(makeAction(QStringLiteral("edit.fade"), QStringLiteral("Fade..."), keys({"Ctrl+Shift+F"}), [this] { fadeDialog(); }));
    edit->addSeparator();
    edit->addAction(makeAction(QStringLiteral("edit.cut"), QStringLiteral("Cu&t"), keys({"Ctrl+X"}), [this] { cut(); }));
    edit->addAction(makeAction(QStringLiteral("edit.copy"), QStringLiteral("&Copy"), keys({"Ctrl+C"}), [this] { copy(false); }));
    edit->addAction(makeAction(QStringLiteral("edit.copyMerged"), QStringLiteral("Copy Merged"), keys({"Ctrl+Shift+C"}), [this] { copy(true); }));
    edit->addAction(makeAction(QStringLiteral("edit.paste"), QStringLiteral("&Paste"), keys({"Ctrl+V"}), [this] { paste(false); }, false));
    QMenu* pasteSpecial = edit->addMenu(QStringLiteral("Paste Special"));
    pasteSpecial->addAction(makeAction(QStringLiteral("edit.pasteInPlace"), QStringLiteral("Paste in Place"), keys({"Ctrl+Shift+V"}), [this] { paste(true); }));
    stub(pasteSpecial, QStringLiteral("Paste Into"), QKeySequence(QStringLiteral("Ctrl+Alt+Shift+V")));
    stub(pasteSpecial, QStringLiteral("Paste Outside"));
    edit->addAction(makeAction(QStringLiteral("edit.clear"), QStringLiteral("Clear"), keys({"Del", "Backspace"}), [this] { clearOrDelete(); }));
    edit->addSeparator();
    stub(edit, QStringLiteral("Search"), QKeySequence(QStringLiteral("Ctrl+F")));
    stub(edit, QStringLiteral("Check Spelling..."));
    stub(edit, QStringLiteral("Find and Replace Text..."));
    edit->addSeparator();
    edit->addAction(makeAction(QStringLiteral("edit.fill"), QStringLiteral("Fill..."), keys({"Shift+F5"}), [this] { fillDialog(); }));
    stub(edit, QStringLiteral("Stroke..."));
    stub(edit, QStringLiteral("Content-Aware Fill..."));
    stub(edit, QStringLiteral("Generative Fill..."));
    edit->addSeparator();
    stub(edit, QStringLiteral("Content-Aware Scale"), QKeySequence(QStringLiteral("Ctrl+Alt+Shift+C")));
    stub(edit, QStringLiteral("Puppet Warp"));
    stub(edit, QStringLiteral("Perspective Warp"));
    edit->addAction(makeAction(QStringLiteral("edit.freeTransform"), QStringLiteral("Free Transform"), keys({"Ctrl+T"}),
                               [this] { startTransform(false, int(FreeTransformTool::Mode::Free)); }));
    QMenu* transform = edit->addMenu(QStringLiteral("Transform"));
    transform->addAction(makeAction(QStringLiteral("transform.again"), QStringLiteral("Again"), keys({"Ctrl+Shift+T"}),
                                    [this] { quickTransform([this] { m_transform->applyLastTransform(); }); }));
    transform->addSeparator();
    const struct { const char* id; const char* text; FreeTransformTool::Mode mode; } modes[] = {
        {"transform.scale", "Scale", FreeTransformTool::Mode::Scale},
        {"transform.rotate", "Rotate", FreeTransformTool::Mode::Rotate},
        {"transform.skew", "Skew", FreeTransformTool::Mode::Skew},
        {"transform.distort", "Distort", FreeTransformTool::Mode::Distort},
        {"transform.perspective", "Perspective", FreeTransformTool::Mode::Perspective},
        {"transform.warp", "Warp", FreeTransformTool::Mode::Warp},
    };
    for (const auto& m : modes) {
        const int mode = int(m.mode);
        transform->addAction(makeAction(QString::fromLatin1(m.id), QString::fromLatin1(m.text), {}, [this, mode] { startTransform(false, mode); }));
    }
    transform->addSeparator();
    const struct { const char* id; const char* text; double degrees; int flip; } fixed[] = {
        {"transform.rot180", "Rotate 180°", 180, 0},
        {"transform.rot90cw", "Rotate 90° Clockwise", 90, 0},
        {"transform.rot90ccw", "Rotate 90° Counter Clockwise", -90, 0},
        {"transform.flipH", "Flip Horizontal", 0, 1},
        {"transform.flipV", "Flip Vertical", 0, 2},
    };
    for (const auto& f : fixed) {
        if (f.flip == 1) transform->addSeparator();
        const QString text = QString::fromUtf8(f.text);
        const double degrees = f.degrees;
        const int flip = f.flip;
        transform->addAction(makeAction(QString::fromLatin1(f.id), text, {}, [this, text, degrees, flip] {
            quickTransform([this, text, degrees, flip] {
                if (flip) m_transform->flip(flip == 1, text);
                else m_transform->rotateBy(degrees, text);
            });
        }));
    }
    stub(edit, QStringLiteral("Auto-Align Layers..."));
    stub(edit, QStringLiteral("Auto-Blend Layers..."));
    edit->addSeparator();
    stub(edit, QStringLiteral("Define Brush Preset..."));
    stub(edit, QStringLiteral("Define Pattern..."));
    stub(edit, QStringLiteral("Define Custom Shape..."));
    edit->addSeparator();
    QMenu* purge = edit->addMenu(QStringLiteral("Purge"));
    purge->addAction(makeAction(QStringLiteral("edit.purgeClipboard"), QStringLiteral("Clipboard"), {}, [this] {
        QApplication::clipboard()->clear();
        m_clipValid = false;
    }, false));
    purge->addAction(makeAction(QStringLiteral("edit.purgeHistories"), QStringLiteral("Histories"), {}, [this] {
        if (QMessageBox::warning(this, QStringLiteral("PhotoSlop"), QStringLiteral("This cannot be undone.\nContinue?"),
                                 QMessageBox::Ok | QMessageBox::Cancel) == QMessageBox::Ok)
            if (Document* d = currentDoc()) d->undoStack()->clear();
    }));
    edit->addSeparator();
    stub(edit, QStringLiteral("Color Settings..."), QKeySequence(QStringLiteral("Ctrl+Shift+K")));
    stub(edit, QStringLiteral("Assign Profile..."));
    stub(edit, QStringLiteral("Convert to Profile..."));
    edit->addSeparator();
    edit->addAction(makeAction(QStringLiteral("edit.shortcuts"), QStringLiteral("Keyboard Shortcuts..."), keys({"Ctrl+Alt+Shift+K"}), [this] { showShortcuts(); }, false));
    stub(edit, QStringLiteral("Menus..."), QKeySequence(QStringLiteral("Ctrl+Alt+Shift+M")));
    stub(edit, QStringLiteral("Toolbar..."));
    QAction* prefs = stub(edit, QStringLiteral("Preferences..."), QKeySequence(QStringLiteral("Ctrl+K")));
    prefs->setMenuRole(QAction::PreferencesRole);

    // ---------------- Image ----------------
    QMenu* image = mb->addMenu(QStringLiteral("&Image"));
    QMenu* mode = image->addMenu(QStringLiteral("Mode"));
    for (const char* m : {"Bitmap", "Grayscale", "Duotone", "Indexed Color..."}) stub(mode, QString::fromLatin1(m));
    QAction* rgb = mode->addAction(QStringLiteral("RGB Color"));
    rgb->setCheckable(true);
    rgb->setChecked(true);
    for (const char* m : {"CMYK Color", "Lab Color", "Multichannel"}) stub(mode, QString::fromLatin1(m));
    mode->addSeparator();
    QAction* bits8 = mode->addAction(QStringLiteral("8 Bits/Channel"));
    bits8->setCheckable(true);
    bits8->setChecked(true);
    stub(mode, QStringLiteral("16 Bits/Channel"));
    stub(mode, QStringLiteral("32 Bits/Channel"));
    image->addSeparator();
    QMenu* adj = image->addMenu(QStringLiteral("Adjustments"));
    // Holding Alt (Option) opens Levels, Curves, Hue/Saturation and Color Balance with the last settings.
    auto altHeld = [] { return bool(QApplication::keyboardModifiers() & Qt::AltModifier); };
    adj->addAction(makeAction(QStringLiteral("image.brightnessContrast"), QStringLiteral("Brightness/Contrast..."), {}, [this] {
        paramDialog(QStringLiteral("Brightness/Contrast"), false, false, setupBrightnessContrast, [](const Values& v) {
            const Adjust::LayerSettings s = brightnessContrastSettings(v);
            return Adjust::spec(QStringLiteral("Brightness/Contrast"), s.buildMap());
        });
    }));
    adj->addAction(makeAction(QStringLiteral("image.levels"), QStringLiteral("Levels..."), keys({"Ctrl+L", "Ctrl+Alt+L"}), [this, altHeld] {
        if (!prepareTarget(QStringLiteral("Levels"))) return;
        LevelsDialog dlg(currentDoc(), altHeld(), this);
        execPreview(dlg);
    }));
    adj->addAction(makeAction(QStringLiteral("image.curves"), QStringLiteral("Curves..."), keys({"Ctrl+M", "Ctrl+Alt+M"}), [this, altHeld] {
        if (!prepareTarget(QStringLiteral("Curves"))) return;
        CurvesDialog dlg(currentDoc(), altHeld(), this);
        execPreview(dlg);
    }));
    stub(adj, QStringLiteral("Exposure..."));
    adj->addSeparator();
    stub(adj, QStringLiteral("Vibrance..."));
    adj->addAction(makeAction(QStringLiteral("image.hueSaturation"), QStringLiteral("Hue/Saturation..."), keys({"Ctrl+U", "Ctrl+Alt+U"}),
                              [this, altHeld] {
        if (!prepareTarget(QStringLiteral("Hue/Saturation"))) return;
        HueSaturationDialog dlg(currentDoc(), altHeld(), this);
        execPreview(dlg);
    }));
    adj->addAction(makeAction(QStringLiteral("image.colorBalance"), QStringLiteral("Color Balance..."), keys({"Ctrl+B", "Ctrl+Alt+B"}),
                              [this, altHeld] {
        if (!prepareTarget(QStringLiteral("Color Balance"))) return;
        ColorBalanceDialog dlg(currentDoc(), altHeld(), this);
        execPreview(dlg);
    }));
    adj->addAction(makeAction(QStringLiteral("image.blackWhite"), QStringLiteral("Black && White..."), keys({"Ctrl+Alt+Shift+B"}), [this] {
        paramDialog(QStringLiteral("Black and White"), false, false, setupBlackWhite, [](const Values& v) {
            return Adjust::spec(QStringLiteral("Black & White"), blackWhiteSettings(v).buildMap());
        });
    }));
    stub(adj, QStringLiteral("Photo Filter..."));
    stub(adj, QStringLiteral("Channel Mixer..."));
    stub(adj, QStringLiteral("Color Lookup..."));
    adj->addSeparator();
    adj->addAction(makeAction(QStringLiteral("image.invert"), QStringLiteral("Invert"), keys({"Ctrl+I"}), [this] {
        applySpec(Adjust::spec(QStringLiteral("Invert"), Adjust::invertMap()));
    }));
    adj->addAction(makeAction(QStringLiteral("image.posterize"), QStringLiteral("Posterize..."), {}, [this] {
        paramDialog(QStringLiteral("Posterize"), false, false, setupPosterize, [](const Values& v) {
            return Adjust::spec(QStringLiteral("Posterize"), posterizeSettings(v).buildMap());
        });
    }));
    adj->addAction(makeAction(QStringLiteral("image.threshold"), QStringLiteral("Threshold..."), {}, [this] {
        if (!prepareTarget(QStringLiteral("Threshold"))) return;
        ThresholdDialog dlg(currentDoc(), this);
        execPreview(dlg);
    }));
    stub(adj, QStringLiteral("Gradient Map..."));
    stub(adj, QStringLiteral("Selective Color..."));
    adj->addSeparator();
    stub(adj, QStringLiteral("Shadows/Highlights..."));
    stub(adj, QStringLiteral("HDR Toning..."));
    adj->addSeparator();
    adj->addAction(makeAction(QStringLiteral("image.desaturate"), QStringLiteral("Desaturate"), keys({"Ctrl+Shift+U"}), [this] {
        applySpec(Adjust::spec(QStringLiteral("Desaturate"), Adjust::desaturateMap()));
    }));
    stub(adj, QStringLiteral("Match Color..."));
    stub(adj, QStringLiteral("Replace Color..."));
    stub(adj, QStringLiteral("Equalize"));
    image->addSeparator();
    image->addAction(makeAction(QStringLiteral("image.autoTone"), QStringLiteral("Auto Tone"), keys({"Ctrl+Shift+L"}),
                                [this] { autoAdjust(Adjust::AutoMode::Tone, QStringLiteral("Auto Tone")); }));
    image->addAction(makeAction(QStringLiteral("image.autoContrast"), QStringLiteral("Auto Contrast"), keys({"Ctrl+Alt+Shift+L"}),
                                [this] { autoAdjust(Adjust::AutoMode::Contrast, QStringLiteral("Auto Contrast")); }));
    image->addAction(makeAction(QStringLiteral("image.autoColor"), QStringLiteral("Auto Color"), keys({"Ctrl+Shift+B"}),
                                [this] { autoAdjust(Adjust::AutoMode::Color, QStringLiteral("Auto Color")); }));
    image->addSeparator();
    image->addAction(makeAction(QStringLiteral("image.imageSize"), QStringLiteral("Image Size..."), keys({"Ctrl+Alt+I"}), [this] { imageSizeDialog(); }));
    image->addAction(makeAction(QStringLiteral("image.canvasSize"), QStringLiteral("Canvas Size..."), keys({"Ctrl+Alt+C"}), [this] { canvasSizeDialog(); }));
    QMenu* rot = image->addMenu(QStringLiteral("Image Rotation"));
    auto rotAct = [&](const QString& id, const QString& text, Ops::Rotation how) {
        rot->addAction(makeAction(id, text, {}, [this, how] {
            if (Document* d = currentDoc()) Ops::rotate(d, how);
        }));
    };
    rotAct(QStringLiteral("image.rot180"), QStringLiteral("180°"), Ops::Rotation::Rotate180);
    rotAct(QStringLiteral("image.rot90cw"), QStringLiteral("90° Clockwise"), Ops::Rotation::Rotate90CW);
    rotAct(QStringLiteral("image.rot90ccw"), QStringLiteral("90° Counter Clockwise"), Ops::Rotation::Rotate90CCW);
    stub(rot, QStringLiteral("Arbitrary..."));
    rot->addSeparator();
    rotAct(QStringLiteral("image.flipH"), QStringLiteral("Flip Canvas Horizontal"), Ops::Rotation::FlipHorizontal);
    rotAct(QStringLiteral("image.flipV"), QStringLiteral("Flip Canvas Vertical"), Ops::Rotation::FlipVertical);
    image->addAction(makeAction(QStringLiteral("image.crop"), QStringLiteral("Crop"), {}, [this] {
        if (Document* d = currentDoc()) Ops::cropToSelection(d);
    }));
    stub(image, QStringLiteral("Trim..."));
    stub(image, QStringLiteral("Reveal All"));
    image->addSeparator();
    image->addAction(makeAction(QStringLiteral("image.duplicate"), QStringLiteral("Duplicate..."), {}, [this] {
        Document* d = currentDoc();
        if (!d) return;
        bool ok = false;
        QString name = QInputDialog::getText(this, QStringLiteral("Duplicate Image"), QStringLiteral("As:"),
                                             QLineEdit::Normal, d->title() + QStringLiteral(" copy"), &ok);
        if (!ok) return;
        auto* copy = new Document(d->size());
        DocState s = d->state();
        Tree::renewIds(s.layers);
        copy->initialize(s);
        copy->setTitle(name);
        addDocument(copy);
    }));
    stub(image, QStringLiteral("Apply Image..."));
    stub(image, QStringLiteral("Calculations..."));

    // ---------------- Layer ----------------
    QMenu* layer = mb->addMenu(QStringLiteral("&Layer"));
    auto withDoc = [this](const std::function<void(Document*)>& fn) {
        return [this, fn] {
            if (Document* d = currentDoc()) fn(d);
        };
    };
    auto withError = [this](const std::function<bool(Document*, QString*)>& fn) {
        return [this, fn] {
            QString err;
            if (Document* d = currentDoc(); d && !fn(d, &err) && !err.isEmpty()) alert(err);
        };
    };
    QMenu* newMenu = layer->addMenu(QStringLiteral("New"));
    newMenu->addAction(makeAction(QStringLiteral("layer.new"), QStringLiteral("Layer..."), keys({"Ctrl+Shift+N"}), [this] { newLayerDialog(); }));
    makeAction(QStringLiteral("layer.newQuick"), QStringLiteral("New Layer"), keys({"Ctrl+Alt+Shift+N"}), [this] {
        if (Document* d = currentDoc()) Ops::newLayer(d);
    });
    addAction(A(QStringLiteral("layer.newQuick")));
    newMenu->addAction(makeAction(QStringLiteral("layer.fromBackground"), QStringLiteral("Layer from Background..."), {}, [this] { layerFromBackground(); }));
    newMenu->addAction(makeAction(QStringLiteral("layer.newGroup"), QStringLiteral("Group..."), {}, [this] {
        Document* d = currentDoc();
        if (!d) return;
        bool ok = false;
        const QString name = QInputDialog::getText(this, QStringLiteral("New Group"), QStringLiteral("Name:"), QLineEdit::Normal,
                                                   d->nextName(QStringLiteral("Group")), &ok);
        if (ok) Ops::newGroup(d, name);
    }));
    makeAction(QStringLiteral("layer.newGroupQuick"), QStringLiteral("Create a New Group"), {}, withDoc([](Document* d) { Ops::newGroup(d); }));
    newMenu->addAction(makeAction(QStringLiteral("layer.groupFromLayers"), QStringLiteral("Group from Layers..."), {},
                                  withError([](Document* d, QString* e) { return Ops::groupLayer(d, e); })));
    newMenu->addSeparator();
    newMenu->addAction(makeAction(QStringLiteral("layer.viaCopy"), QStringLiteral("Layer via Copy"), keys({"Ctrl+J"}),
                                  withError([](Document* d, QString* e) { return Ops::layerViaCopy(d, false, e); })));
    newMenu->addAction(makeAction(QStringLiteral("layer.viaCut"), QStringLiteral("Layer via Cut"), keys({"Ctrl+Shift+J"}),
                                  withError([](Document* d, QString* e) { return Ops::layerViaCopy(d, true, e); })));
    layer->addAction(makeAction(QStringLiteral("layer.duplicate"), QStringLiteral("Duplicate Layer..."), {}, [this] { duplicateLayerDialog(); }));
    layer->addAction(makeAction(QStringLiteral("layer.delete"), QStringLiteral("Delete Layer"), {},
                                withError([](Document* d, QString* e) { return Ops::deleteLayer(d, e); })));
    layer->addSeparator();
    layer->addAction(makeAction(QStringLiteral("layer.rename"), QStringLiteral("Rename Layer..."), {}, [this] {
        Document* d = currentDoc();
        if (!d || !d->activeLayer()) return;
        bool ok = false;
        QString n = QInputDialog::getText(this, QStringLiteral("Rename Layer"), QStringLiteral("Name:"), QLineEdit::Normal,
                                          d->activeLayer()->name, &ok);
        if (ok) Ops::rename(d, d->activeIndex(), n);
    }));

    // Layer Style
    QMenu* styleMenu = layer->addMenu(QStringLiteral("Layer Style"));
    const struct { const char* id; const char* text; LayerStyleDialog::Page page; } stylePages[] = {
        {"layer.styleBlending", "Blending Options...", LayerStyleDialog::Blending},
        {"layer.styleStroke", "Stroke...", LayerStyleDialog::StrokePage},
        {"layer.styleColorOverlay", "Color Overlay...", LayerStyleDialog::ColorOverlayPage},
        {"layer.styleOuterGlow", "Outer Glow...", LayerStyleDialog::OuterGlowPage},
        {"layer.styleDropShadow", "Drop Shadow...", LayerStyleDialog::DropShadowPage},
    };
    for (const auto& sp : stylePages) {
        const LayerStyleDialog::Page page = sp.page;
        styleMenu->addAction(makeAction(QString::fromLatin1(sp.id), QString::fromLatin1(sp.text), {}, [this, page] { layerStyleDialog(page); }));
        if (page == LayerStyleDialog::Blending) styleMenu->addSeparator();
    }
    styleMenu->addSeparator();
    styleMenu->addAction(makeAction(QStringLiteral("layer.styleCopy"), QStringLiteral("Copy Layer Style"), {}, [this] {
        if (Document* d = currentDoc(); d && d->activeLayer() && d->activeLayer()->style) m_copiedStyle = d->activeLayer()->style;
        updateActions();
    }));
    styleMenu->addAction(makeAction(QStringLiteral("layer.stylePaste"), QStringLiteral("Paste Layer Style"), {}, [this] {
        if (Document* d = currentDoc(); d && m_copiedStyle) Ops::setStyle(d, d->activeIndex(), m_copiedStyle, QStringLiteral("Paste Layer Style"));
    }));
    styleMenu->addAction(makeAction(QStringLiteral("layer.styleClear"), QStringLiteral("Clear Layer Style"), {},
                                    withDoc([](Document* d) { Ops::setStyle(d, d->activeIndex(), nullptr, QStringLiteral("Clear Layer Style")); })));
    styleMenu->addSeparator();
    styleMenu->addAction(makeAction(QStringLiteral("layer.styleHide"), QStringLiteral("Hide All Effects"), {}, withDoc([](Document* d) {
        const Layer* l = d->activeLayer();
        if (!l || !l->style) return;
        LayerStyle st = *l->style;
        st.visible = !st.visible;
        Ops::setStyle(d, d->activeIndex(), std::make_shared<const LayerStyle>(st),
                      st.visible ? QStringLiteral("Show Layer Effects") : QStringLiteral("Hide Layer Effects"));
    })));
    stub(layer, QStringLiteral("Smart Filter"));
    layer->addSeparator();
    stub(layer, QStringLiteral("New Fill Layer"));

    // New Adjustment Layer
    QMenu* adjLayer = layer->addMenu(QStringLiteral("New Adjustment Layer"));
    const struct { const char* id; const char* text; Adjust::Kind kind; } adjKinds[] = {
        {"brightnessContrast", "Brightness/Contrast...", Adjust::Kind::BrightnessContrast},
        {"levels", "Levels...", Adjust::Kind::Levels},
        {"curves", "Curves...", Adjust::Kind::Curves},
        {"hueSaturation", "Hue/Saturation...", Adjust::Kind::HueSaturation},
        {"colorBalance", "Color Balance...", Adjust::Kind::ColorBalance},
        {"blackWhite", "Black && White...", Adjust::Kind::BlackWhite},
        {"invert", "Invert...", Adjust::Kind::Invert},
        {"posterize", "Posterize...", Adjust::Kind::Posterize},
        {"threshold", "Threshold...", Adjust::Kind::Threshold},
    };
    for (const auto& k : adjKinds) {
        const Adjust::Kind kind = k.kind;
        if (kind == Adjust::Kind::HueSaturation || kind == Adjust::Kind::Invert) adjLayer->addSeparator();
        adjLayer->addAction(makeAction(QStringLiteral("layer.newAdjustment.") + QLatin1String(k.id), QString::fromUtf8(k.text), {},
                                       [this, kind] { newAdjustmentLayer(kind); }));
    }
    layer->addAction(makeAction(QStringLiteral("layer.contentOptions"), QStringLiteral("Layer Content Options..."), {}, [this] {
        if (Document* d = currentDoc()) editAdjustmentLayer(d->activeIndex());
    }));
    layer->addSeparator();

    // Layer Mask
    QMenu* maskMenu = layer->addMenu(QStringLiteral("Layer Mask"));
    const struct { const char* id; const char* text; Ops::MaskFill fill; } maskFills[] = {
        {"layer.maskRevealAll", "Reveal All", Ops::MaskFill::RevealAll},
        {"layer.maskHideAll", "Hide All", Ops::MaskFill::HideAll},
        {"layer.maskRevealSelection", "Reveal Selection", Ops::MaskFill::RevealSelection},
        {"layer.maskHideSelection", "Hide Selection", Ops::MaskFill::HideSelection},
    };
    for (const auto& mf : maskFills) {
        const Ops::MaskFill fill = mf.fill;
        maskMenu->addAction(makeAction(QString::fromLatin1(mf.id), QString::fromLatin1(mf.text), {},
                                       withError([fill](Document* d, QString* e) { return Ops::addMask(d, fill, e); })));
    }
    // The Layers panel's mask button: from the selection when there is one; Alt hides.
    makeAction(QStringLiteral("layer.maskAdd"), QStringLiteral("Add Layer Mask"), {}, withError([](Document* d, QString* e) {
        const bool hide = QApplication::keyboardModifiers() & Qt::AltModifier;
        const Ops::MaskFill fill = d->hasSelection() ? (hide ? Ops::MaskFill::HideSelection : Ops::MaskFill::RevealSelection)
                                                     : (hide ? Ops::MaskFill::HideAll : Ops::MaskFill::RevealAll);
        return Ops::addMask(d, fill, e);
    }));
    maskMenu->addSeparator();
    maskMenu->addAction(makeAction(QStringLiteral("layer.maskDelete"), QStringLiteral("Delete"), {},
                                   withError([](Document* d, QString* e) { return Ops::deleteMask(d, false, e); })));
    maskMenu->addAction(makeAction(QStringLiteral("layer.maskApply"), QStringLiteral("Apply"), {},
                                   withError([](Document* d, QString* e) { return Ops::deleteMask(d, true, e); })));
    maskMenu->addSeparator();
    maskMenu->addAction(makeAction(QStringLiteral("layer.maskToggle"), QStringLiteral("Disable"), {}, withDoc([](Document* d) {
        if (const Layer* l = d->activeLayer(); l && l->mask) Ops::setMaskEnabled(d, d->activeIndex(), !l->maskEnabled);
    })));
    maskMenu->addAction(makeAction(QStringLiteral("layer.maskLink"), QStringLiteral("Unlink"), {}, withDoc([](Document* d) {
        if (const Layer* l = d->activeLayer(); l && l->mask) Ops::setMaskLinked(d, d->activeIndex(), !l->maskLinked);
    })));
    maskMenu->addSeparator();
    maskMenu->addAction(makeAction(QStringLiteral("layer.maskLoadSelection"), QStringLiteral("Load Selection"), {},
                                   withDoc([](Document* d) { Ops::loadSelectionFromMask(d, d->activeIndex()); })));
    stub(layer, QStringLiteral("Vector Mask"));
    layer->addAction(makeAction(QStringLiteral("layer.clipping"), QStringLiteral("Create Clipping Mask"), keys({"Ctrl+Alt+G"}),
                                withError([](Document* d, QString* e) { return Ops::toggleClippingMask(d, e); })));
    layer->addSeparator();
    stub(layer, QStringLiteral("Smart Objects"));
    QMenu* rasterize = layer->addMenu(QStringLiteral("Rasterize"));
    rasterize->addAction(makeAction(QStringLiteral("layer.rasterizeType"), QStringLiteral("Type"), {},
                                    withDoc([](Document* d) { Ops::rasterizeLayer(d, d->activeIndex()); })));
    rasterize->addAction(makeAction(QStringLiteral("layer.rasterizeShape"), QStringLiteral("Shape"), {},
                                    withDoc([](Document* d) { Ops::rasterizeLayer(d, d->activeIndex()); })));
    rasterize->addAction(makeAction(QStringLiteral("layer.rasterizeStyle"), QStringLiteral("Layer Style"), {},
                                    withDoc([](Document* d) { Ops::rasterizeStyle(d, d->activeIndex()); })));
    rasterize->addAction(makeAction(QStringLiteral("layer.rasterizeLayer"), QStringLiteral("Layer"), {}, withDoc([](Document* d) {
        const int i = d->activeIndex();
        if (d->layerAt(i).isVector()) Ops::rasterizeLayer(d, i);
        if (d->layerAt(i).hasStyle()) Ops::rasterizeStyle(d, i);
    })));
    layer->addSeparator();
    layer->addAction(makeAction(QStringLiteral("layer.group"), QStringLiteral("Group Layers"), keys({"Ctrl+G"}),
                                withError([](Document* d, QString* e) { return Ops::groupLayer(d, e); })));
    layer->addAction(makeAction(QStringLiteral("layer.ungroup"), QStringLiteral("Ungroup Layers"), keys({"Ctrl+Shift+G"}),
                                withError([](Document* d, QString* e) { return Ops::ungroup(d, e); })));
    layer->addAction(makeAction(QStringLiteral("layer.hide"), QStringLiteral("Hide Layers"), keys({"Ctrl+,"}), [this] {
        if (Document* d = currentDoc(); d && d->activeLayer())
            Ops::setVisible(d, d->activeIndex(), !d->activeLayer()->visible);
    }));
    layer->addSeparator();
    QMenu* arrange = layer->addMenu(QStringLiteral("Arrange"));
    auto arrAct = [&](const QString& id, const QString& text, const char* key, Ops::Arrange how) {
        arrange->addAction(makeAction(id, text, keys({key}), [this, how] {
            if (Document* d = currentDoc()) Ops::arrange(d, how);
        }));
    };
    arrAct(QStringLiteral("layer.front"), QStringLiteral("Bring to Front"), "Ctrl+Shift+]", Ops::Arrange::BringToFront);
    arrAct(QStringLiteral("layer.forward"), QStringLiteral("Bring Forward"), "Ctrl+]", Ops::Arrange::BringForward);
    arrAct(QStringLiteral("layer.backward"), QStringLiteral("Send Backward"), "Ctrl+[", Ops::Arrange::SendBackward);
    arrAct(QStringLiteral("layer.back"), QStringLiteral("Send to Back"), "Ctrl+Shift+[", Ops::Arrange::SendToBack);
    stub(layer, QStringLiteral("Combine Shapes"));
    stub(layer, QStringLiteral("Align"));
    stub(layer, QStringLiteral("Distribute"));
    layer->addSeparator();
    layer->addAction(makeAction(QStringLiteral("layer.lockAll"), QStringLiteral("Lock Layers..."), keys({"Ctrl+/"}), [this] {
        Document* d = currentDoc();
        if (!d || !d->activeLayer() || d->activeLayer()->isBackground) return;
        const Layer& l = *d->activeLayer();
        Ops::setLocks(d, d->activeIndex(), l.lockTransparency, l.lockPixels, l.lockPosition, !l.lockAll);
    }));
    layer->addSeparator();
    stub(layer, QStringLiteral("Link Layers"));
    stub(layer, QStringLiteral("Select Linked Layers"));
    layer->addSeparator();
    layer->addAction(makeAction(QStringLiteral("layer.mergeDown"), QStringLiteral("Merge Down"), keys({"Ctrl+E"}),
                                withError([](Document* d, QString* e) { return Ops::mergeDown(d, e); })));
    layer->addAction(makeAction(QStringLiteral("layer.mergeVisible"), QStringLiteral("Merge Visible"), keys({"Ctrl+Shift+E"}),
                                withError([](Document* d, QString* e) { return Ops::mergeVisible(d, e); })));
    layer->addAction(makeAction(QStringLiteral("layer.flatten"), QStringLiteral("Flatten Image"), {}, [this] {
        Document* d = currentDoc();
        if (!d) return;
        bool hidden = false;
        for (int i = 0; i < d->layerCount(); ++i)
            if (!Tree::effectivelyVisible(d->layers(), i)) hidden = true;
        if (hidden && QMessageBox::question(this, QStringLiteral("PhotoSlop"), QStringLiteral("Discard hidden layers?"),
                                            QMessageBox::Ok | QMessageBox::Cancel) != QMessageBox::Ok)
            return;
        Ops::flatten(d);
    }));
    stub(layer, QStringLiteral("Matting"));

    // ---------------- Type ----------------
    QMenu* type = mb->addMenu(QStringLiteral("&Type"));
    for (const char* t : {"More Fonts...", "Panels", "Anti-Alias", "Orientation", "OpenType", "Extrude to 3D", "Create Work Path",
                          "Convert to Shape"})
        stub(type, QString::fromLatin1(t));
    type->addAction(makeAction(QStringLiteral("type.rasterize"), QStringLiteral("Rasterize Type Layer"), {},
                               withDoc([](Document* d) { Ops::rasterizeLayer(d, d->activeIndex()); })));
    // Double-clicking a type layer's thumbnail edits its text with the Type tool.
    makeAction(QStringLiteral("type.edit"), QStringLiteral("Edit Type"), {}, [this] {
        Document* d = currentDoc();
        CanvasView* v = currentView();
        if (!d || !v || !d->activeLayer() || d->activeLayer()->kind != LayerKind::Text) return;
        auto* t = static_cast<TypeTool*>(m_tools->tool(QStringLiteral("type")));
        m_tools->select(t);
        if (t->beginEdit(v, d->activeIndex(), QPointF())) t->setCaret(int(t->textData().text.size()));
        v->setFocus();
    });
    for (const char* t : {"Convert Text Shape Type", "Warp Text...", "Match Font...", "Font Preview Size", "Language Options",
                          "Update All Text Layers", "Manage Missing Fonts", "Paste Lorem Ipsum", "Load Default Type Styles",
                          "Save Default Type Styles"})
        stub(type, QString::fromLatin1(t));

    // ---------------- Select ----------------
    QMenu* select = mb->addMenu(QStringLiteral("&Select"));
    select->addAction(makeAction(QStringLiteral("select.all"), QStringLiteral("All"), keys({"Ctrl+A"}), [this] {
        if (Document* d = currentDoc()) Ops::selectAll(d);
    }));
    select->addAction(makeAction(QStringLiteral("select.deselect"), QStringLiteral("Deselect"), keys({"Ctrl+D"}), [this] {
        if (Document* d = currentDoc()) Ops::deselect(d);
    }));
    select->addAction(makeAction(QStringLiteral("select.reselect"), QStringLiteral("Reselect"), keys({"Ctrl+Shift+D"}), [this] {
        if (Document* d = currentDoc()) Ops::reselect(d);
    }));
    select->addAction(makeAction(QStringLiteral("select.inverse"), QStringLiteral("Inverse"), keys({"Ctrl+Shift+I"}), [this] {
        if (Document* d = currentDoc()) Ops::inverse(d);
    }));
    select->addSeparator();
    stub(select, QStringLiteral("All Layers"), QKeySequence(QStringLiteral("Ctrl+Alt+A")));
    stub(select, QStringLiteral("Deselect Layers"));
    stub(select, QStringLiteral("Find Layers"), QKeySequence(QStringLiteral("Ctrl+Alt+Shift+F")));
    stub(select, QStringLiteral("Isolate Layers"));
    select->addSeparator();
    stub(select, QStringLiteral("Color Range..."));
    stub(select, QStringLiteral("Focus Area..."));
    stub(select, QStringLiteral("Subject"));
    stub(select, QStringLiteral("Sky"));
    stub(select, QStringLiteral("Select and Mask..."), QKeySequence(QStringLiteral("Ctrl+Alt+R")));
    QMenu* modify = select->addMenu(QStringLiteral("Modify"));
    const struct { const char* id; const char* text; Ops::Modify how; } modifyItems[] = {
        {"select.border", "Border...", Ops::Modify::Border},
        {"select.smooth", "Smooth...", Ops::Modify::Smooth},
        {"select.expand", "Expand...", Ops::Modify::Expand},
        {"select.contract", "Contract...", Ops::Modify::Contract},
    };
    for (const auto& m : modifyItems) {
        const int how = int(m.how);
        modify->addAction(makeAction(QString::fromLatin1(m.id), QString::fromLatin1(m.text), {}, [this, how] { modifySelectionDialog(how); }));
    }
    modify->addAction(makeAction(QStringLiteral("select.feather"), QStringLiteral("Feather..."), keys({"Shift+F6"}), [this] { featherDialog(); }));
    select->addSeparator();
    auto wandTolerance = [this] { return static_cast<MagicWandTool*>(m_tools->tool(QStringLiteral("magic-wand")))->tolerance(); };
    select->addAction(makeAction(QStringLiteral("select.grow"), QStringLiteral("Grow"), {}, [this, wandTolerance] {
        if (Document* d = currentDoc()) Ops::growSelection(d, wandTolerance(), true);
    }));
    select->addAction(makeAction(QStringLiteral("select.similar"), QStringLiteral("Similar"), {}, [this, wandTolerance] {
        if (Document* d = currentDoc()) Ops::growSelection(d, wandTolerance(), false);
    }));
    select->addSeparator();
    select->addAction(makeAction(QStringLiteral("select.transform"), QStringLiteral("Transform Selection"), {},
                                 [this] { startTransform(true, int(FreeTransformTool::Mode::Free)); }));
    select->addSeparator();
    QAction* quickMask = makeAction(QStringLiteral("select.quickMask"), QStringLiteral("Edit in Quick Mask Mode"), keys({"Q"}),
                                    [this] { toggleQuickMask(); });
    quickMask->setCheckable(true);
    select->addAction(quickMask);
    select->addSeparator();
    stub(select, QStringLiteral("Load Selection..."));
    stub(select, QStringLiteral("Save Selection..."));

    // ---------------- Filter ----------------
    QMenu* filter = mb->addMenu(QStringLiteral("Fil&ter"));
    filter->addAction(makeAction(QStringLiteral("filter.last"), QStringLiteral("Last Filter"), keys({"Ctrl+Alt+F"}), [this] {
        if (m_lastFilter) applySpec(m_lastFilter(), m_lastFilter);
    }));
    filter->addSeparator();
    stub(filter, QStringLiteral("Convert for Smart Filters"));
    filter->addSeparator();
    stub(filter, QStringLiteral("Neural Filters..."));
    stub(filter, QStringLiteral("Filter Gallery..."));
    stub(filter, QStringLiteral("Adaptive Wide Angle..."), QKeySequence(QStringLiteral("Ctrl+Alt+Shift+A")));
    stub(filter, QStringLiteral("Camera Raw Filter..."), QKeySequence(QStringLiteral("Ctrl+Shift+A")));
    stub(filter, QStringLiteral("Lens Correction..."), QKeySequence(QStringLiteral("Ctrl+Shift+R")));
    stub(filter, QStringLiteral("Liquify..."), QKeySequence(QStringLiteral("Ctrl+Shift+X")));
    stub(filter, QStringLiteral("Vanishing Point..."), QKeySequence(QStringLiteral("Ctrl+Alt+V")));
    filter->addSeparator();
    const QList<QPair<QString, QStringList>> filterGroups = {
        {QStringLiteral("Blur"), {QStringLiteral("Average"), QStringLiteral("Blur"), QStringLiteral("Blur More"), QStringLiteral("Box Blur..."),
                                  QStringLiteral("Gaussian Blur..."), QStringLiteral("Lens Blur..."), QStringLiteral("Motion Blur..."),
                                  QStringLiteral("Radial Blur..."), QStringLiteral("Shape Blur..."), QStringLiteral("Smart Blur..."),
                                  QStringLiteral("Surface Blur...")}},
        {QStringLiteral("Blur Gallery"), {QStringLiteral("Field Blur..."), QStringLiteral("Iris Blur..."), QStringLiteral("Tilt-Shift..."),
                                          QStringLiteral("Path Blur..."), QStringLiteral("Spin Blur...")}},
        {QStringLiteral("Distort"), {QStringLiteral("Displace..."), QStringLiteral("Pinch..."), QStringLiteral("Polar Coordinates..."),
                                     QStringLiteral("Ripple..."), QStringLiteral("Shear..."), QStringLiteral("Spherize..."),
                                     QStringLiteral("Twirl..."), QStringLiteral("Wave..."), QStringLiteral("ZigZag...")}},
        {QStringLiteral("Noise"), {QStringLiteral("Add Noise..."), QStringLiteral("Despeckle"), QStringLiteral("Dust & Scratches..."),
                                   QStringLiteral("Median..."), QStringLiteral("Reduce Noise...")}},
        {QStringLiteral("Pixelate"), {QStringLiteral("Color Halftone..."), QStringLiteral("Crystallize..."), QStringLiteral("Facet"),
                                      QStringLiteral("Fragment"), QStringLiteral("Mezzotint..."), QStringLiteral("Mosaic..."),
                                      QStringLiteral("Pointillize...")}},
        {QStringLiteral("Render"), {QStringLiteral("Flame..."), QStringLiteral("Picture Frame..."), QStringLiteral("Tree..."),
                                    QStringLiteral("Clouds"), QStringLiteral("Difference Clouds"), QStringLiteral("Fibers..."),
                                    QStringLiteral("Lens Flare..."), QStringLiteral("Lighting Effects...")}},
        {QStringLiteral("Sharpen"), {QStringLiteral("Shake Reduction..."), QStringLiteral("Sharpen"), QStringLiteral("Sharpen Edges"),
                                     QStringLiteral("Sharpen More"), QStringLiteral("Smart Sharpen..."), QStringLiteral("Unsharp Mask...")}},
        {QStringLiteral("Stylize"), {QStringLiteral("Diffuse..."), QStringLiteral("Emboss..."), QStringLiteral("Extrude..."),
                                     QStringLiteral("Find Edges"), QStringLiteral("Oil Paint..."), QStringLiteral("Solarize"),
                                     QStringLiteral("Tiles..."), QStringLiteral("Trace Contour..."), QStringLiteral("Wind...")}},
        {QStringLiteral("Video"), {QStringLiteral("De-Interlace..."), QStringLiteral("NTSC Colors")}},
        {QStringLiteral("Other"), {QStringLiteral("Custom..."), QStringLiteral("High Pass..."), QStringLiteral("HSB/HSL"),
                                   QStringLiteral("Maximum..."), QStringLiteral("Minimum..."), QStringLiteral("Offset...")}},
    };
    // Implemented filters: fixed ones apply at once, the others open a dialog.
    auto fixedFilter = [this](Filters::Spec (*make)()) { return [this, make] { applySpec(make(), make); }; };
    auto radiusSlider = [](ParamDialog& d, double def) {
        d.addSlider(QStringLiteral("radius"), QStringLiteral("Radius:"), 0.1, 1000, def, 1, QStringLiteral("Pixels"), 250);
    };
    const QHash<QString, std::function<void()>> filterActions = {
        {QStringLiteral("Blur"), fixedFilter(&Filters::blurSpec)},
        {QStringLiteral("Blur More"), fixedFilter(&Filters::blurMoreSpec)},
        {QStringLiteral("Box Blur..."), [this] {
             paramDialog(QStringLiteral("Box Blur"), true, true, [](ParamDialog& d) {
                 d.addSlider(QStringLiteral("radius"), QStringLiteral("Radius:"), 1, 2000, 5, 0, QStringLiteral("Pixels"), 250);
             }, [](const Values& v) { return Filters::boxBlurSpec(int(v[QStringLiteral("radius")])); });
         }},
        {QStringLiteral("Gaussian Blur..."), [this, radiusSlider] {
             paramDialog(QStringLiteral("Gaussian Blur"), true, true, [radiusSlider](ParamDialog& d) { radiusSlider(d, 1.0); },
                         [](const Values& v) { return Filters::gaussianBlurSpec(v[QStringLiteral("radius")]); });
         }},
        {QStringLiteral("Motion Blur..."), [this] {
             paramDialog(QStringLiteral("Motion Blur"), true, true, [](ParamDialog& d) {
                 d.addSlider(QStringLiteral("angle"), QStringLiteral("Angle:"), -360, 360, 0, 0, QStringLiteral("°"));
                 d.addSlider(QStringLiteral("distance"), QStringLiteral("Distance:"), 1, 2000, 10, 0, QStringLiteral("Pixels"), 500);
             }, [](const Values& v) {
                 return Filters::motionBlurSpec(v[QStringLiteral("angle")], int(v[QStringLiteral("distance")]));
             });
         }},
        {QStringLiteral("Add Noise..."), [this] {
             paramDialog(QStringLiteral("Add Noise"), false, true, [](ParamDialog& d) {
                 d.addSlider(QStringLiteral("amount"), QStringLiteral("Amount:"), 0.1, 400, 12.5, 1, QStringLiteral("%"), 100);
                 d.addChoice(QStringLiteral("gaussian"), QStringLiteral("Distribution"), {QStringLiteral("Uniform"), QStringLiteral("Gaussian")}, 0);
                 d.addCheck(QStringLiteral("mono"), QStringLiteral("Monochromatic"), false);
                 // One noise pattern per dialog, so the preview matches the result.
                 d.addHidden(QStringLiteral("seed"), QRandomGenerator::global()->generate());
             }, [](const Values& v) {
                 return Filters::addNoiseSpec(v[QStringLiteral("amount")], v[QStringLiteral("gaussian")] != 0,
                                              v[QStringLiteral("mono")] != 0, quint32(v[QStringLiteral("seed")]));
             });
         }},
        {QStringLiteral("Median..."), [this] {
             paramDialog(QStringLiteral("Median"), true, true, [](ParamDialog& d) {
                 d.addSlider(QStringLiteral("radius"), QStringLiteral("Radius:"), 1, 500, 1, 0, QStringLiteral("Pixels"), 100);
             }, [](const Values& v) { return Filters::medianSpec(int(v[QStringLiteral("radius")])); });
         }},
        {QStringLiteral("Mosaic..."), [this] {
             paramDialog(QStringLiteral("Mosaic"), true, true, [](ParamDialog& d) {
                 d.addSlider(QStringLiteral("cell"), QStringLiteral("Cell Size:"), 2, 200, 10, 0, QStringLiteral("square"));
             }, [](const Values& v) { return Filters::mosaicSpec(int(v[QStringLiteral("cell")])); });
         }},
        {QStringLiteral("Sharpen"), fixedFilter(&Filters::sharpenSpec)},
        {QStringLiteral("Sharpen More"), fixedFilter(&Filters::sharpenMoreSpec)},
        {QStringLiteral("Unsharp Mask..."), [this] {
             paramDialog(QStringLiteral("Unsharp Mask"), false, true, [](ParamDialog& d) {
                 d.addSlider(QStringLiteral("amount"), QStringLiteral("Amount:"), 1, 500, 100, 0, QStringLiteral("%"));
                 d.addSlider(QStringLiteral("radius"), QStringLiteral("Radius:"), 0.1, 1000, 1.0, 1, QStringLiteral("Pixels"), 250);
                 d.addSlider(QStringLiteral("threshold"), QStringLiteral("Threshold:"), 0, 255, 0, 0, QStringLiteral("levels"));
             }, [](const Values& v) {
                 return Filters::unsharpMaskSpec(v[QStringLiteral("amount")], v[QStringLiteral("radius")], int(v[QStringLiteral("threshold")]));
             });
         }},
        {QStringLiteral("High Pass..."), [this, radiusSlider] {
             paramDialog(QStringLiteral("High Pass"), false, true, [radiusSlider](ParamDialog& d) { radiusSlider(d, 10.0); },
                         [](const Values& v) { return Filters::highPassSpec(v[QStringLiteral("radius")]); });
         }},
    };
    for (const auto& [groupName, items] : filterGroups) {
        QMenu* sub = filter->addMenu(groupName);
        for (const QString& item : items) {
            const auto it = filterActions.constFind(item);
            if (it == filterActions.constEnd()) {
                stub(sub, item);
                continue;
            }
            QString id = item;
            id.remove(QStringLiteral("...")).remove(QLatin1Char(' '));
            id[0] = id[0].toLower();
            sub->addAction(makeAction(QStringLiteral("filter.") + id, item, {}, *it));
        }
    }

    // ---------------- View ----------------
    QMenu* view = mb->addMenu(QStringLiteral("&View"));
    stub(view, QStringLiteral("Proof Setup"));
    stub(view, QStringLiteral("Proof Colors"), QKeySequence(QStringLiteral("Ctrl+Y")));
    stub(view, QStringLiteral("Gamut Warning"), QKeySequence(QStringLiteral("Ctrl+Shift+Y")));
    stub(view, QStringLiteral("Pixel Aspect Ratio"));
    view->addSeparator();
    view->addAction(makeAction(QStringLiteral("view.zoomIn"), QStringLiteral("Zoom In"), keys({"Ctrl+=", "Ctrl++"}), [this] {
        if (CanvasView* v = currentView()) v->zoomIn();
    }));
    view->addAction(makeAction(QStringLiteral("view.zoomOut"), QStringLiteral("Zoom Out"), keys({"Ctrl+-"}), [this] {
        if (CanvasView* v = currentView()) v->zoomOut();
    }));
    view->addAction(makeAction(QStringLiteral("view.fit"), QStringLiteral("Fit on Screen"), keys({"Ctrl+0"}), [this] {
        if (CanvasView* v = currentView()) v->fitOnScreen();
    }));
    stub(view, QStringLiteral("Fit Layer(s) on Screen"));
    view->addAction(makeAction(QStringLiteral("view.100"), QStringLiteral("100%"), keys({"Ctrl+1", "Ctrl+Alt+0"}), [this] {
        if (CanvasView* v = currentView()) v->actualPixels();
    }));
    view->addAction(makeAction(QStringLiteral("view.200"), QStringLiteral("200%"), {}, [this] {
        if (CanvasView* v = currentView()) v->setZoom(2.0);
    }));
    stub(view, QStringLiteral("Print Size"));
    stub(view, QStringLiteral("Flip Horizontal"));
    view->addSeparator();
    QMenu* screen = view->addMenu(QStringLiteral("Screen Mode"));
    screen->addAction(makeAction(QStringLiteral("view.screenStandard"), QStringLiteral("Standard Screen Mode"), {}, [this] { setScreenMode(0); }, false));
    screen->addAction(makeAction(QStringLiteral("view.screenFullMenu"), QStringLiteral("Full Screen Mode With Menu Bar"), {}, [this] { setScreenMode(1); }, false));
    screen->addAction(makeAction(QStringLiteral("view.screenFull"), QStringLiteral("Full Screen Mode"), {}, [this] { setScreenMode(2); }, false));
    view->addSeparator();
    view->addAction(viewToggle(QStringLiteral("view.extras"), QStringLiteral("Extras"), keys({"Ctrl+H"}), &ViewOptions::extras));
    QMenu* show = view->addMenu(QStringLiteral("Show"));
    show->addAction(viewToggle(QStringLiteral("view.selectionEdges"), QStringLiteral("Selection Edges"), {}, &ViewOptions::selectionEdges));
    show->addAction(viewToggle(QStringLiteral("view.pixelGrid"), QStringLiteral("Pixel Grid"), {}, &ViewOptions::pixelGrid));
    show->addAction(viewToggle(QStringLiteral("view.grid"), QStringLiteral("Grid"), keys({"Ctrl+'"}), &ViewOptions::grid));
    show->addAction(viewToggle(QStringLiteral("view.guides"), QStringLiteral("Guides"), keys({"Ctrl+;"}), &ViewOptions::guides));
    view->addSeparator();
    view->addAction(viewToggle(QStringLiteral("view.rulers"), QStringLiteral("Rulers"), keys({"Ctrl+R"}), &ViewOptions::rulers));
    view->addSeparator();
    view->addAction(viewToggle(QStringLiteral("view.snap"), QStringLiteral("Snap"), keys({"Ctrl+Shift+;", "Ctrl+:"}), &ViewOptions::snap));
    QMenu* snapTo = view->addMenu(QStringLiteral("Snap To"));
    snapTo->addAction(viewToggle(QStringLiteral("view.snapGuides"), QStringLiteral("Guides"), {}, &ViewOptions::snapGuides));
    snapTo->addAction(viewToggle(QStringLiteral("view.snapGrid"), QStringLiteral("Grid"), {}, &ViewOptions::snapGrid));
    stub(snapTo, QStringLiteral("Layers"));
    stub(snapTo, QStringLiteral("Slices"));
    snapTo->addAction(viewToggle(QStringLiteral("view.snapBounds"), QStringLiteral("Document Bounds"), {}, &ViewOptions::snapBounds));
    snapTo->addSeparator();
    auto snapAll = [this](bool on) {
        m_viewOptions->snapGuides = m_viewOptions->snapGrid = m_viewOptions->snapBounds = on;
        m_viewOptions->notify();
    };
    snapTo->addAction(makeAction(QStringLiteral("view.snapAll"), QStringLiteral("All"), {}, [snapAll] { snapAll(true); }, false));
    snapTo->addAction(makeAction(QStringLiteral("view.snapNone"), QStringLiteral("None"), {}, [snapAll] { snapAll(false); }, false));
    view->addSeparator();
    view->addAction(viewToggle(QStringLiteral("view.lockGuides"), QStringLiteral("Lock Guides"), keys({"Ctrl+Alt+;"}), &ViewOptions::lockGuides));
    view->addAction(makeAction(QStringLiteral("view.clearGuides"), QStringLiteral("Clear Guides"), {}, [this] {
        if (Document* d = currentDoc()) d->changeGuides({}, QStringLiteral("Clear Guides"));
    }));
    view->addAction(makeAction(QStringLiteral("view.newGuide"), QStringLiteral("New Guide..."), {}, [this] { newGuideDialog(); }));
    stub(view, QStringLiteral("New Guide Layout..."));
    stub(view, QStringLiteral("New Guides From Shape"));

    // ---------------- Window ----------------
    m_windowMenu = mb->addMenu(QStringLiteral("&Window"));
    QMenu* arrangeWin = m_windowMenu->addMenu(QStringLiteral("Arrange"));
    stub(arrangeWin, QStringLiteral("Tile All Vertically"));
    stub(arrangeWin, QStringLiteral("Tile All Horizontally"));
    arrangeWin->addAction(QStringLiteral("Consolidate All to Tabs"));
    stub(arrangeWin, QStringLiteral("Float in Window"));
    QMenu* workspace = m_windowMenu->addMenu(QStringLiteral("Workspace"));
    QAction* essentials = workspace->addAction(QStringLiteral("Essentials (Default)"));
    essentials->setCheckable(true);
    essentials->setChecked(true);
    workspace->addSeparator();
    workspace->addAction(makeAction(QStringLiteral("window.resetWorkspace"), QStringLiteral("Reset Essentials"), {}, [this] {
        restoreState(m_defaultState, kStateVersion);
        m_toolsBar->show();
        m_optionsBar->show();
    }, false));
    m_windowMenu->addSeparator();
    stub(m_windowMenu, QStringLiteral("Actions"), QKeySequence(QStringLiteral("Alt+F9")));
    auto dockAct = [this](const QString& objectName, const char* key) {
        for (QDockWidget* d : m_docks) {
            if (d->objectName() != objectName) continue;
            QAction* a = d->toggleViewAction();
            if (key) {
                a->setShortcut(QKeySequence(QString::fromLatin1(key)));
                addAction(a);
            }
            m_windowMenu->addAction(a);
        }
    };
    dockAct(QStringLiteral("AdjustmentsDock"), nullptr);
    stub(m_windowMenu, QStringLiteral("Brush Settings"), QKeySequence(QStringLiteral("F5")));
    stub(m_windowMenu, QStringLiteral("Brushes"));
    dockAct(QStringLiteral("ChannelsDock"), nullptr);
    stub(m_windowMenu, QStringLiteral("Character"));
    dockAct(QStringLiteral("ColorDock"), "F6");
    stub(m_windowMenu, QStringLiteral("Gradients"));
    stub(m_windowMenu, QStringLiteral("Histogram"));
    m_windowMenu->addAction(action(QStringLiteral("window.history")));
    m_windowMenu->addAction(action(QStringLiteral("window.info")));
    dockAct(QStringLiteral("LayersDock"), "F7");
    m_windowMenu->addAction(action(QStringLiteral("window.navigator")));
    stub(m_windowMenu, QStringLiteral("Paragraph"));
    dockAct(QStringLiteral("PathsDock"), nullptr);
    stub(m_windowMenu, QStringLiteral("Patterns"));
    dockAct(QStringLiteral("PropertiesDock"), nullptr);
    stub(m_windowMenu, QStringLiteral("Shapes"));
    stub(m_windowMenu, QStringLiteral("Styles"));
    dockAct(QStringLiteral("SwatchesDock"), nullptr);
    stub(m_windowMenu, QStringLiteral("Timeline"));
    m_windowMenu->addSeparator();
    QAction* optionsToggle = m_optionsBar->toggleViewAction();
    optionsToggle->setText(QStringLiteral("Options"));
    m_windowMenu->addAction(optionsToggle);
    QAction* toolsToggle = m_toolsBar->toggleViewAction();
    toolsToggle->setText(QStringLiteral("Tools"));
    m_windowMenu->addAction(toolsToggle);
    m_windowMenu->addSeparator();

    // ---------------- Help ----------------
    QMenu* help = mb->addMenu(QStringLiteral("&Help"));
    QAction* about = help->addAction(QStringLiteral("About PhotoSlop..."));
    about->setMenuRole(QAction::AboutRole);
    connect(about, &QAction::triggered, this, &MainWindow::showAbout);
    help->addAction(A(QStringLiteral("edit.shortcuts")));
}

void MainWindow::createHiddenShortcuts()
{
    // Tool letters: the key selects the group's last tool; Shift+key cycles through the group.
    for (const auto& grp : m_tools->groups()) {
        if (grp.tools.isEmpty() || grp.tools.first()->shortcut().isNull()) continue; // e.g. Blur/Sharpen/Smudge
        const QChar key = grp.tools.first()->shortcut();
        QAction* a = makeAction(QStringLiteral("tool.%1").arg(key), grp.tools.first()->name(),
                                {QKeySequence(QString(key))}, [this, key] { m_tools->selectByShortcut(key, false); }, false);
        QAction* c = makeAction(QStringLiteral("tool.%1.cycle").arg(key), grp.tools.first()->name(),
                                {QKeySequence(QStringLiteral("Shift+") + key)}, [this, key] { m_tools->selectByShortcut(key, true); }, false);
        addAction(a);
        addAction(c);
    }
    addAction(makeAction(QStringLiteral("color.default"), QStringLiteral("Default Foreground/Background Colors"), keys({"D"}),
                         [this] { m_colors->reset(); }, false));
    addAction(makeAction(QStringLiteral("color.swap"), QStringLiteral("Switch Foreground/Background Colors"), keys({"X"}),
                         [this] { m_colors->swap(); }, false));
    addAction(makeAction(QStringLiteral("view.screenCycle"), QStringLiteral("Cycle Screen Mode"), keys({"F"}),
                         [this] { cycleScreenMode(); }, false));
    addAction(makeAction(QStringLiteral("brush.smaller"), QStringLiteral("Decrease Brush Size"), keys({"["}), [this] {
        if (Tool* t = m_tools->current()) t->adjustSize(-1);
    }, false));
    addAction(makeAction(QStringLiteral("brush.larger"), QStringLiteral("Increase Brush Size"), keys({"]"}), [this] {
        if (Tool* t = m_tools->current()) t->adjustSize(1);
    }, false));
    addAction(makeAction(QStringLiteral("brush.softer"), QStringLiteral("Decrease Brush Hardness"), keys({"Shift+[", "{"}), [this] {
        if (Tool* t = m_tools->current()) t->adjustHardness(-1);
    }, false));
    addAction(makeAction(QStringLiteral("brush.harder"), QStringLiteral("Increase Brush Hardness"), keys({"Shift+]", "}"}), [this] {
        if (Tool* t = m_tools->current()) t->adjustHardness(1);
    }, false));
    addAction(makeAction(QStringLiteral("edit.fillForeground"), QStringLiteral("Fill with Foreground Color"),
                         keys({"Alt+Backspace", "Alt+Del"}), [this] { quickFill(false, false); }));
    addAction(makeAction(QStringLiteral("edit.fillForegroundPreserve"), QStringLiteral("Fill with Foreground Color (Preserve Transparency)"),
                         keys({"Alt+Shift+Backspace", "Alt+Shift+Del"}), [this] { quickFill(false, true); }));
    addAction(makeAction(QStringLiteral("edit.fillBackground"), QStringLiteral("Fill with Background Color"),
                         keys({"Ctrl+Backspace", "Ctrl+Del"}), [this] { quickFill(true, false); }));
    addAction(makeAction(QStringLiteral("edit.fillBackgroundPreserve"), QStringLiteral("Fill with Background Color (Preserve Transparency)"),
                         keys({"Ctrl+Shift+Backspace", "Ctrl+Shift+Del"}), [this] { quickFill(true, true); }));
}

// ---------------- Documents ----------------

DocumentPage* MainWindow::currentPage() const { return qobject_cast<DocumentPage*>(m_tabs->currentWidget()); }
Document* MainWindow::currentDoc() const { return currentPage() ? currentPage()->document() : nullptr; }
CanvasView* MainWindow::currentView() const { return currentPage() ? currentPage()->view() : nullptr; }

void MainWindow::addDocument(Document* doc)
{
    auto* page = new DocumentPage(doc, m_tools, m_viewOptions, m_tabs);
    m_undoGroup->addStack(doc->undoStack());
    const int idx = m_tabs->addTab(page, page->tabTitle());
    connect(page, &DocumentPage::titleChanged, this, &MainWindow::updateTabTitles);
    connect(doc, &Document::selectionChanged, this, &MainWindow::updateActions);
    connect(doc, &Document::layersChanged, this, &MainWindow::updateActions);
    connect(doc, &Document::quickMaskChanged, this, &MainWindow::updateActions);
    connect(doc, &Document::activeLayerChanged, this, &MainWindow::updateActions);
    connect(doc, &Document::editTargetChanged, this, &MainWindow::updateActions);
    connect(doc->undoStack(), &QUndoStack::indexChanged, this, &MainWindow::updateActions);
    m_tabs->setCurrentIndex(idx);
    m_central->setCurrentWidget(m_tabs);
    onCurrentChanged();
    page->view()->setFocus();
}

void MainWindow::newDocument()
{
    QSize clip;
    const QImage ci = QApplication::clipboard()->image();
    if (!ci.isNull()) clip = ci.size();
    NewDocumentDialog dlg(QStringLiteral("Untitled-%1").arg(m_untitled + 1), clip, this);
    if (dlg.exec() != QDialog::Accepted) return;
    ++m_untitled;
    const QSize size = dlg.pixelSize();
    auto* doc = new Document(size);
    DocState s;
    s.size = size;
    s.dpi = dlg.resolution();
    if (dlg.background() == NewDocumentDialog::Background::Transparent) {
        s.layers = {Layer::create(QStringLiteral("Layer 1"))};
    } else {
        QColor fill = Qt::white;
        switch (dlg.background()) {
        case NewDocumentDialog::Background::Black: fill = Qt::black; break;
        case NewDocumentDialog::Background::BackgroundColor: fill = m_colors->background(); break;
        case NewDocumentDialog::Background::Custom: fill = dlg.customColor(); break;
        default: break;
        }
        Layer bg = Layer::create(QStringLiteral("Background"));
        bg.isBackground = true;
        bg.image = QImage(size, QImage::Format_ARGB32_Premultiplied);
        bg.image.fill(fill);
        s.layers = {bg};
    }
    doc->initialize(s);
    if (dlg.background() == NewDocumentDialog::Background::Transparent) doc->nextLayerName(); // "Layer 1" is taken
    doc->setTitle(dlg.name());
    addDocument(doc);
}

void MainWindow::openDialog()
{
    QSettings s;
    const QString dir = s.value(QStringLiteral("lastDir"), QStandardPaths::writableLocation(QStandardPaths::PicturesLocation)).toString();
    const QStringList files = QFileDialog::getOpenFileNames(this, QStringLiteral("Open"), dir, DocumentIO::openFilter());
    if (files.isEmpty()) return;
    s.setValue(QStringLiteral("lastDir"), QFileInfo(files.first()).absolutePath());
    openFiles(files);
}

void MainWindow::openFiles(const QStringList& paths)
{
    for (const QString& path : paths) {
        // Re-activate a document that is already open.
        bool found = false;
        for (int i = 0; i < m_tabs->count(); ++i) {
            auto* page = static_cast<DocumentPage*>(m_tabs->widget(i));
            if (QFileInfo(page->document()->filePath()) == QFileInfo(path)) {
                m_tabs->setCurrentIndex(i);
                found = true;
            }
        }
        if (found) continue;
        QString err;
        Document* doc = DocumentIO::load(path, &err);
        if (!doc) {
            alert(QStringLiteral("Could not open “%1” because %2").arg(QFileInfo(path).fileName(), err));
            continue;
        }
        addRecent(path);
        addDocument(doc);
    }
}

bool MainWindow::closeDocument(int index)
{
    auto* page = qobject_cast<DocumentPage*>(m_tabs->widget(index));
    if (!page) return true;
    Document* doc = page->document();
    if (doc->isModified()) {
        m_tabs->setCurrentIndex(index);
        auto r = QMessageBox::warning(this, QStringLiteral("PhotoSlop"),
                                      QStringLiteral("Save changes to the PhotoSlop document “%1” before closing?").arg(doc->title()),
                                      QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::Yes);
        if (r == QMessageBox::Cancel) return false;
        if (r == QMessageBox::Yes && !saveDocument(doc, false)) return false;
    }
    if (m_tools->activeView() == page->view()) m_tools->setActiveView(nullptr);
    m_tabs->removeTab(index);
    m_undoGroup->removeStack(doc->undoStack());
    // Detach panels before the document goes away.
    if (m_tabs->count() == 0) onCurrentChanged();
    page->deleteLater();
    onCurrentChanged();
    return true;
}

bool MainWindow::closeAll()
{
    while (m_tabs->count() > 0)
        if (!closeDocument(m_tabs->count() - 1)) return false;
    return true;
}

bool MainWindow::saveDocument(Document* doc, bool saveAs, bool asCopy)
{
    if (!doc) return false;
    QString path = doc->filePath();
    const bool native = DocumentIO::isNativePath(path);
    const bool flatOk = !path.isEmpty() && !native && doc->layerCount() == 1;
    if (!saveAs && !path.isEmpty() && (native || flatOk)) {
        // fall through with existing path
    } else {
        QSettings s;
        QString dir = s.value(QStringLiteral("lastDir"), QStandardPaths::writableLocation(QStandardPaths::PicturesLocation)).toString();
        QString base = QFileInfo(doc->title()).completeBaseName();
        if (base.isEmpty()) base = doc->title();
        QString selected = QStringLiteral("PhotoSlop (*.pslop)");
        path = QFileDialog::getSaveFileName(this, asCopy ? QStringLiteral("Save a Copy") : QStringLiteral("Save As"),
                                            dir + QLatin1Char('/') + base + QStringLiteral(".pslop"), DocumentIO::saveFilter(), &selected);
        if (path.isEmpty()) return false;
        if (QFileInfo(path).suffix().isEmpty()) path += QStringLiteral(".pslop");
        s.setValue(QStringLiteral("lastDir"), QFileInfo(path).absolutePath());
    }

    QString err;
    bool ok;
    if (DocumentIO::isNativePath(path)) {
        ok = DocumentIO::saveNative(doc, path, &err);
    } else {
        const QByteArray fmt = DocumentIO::formatForPath(path);
        int quality = -1;
        if (fmt == "jpeg" || fmt == "webp") {
            // Photoshop's JPEG Options dialog uses a 0–12 quality scale.
            bool accepted = false;
            int q = QInputDialog::getInt(this, QStringLiteral("JPEG Options"), QStringLiteral("Quality (0 = Low, 12 = Maximum):"),
                                         10, 0, 12, 1, &accepted);
            if (!accepted) return false;
            quality = q * 100 / 12;
        }
        ok = DocumentIO::exportFlat(doc, path, fmt, quality, &err);
        // Layered documents saved to flat formats are copies; the original stays unsaved.
        if (ok && doc->layerCount() > 1) asCopy = true;
    }
    if (!ok) {
        alert(QStringLiteral("Could not save “%1” because %2").arg(QFileInfo(path).fileName(), err));
        return false;
    }
    addRecent(path);
    if (!asCopy) {
        doc->setFilePath(path);
        doc->setTitle(QFileInfo(path).fileName());
        doc->setClean();
    }
    updateTabTitles();
    return true;
}

void MainWindow::exportAs()
{
    Document* doc = currentDoc();
    if (!doc) return;
    ExportDialog dlg(doc, this);
    if (dlg.exec() != QDialog::Accepted) return;
    QSettings s;
    const QString dir = s.value(QStringLiteral("lastDir"), QStandardPaths::writableLocation(QStandardPaths::PicturesLocation)).toString();
    const QString base = QFileInfo(doc->title()).completeBaseName();
    QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Export"), dir + QLatin1Char('/') + base + QLatin1Char('.') + dlg.extension());
    if (path.isEmpty()) return;
    QString err;
    if (!DocumentIO::exportFlat(doc, path, dlg.format(), dlg.quality(), &err)) alert(err);
}

void MainWindow::quickExportPng()
{
    Document* doc = currentDoc();
    if (!doc) return;
    QSettings s;
    const QString dir = s.value(QStringLiteral("lastDir"), QStandardPaths::writableLocation(QStandardPaths::PicturesLocation)).toString();
    QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Quick Export as PNG"),
                                                dir + QLatin1Char('/') + QFileInfo(doc->title()).completeBaseName() + QStringLiteral(".png"),
                                                QStringLiteral("PNG (*.png)"));
    if (path.isEmpty()) return;
    QString err;
    if (!DocumentIO::exportFlat(doc, path, "png", -1, &err)) alert(err);
}

void MainWindow::revert()
{
    Document* doc = currentDoc();
    if (!doc || doc->filePath().isEmpty()) return;
    QString err;
    std::unique_ptr<Document> fresh(DocumentIO::load(doc->filePath(), &err));
    if (!fresh) {
        alert(err);
        return;
    }
    DocState before = doc->state();
    DocState after = fresh->state();
    doc->restoreState(after);
    doc->pushSnapshot(QStringLiteral("Revert"), before);
}

void MainWindow::addRecent(const QString& path)
{
    QSettings s;
    QStringList files = s.value(QStringLiteral("recentFiles")).toStringList();
    files.removeAll(path);
    files.prepend(path);
    while (files.size() > 20) files.removeLast();
    s.setValue(QStringLiteral("recentFiles"), files);
    refreshRecent();
}

void MainWindow::refreshRecent()
{
    QStringList files = QSettings().value(QStringLiteral("recentFiles")).toStringList();
    m_recentMenu->clear();
    for (const QString& f : files) {
        QAction* a = m_recentMenu->addAction(QFileInfo(f).fileName());
        a->setToolTip(f);
        connect(a, &QAction::triggered, this, [this, f] { openFiles({f}); });
    }
    m_recentMenu->addSeparator();
    QAction* clear = m_recentMenu->addAction(QStringLiteral("Clear Recent File List"));
    connect(clear, &QAction::triggered, this, [this] {
        QSettings().remove(QStringLiteral("recentFiles"));
        refreshRecent();
    });
    clear->setEnabled(!files.isEmpty());
    m_home->setRecentFiles(files.mid(0, 8));
}

void MainWindow::onCurrentChanged()
{
    DocumentPage* page = currentPage();
    Document* doc = page ? page->document() : nullptr;
    CanvasView* view = page ? page->view() : nullptr;
    m_central->setCurrentWidget(page ? static_cast<QWidget*>(m_tabs) : m_home);
    m_tools->setActiveView(view);
    if (doc) m_undoGroup->setActiveStack(doc->undoStack());
    else m_undoGroup->setActiveStack(nullptr);
    m_layersPanel->setDocument(doc);
    m_propertiesPanel->setDocument(doc);
    m_propertiesPanel->setActionLookup([this](const QString& id) { return action(id); });
    m_channelsPanel->setDocument(doc);
    m_historyPanel->setDocument(doc);
    m_navigatorPanel->setView(view);
    m_infoPanel->setView(view);
    if (view) view->updateCursor();
    updateTabTitles();
    updateActions();
    rebuildWindowMenuDocs();
}

void MainWindow::updateTabTitles()
{
    for (int i = 0; i < m_tabs->count(); ++i) {
        auto* page = static_cast<DocumentPage*>(m_tabs->widget(i));
        m_tabs->setTabText(i, page->tabTitle());
        m_tabs->setTabToolTip(i, page->document()->filePath());
    }
    rebuildWindowMenuDocs();
}

void MainWindow::updateActions()
{
    Document* doc = currentDoc();
    for (QAction* a : std::as_const(m_docActions)) a->setEnabled(doc != nullptr);
    m_toolBox->setQuickMask(doc && doc->inQuickMask());
    action(QStringLiteral("select.quickMask"))->setChecked(doc && doc->inQuickMask());
    if (!doc) return;
    const bool sel = doc->hasSelection();
    const Layer* l = doc->activeLayer();
    action(QStringLiteral("select.deselect"))->setEnabled(sel);
    action(QStringLiteral("select.inverse"))->setEnabled(sel);
    action(QStringLiteral("select.reselect"))->setEnabled(!doc->lastSelection().isNull());
    for (const char* id : {"select.feather", "select.border", "select.smooth", "select.expand", "select.contract",
                           "select.grow", "select.similar", "select.transform"})
        action(QString::fromLatin1(id))->setEnabled(sel);
    action(QStringLiteral("transform.again"))->setEnabled(FreeTransformTool::hasLastTransform());
    action(QStringLiteral("view.clearGuides"))->setEnabled(!doc->guides().isEmpty());
    action(QStringLiteral("image.crop"))->setEnabled(sel);
    action(QStringLiteral("edit.cut"))->setEnabled(sel);
    action(QStringLiteral("layer.viaCut"))->setEnabled(sel);
    action(QStringLiteral("layer.fromBackground"))->setEnabled(l && l->isBackground);
    const QList<Layer>& layers = doc->layers();
    const int ai = doc->activeIndex();
    const QList<int> siblings = Tree::children(layers, Tree::parentIndex(layers, ai));
    const bool hasBelow = siblings.indexOf(ai) > 0;
    const bool isGroup = l && l->isGroup();
    const bool isBg = l && l->isBackground;
    action(QStringLiteral("layer.mergeDown"))->setEnabled(isGroup || hasBelow);
    action(QStringLiteral("layer.mergeDown"))->setText(isGroup ? QStringLiteral("Merge Group") : QStringLiteral("Merge Down"));
    action(QStringLiteral("layer.delete"))->setEnabled(doc->layerCount() - (ai - Tree::subtreeStart(layers, ai) + 1) >= 1);
    action(QStringLiteral("layer.delete"))->setText(isGroup ? QStringLiteral("Delete Group") : QStringLiteral("Delete Layer"));
    // Groups and clipping
    action(QStringLiteral("layer.group"))->setEnabled(l && !isBg);
    action(QStringLiteral("layer.groupFromLayers"))->setEnabled(l && !isBg);
    action(QStringLiteral("layer.ungroup"))->setEnabled(isGroup);
    action(QStringLiteral("layer.clipping"))->setEnabled(l && !isBg && (l->clipped || hasBelow));
    action(QStringLiteral("layer.clipping"))->setText(l && l->clipped ? QStringLiteral("Release Clipping Mask")
                                                                       : QStringLiteral("Create Clipping Mask"));
    // Styles
    const bool styleable = l && !isBg && !isGroup && l->kind != LayerKind::Adjustment;
    for (const char* id : {"layer.styleBlending", "layer.styleStroke", "layer.styleColorOverlay", "layer.styleOuterGlow",
                           "layer.styleDropShadow"})
        action(QString::fromLatin1(id))->setEnabled(styleable);
    const bool styled = l && l->style;
    action(QStringLiteral("layer.styleCopy"))->setEnabled(styled);
    action(QStringLiteral("layer.stylePaste"))->setEnabled(styleable && m_copiedStyle);
    action(QStringLiteral("layer.styleClear"))->setEnabled(styled);
    action(QStringLiteral("layer.styleHide"))->setEnabled(styled);
    action(QStringLiteral("layer.styleHide"))->setText(styled && !l->style->visible ? QStringLiteral("Show All Effects")
                                                                                    : QStringLiteral("Hide All Effects"));
    // Masks
    const bool masked = l && l->mask;
    for (const char* id : {"layer.maskRevealAll", "layer.maskHideAll", "layer.maskAdd"})
        action(QString::fromLatin1(id))->setEnabled(l && !isBg && !masked);
    for (const char* id : {"layer.maskRevealSelection", "layer.maskHideSelection"})
        action(QString::fromLatin1(id))->setEnabled(l && !isBg && !masked && sel);
    for (const char* id : {"layer.maskDelete", "layer.maskToggle", "layer.maskLink", "layer.maskLoadSelection"})
        action(QString::fromLatin1(id))->setEnabled(masked);
    action(QStringLiteral("layer.maskApply"))->setEnabled(masked && l->isPixel());
    action(QStringLiteral("layer.maskToggle"))->setText(masked && !l->maskEnabled ? QStringLiteral("Enable") : QStringLiteral("Disable"));
    action(QStringLiteral("layer.maskLink"))->setText(masked && !l->maskLinked ? QStringLiteral("Link") : QStringLiteral("Unlink"));
    // Content
    const bool adjustment = l && l->kind == LayerKind::Adjustment;
    action(QStringLiteral("layer.contentOptions"))->setEnabled(adjustment && l->adjustment && l->adjustment->kind != Adjust::Kind::Invert);
    action(QStringLiteral("layer.rasterizeType"))->setEnabled(l && l->kind == LayerKind::Text);
    action(QStringLiteral("type.rasterize"))->setEnabled(l && l->kind == LayerKind::Text);
    action(QStringLiteral("layer.rasterizeShape"))->setEnabled(l && l->kind == LayerKind::Shape);
    action(QStringLiteral("layer.rasterizeStyle"))->setEnabled(l && l->hasStyle());
    action(QStringLiteral("layer.rasterizeLayer"))->setEnabled(l && (l->isVector() || l->hasStyle()));
    action(QStringLiteral("layer.flatten"))->setEnabled(doc->layerCount() > 1 || (l && !l->isBackground));
    action(QStringLiteral("file.revert"))->setEnabled(!doc->filePath().isEmpty());
    action(QStringLiteral("filter.last"))->setEnabled(bool(m_lastFilter));
    const bool fade = canFade();
    action(QStringLiteral("edit.fade"))->setEnabled(fade);
    action(QStringLiteral("edit.fade"))->setText(fade ? QStringLiteral("Fade %1...").arg(m_fade.name) : QStringLiteral("Fade..."));
}

void MainWindow::rebuildWindowMenuDocs()
{
    if (!m_windowMenu) return;
    for (QAction* a : std::as_const(m_windowDocActions)) {
        m_windowMenu->removeAction(a);
        a->deleteLater();
    }
    m_windowDocActions.clear();
    for (int i = 0; i < m_tabs->count(); ++i) {
        auto* page = static_cast<DocumentPage*>(m_tabs->widget(i));
        QAction* a = m_windowMenu->addAction(page->document()->title());
        a->setCheckable(true);
        a->setChecked(i == m_tabs->currentIndex());
        connect(a, &QAction::triggered, this, [this, page] { m_tabs->setCurrentWidget(page); });
        m_windowDocActions.append(a);
    }
}

// ---------------- Commands ----------------

void MainWindow::alert(const QString& message)
{
    QMessageBox box(QMessageBox::Warning, QStringLiteral("PhotoSlop"), message, QMessageBox::Ok, this);
    box.exec();
}

void MainWindow::copy(bool merged)
{
    Document* doc = currentDoc();
    if (!doc) return;
    QString err;
    QPoint at;
    QImage img = Ops::copy(doc, merged, &at, &err);
    if (img.isNull()) {
        if (!err.isEmpty()) alert(err);
        return;
    }
    m_settingClipboard = true;
    QApplication::clipboard()->setImage(img.convertToFormat(QImage::Format_ARGB32));
    m_settingClipboard = false;
    m_clipPos = at;
    m_clipSize = img.size();
    m_clipValid = true;
}

void MainWindow::cut()
{
    Document* doc = currentDoc();
    if (!doc || !doc->hasSelection()) return;
    copy(false);
    if (!m_clipValid) return;
    QString err;
    if (!Ops::clear(doc, m_colors->background(), &err) && !err.isEmpty()) alert(err);
}

void MainWindow::paste(bool inPlace)
{
    const QImage img = QApplication::clipboard()->image();
    if (img.isNull()) return;
    Document* doc = currentDoc();
    if (!doc) {
        Document* d = DocumentIO::fromImage(img, QStringLiteral("Untitled-%1").arg(++m_untitled));
        addDocument(d);
        return;
    }
    QPoint pos;
    if (m_clipValid && img.size() == m_clipSize && (inPlace || QRect(m_clipPos, m_clipSize).intersects(doc->bounds()))) {
        pos = m_clipPos;
    } else {
        // Centre on the visible part of the canvas, as Photoshop does.
        const QRectF vis = currentView()->visibleCanvasRect();
        const QPointF c = vis.isEmpty() ? QPointF(doc->width() / 2.0, doc->height() / 2.0) : vis.center();
        pos = QPoint(int(c.x() - img.width() / 2.0), int(c.y() - img.height() / 2.0));
    }
    Ops::paste(doc, img, &pos);
}

void MainWindow::fillDialog()
{
    Document* doc = currentDoc();
    if (!doc || !prepareTarget(QStringLiteral("Fill"))) return;
    FillDialog dlg(m_colors, this);
    if (dlg.exec() != QDialog::Accepted) return;
    QString err;
    if (!Ops::fill(doc, dlg.color(), dlg.mode(), dlg.opacity(), dlg.preserveTransparency(), &err)) alert(err);
}

void MainWindow::quickFill(bool background, bool preserve)
{
    Document* doc = currentDoc();
    if (!doc || !prepareTarget(QStringLiteral("Fill"))) return;
    QString err;
    if (!Ops::fill(doc, background ? m_colors->background() : m_colors->foreground(), BlendMode::Normal, 1.f, preserve, &err))
        alert(err);
}

void MainWindow::clearOrDelete()
{
    Document* doc = currentDoc();
    if (!doc) return;
    QString err;
    if (doc->hasSelection()) {
        if (!Ops::clear(doc, m_colors->background(), &err) && !err.isEmpty()) alert(err);
    } else if (doc->activeLayer() && !doc->activeLayer()->isBackground) {
        if (!Ops::deleteLayer(doc, &err)) alert(err);
    }
}

void MainWindow::newLayerDialog()
{
    Document* doc = currentDoc();
    if (!doc) return;
    // Preview the next free name without consuming it if the dialog is cancelled.
    QString name;
    for (int n = 1;; ++n) {
        name = QStringLiteral("Layer %1").arg(n);
        bool taken = false;
        for (const Layer& l : doc->layers())
            if (l.name == name) taken = true;
        if (!taken) break;
    }
    NewLayerDialog dlg(name, this);
    if (dlg.exec() != QDialog::Accepted) return;
    Ops::newLayer(doc, dlg.name(), dlg.mode(), dlg.opacity());
}

void MainWindow::duplicateLayerDialog()
{
    Document* doc = currentDoc();
    if (!doc || !doc->activeLayer()) return;
    bool ok = false;
    const QString n = QInputDialog::getText(this, QStringLiteral("Duplicate Layer"), QStringLiteral("As:"), QLineEdit::Normal,
                                            doc->activeLayer()->name + QStringLiteral(" copy"), &ok);
    if (ok) Ops::duplicateLayer(doc, n);
}

void MainWindow::layerFromBackground()
{
    Document* doc = currentDoc();
    if (!doc || !doc->activeLayer() || !doc->activeLayer()->isBackground) return;
    NewLayerDialog dlg(QStringLiteral("Layer 0"), this);
    if (dlg.exec() != QDialog::Accepted) return;
    const int idx = doc->activeIndex();
    doc->modify(QStringLiteral("Layer From Background"), [&] {
        Layer& l = doc->layerRef(idx);
        l.isBackground = false;
        l.name = dlg.name();
        l.mode = dlg.mode();
        l.opacity = dlg.opacity();
    });
}

void MainWindow::imageSizeDialog()
{
    Document* doc = currentDoc();
    if (!doc) return;
    ImageSizeDialog dlg(doc, this);
    if (dlg.exec() != QDialog::Accepted) return;
    Ops::imageSize(doc, dlg.newSize(), dlg.resolution(), dlg.method());
    if (CanvasView* v = currentView()) v->fitOnScreen();
}

void MainWindow::canvasSizeDialog()
{
    Document* doc = currentDoc();
    if (!doc) return;
    CanvasSizeDialog dlg(doc, m_colors, this);
    if (dlg.exec() != QDialog::Accepted) return;
    const QSize n = dlg.newSize();
    if (n.width() < doc->width() || n.height() < doc->height()) {
        if (QMessageBox::warning(this, QStringLiteral("PhotoSlop"),
                                 QStringLiteral("The new canvas size is smaller than the current canvas size; some clipping will occur."),
                                 QMessageBox::Ok | QMessageBox::Cancel) != QMessageBox::Ok)
            return;
    }
    Ops::canvasSize(doc, n, dlg.anchorOffset(), dlg.extensionColor());
}

void MainWindow::featherDialog()
{
    Document* doc = currentDoc();
    if (!doc || !doc->hasSelection()) return;
    bool ok = false;
    const double r = QInputDialog::getDouble(this, QStringLiteral("Feather Selection"), QStringLiteral("Feather Radius (pixels):"),
                                             5.0, 0.1, 1000.0, 1, &ok);
    if (!ok) return;
    QImage mask = doc->selection().copy();
    Sel::feather(mask, r);
    doc->changeSelection(Sel::isEmpty(mask) ? QImage() : mask, QStringLiteral("Feather"));
}

void MainWindow::modifySelectionDialog(int howInt)
{
    Document* doc = currentDoc();
    if (!doc || !doc->hasSelection()) return;
    const auto how = Ops::Modify(howInt);
    static int last[4] = {1, 2, 1, 1};
    const struct { const char* title; const char* label; bool bounds; } info[] = {
        {"Border Selection", "Width:", false},
        {"Smooth Selection", "Sample Radius:", true},
        {"Expand Selection", "Expand By:", true},
        {"Contract Selection", "Contract By:", true},
    };
    const auto& i = info[howInt];
    ModifySelectionDialog dlg(QString::fromLatin1(i.title), QString::fromLatin1(i.label), last[howInt], i.bounds, this);
    if (dlg.exec() != QDialog::Accepted) return;
    last[howInt] = dlg.amount();
    Ops::modifySelection(doc, how, dlg.amount(), dlg.atCanvasBounds());
}

void MainWindow::toggleQuickMask()
{
    if (Document* doc = currentDoc()) Ops::setQuickMask(doc, !doc->inQuickMask());
}

void MainWindow::layerStyleDialog(int page)
{
    Document* doc = currentDoc();
    if (!doc || !doc->activeLayer()) return;
    const Layer& l = *doc->activeLayer();
    if (l.isBackground || l.isGroup() || l.kind == LayerKind::Adjustment) return;
    LayerStyleDialog dlg(doc, doc->activeIndex(), LayerStyleDialog::Page(page), this);
    dlg.exec();
}

void MainWindow::newAdjustmentLayer(Adjust::Kind kind)
{
    Document* doc = currentDoc();
    if (!doc) return;
    Ops::newAdjustmentLayer(doc, Adjust::LayerSettings::make(kind));
    if (kind == Adjust::Kind::Invert) return; // nothing to set
    if (!editAdjustmentLayer(doc->activeIndex())) doc->undoStack()->undo();
}

bool MainWindow::editAdjustmentLayer(int index)
{
    Document* doc = currentDoc();
    if (!doc || index < 0 || index >= doc->layerCount()) return false;
    const Layer& l = doc->layerAt(index);
    if (l.kind != LayerKind::Adjustment || !l.adjustment) return false;
    const Adjust::LayerSettings s = *l.adjustment;
    auto run = [](PreviewDialog& dlg) { return dlg.exec() == QDialog::Accepted; };
    auto param = [&](const QString& title, void (*setup)(ParamDialog&), Adjust::LayerSettings (*settings)(const Values&)) {
        ParamDialog dlg(doc, title, false, [settings, title](const Values& v) { return Adjust::spec(title, settings(v).buildMap()); },
                        this, index);
        setup(dlg);
        dlg.setLayerBuilder(settings);
        const Values v = valuesOf(s);
        for (auto it = v.cbegin(); it != v.cend(); ++it) dlg.setValue(it.key(), it.value());
        dlg.ready();
        return run(dlg);
    };
    switch (s.kind) {
    case Adjust::Kind::Levels: {
        LevelsDialog dlg(doc, false, this, index);
        dlg.setLevels(s.levels);
        return run(dlg);
    }
    case Adjust::Kind::Curves: {
        CurvesDialog dlg(doc, false, this, index);
        dlg.setCurves(s.curves);
        return run(dlg);
    }
    case Adjust::Kind::HueSaturation: {
        HueSaturationDialog dlg(doc, false, this, index);
        dlg.setSettings(s.hueSaturation);
        return run(dlg);
    }
    case Adjust::Kind::ColorBalance: {
        ColorBalanceDialog dlg(doc, false, this, index);
        dlg.setSettings(s.colorBalance);
        return run(dlg);
    }
    case Adjust::Kind::Threshold: {
        ThresholdDialog dlg(doc, this, index);
        dlg.setLevel(s.thresholdLevel);
        return run(dlg);
    }
    case Adjust::Kind::BrightnessContrast:
        return param(QStringLiteral("Brightness/Contrast"), setupBrightnessContrast, brightnessContrastSettings);
    case Adjust::Kind::BlackWhite: return param(QStringLiteral("Black and White"), setupBlackWhite, blackWhiteSettings);
    case Adjust::Kind::Posterize: return param(QStringLiteral("Posterize"), setupPosterize, posterizeSettings);
    case Adjust::Kind::Invert: return true;
    }
    return false;
}

void MainWindow::startTransform(bool selectionOnly, int mode)
{
    CanvasView* v = currentView();
    if (!v) return;
    if (m_transform->isActive()) {
        if (!selectionOnly) m_transform->setMode(FreeTransformTool::Mode(mode));
        return;
    }
    m_tempFromKey = false;
    m_transform->begin(v, selectionOnly, FreeTransformTool::Mode(mode));
}

void MainWindow::quickTransform(const std::function<void()>& apply)
{
    // Inside Free Transform these adjust the pending transform; otherwise they apply at once.
    if (m_transform->isActive()) {
        apply();
        return;
    }
    CanvasView* v = currentView();
    if (!v || !m_transform->begin(v, false)) return;
    apply();
    m_transform->commit(v);
}

void MainWindow::newGuideDialog()
{
    Document* doc = currentDoc();
    if (!doc) return;
    NewGuideDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted) return;
    QList<Guide> guides = doc->guides();
    guides.append(Guide{dlg.orientation(), dlg.position()});
    doc->changeGuides(guides, QStringLiteral("New Guide"));
    if (!m_viewOptions->showsGuides()) {
        m_viewOptions->extras = m_viewOptions->guides = true;
        m_viewOptions->notify();
    }
}

QAction* MainWindow::viewToggle(const QString& id, const QString& text, const QList<QKeySequence>& ks, bool ViewOptions::*field)
{
    QAction* a = makeAction(id, text, ks, [this, id, field] {
        m_viewOptions->*field = action(id)->isChecked();
        m_viewOptions->notify();
    }, false);
    a->setCheckable(true);
    a->setChecked(m_viewOptions->*field);
    m_viewToggles.insert(id, field);
    return a;
}

void MainWindow::syncViewActions()
{
    for (auto it = m_viewToggles.cbegin(); it != m_viewToggles.cend(); ++it) {
        QAction* a = action(it.key());
        QSignalBlocker block(a);
        a->setChecked(m_viewOptions->*(it.value()));
    }
}

bool MainWindow::execPreview(PreviewDialog& dlg)
{
    if (!dlg.isReady()) {
        alert(dlg.error());
        return false;
    }
    if (dlg.exec() != QDialog::Accepted) return false;
    rememberFade(dlg.spec().name, dlg.applied());
    return true;
}

bool MainWindow::prepareTarget(const QString& command)
{
    Document* doc = currentDoc();
    return doc && m_tools->preparePixelEdit(doc, QStringLiteral("Could not complete the %1 command").arg(command));
}

void MainWindow::applySpec(const Filters::Spec& spec, const SpecRecipe& recipe)
{
    Document* doc = currentDoc();
    if (!doc || !prepareTarget(spec.name)) return;
    QString err;
    Filters::Applied applied;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const bool ok = Filters::apply(doc, spec, &err, &applied);
    QApplication::restoreOverrideCursor();
    if (!ok) {
        alert(err);
        return;
    }
    if (recipe) setLastFilter(spec.name, recipe);
    rememberFade(spec.name, applied);
}

void MainWindow::paramDialog(const QString& title, bool spreads, bool isFilter, const std::function<void(ParamDialog&)>& setup,
                             const ParamBuilder& build)
{
    Document* doc = currentDoc();
    if (!doc || !prepareTarget(title)) return;
    const bool useLast = isFilter || (QApplication::keyboardModifiers() & Qt::AltModifier);
    ParamDialog dlg(doc, title, spreads, build, this);
    setup(dlg);
    if (useLast) dlg.useLastValues();
    dlg.ready();
    if (!execPreview(dlg) || !isFilter) return;
    const QHash<QString, double> values = dlg.values();
    setLastFilter(dlg.spec().name, [build, values] {
        QHash<QString, double> v = values;
        // Each repeat gets fresh noise.
        if (v.contains(QStringLiteral("seed"))) v[QStringLiteral("seed")] = QRandomGenerator::global()->generate();
        return build(v);
    });
}

void MainWindow::setLastFilter(const QString& name, const SpecRecipe& recipe)
{
    m_lastFilter = recipe;
    // The menu item names the filter it repeats.
    action(QStringLiteral("filter.last"))->setText(name);
    updateActions();
}

void MainWindow::autoAdjust(Adjust::AutoMode mode, const QString& name)
{
    Document* doc = currentDoc();
    if (!doc || !prepareTarget(name)) return;
    Adjust::Levels levels;
    {
        // A throwaway session reads the pixels the command will change.
        Filters::Session probe(doc, name, false);
        if (!probe.isValid()) {
            alert(probe.error());
            return;
        }
        levels = Adjust::autoLevels(Adjust::histogram(probe.originalTarget(), probe.selectionTarget()), mode);
    }
    applySpec(Adjust::spec(name, Adjust::levelsMap(levels)));
}

void MainWindow::rememberFade(const QString& name, const Filters::Applied& applied)
{
    Document* doc = currentDoc();
    if (!doc) return;
    QUndoStack* stack = doc->undoStack();
    m_fade = {doc, stack->index(), stack->index() > 0 ? stack->command(stack->index() - 1) : nullptr, name, applied};
    updateActions();
}

bool MainWindow::canFade()
{
    Document* doc = currentDoc();
    if (!doc || m_fade.doc != doc || m_fade.index < 1 || !m_fade.command) return false;
    // Only straight after the command, on the same layer.
    QUndoStack* stack = doc->undoStack();
    if (stack->index() != m_fade.index || stack->command(m_fade.index - 1) != m_fade.command) return false;
    const Layer* l = doc->editLayer();
    return l && l->id == m_fade.applied.layerId;
}

void MainWindow::fadeDialog()
{
    if (!canFade()) return;
    FadeDialog dlg(currentDoc(), m_fade.name, m_fade.applied, this);
    if (!dlg.isReady()) {
        alert(dlg.error());
        return;
    }
    // A fade cannot be faded again.
    if (dlg.exec() == QDialog::Accepted) m_fade = {};
    updateActions();
}

void MainWindow::showShortcuts()
{
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("Keyboard Shortcuts"));
    dlg.resize(620, 640);
    auto* lay = new QVBoxLayout(&dlg);
    auto* table = new QTableWidget(&dlg);
    table->setColumnCount(2);
    table->setHorizontalHeaderLabels({QStringLiteral("Application Menu Command"), QStringLiteral("Shortcut")});
    table->horizontalHeader()->setStretchLastSection(true);
    table->setColumnWidth(0, 360);
    table->verticalHeader()->hide();
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    std::function<void(QMenu*, const QString&)> walk = [&](QMenu* menu, const QString& prefix) {
        for (QAction* a : menu->actions()) {
            if (a->isSeparator()) continue;
            QString text = a->text().section(QLatin1Char('\t'), 0, 0);
            text.remove(QLatin1Char('&'));
            if (a->menu()) {
                walk(a->menu(), prefix + text + QStringLiteral(" > "));
                continue;
            }
            QString key = a->shortcut().toString(QKeySequence::NativeText);
            if (key.isEmpty()) key = a->text().section(QLatin1Char('\t'), 1);
            const int row = table->rowCount();
            table->insertRow(row);
            table->setItem(row, 0, new QTableWidgetItem(prefix + text));
            table->setItem(row, 1, new QTableWidgetItem(key));
        }
    };
    for (QAction* a : menuBar()->actions())
        if (a->menu()) {
            QString t = a->text();
            t.remove(QLatin1Char('&'));
            walk(a->menu(), t + QStringLiteral(" > "));
        }
    const QStringList hidden = {QStringLiteral("color.default"), QStringLiteral("color.swap"), QStringLiteral("brush.smaller"),
                                QStringLiteral("brush.larger"), QStringLiteral("brush.softer"), QStringLiteral("brush.harder"),
                                QStringLiteral("edit.fillForeground"), QStringLiteral("edit.fillBackground"),
                                QStringLiteral("view.screenCycle"), QStringLiteral("layer.newQuick")};
    for (const QString& id : hidden) {
        QAction* a = action(id);
        const int row = table->rowCount();
        table->insertRow(row);
        table->setItem(row, 0, new QTableWidgetItem(QStringLiteral("Tools > ") + a->text()));
        table->setItem(row, 1, new QTableWidgetItem(a->shortcut().toString(QKeySequence::NativeText)));
    }
    for (const auto& grp : m_tools->groups()) {
        for (Tool* t : grp.tools) {
            const int row = table->rowCount();
            table->insertRow(row);
            table->setItem(row, 0, new QTableWidgetItem(QStringLiteral("Tools > ") + t->name()));
            table->setItem(row, 1, new QTableWidgetItem(QString(t->shortcut())));
        }
    }
    lay->addWidget(table);
    auto* box = new QDialogButtonBox(QDialogButtonBox::Ok, &dlg);
    connect(box, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    lay->addWidget(box);
    dlg.exec();
}

void MainWindow::showAbout()
{
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("About PhotoSlop"));
    auto* lay = new QVBoxLayout(&dlg);
    auto* logo = new QLabel(&dlg);
    logo->setPixmap(logoPixmap(240, devicePixelRatioF()));
    logo->setAlignment(Qt::AlignCenter);
    logo->setStyleSheet(QStringLiteral("background: white; border-radius: 10px; padding: 16px;"));
    lay->addWidget(logo);
    auto* text = new QLabel(QStringLiteral("<p align=center><b>PhotoSlop %1</b><br><i>%2</i><br><br>"
                                           "Built with Qt %3. Not affiliated with Adobe.</p>")
                                .arg(QApplication::applicationVersion(), kSlogan, QString::fromLatin1(qVersion())),
                            &dlg);
    text->setAlignment(Qt::AlignCenter);
    lay->addWidget(text);
    auto* box = new QDialogButtonBox(QDialogButtonBox::Ok, &dlg);
    connect(box, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    lay->addWidget(box);
    dlg.exec();
}

void MainWindow::toggleAllPanels(bool docksOnly)
{
    if (!m_panelsHidden) {
        m_hiddenByTab.clear();
        QList<QWidget*> candidates;
        for (QDockWidget* d : findChildren<QDockWidget*>()) candidates << d;
        if (!docksOnly) candidates << m_toolsBar << m_optionsBar;
        for (QWidget* w : candidates) {
            if (w->isVisible() && !(qobject_cast<QDockWidget*>(w) && static_cast<QDockWidget*>(w)->isFloating() && docksOnly)) {
                m_hiddenByTab << w;
                w->hide();
            }
        }
        m_strip->hideFlyout();
        m_panelsHidden = true;
    } else {
        for (QWidget* w : std::as_const(m_hiddenByTab)) w->show();
        m_hiddenByTab.clear();
        m_panelsHidden = false;
    }
}

void MainWindow::setScreenMode(int mode)
{
    if (mode == m_screenMode) return;
    if (m_screenMode == 0) m_wasMaximized = isMaximized();
    if (m_screenMode == 2) {
        menuBar()->show();
        if (m_panelsHidden) toggleAllPanels(false);
    }
    m_screenMode = mode;
    switch (mode) {
    case 0:
        if (m_wasMaximized) showMaximized();
        else showNormal();
        break;
    case 1:
        showFullScreen();
        break;
    case 2:
        showFullScreen();
        if (!m_panelsHidden) toggleAllPanels(false);
#ifndef Q_OS_MACOS
        menuBar()->hide();
#endif
        break;
    }
}

void MainWindow::cycleScreenMode() { setScreenMode((m_screenMode + 1) % 3); }

void MainWindow::handleDigit(int digit)
{
    // Typing "4" sets 40%, "0" 100%, and two quick digits ("45") set 45% — like Photoshop.
    int value;
    if (m_pendingDigit >= 0 && m_digitTimer.isValid() && m_digitTimer.elapsed() < 600) {
        value = m_pendingDigit * 10 + digit;
        if (value == 0) value = 100;
        m_pendingDigit = -1;
    } else {
        value = digit == 0 ? 100 : digit * 10;
        m_pendingDigit = digit;
        m_digitTimer.start();
    }
    Tool* t = m_tools->current();
    if (t && t->setOpacityPercent(value)) return;
    Document* doc = currentDoc();
    if (doc && doc->activeLayer() && !doc->activeLayer()->isBackground) Ops::setOpacity(doc, doc->activeIndex(), value / 100.f);
}

bool MainWindow::isTypingTarget() const
{
    QWidget* f = QApplication::focusWidget();
    if (!f) return false;
    if (qobject_cast<QLineEdit*>(f) || qobject_cast<QAbstractSpinBox*>(f) || qobject_cast<QTextEdit*>(f)
        || qobject_cast<QPlainTextEdit*>(f))
        return true;
    if (auto* cb = qobject_cast<QComboBox*>(f); cb && cb->isEditable()) return true;
    return false;
}

bool MainWindow::eventFilter(QObject* obj, QEvent* e)
{
    if (e->type() == QEvent::MouseButtonPress && m_tools->modalTool()) {
        // Clicking a panel applies a pending Free Transform first; the canvas, the options
        // bar and menus keep it open.
        auto* w = qobject_cast<QWidget*>(obj);
        CanvasView* v = currentView();
        const bool keep = !w || (v && (w == v || v->isAncestorOf(w))) || m_optionsBar->isAncestorOf(w) || w == m_optionsBar
            || qobject_cast<QMenu*>(w) || qobject_cast<QMenuBar*>(w) || w->window() != this;
        if (!keep) m_tools->commitModal();
    }
    // Text entry (the Type tool) takes keys ahead of the single-letter shortcuts.
    if ((e->type() == QEvent::KeyPress || e->type() == QEvent::ShortcutOverride) && !QApplication::activeModalWidget()
        && !QApplication::activePopupWidget() && !isTypingTarget()) {
        Tool* cur = m_tools->current();
        CanvasView* v = currentView();
        auto* ke = static_cast<QKeyEvent*>(e);
        if (cur && v && cur->wantsKey(ke)) {
            if (e->type() == QEvent::ShortcutOverride) {
                e->accept();
                return true;
            }
            cur->keyPress(v, ke);
            return true;
        }
    }
    if (e->type() != QEvent::KeyPress && e->type() != QEvent::KeyRelease) return QMainWindow::eventFilter(obj, e);
    if (QApplication::activeModalWidget() || QApplication::activePopupWidget() || !isActiveWindow() || isTypingTarget())
        return QMainWindow::eventFilter(obj, e);
    auto* ke = static_cast<QKeyEvent*>(e);
    const bool press = e->type() == QEvent::KeyPress;
    const Qt::KeyboardModifiers mods = ke->modifiers() & ~Qt::KeypadModifier;
    Tool* cur = m_tools->current();
    const QString curId = cur ? cur->id() : QString();

    switch (ke->key()) {
    case Qt::Key_Space:
        // Hold Space for the Hand tool.
        if (press && !ke->isAutoRepeat() && !m_tools->hasTemporary() && currentView()) {
            m_tools->pushTemporary(QStringLiteral("hand"));
            m_tempFromKey = m_tools->hasTemporary();
        } else if (!press && !ke->isAutoRepeat() && m_tempFromKey && curId == QLatin1String("hand")) {
            m_tools->popTemporary();
            m_tempFromKey = false;
        }
        return true;
    case Qt::Key_Alt:
        // Hold Alt/Option with painting tools for the Eyedropper.
        if (press && !ke->isAutoRepeat() && !m_tools->hasTemporary()
            && (curId == QLatin1String("brush") || curId == QLatin1String("pencil") || curId == QLatin1String("gradient")
                || curId == QLatin1String("bucket"))) {
            m_tools->pushTemporary(QStringLiteral("eyedropper"));
            m_tempFromKey = m_tools->hasTemporary();
        } else if (!press && m_tempFromKey && curId == QLatin1String("eyedropper")) {
            m_tools->popTemporary();
            m_tempFromKey = false;
        }
        if (CanvasView* v = currentView()) v->updateCursor();
        break;
    case Qt::Key_Control:
        // Hold Ctrl/Cmd for the Move tool.
        if (press && !ke->isAutoRepeat() && !m_tools->hasTemporary()
            && !(curId == QLatin1String("move") || curId == QLatin1String("hand") || curId == QLatin1String("zoom")
                 || curId == QLatin1String("crop") || curId == QLatin1String("transform"))) {
            m_tools->pushTemporary(QStringLiteral("move"));
            m_tempFromKey = m_tools->hasTemporary();
        } else if (!press && m_tempFromKey && curId == QLatin1String("move")) {
            m_tools->popTemporary();
            m_tempFromKey = false;
        }
        break;
    case Qt::Key_Tab:
    case Qt::Key_Backtab:
        if (press && !(mods & (Qt::ControlModifier | Qt::AltModifier))) {
            toggleAllPanels(ke->key() == Qt::Key_Backtab || (mods & Qt::ShiftModifier));
            return true;
        }
        break;
    case Qt::Key_Return:
    case Qt::Key_Enter:
    case Qt::Key_Escape:
        if (press && cur && currentView()) {
            const bool handled = ke->key() == Qt::Key_Escape ? cur->cancel(currentView()) : cur->commit(currentView());
            if (handled) return true;
        }
        break;
    default:
        if (press && ke->key() >= Qt::Key_0 && ke->key() <= Qt::Key_9 && mods == Qt::NoModifier && currentDoc()
            && !m_tools->modalTool()) {
            handleDigit(ke->key() - Qt::Key_0);
            return true;
        }
        break;
    }
    return QMainWindow::eventFilter(obj, e);
}

void MainWindow::dragEnterEvent(QDragEnterEvent* e)
{
    if (e->mimeData()->hasUrls() || e->mimeData()->hasImage()) e->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent* e)
{
    QStringList files;
    for (const QUrl& u : e->mimeData()->urls())
        if (u.isLocalFile()) files << u.toLocalFile();
    if (!files.isEmpty()) {
        openFiles(files);
    } else if (e->mimeData()->hasImage()) {
        m_tools->commitModal();
        QImage img = qvariant_cast<QImage>(e->mimeData()->imageData());
        if (Document* doc = currentDoc()) Ops::paste(doc, img);
        else addDocument(DocumentIO::fromImage(img, QStringLiteral("Untitled-%1").arg(++m_untitled)));
    }
    e->acceptProposedAction();
}

void MainWindow::closeEvent(QCloseEvent* e)
{
    if (!closeAll()) {
        e->ignore();
        return;
    }
    if (m_screenMode != 0) setScreenMode(0);
    QSettings s;
    s.setValue(QStringLiteral("geometry"), saveGeometry());
    s.setValue(QStringLiteral("windowState"), saveState(kStateVersion));
    e->accept();
}
