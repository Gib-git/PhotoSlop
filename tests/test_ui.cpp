// Drives the real main window with simulated input to check tools end to end.
#include "core/ColorState.h"
#include "core/Document.h"
#include "core/DocumentOps.h"
#include "tools/FreeTransformTool.h"
#include "tools/SelectionTools.h"
#include "tools/Tool.h"
#include "tools/ToolManager.h"
#include "ui/CanvasView.h"
#include "ui/DocumentPage.h"
#include "ui/MainWindow.h"
#include "ui/Ruler.h"
#include "ui/ViewOptions.h"
#include "ui/dialogs/ColorPickerDialog.h"
#include "ui/dialogs/Dialogs.h"

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

    void invertAdjustment()
    {
        openWhite();
        w->action(QStringLiteral("image.invert"))->trigger();
        QCOMPARE(pixel(10, 10), QColor(Qt::black));
    }
};

QTEST_MAIN(TestUi)
#include "test_ui.moc"
#include <algorithm>
#include <iterator>
