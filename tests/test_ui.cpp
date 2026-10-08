// Drives the real main window with simulated input to check tools end to end.
#include "core/ColorState.h"
#include "core/Document.h"
#include "core/Adjustments.h"
#include "core/DocumentOps.h"
#include "core/LayerStyle.h"
#include "core/LayerTree.h"
#include "core/VectorLayers.h"
#include "tools/FreeTransformTool.h"
#include "tools/RetouchTools.h"
#include "tools/SelectionTools.h"
#include "tools/Tool.h"
#include "tools/ToolManager.h"
#include "tools/VectorTools.h"
#include "ui/CanvasView.h"
#include "ui/DocumentPage.h"
#include "ui/MainWindow.h"
#include "ui/Ruler.h"
#include "ui/ViewOptions.h"
#include "ui/dialogs/AdjustmentDialogs.h"
#include "ui/dialogs/ColorPickerDialog.h"
#include "ui/dialogs/Dialogs.h"
#include "ui/dialogs/LayerStyleDialog.h"
#include "ui/panels/LayersPanel.h"
#include "ui/panels/Panels.h"

#include <QListView>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QUndoStack>
#include <QtTest>

class TestUi : public QObject {
    Q_OBJECT

    std::unique_ptr<MainWindow> w;
    QTemporaryDir dir;

    ToolManager* tools() { return w->findChild<ToolManager*>(); }
    ColorState* colors() { return w->findChild<ColorState*>(); }
    DocumentPage* page() { return w->findChild<DocumentPage*>(); }
    Document* doc() { return page()->document(); }
    CanvasView* view() { return page()->view(); }

    QPoint at(double x, double y) { return view()->canvasToView(QPointF(x, y)).toPoint(); }

    void drag(QPointF from, QPointF to, Qt::KeyboardModifiers mods = {}, int steps = 10)
    {
        QWidget* vp = view()->viewport();
        QTest::mousePress(vp, Qt::LeftButton, mods, at(from.x(), from.y()));
        for (int i = 1; i <= steps; ++i) {
            QPointF p = from + (to - from) * (double(i) / steps);
            if (mods) {
                // QTest::mouseMove carries no modifiers; tools read them on every move.
                const QPoint vpPos = at(p.x(), p.y());
                QMouseEvent ev(QEvent::MouseMove, vpPos, vp->mapToGlobal(vpPos), Qt::NoButton, Qt::LeftButton, mods);
                QApplication::sendEvent(vp, &ev);
            } else {
                QTest::mouseMove(vp, at(p.x(), p.y()));
            }
        }
        QTest::mouseRelease(vp, Qt::LeftButton, mods, at(to.x(), to.y()));
        QCoreApplication::processEvents();
    }

    void click(QPointF p, Qt::KeyboardModifiers mods = {})
    {
        QTest::mouseClick(view()->viewport(), Qt::LeftButton, mods, at(p.x(), p.y()));
        QCoreApplication::processEvents();
    }

    FreeTransformTool* transform() { return static_cast<FreeTransformTool*>(tools()->tool(QStringLiteral("transform"))); }
    ViewOptions* options() { return w->findChild<ViewOptions*>(); }

    // A new transparent layer with a black square at `r`.
    void squareLayer(QRect r)
    {
        Ops::newLayer(doc());
        QPainterPath path;
        path.addRect(r);
        doc()->changeSelection(Sel::pathMask(doc()->size(), path, false), QStringLiteral("Rectangular Marquee"));
        QVERIFY(Ops::fill(doc(), Qt::black, BlendMode::Normal, 1.f, false));
        Ops::deselect(doc());
    }

    QColor pixel(int x, int y) { return QColor::fromRgba(qUnpremultiply(doc()->compositePixel(QPoint(x, y)))); }

    void openWhite(QSize size = QSize(200, 150))
    {
        if (transform()->isActive()) transform()->cancel(view());
        while (page()) {
            doc()->setClean();
            w->action(QStringLiteral("file.close"))->trigger();
            QCoreApplication::processEvents();
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        }
        QImage img(size, QImage::Format_RGB32);
        img.fill(Qt::white);
        const QString path = dir.filePath(QStringLiteral("white.png"));
        img.save(path);
        w->openFiles({path});
        QCoreApplication::processEvents();
        QVERIFY(page());
        view()->actualPixels();
        view()->resize(600, 400);
        QCoreApplication::processEvents();
    }

    // Triggers an action that opens a modal dialog of type D and runs `fn` on it inside exec().
    // Any other modal window (an alert) is closed so the test cannot hang.
    template <typename D>
    bool withDialog(const QString& actionId, const std::function<void(D*)>& fn)
    {
        bool seen = false;
        QTimer::singleShot(0, w.get(), [&] {
            QWidget* m = QApplication::activeModalWidget();
            if (auto* d = qobject_cast<D*>(m)) {
                seen = true;
                fn(d);
                if (d->isVisible()) d->reject();
            } else if (m) {
                m->close();
            }
        });
        w->action(actionId)->trigger();
        QCoreApplication::processEvents();
        return seen;
    }


    // ---------------- Stage 4 helpers ----------------

    LayersPanel* layersPanel() { return w->findChild<LayersPanel*>(); }

    // Clicks a part (thumbnail, mask...) of a layer's row in the Layers panel.
    void clickRowPart(int docIndex, LayerDelegate::Part part, Qt::KeyboardModifiers mods = {})
    {
        LayersPanel* lp = layersPanel();
        auto* model = static_cast<LayerModel*>(lp->list()->model());
        const QRect row = lp->list()->visualRect(model->index(model->rowFor(docIndex)));
        const QRect r = lp->delegate()->partRect(docIndex, part, row.width()).translated(row.topLeft());
        QTest::mouseClick(lp->list()->viewport(), Qt::LeftButton, mods, r.center());
        QCoreApplication::processEvents();
    }

private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName(QStringLiteral("PhotoSlopTests"));
        QSettings().clear();
        w = std::make_unique<MainWindow>();
        w->resize(1200, 800);
        w->show();
        QVERIFY(QTest::qWaitForWindowExposed(w.get()));
    }

    void cleanupTestCase()
    {
        if (page()) doc()->setClean();
        QSettings().clear();
        w.reset();
    }

    void brushStrokeAndUndo()
    {
        openWhite();
        colors()->setForeground(Qt::red);
        tools()->select(QStringLiteral("brush"));
        drag({20, 75}, {180, 75});
        QCOMPARE(pixel(100, 75), QColor(Qt::red));
        QCOMPARE(pixel(100, 20), QColor(Qt::white));
        QCOMPARE(doc()->undoStack()->count(), 1);
        QCOMPARE(doc()->undoStack()->text(0), QStringLiteral("Brush Tool"));
        doc()->undoStack()->undo();
        QCOMPARE(pixel(100, 75), QColor(Qt::white));
    }

    void eraserOnBackgroundPaintsBackgroundColor()
    {
        openWhite();
        colors()->setBackground(Qt::blue);
        tools()->select(QStringLiteral("eraser"));
        drag({20, 40}, {120, 40});
        QCOMPARE(pixel(60, 40), QColor(Qt::blue));
        colors()->reset();
    }

    void marqueeSelectsAndClickDeselects()
    {
        openWhite();
        tools()->select(QStringLiteral("marquee-rect"));
        drag({10, 10}, {60, 40});
        QVERIFY(doc()->hasSelection());
        QCOMPARE(doc()->selectionBounds(), QRect(10, 10, 50, 30));
        QVERIFY(Ops::fill(doc(), Qt::green, BlendMode::Normal, 1.f, false));
        QCOMPARE(pixel(30, 20), QColor(Qt::green));
        QCOMPARE(pixel(100, 100), QColor(Qt::white));
        click({150, 120});
        QVERIFY(!doc()->hasSelection());
    }

    void lassoMakesSelection()
    {
        openWhite();
        tools()->select(QStringLiteral("lasso"));
        QWidget* vp = view()->viewport();
        QTest::mousePress(vp, Qt::LeftButton, {}, at(20, 20));
        for (QPointF p : {QPointF(100, 20), QPointF(100, 100), QPointF(20, 100), QPointF(20, 22)})
            QTest::mouseMove(vp, at(p.x(), p.y()));
        QTest::mouseRelease(vp, Qt::LeftButton, {}, at(20, 22));
        QVERIFY(doc()->hasSelection());
        QRect b = doc()->selectionBounds();
        QVERIFY(std::abs(b.left() - 20) <= 1 && std::abs(b.right() - 100) <= 1);
    }

    void moveToolMovesLayer()
    {
        openWhite();
        Ops::newLayer(doc());
        Ops::selectAll(doc());
        Ops::deselect(doc());
        QPainterPath path;
        path.addRect(10, 10, 20, 20);
        doc()->changeSelection(Sel::pathMask(doc()->size(), path, false), QStringLiteral("Rectangular Marquee"));
        QVERIFY(Ops::fill(doc(), Qt::black, BlendMode::Normal, 1.f, false));
        Ops::deselect(doc());
        tools()->select(QStringLiteral("move"));
        const QPoint before = doc()->activeLayer()->offset;
        drag({15, 15}, {45, 15});
        QCOMPARE(doc()->activeLayer()->offset, before + QPoint(30, 0));
        QCOMPARE(pixel(50, 20), QColor(Qt::black));
        QCOMPARE(pixel(15, 20), QColor(Qt::white));
        QCOMPARE(doc()->undoStack()->text(doc()->undoStack()->index() - 1), QStringLiteral("Move"));
    }

    void moveToolMovesSelectedPixels()
    {
        openWhite();
        QPainterPath path;
        path.addRect(10, 10, 20, 20);
        doc()->changeSelection(Sel::pathMask(doc()->size(), path, false), QStringLiteral("Rectangular Marquee"));
        QVERIFY(Ops::fill(doc(), Qt::black, BlendMode::Normal, 1.f, false));
        colors()->reset();
        tools()->select(QStringLiteral("move"));
        drag({15, 15}, {75, 15});
        QCOMPARE(pixel(80, 20), QColor(Qt::black));
        // The Background layer fills the hole with the background colour.
        QCOMPARE(pixel(15, 20), QColor(Qt::white));
        QCOMPARE(doc()->selectionBounds(), QRect(70, 10, 20, 20));
    }

    void bucketFillsContiguousArea()
    {
        openWhite();
        colors()->setForeground(Qt::magenta);
        tools()->select(QStringLiteral("bucket"));
        click({50, 50});
        QCOMPARE(pixel(5, 5), QColor(Qt::magenta));
        QCOMPARE(pixel(190, 140), QColor(Qt::magenta));
    }

    void gradientRendersRamp()
    {
        openWhite();
        colors()->reset();
        tools()->select(QStringLiteral("gradient"));
        drag({0, 75}, {200, 75}, {}, 4);
        QVERIFY(pixel(5, 75).lightness() < 30);
        QVERIFY(pixel(195, 75).lightness() > 225);
        QVERIFY(std::abs(pixel(100, 75).lightness() - 128) < 20);
    }

    void eyedropperSamples()
    {
        openWhite();
        QVERIFY(Ops::fill(doc(), QColor(10, 200, 30), BlendMode::Normal, 1.f, false));
        colors()->reset();
        tools()->select(QStringLiteral("eyedropper"));
        click({40, 40});
        QCOMPARE(colors()->foreground(), QColor(10, 200, 30));
    }

    void cropToolCrops()
    {
        openWhite();
        tools()->select(QStringLiteral("crop"));
        drag({20, 20}, {120, 90});
        QVERIFY(tools()->current()->commit(view()));
        QCOMPARE(doc()->size(), QSize(100, 70));
        doc()->undoStack()->undo();
        QCOMPARE(doc()->size(), QSize(200, 150));
    }

    void zoomToolZoomsInAndOut()
    {
        openWhite();
        tools()->select(QStringLiteral("zoom"));
        const double z = view()->zoom();
        click({100, 75});
        QVERIFY(view()->zoom() > z);
        click({100, 75}, Qt::AltModifier);
        QCOMPARE(view()->zoom(), z);
    }

    void toolShortcutsCycleGroups()
    {
        tools()->select(QStringLiteral("brush"));
        QVERIFY(tools()->selectByShortcut(QLatin1Char('B'), true));
        QCOMPARE(tools()->current()->id(), QStringLiteral("pencil"));
        QVERIFY(tools()->selectByShortcut(QLatin1Char('M'), false));
        QVERIFY(tools()->current()->id().startsWith(QStringLiteral("marquee")));
        QVERIFY(tools()->selectByShortcut(QLatin1Char('B'), false));
        QCOMPARE(tools()->current()->id(), QStringLiteral("pencil")); // remembers last used in group
    }

    void layerViaCopyAndMerge()
    {
        openWhite();
        Ops::selectAll(doc());
        w->action(QStringLiteral("layer.viaCopy"))->trigger();
        QCOMPARE(doc()->layerCount(), 2);
        w->action(QStringLiteral("layer.mergeDown"))->trigger();
        QCOMPARE(doc()->layerCount(), 1);
    }

    void dialogsConstruct()
    {
        // Smoke test; set PHOTOSLOP_DIALOG_SHOTS=<dir> to save screenshots for review.
        openWhite();
        const QString shots = qEnvironmentVariable("PHOTOSLOP_DIALOG_SHOTS");
        auto check = [&](QDialog& d, const QString& name) {
            d.show();
            QCoreApplication::processEvents();
            QVERIFY(d.isVisible());
            if (!shots.isEmpty()) d.grab().save(shots + QLatin1Char('/') + name + QStringLiteral(".png"));
            d.close();
        };
        NewDocumentDialog nd(QStringLiteral("Untitled-1"), QSize(), w.get());
        check(nd, QStringLiteral("new"));
        QCOMPARE(nd.pixelSize(), QSize(2100, 1500));
        ColorPickerDialog cp(QColor(200, 60, 40), QStringLiteral("Color Picker (Foreground Color)"), w.get());
        check(cp, QStringLiteral("picker"));
        QCOMPARE(cp.color(), QColor(200, 60, 40));
        ImageSizeDialog is(doc(), w.get());
        check(is, QStringLiteral("imagesize"));
        QCOMPARE(is.newSize(), doc()->size());
        CanvasSizeDialog cs(doc(), colors(), w.get());
        check(cs, QStringLiteral("canvassize"));
        QCOMPARE(cs.newSize(), doc()->size());
        FillDialog fd(colors(), w.get());
        check(fd, QStringLiteral("fill"));
        ExportDialog ed(doc(), w.get());
        check(ed, QStringLiteral("export"));
        ModifySelectionDialog md(QStringLiteral("Expand Selection"), QStringLiteral("Expand By:"), 3, true, w.get());
        check(md, QStringLiteral("expand"));
        QCOMPARE(md.amount(), 3);
        NewGuideDialog gd(w.get());
        check(gd, QStringLiteral("newguide"));
        QCOMPARE(gd.orientation(), Qt::Horizontal);
        LevelsDialog lv(doc(), false, w.get());
        check(lv, QStringLiteral("levels"));
        CurvesDialog cv(doc(), false, w.get());
        check(cv, QStringLiteral("curves"));
        HueSaturationDialog hs(doc(), false, w.get());
        check(hs, QStringLiteral("huesat"));
        ColorBalanceDialog cb(doc(), false, w.get());
        check(cb, QStringLiteral("colorbalance"));
        ThresholdDialog th(doc(), w.get());
        check(th, QStringLiteral("threshold"));
        ParamDialog gb(doc(), QStringLiteral("Gaussian Blur"), true,
                       [](const ParamDialog::Values& v) { return Filters::gaussianBlurSpec(v[QStringLiteral("radius")]); }, w.get());
        gb.addSlider(QStringLiteral("radius"), QStringLiteral("Radius:"), 0.1, 1000, 1.0, 1, QStringLiteral("Pixels"), 250);
        gb.ready();
        check(gb, QStringLiteral("gaussian"));
        QCOMPARE(doc()->undoStack()->count(), 0);
        if (!shots.isEmpty()) w->grab().save(shots + QStringLiteral("/window.png"));
    }

    void magicWandSelectsColorRegion()
    {
        openWhite();
        squareLayer(QRect(20, 20, 40, 30));
        tools()->select(QStringLiteral("magic-wand"));
        auto* wand = static_cast<MagicWandTool*>(tools()->current());
        wand->setTolerance(10);
        click({30, 30});
        QCOMPARE(doc()->selectionBounds(), QRect(20, 20, 40, 30));
        // Shift-click adds the white area of the background layer.
        Ops::setVisible(doc(), 1, true);
        doc()->setActiveIndex(0);
        click({150, 100}, Qt::ShiftModifier);
        QCOMPARE(doc()->selectionBounds(), doc()->bounds());
    }

    void quickSelectionFollowsColour()
    {
        openWhite();
        squareLayer(QRect(40, 40, 80, 60));
        doc()->setActiveIndex(0);
        tools()->select(QStringLiteral("quick-selection"));
        // Sample all layers so the black square is visible to the tool.
        Ops::mergeVisible(doc());
        drag({60, 60}, {100, 70}, {}, 6);
        QVERIFY(doc()->hasSelection());
        // It spreads through the black square but stops at its edges (Auto-Enhance softens them slightly).
        auto sel = [&](int x, int y) { return doc()->selection().constScanLine(y)[x]; };
        QVERIFY(sel(42, 42) >= 128 && sel(117, 97) >= 128);
        QVERIFY(sel(36, 70) < 128 && sel(124, 70) < 128 && sel(80, 36) < 128 && sel(80, 104) < 128);
        QCOMPARE(doc()->undoStack()->text(doc()->undoStack()->index() - 1), QStringLiteral("Quick Selection"));
    }

    void magneticLassoSnapsToEdges()
    {
        openWhite();
        squareLayer(QRect(50, 40, 80, 60));
        tools()->select(QStringLiteral("lasso-magnetic"));
        auto* lasso = static_cast<MagneticLassoTool*>(tools()->current());
        lasso->prepare(doc());
        // Near the square's left edge, the strongest edge is on the boundary.
        const QPoint snapped = lasso->snapToEdge(QPoint(44, 70));
        QVERIFY(std::abs(snapped.x() - 50) <= 1);
        // The live wire between two edge points hugs the boundary rather than cutting across.
        const auto path = lasso->liveWire(QPoint(50, 45), QPoint(55, 40));
        for (const QPoint& p : path) QVERIFY(std::abs(p.x() - 50) <= 1 || std::abs(p.y() - 40) <= 1);
        // Trace it end to end with clicks and close with Enter.
        QWidget* vp = view()->viewport();
        for (QPointF p : {QPointF(48, 38), QPointF(132, 38), QPointF(132, 102), QPointF(48, 102)}) {
            QTest::mouseMove(vp, at(p.x(), p.y()));
            click(p);
        }
        QVERIFY(lasso->commit(view()));
        QVERIFY(doc()->hasSelection());
        const QRect b = doc()->selectionBounds();
        QVERIFY(std::abs(b.left() - 50) <= 2 && std::abs(b.right() - 129) <= 2);
        QVERIFY(std::abs(b.top() - 40) <= 2 && std::abs(b.bottom() - 99) <= 2);
    }

    void freeTransformMoveScaleAndUndo()
    {
        openWhite();
        squareLayer(QRect(20, 20, 20, 20));
        tools()->select(QStringLiteral("brush"));
        w->action(QStringLiteral("edit.freeTransform"))->trigger();
        QVERIFY(transform()->isActive());
        QCOMPARE(tools()->current(), transform());
        QCOMPARE(transform()->quad().boundingRect(), QRectF(20, 20, 20, 20));
        // Drag inside (away from the reference point) to move, then drag the bottom-right
        // handle out (proportional scale).
        drag({25, 25}, {55, 35});
        QCOMPARE(transform()->quad().boundingRect(), QRectF(50, 30, 20, 20));
        drag({70, 50}, {90, 70});
        QCOMPARE(transform()->quad().boundingRect(), QRectF(50, 30, 40, 40));
        QCOMPARE(pixel(85, 65), QColor(Qt::black));
        QVERIFY(transform()->commit(view()));
        QVERIFY(!transform()->isActive());
        QCOMPARE(tools()->current()->id(), QStringLiteral("brush")); // back to the previous tool
        QCOMPARE(doc()->undoStack()->text(doc()->undoStack()->index() - 1), QStringLiteral("Free Transform"));
        QCOMPARE(pixel(25, 25), QColor(Qt::white));
        QCOMPARE(pixel(70, 50), QColor(Qt::black));
        doc()->undoStack()->undo();
        QCOMPARE(pixel(25, 25), QColor(Qt::black));
        QCOMPARE(pixel(70, 50), QColor(Qt::white));
    }

    void freeTransformCancelRestores()
    {
        openWhite();
        squareLayer(QRect(20, 20, 20, 20));
        const int count = doc()->undoStack()->count();
        w->action(QStringLiteral("edit.freeTransform"))->trigger();
        drag({25, 25}, {100, 100});
        QCOMPARE(pixel(25, 25), QColor(Qt::white));
        QTest::keyClick(view(), Qt::Key_Escape);
        QVERIFY(!transform()->isActive());
        QCOMPARE(pixel(25, 25), QColor(Qt::black));
        QCOMPARE(doc()->undoStack()->count(), count);
        // Undo inside a transform cancels it rather than stepping back.
        w->action(QStringLiteral("edit.freeTransform"))->trigger();
        drag({25, 25}, {100, 100});
        w->action(QStringLiteral("edit.undo"))->trigger();
        QVERIFY(!transform()->isActive());
        QCOMPARE(pixel(25, 25), QColor(Qt::black));
        QCOMPARE(doc()->undoStack()->count(), count);
    }

    void freeTransformRotateAndDistort()
    {
        openWhite();
        squareLayer(QRect(20, 20, 40, 20));
        w->action(QStringLiteral("edit.freeTransform"))->trigger();
        // Drag outside the box to rotate; Shift snaps to 15° steps, here 90°.
        drag({80, 30}, {40, 80}, Qt::ShiftModifier);
        const QRectF b = transform()->quad().boundingRect();
        QVERIFY(std::abs(b.width() - 20) < 0.01 && std::abs(b.height() - 40) < 0.01);
        // Ctrl-drag a corner distorts just that corner.
        const QPolygonF before = transform()->quad();
        int corner = 0;
        for (int i = 1; i < 4; ++i)
            if (before[i].x() + before[i].y() > before[corner].x() + before[corner].y()) corner = i;
        const QPointF c = before[corner];
        drag(c, c + QPointF(10, 5), Qt::ControlModifier);
        const QPolygonF after = transform()->quad();
        QCOMPARE(after[corner], c + QPointF(10, 5));
        QCOMPARE(after[(corner + 2) % 4], before[(corner + 2) % 4]);
        QTest::keyClick(view(), Qt::Key_Return);
        QVERIFY(!transform()->isActive());
    }

    void transformMenuRotatesLosslessly()
    {
        openWhite();
        squareLayer(QRect(20, 20, 30, 10));
        doc()->layerRef(1).image.setPixel(20 - doc()->layerAt(1).offset.x(), 20 - doc()->layerAt(1).offset.y(), qRgb(255, 0, 0));
        doc()->notifyLayersChanged();
        w->action(QStringLiteral("transform.rot90cw"))->trigger();
        QVERIFY(!transform()->isActive());
        QCOMPARE(doc()->undoStack()->text(doc()->undoStack()->index() - 1), QStringLiteral("Rotate 90° Clockwise"));
        Layer l = doc()->layerAt(1);
        l.trimToContent();
        QCOMPARE(l.rect().size(), QSize(10, 30));
        // The red top-left pixel lands at the top-right, with no blurring.
        QCOMPARE(QColor::fromRgba(qUnpremultiply(l.pixelAt(QPoint(l.rect().right(), l.rect().top())))), QColor(Qt::red));
        QCOMPARE(QColor::fromRgba(qUnpremultiply(l.pixelAt(QPoint(l.rect().left(), l.rect().top() + 5)))), QColor(Qt::black));
        w->action(QStringLiteral("transform.again"))->trigger();
        l = doc()->layerAt(1);
        l.trimToContent();
        QCOMPARE(l.rect().size(), QSize(30, 10));
    }

    void transformSelectionScalesOutline()
    {
        openWhite();
        QPainterPath path;
        path.addRect(20, 20, 40, 40);
        doc()->changeSelection(Sel::pathMask(doc()->size(), path, false), QStringLiteral("Rectangular Marquee"));
        w->action(QStringLiteral("select.transform"))->trigger();
        QVERIFY(transform()->isActive());
        transform()->setQuad(QPolygonF{{20, 20}, {100, 20}, {100, 60}, {20, 60}});
        QVERIFY(transform()->commit(view()));
        QCOMPARE(doc()->selectionBounds(), QRect(20, 20, 80, 40));
        QCOMPARE(pixel(30, 30), QColor(Qt::white)); // pixels untouched
        QCOMPARE(doc()->undoStack()->text(doc()->undoStack()->index() - 1), QStringLiteral("Transform Selection"));
    }

    void warpBendsPixels()
    {
        openWhite();
        squareLayer(QRect(20, 20, 60, 60));
        w->action(QStringLiteral("transform.warp"))->trigger();
        QVERIFY(transform()->isActive());
        QCOMPARE(transform()->mode(), FreeTransformTool::Mode::Warp);
        // Pull the middle of the grid to the right.
        drag({50, 50}, {70, 50}, {}, 4);
        QVERIFY(transform()->commit(view()));
        QCOMPARE(doc()->undoStack()->text(doc()->undoStack()->index() - 1), QStringLiteral("Warp"));
        QCOMPARE(pixel(85, 50), QColor(Qt::black)); // bulges past the old right edge
        QCOMPARE(pixel(21, 50), QColor(Qt::white)); // and away from the left
        QCOMPARE(pixel(21, 21), QColor(Qt::black)); // corners stay put
    }

    void switchingToolsCommitsTransform()
    {
        openWhite();
        squareLayer(QRect(20, 20, 20, 20));
        w->action(QStringLiteral("edit.freeTransform"))->trigger();
        drag({25, 25}, {55, 25});
        tools()->select(QStringLiteral("marquee-rect"));
        QVERIFY(!transform()->isActive());
        QCOMPARE(tools()->current()->id(), QStringLiteral("marquee-rect"));
        QCOMPARE(doc()->undoStack()->text(doc()->undoStack()->index() - 1), QStringLiteral("Free Transform"));
        QCOMPARE(pixel(55, 30), QColor(Qt::black));
    }

    void backgroundTransformNeedsSelection()
    {
        openWhite();
        QString alert;
        auto c = connect(tools(), &ToolManager::alertRequested, this, [&](const QString& m) { alert = m; });
        // The window shows the alert in a modal box; dismiss it once it is up.
        QTimer::singleShot(0, [] {
            if (QWidget* box = QApplication::activeModalWidget()) box->close();
        });
        QVERIFY(!transform()->begin(view(), false));
        disconnect(c);
        QVERIFY(alert.contains(QStringLiteral("locked")));
        QVERIFY(!transform()->isActive());
    }

    void quickMaskPaintingMakesSelection()
    {
        openWhite();
        colors()->reset();
        QPainterPath path;
        path.addRect(10, 10, 60, 60);
        doc()->changeSelection(Sel::pathMask(doc()->size(), path, false), QStringLiteral("Rectangular Marquee"));
        // Offscreen windows are never active, so trigger the Q shortcut's action directly.
        QAction* qm = w->action(QStringLiteral("select.quickMask"));
        QCOMPARE(qm->shortcut(), QKeySequence(Qt::Key_Q));
        qm->trigger();
        QVERIFY(doc()->inQuickMask());
        QVERIFY(page()->tabTitle().contains(QStringLiteral("Quick Mask")));
        // Paint black (mask out) across the middle of the selection.
        tools()->select(QStringLiteral("brush"));
        drag({0, 40}, {199, 40});
        QCOMPARE(pixel(40, 40), QColor(Qt::white)); // the image itself is untouched
        qm->trigger();
        QVERIFY(!doc()->inQuickMask());
        QVERIFY(doc()->hasSelection());
        QCOMPARE(doc()->selection().constScanLine(20)[20], uchar(255));
        QVERIFY(doc()->selection().constScanLine(40)[40] < 10);
    }

    void rulersMakeGuidesAndMarqueeSnaps()
    {
        openWhite();
        w->action(QStringLiteral("view.rulers"))->trigger();
        QCoreApplication::processEvents();
        auto rulers = page()->findChildren<Ruler*>();
        QCOMPARE(rulers.size(), 2);
        Ruler* top = rulers[0]->height() == Ruler::kThickness ? rulers[0] : rulers[1];
        QVERIFY(top->isVisible());
        // Drag a horizontal guide out of the top ruler down to y = 50.
        const QPoint target = top->mapFromGlobal(view()->viewport()->mapToGlobal(at(100, 50)));
        QTest::mousePress(top, Qt::LeftButton, {}, QPoint(target.x(), 5));
        QTest::mouseMove(top, target);
        QTest::mouseRelease(top, Qt::LeftButton, {}, target);
        QCOMPARE(doc()->guides().size(), 1);
        QCOMPARE(doc()->guides()[0].orientation, Qt::Horizontal);
        QCOMPARE(doc()->guides()[0].position, 50.0);
        // A marquee dragged to just short of the guide snaps onto it.
        tools()->select(QStringLiteral("marquee-rect"));
        drag({10, 10}, {60, 48});
        QCOMPARE(doc()->selectionBounds(), QRect(10, 10, 50, 40));
        // With snapping off it does not.
        Ops::deselect(doc());
        w->action(QStringLiteral("view.snap"))->trigger();
        QVERIFY(!options()->snap);
        drag({10, 10}, {60, 48});
        QCOMPARE(doc()->selectionBounds(), QRect(10, 10, 50, 38));
        w->action(QStringLiteral("view.snap"))->trigger();
        // Move tool drags the guide off the canvas window to delete it.
        tools()->select(QStringLiteral("move"));
        QWidget* vp = view()->viewport();
        QTest::mousePress(vp, Qt::LeftButton, {}, at(100, 50));
        QTest::mouseMove(vp, QPoint(100, -40));
        QTest::mouseRelease(vp, Qt::LeftButton, {}, QPoint(100, -40));
        QVERIFY(doc()->guides().isEmpty());
        doc()->undoStack()->undo();
        QCOMPARE(doc()->guides().size(), 1);
        w->action(QStringLiteral("view.rulers"))->trigger();
        QVERIFY(!top->isVisible());
    }

    void moveToolSnapsToGuides()
    {
        openWhite();
        options()->snap = options()->snapGuides = true;
        options()->notify();
        squareLayer(QRect(20, 20, 20, 20));
        doc()->changeGuides({Guide{Qt::Vertical, 60}}, QStringLiteral("New Guide"));
        tools()->select(QStringLiteral("move"));
        // Dragging 18 px puts the right edge 2 px from the guide, so it snaps onto it.
        drag({25, 25}, {43, 25});
        QCOMPARE(pixel(59, 30), QColor(Qt::black));
        QCOMPARE(pixel(60, 30), QColor(Qt::white));
        QCOMPARE(pixel(39, 30), QColor(Qt::white));
    }

    void gridAndGuideTogglesTrackOptions()
    {
        openWhite();
        options()->snap = options()->snapGuides = true;
        options()->notify();
        QAction* grid = w->action(QStringLiteral("view.grid"));
        const bool was = options()->grid;
        grid->trigger();
        QCOMPARE(options()->grid, !was);
        grid->trigger();
        QCOMPARE(options()->grid, was);
        // Hiding Extras hides guides; snapping ignores hidden guides.
        doc()->changeGuides({Guide{Qt::Vertical, 30}}, QStringLiteral("New Guide"));
        QCOMPARE(view()->snapPoint(QPointF(31, 70)).x(), 30.0);
        w->action(QStringLiteral("view.extras"))->trigger();
        QVERIFY(!options()->showsGuides());
        QCOMPARE(view()->snapPoint(QPointF(31, 70)).x(), 31.0);
        w->action(QStringLiteral("view.extras"))->trigger();
    }

    void selectModifyMenus()
    {
        openWhite();
        QPainterPath path;
        path.addRect(20, 20, 40, 40);
        doc()->changeSelection(Sel::pathMask(doc()->size(), path, false), QStringLiteral("Rectangular Marquee"));
        QVERIFY(Ops::modifySelection(doc(), Ops::Modify::Contract, 5));
        QCOMPARE(doc()->selectionBounds(), QRect(25, 25, 30, 30));
        QVERIFY(w->action(QStringLiteral("select.grow"))->isEnabled());
        w->action(QStringLiteral("select.grow"))->trigger();
        QCOMPARE(doc()->selectionBounds(), doc()->bounds()); // all white
    }

    void levelsPreviewOkAndCancel()
    {
        openWhite();
        const int count = doc()->undoStack()->count();
        QColor preview;
        QVERIFY(withDialog<LevelsDialog>(QStringLiteral("image.levels"), [&](LevelsDialog* d) {
            Adjust::Levels lv;
            lv.channels[0].outWhite = 128;
            d->setLevels(lv);
            d->waitForPreview();
            preview = pixel(10, 10);
            d->accept();
        }));
        QCOMPARE(preview, QColor(128, 128, 128));
        QCOMPARE(pixel(10, 10), QColor(128, 128, 128));
        QCOMPARE(doc()->undoStack()->count(), count + 1);
        QCOMPARE(doc()->undoStack()->text(count), QStringLiteral("Levels"));
        // Cancel restores the layer and adds no history.
        QVERIFY(withDialog<LevelsDialog>(QStringLiteral("image.levels"), [&](LevelsDialog* d) {
            QCOMPARE(d->levels(), Adjust::Levels()); // starts from defaults
            Adjust::Levels lv;
            lv.channels[0].outWhite = 0;
            d->setLevels(lv);
            d->waitForPreview();
            preview = pixel(10, 10);
            d->reject();
        }));
        QCOMPARE(preview, QColor(0, 0, 0));
        QCOMPARE(pixel(10, 10), QColor(128, 128, 128));
        QCOMPARE(doc()->undoStack()->count(), count + 1);
        // Preview off shows the original until OK.
        QVERIFY(withDialog<HueSaturationDialog>(QStringLiteral("image.hueSaturation"), [&](HueSaturationDialog* d) {
            d->setPreviewEnabled(false);
            Adjust::HueSaturation hs;
            hs.ranges[0].lightness = 100;
            d->setSettings(hs);
            QCoreApplication::processEvents();
            preview = pixel(10, 10);
            d->accept();
        }));
        QCOMPARE(preview, QColor(128, 128, 128));
        QCOMPARE(pixel(10, 10), QColor(Qt::white));
    }

    void gaussianBlurLastFilterAndFade()
    {
        openWhite();
        squareLayer(QRect(50, 50, 40, 40));
        QVERIFY(!w->action(QStringLiteral("edit.fade"))->isEnabled());
        const int count = doc()->undoStack()->count();
        QVERIFY(withDialog<ParamDialog>(QStringLiteral("filter.gaussianBlur"), [&](ParamDialog* d) {
            d->setValue(QStringLiteral("radius"), 3.0);
            d->waitForPreview();
            d->accept();
        }));
        QCOMPARE(doc()->undoStack()->text(count), QStringLiteral("Gaussian Blur"));
        const QRgb once = doc()->activeLayer()->pixelAt(QPoint(48, 70));
        QVERIFY(qAlpha(once) > 0 && qAlpha(once) < 255);
        // Last Filter repeats it with the same radius, without a dialog.
        QAction* last = w->action(QStringLiteral("filter.last"));
        QVERIFY(last->isEnabled());
        QCOMPARE(last->text(), QStringLiteral("Gaussian Blur"));
        last->trigger();
        QCOMPARE(doc()->undoStack()->count(), count + 2);
        QVERIFY(doc()->activeLayer()->pixelAt(QPoint(48, 70)) != once);
        // Fading it to 0% gives back the single blur.
        QAction* fade = w->action(QStringLiteral("edit.fade"));
        QVERIFY(fade->isEnabled());
        QCOMPARE(fade->text(), QStringLiteral("Fade Gaussian Blur..."));
        QVERIFY(withDialog<FadeDialog>(QStringLiteral("edit.fade"), [&](FadeDialog* d) {
            d->setOpacity(0);
            d->waitForPreview();
            d->accept();
        }));
        QCOMPARE(doc()->undoStack()->text(count + 2), QStringLiteral("Fade Gaussian Blur"));
        QCOMPARE(doc()->activeLayer()->pixelAt(QPoint(48, 70)), once);
        QVERIFY(!fade->isEnabled());
        // The filter remembers its radius next time.
        QVERIFY(withDialog<ParamDialog>(QStringLiteral("filter.gaussianBlur"), [&](ParamDialog* d) {
            QCOMPARE(d->value(QStringLiteral("radius")), 3.0);
        }));
        QCOMPARE(doc()->undoStack()->count(), count + 3);
    }

    void curvesEditorAddsPointByDragging()
    {
        openWhite();
        Ops::newLayer(doc());
        Ops::selectAll(doc());
        QVERIFY(Ops::fill(doc(), QColor(128, 128, 128), BlendMode::Normal, 1.f, false));
        Ops::deselect(doc());
        CurvesDialog d(doc(), false, w.get());
        QVERIFY(d.isReady());
        d.show();
        CurveEditor* e = d.editor();
        auto toWidget = [e](double x, double y) {
            return QPoint(int(5 + x / 255.0 * (e->width() - 10)), int(5 + (255 - y) / 255.0 * (e->height() - 10)));
        };
        QTest::mousePress(e, Qt::LeftButton, {}, toWidget(128, 128));
        QTest::mouseMove(e, toWidget(128, 200));
        QTest::mouseRelease(e, Qt::LeftButton, {}, toWidget(128, 200));
        QCOMPARE(d.curves().channels[0].size(), 3);
        QVERIFY(std::abs(d.curves().channels[0][1].y() - 200) <= 2);
        d.waitForPreview();
        QVERIFY(std::abs(pixel(10, 10).red() - 200) <= 3);
        // Dragging it off the graph removes it.
        QTest::mousePress(e, Qt::LeftButton, {}, toWidget(128, 200));
        QTest::mouseMove(e, QPoint(e->width() + 40, e->height() / 2));
        QTest::mouseRelease(e, Qt::LeftButton, {}, QPoint(e->width() + 40, e->height() / 2));
        QCOMPARE(d.curves().channels[0].size(), 2);
        d.reject();
        QCOMPARE(pixel(10, 10), QColor(128, 128, 128));
    }

    void adjustmentOnLockedLayerAlerts()
    {
        openWhite();
        Ops::newLayer(doc());
        Ops::setLocks(doc(), doc()->activeIndex(), false, true, false, false);
        const int count = doc()->undoStack()->count();
        // The alert is closed by withDialog; no Levels dialog opens.
        QVERIFY(!withDialog<LevelsDialog>(QStringLiteral("image.levels"), [](LevelsDialog*) {}));
        QVERIFY(!withDialog<ParamDialog>(QStringLiteral("filter.gaussianBlur"), [](ParamDialog*) {}));
        QCOMPARE(doc()->undoStack()->count(), count);
    }

    void autoContrastStretches()
    {
        openWhite();
        Ops::selectAll(doc());
        QVERIFY(Ops::fill(doc(), QColor(100, 100, 100), BlendMode::Normal, 1.f, false));
        QPainterPath path;
        path.addRect(0, 0, 100, 150);
        doc()->changeSelection(Sel::pathMask(doc()->size(), path, false), QStringLiteral("Rectangular Marquee"));
        QVERIFY(Ops::fill(doc(), QColor(150, 150, 150), BlendMode::Normal, 1.f, false));
        Ops::deselect(doc());
        w->action(QStringLiteral("image.autoContrast"))->trigger();
        QCOMPARE(pixel(10, 10), QColor(Qt::white));
        QCOMPARE(pixel(150, 10), QColor(Qt::black));
        QCOMPARE(doc()->undoStack()->text(doc()->undoStack()->index() - 1), QStringLiteral("Auto Contrast"));
    }

    void invertAdjustment()
    {
        openWhite();
        w->action(QStringLiteral("image.invert"))->trigger();
        QCOMPARE(pixel(10, 10), QColor(Qt::black));
    }
    void typeToolTypesAndCommits()
    {
        openWhite(QSize(300, 150));
        colors()->setForeground(Qt::black);
        tools()->select(QStringLiteral("type"));
        const int count = doc()->undoStack()->count();
        click({20, 100});
        auto* type = static_cast<TypeTool*>(tools()->tool(QStringLiteral("type")));
        QVERIFY(type->isEditing());
        QCOMPARE(tools()->modalTool(), static_cast<Tool*>(type));
        // Letters are typed, not taken as tool shortcuts.
        QTest::keyClicks(view()->viewport(), QStringLiteral("BVM"));
        QCOMPARE(tools()->current(), static_cast<Tool*>(type));
        QTest::keyClick(view()->viewport(), Qt::Key_Backspace);
        QCOMPARE(type->textData().text, QStringLiteral("BV"));
        // Enter on the keypad commits.
        QTest::keyClick(view()->viewport(), Qt::Key_Enter, Qt::KeypadModifier);
        QVERIFY(!type->isEditing());
        const Layer& l = doc()->layerAt(doc()->activeIndex());
        QCOMPARE(l.kind, LayerKind::Text);
        QCOMPARE(l.text->text, QStringLiteral("BV"));
        QCOMPARE(l.name, QStringLiteral("BV"));
        QCOMPARE(doc()->undoStack()->count(), count + 1);
        QCOMPARE(doc()->undoStack()->text(count), QStringLiteral("Type Layer"));
        // Clicking the text edits it again; Escape cancels the edit.
        click({30, 90});
        QVERIFY(type->isEditing());
        QTest::keyClicks(view()->viewport(), QStringLiteral("ZZZ"));
        QTest::keyClick(view()->viewport(), Qt::Key_Escape);
        QCOMPARE(doc()->layerAt(doc()->activeIndex()).text->text, QStringLiteral("BV"));
        QCOMPARE(doc()->undoStack()->count(), count + 1);
        // An empty type layer is discarded.
        const int layers = doc()->layerCount();
        click({200, 30});
        QTest::keyClick(view()->viewport(), Qt::Key_Enter, Qt::KeypadModifier);
        QCOMPARE(doc()->layerCount(), layers);
        // Undo removes the type layer.
        doc()->undoStack()->undo();
        QCOMPARE(doc()->layerCount(), layers - 1);
    }

    void switchingToolsCommitsType()
    {
        openWhite();
        tools()->select(QStringLiteral("type"));
        click({20, 100});
        QTest::keyClicks(view()->viewport(), QStringLiteral("Hi"));
        tools()->select(QStringLiteral("move"));
        QCOMPARE(doc()->layerAt(doc()->activeIndex()).text->text, QStringLiteral("Hi"));
        QVERIFY(!tools()->modalTool());
    }

    void paintingTypeLayerAsksToRasterize()
    {
        openWhite();
        TextData t;
        t.text = QStringLiteral("Big");
        t.size = 60;
        t.position = QPointF(10, 90);
        Ops::newTextLayer(doc(), t);
        colors()->setForeground(Qt::red);
        tools()->select(QStringLiteral("brush"));
        // Cancel keeps the text and paints nothing.
        QTimer::singleShot(0, w.get(), [] {
            if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) box->reject();
        });
        drag({10, 70}, {150, 70});
        QCOMPARE(doc()->activeLayer()->kind, LayerKind::Text);
        // OK rasterizes, then paints.
        QTimer::singleShot(0, w.get(), [] {
            if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) box->button(QMessageBox::Ok)->click();
        });
        drag({10, 70}, {150, 70});
        QCOMPARE(doc()->activeLayer()->kind, LayerKind::Pixel);
        QCOMPARE(pixel(120, 70), QColor(Qt::red));
    }

    void shapeToolDrawsLiveShape()
    {
        openWhite();
        colors()->setForeground(Qt::blue);
        tools()->select(QStringLiteral("rectangle"));
        drag({40, 30}, {120, 90});
        const Layer& l = doc()->layerAt(doc()->activeIndex());
        QCOMPARE(l.kind, LayerKind::Shape);
        QCOMPARE(l.name, QStringLiteral("Rectangle 1"));
        QCOMPARE(pixel(80, 60), QColor(Qt::blue));
        QCOMPARE(pixel(130, 60), QColor(Qt::white));
        // Shift draws a circle with the Ellipse tool.
        tools()->select(QStringLiteral("ellipse"));
        drag({140, 20}, {190, 40}, Qt::ShiftModifier);
        const QRectF b = doc()->layerAt(doc()->activeIndex()).shape->path.boundingRect();
        QVERIFY(std::abs(b.width() - b.height()) < 1.0);
        // Free Transform keeps it a shape and re-renders it crisply.
        doc()->setActiveIndex(doc()->activeIndex() - 1);
        QVERIFY(transform()->begin(view(), false));
        QPolygonF q = transform()->quad();
        QTransform grow = QTransform::fromTranslate(-40, -30) * QTransform::fromScale(1.5, 1.0) * QTransform::fromTranslate(40, 30);
        transform()->setQuad(grow.map(q));
        QVERIFY(transform()->commit(view()));
        const Layer& r = doc()->layerAt(doc()->activeIndex());
        QCOMPARE(r.kind, LayerKind::Shape);
        QVERIFY(std::abs(r.shape->path.boundingRect().width() - 120.0) < 1.0);
        QCOMPARE(pixel(150, 60), QColor(Qt::blue));
    }

    static bool near(const QColor& a, const QColor& b, int tol = 3)
    {
        return std::abs(a.red() - b.red()) <= tol && std::abs(a.green() - b.green()) <= tol && std::abs(a.blue() - b.blue()) <= tol;
    }

    void cloneStampCopiesFromSource()
    {
        openWhite();
        squareLayer(QRect(10, 10, 40, 40));
        tools()->select(QStringLiteral("clone-stamp"));
        auto* clone = static_cast<CloneStampTool*>(tools()->tool(QStringLiteral("clone-stamp")));
        QVERIFY(!clone->hasSource());
        // Painting without a source alerts.
        QTimer::singleShot(0, w.get(), [] {
            if (QWidget* m = QApplication::activeModalWidget()) m->close();
        });
        drag({120, 30}, {125, 30});
        QCOMPARE(pixel(122, 30), QColor(Qt::white));
        click({30, 30}, Qt::AltModifier);
        QVERIFY(clone->hasSource());
        drag({130, 30}, {140, 30});
        QVERIFY(near(pixel(135, 30), Qt::black)); // soft tip: within a level or two
        QCOMPARE(doc()->undoStack()->text(doc()->undoStack()->index() - 1), QStringLiteral("Clone Stamp"));
    }

    void historyBrushRestoresOpenState()
    {
        openWhite();
        colors()->setForeground(Qt::black);
        tools()->select(QStringLiteral("brush"));
        drag({20, 75}, {180, 75});
        QCOMPARE(pixel(100, 75), QColor(Qt::black));
        tools()->select(QStringLiteral("history-brush"));
        drag({20, 75}, {180, 75});
        QVERIFY(near(pixel(100, 75), Qt::white));
        QCOMPARE(doc()->undoStack()->count(), 2);
        // With the black stroke's state as the source, the History Brush paints it back.
        w->findChild<HistoryPanel*>()->setHistoryBrushSource(1);
        QVERIFY(near(pixel(100, 75), Qt::white)); // picking a source changes nothing on screen
        QCOMPARE(doc()->undoStack()->index(), 2);
        drag({20, 75}, {180, 75});
        QVERIFY(near(pixel(100, 75), Qt::black));
    }

    void toningToolsChangeTones()
    {
        openWhite();
        doc()->modify(QStringLiteral("Gray"), [&] { doc()->layerRef(0).image.fill(QColor(128, 128, 128)); });
        tools()->select(QStringLiteral("dodge"));
        drag({20, 40}, {180, 40});
        QVERIFY(pixel(100, 40).red() > 140);
        tools()->select(QStringLiteral("burn"));
        drag({20, 110}, {180, 110});
        QVERIFY(pixel(100, 110).red() < 120);
        doc()->modify(QStringLiteral("Red"), [&] { doc()->layerRef(0).image.fill(QColor(220, 30, 30)); });
        tools()->select(QStringLiteral("sponge"));
        drag({20, 75}, {180, 75});
        const QColor c = pixel(100, 75);
        QVERIFY(c.red() - c.green() < 170);
    }

    void blurSmudgeAndHealing()
    {
        openWhite();
        squareLayer(QRect(0, 0, 100, 150));
        // Blur softens the hard edge at x = 100.
        tools()->select(QStringLiteral("blur"));
        for (int i = 0; i < 3; ++i) drag({100, 20}, {100, 130});
        const int edge = pixel(100, 75).red();
        QVERIFY(edge > 10 && edge < 245);
        // Smudge drags black out over white.
        tools()->select(QStringLiteral("smudge"));
        drag({90, 40}, {130, 40}, {}, 40);
        QVERIFY(pixel(104, 40).red() < 230);

        // Spot Healing removes a small dark spot on white.
        openWhite();
        squareLayer(QRect(98, 73, 4, 4));
        doc()->setActiveIndex(1);
        tools()->select(QStringLiteral("spot-healing"));
        drag({97, 75}, {103, 75});
        QVERIFY(pixel(100, 75).red() > 200);
        QCOMPARE(doc()->undoStack()->text(doc()->undoStack()->index() - 1), QStringLiteral("Spot Healing Brush"));
    }

    void layerMaskTargetsAndHides()
    {
        openWhite();
        squareLayer(QRect(20, 20, 100, 100));
        w->action(QStringLiteral("layer.maskRevealAll"))->trigger();
        QVERIFY(doc()->activeLayer()->hasMask());
        QVERIFY(doc()->editingMask());
        QVERIFY(page()->tabTitle().contains(QStringLiteral("Layer Mask")));
        colors()->setForeground(QColor(255, 0, 0)); // paints as its grey on a mask
        tools()->select(QStringLiteral("brush"));
        colors()->reset();
        drag({20, 50}, {120, 50});
        QCOMPARE(pixel(70, 50), QColor(Qt::white)); // hidden by the mask
        QCOMPARE(pixel(70, 90), QColor(Qt::black));
        QCOMPARE(QColor(doc()->activeLayer()->pixelAt(QPoint(70, 50))), QColor(Qt::black)); // pixels untouched
        // The panel's thumbnails switch the target.
        const int idx = doc()->activeIndex();
        clickRowPart(idx, LayerDelegate::Part::Thumbnail);
        QVERIFY(!doc()->editingMask());
        clickRowPart(idx, LayerDelegate::Part::Mask);
        QVERIFY(doc()->editingMask());
        // Shift-click disables the mask.
        clickRowPart(idx, LayerDelegate::Part::Mask, Qt::ShiftModifier);
        QVERIFY(!doc()->activeLayer()->maskEnabled);
        QCOMPARE(pixel(70, 50), QColor(Qt::black));
        QCOMPARE(w->action(QStringLiteral("layer.maskToggle"))->text(), QStringLiteral("Enable"));
    }

    void adjustmentLayerFromMenu()
    {
        openWhite();
        const int layers = doc()->layerCount();
        QVERIFY(withDialog<LevelsDialog>(QStringLiteral("layer.newAdjustment.levels"), [&](LevelsDialog* d) {
            QVERIFY(d->editsLayer());
            Adjust::Levels lv;
            lv.channels[0].outWhite = 128;
            d->setLevels(lv);
            d->waitForPreview();
            QCOMPARE(pixel(10, 10), QColor(128, 128, 128));
            d->accept();
        }));
        QCOMPARE(doc()->layerCount(), layers + 1);
        const Layer& l = doc()->layerAt(doc()->activeIndex());
        QCOMPARE(l.kind, LayerKind::Adjustment);
        QCOMPARE(l.adjustment->levels.channels[0].outWhite, 128);
        QCOMPARE(pixel(10, 10), QColor(128, 128, 128));
        QCOMPARE(QColor(doc()->layerAt(0).pixelAt(QPoint(10, 10))), QColor(Qt::white)); // non-destructive
        // Layer Content Options reopens it with its settings; Cancel changes nothing.
        QVERIFY(withDialog<LevelsDialog>(QStringLiteral("layer.contentOptions"), [&](LevelsDialog* d) {
            QCOMPARE(d->levels().channels[0].outWhite, 128);
            Adjust::Levels lv;
            d->setLevels(lv);
            d->waitForPreview();
            d->reject();
        }));
        QCOMPARE(pixel(10, 10), QColor(128, 128, 128));
        // Cancelling a new adjustment layer removes it.
        QVERIFY(withDialog<CurvesDialog>(QStringLiteral("layer.newAdjustment.curves"), [&](CurvesDialog* d) { d->reject(); }));
        QCOMPARE(doc()->layerCount(), layers + 1);
        // Brightness/Contrast goes through the slider dialog.
        QVERIFY(withDialog<ParamDialog>(QStringLiteral("layer.newAdjustment.brightnessContrast"), [&](ParamDialog* d) {
            QVERIFY(d->editsLayer());
            d->setValue(QStringLiteral("brightness"), -150);
            d->accept();
        }));
        QCOMPARE(doc()->layerAt(doc()->activeIndex()).adjustment->brightness, -150);
        QVERIFY(pixel(10, 10).red() < 128);
        // The Adjustments panel buttons add layers too.
        QVERIFY(w->action(QStringLiteral("layer.newAdjustment.invert"))->isEnabled());
        w->action(QStringLiteral("layer.newAdjustment.invert"))->trigger();
        QCOMPARE(doc()->layerAt(doc()->activeIndex()).adjustment->kind, Adjust::Kind::Invert);
    }

    void layerStyleDialogAddsEffects()
    {
        openWhite();
        squareLayer(QRect(50, 50, 40, 40));
        const int count = doc()->undoStack()->count();
        QVERIFY(withDialog<LayerStyleDialog>(QStringLiteral("layer.styleStroke"), [&](LayerStyleDialog* d) {
            QVERIFY(d->style().stroke.enabled); // opening on an effect's page turns it on
            LayerStyle st = d->style();
            st.stroke.color = Qt::red;
            st.stroke.size = 4;
            d->setStyle(st);
            QCoreApplication::processEvents();
            QCOMPARE(pixel(47, 70), QColor(Qt::red)); // live preview
            d->accept();
        }));
        QVERIFY(doc()->activeLayer()->hasStyle());
        QCOMPARE(doc()->undoStack()->count(), count + 1);
        QCOMPARE(pixel(47, 70), QColor(Qt::red));
        // Cancel restores.
        QVERIFY(withDialog<LayerStyleDialog>(QStringLiteral("layer.styleDropShadow"), [&](LayerStyleDialog* d) {
            d->setOpacity(10);
            d->reject();
        }));
        QCOMPARE(doc()->activeLayer()->opacity, 1.0f);
        QVERIFY(!doc()->activeLayer()->style->dropShadow.enabled);
        // Copy, clear and paste.
        w->action(QStringLiteral("layer.styleCopy"))->trigger();
        w->action(QStringLiteral("layer.styleClear"))->trigger();
        QVERIFY(!doc()->activeLayer()->style);
        QCOMPARE(pixel(47, 70), QColor(Qt::white));
        w->action(QStringLiteral("layer.stylePaste"))->trigger();
        QCOMPARE(pixel(47, 70), QColor(Qt::red));
    }

    void groupAndClippingShortcuts()
    {
        openWhite();
        squareLayer(QRect(10, 10, 30, 30));
        squareLayer(QRect(0, 0, 200, 150));
        QCOMPARE(w->action(QStringLiteral("layer.clipping"))->shortcut(), QKeySequence(QStringLiteral("Ctrl+Alt+G")));
        w->action(QStringLiteral("layer.clipping"))->trigger();
        QVERIFY(doc()->activeLayer()->clipped);
        QCOMPARE(pixel(100, 100), QColor(Qt::white));
        QCOMPARE(w->action(QStringLiteral("layer.clipping"))->text(), QStringLiteral("Release Clipping Mask"));
        QCOMPARE(w->action(QStringLiteral("layer.group"))->shortcut(), QKeySequence(QStringLiteral("Ctrl+G")));
        w->action(QStringLiteral("layer.group"))->trigger();
        QVERIFY(doc()->activeLayer()->isGroup());
        QCOMPARE(w->action(QStringLiteral("layer.mergeDown"))->text(), QStringLiteral("Merge Group"));
        w->action(QStringLiteral("layer.ungroup"))->trigger();
        QVERIFY(!doc()->activeLayer()->isGroup());
        // Dragging a row onto a group in the Layers panel moves it inside.
        w->action(QStringLiteral("layer.newGroupQuick"))->trigger();
        const int group = doc()->activeIndex();
        auto* model = static_cast<LayerModel*>(layersPanel()->list()->model());
        std::unique_ptr<QMimeData> md(model->mimeData({model->index(model->rowFor(1))}));
        model->dropMimeData(md.get(), Qt::MoveAction, -1, 0, model->index(model->rowFor(group)));
        const int moved = Tree::indexOf(doc()->layers(), doc()->layerAt(doc()->activeIndex()).id);
        QVERIFY(doc()->layerAt(moved).parent != 0);
        QVERIFY(doc()->layerAt(Tree::parentIndex(doc()->layers(), moved)).isGroup());
    }

    void newToolShortcuts()
    {
        for (const char* key : {"J", "S", "Y", "O", "T", "U"}) QVERIFY2(w->action(QStringLiteral("tool.%1").arg(QLatin1String(key))), key);
        tools()->select(QStringLiteral("rectangle"));
        w->action(QStringLiteral("tool.U.cycle"))->trigger();
        QCOMPARE(tools()->current()->id(), QStringLiteral("ellipse"));
        // A letter returns to the group's last used tool (the Sponge, from an earlier test).
        w->action(QStringLiteral("tool.O"))->trigger();
        QCOMPARE(tools()->current()->id(), QStringLiteral("sponge"));
    }
};

QTEST_MAIN(TestUi)
#include "test_ui.moc"
#include <algorithm>
#include <iterator>
