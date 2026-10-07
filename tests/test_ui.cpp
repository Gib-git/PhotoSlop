// Drives the real main window with simulated input to check tools end to end.
#include "core/ColorState.h"
#include "core/Document.h"
#include "core/DocumentOps.h"
#include "tools/Tool.h"
#include "tools/ToolManager.h"
#include "ui/CanvasView.h"
#include "ui/DocumentPage.h"
#include "ui/MainWindow.h"
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
            QTest::mouseMove(vp, at(p.x(), p.y()));
        }
        QTest::mouseRelease(vp, Qt::LeftButton, mods, at(to.x(), to.y()));
        QCoreApplication::processEvents();
    }

    void click(QPointF p, Qt::KeyboardModifiers mods = {})
    {
        QTest::mouseClick(view()->viewport(), Qt::LeftButton, mods, at(p.x(), p.y()));
        QCoreApplication::processEvents();
    }

    QColor pixel(int x, int y) { return QColor::fromRgba(qUnpremultiply(doc()->compositePixel(QPoint(x, y)))); }

    void openWhite(QSize size = QSize(200, 150))
    {
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
        if (!shots.isEmpty()) w->grab().save(shots + QStringLiteral("/window.png"));
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
