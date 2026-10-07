#include "core/BlendMode.h"
#include "core/Commands.h"
#include "core/Document.h"
#include "core/DocumentOps.h"
#include "core/ImageOps.h"
#include "core/Selection.h"
#include "io/DocumentIO.h"

#include <QTemporaryDir>
#include <QUndoStack>
#include <QtTest>

static Document* makeDoc(QSize size = QSize(64, 48))
{
    auto* doc = new Document(size);
    DocState s;
    s.size = size;
    Layer bg = Layer::create("Background");
    bg.isBackground = true;
    bg.image = QImage(size, QImage::Format_ARGB32_Premultiplied);
    bg.image.fill(Qt::white);
    s.layers = {bg};
    doc->initialize(s);
    return doc;
}

class TestCore : public QObject {
    Q_OBJECT
private slots:
    void normalBlendHalfOpacity()
    {
        uint32_t dst = 0xffffffff;
        uint32_t src = 0xffff0000;
        Blend::compositeRow(&dst, &src, 1, BlendMode::Normal, 0.5f);
        QCOMPARE(qAlpha(dst), 255);
        QCOMPARE(qRed(dst), 255);
        QVERIFY(std::abs(qGreen(dst) - 128) <= 1);
        QVERIFY(std::abs(qBlue(dst) - 128) <= 1);
    }

    void multiplyBlend()
    {
        uint32_t dst = qRgb(200, 100, 50);
        uint32_t src = qRgb(128, 128, 128);
        Blend::compositeRow(&dst, &src, 1, BlendMode::Multiply, 1.f);
        QVERIFY(std::abs(qRed(dst) - 100) <= 1);
        QVERIFY(std::abs(qGreen(dst) - 50) <= 1);
        QVERIFY(std::abs(qBlue(dst) - 25) <= 1);
    }

    void screenOverTransparentIsSource()
    {
        uint32_t dst = 0;
        uint32_t src = qRgb(10, 20, 30);
        Blend::compositeRow(&dst, &src, 1, BlendMode::Screen, 1.f);
        QCOMPARE(dst, src);
    }

    void preserveAlphaKeepsDestinationAlpha()
    {
        uint32_t dst = qPremultiply(qRgba(0, 0, 255, 128));
        uint32_t src = qRgb(255, 0, 0);
        Blend::compositeRowPreserveAlpha(&dst, &src, 1, BlendMode::Normal, 1.f);
        QCOMPARE(qAlpha(dst), 128);
        QCOMPARE(qBlue(dst), 0);
    }

    void newLayerUndoRedo()
    {
        std::unique_ptr<Document> doc(makeDoc());
        Ops::newLayer(doc.get());
        QCOMPARE(doc->layerCount(), 2);
        QCOMPARE(doc->layerAt(1).name, QStringLiteral("Layer 1"));
        QCOMPARE(doc->activeIndex(), 1);
        doc->undoStack()->undo();
        QCOMPARE(doc->layerCount(), 1);
        doc->undoStack()->redo();
        QCOMPARE(doc->layerCount(), 2);
    }

    void backgroundCannotMove()
    {
        std::unique_ptr<Document> doc(makeDoc());
        Ops::newLayer(doc.get());
        QVERIFY(!Ops::moveLayer(doc.get(), 0, 1));
        QVERIFY(!Ops::moveLayer(doc.get(), 1, 0));
    }

    void fillSelectionAndUndo()
    {
        std::unique_ptr<Document> doc(makeDoc());
        QPainterPath path;
        path.addRect(10, 10, 20, 10);
        doc->changeSelection(Sel::pathMask(doc->size(), path, false), "Rectangular Marquee");
        QVERIFY(doc->hasSelection());
        QCOMPARE(doc->selectionBounds(), QRect(10, 10, 20, 10));
        QVERIFY(Ops::fill(doc.get(), Qt::red, BlendMode::Normal, 1.f, false));
        QCOMPARE(QColor(doc->composite().pixel(15, 15)), QColor(Qt::red));
        QCOMPARE(QColor(doc->composite().pixel(5, 5)), QColor(Qt::white));
        doc->undoStack()->undo();
        QCOMPARE(QColor(doc->composite().pixel(15, 15)), QColor(Qt::white));
        doc->undoStack()->redo();
        QCOMPARE(QColor(doc->composite().pixel(15, 15)), QColor(Qt::red));
    }

    void pixelEditGrowsEmptyLayerAndUndoShrinks()
    {
        std::unique_ptr<Document> doc(makeDoc());
        Ops::newLayer(doc.get());
        QVERIFY(doc->layerAt(1).image.isNull());
        {
            PixelEdit edit(doc.get(), 1, doc->bounds());
            ImageOps::fillColor(edit.layer(), QRect(0, 0, 4, 4), ImageOps::premultiplied(Qt::blue),
                                BlendMode::Normal, 1.f, QImage(), false);
            edit.markDirty(QRect(0, 0, 4, 4));
            edit.commit("Brush Tool");
        }
        QCOMPARE(doc->layerAt(1).rect(), doc->bounds());
        doc->undoStack()->undo();
        QVERIFY(doc->layerAt(1).image.isNull());
        doc->undoStack()->redo();
        QCOMPARE(QColor(doc->composite().pixel(1, 1)), QColor(Qt::blue));
    }

    void selectionCombine()
    {
        QSize s(10, 10);
        QPainterPath a, b;
        a.addRect(0, 0, 6, 10);
        b.addRect(4, 0, 6, 10);
        QImage ma = Sel::pathMask(s, a, false), mb = Sel::pathMask(s, b, false);
        QCOMPARE(Sel::bounds(Sel::combine(ma, mb, Sel::Op::Add)), QRect(0, 0, 10, 10));
        QCOMPARE(Sel::bounds(Sel::combine(ma, mb, Sel::Op::Intersect)), QRect(4, 0, 2, 10));
        QCOMPARE(Sel::bounds(Sel::combine(ma, mb, Sel::Op::Subtract)), QRect(0, 0, 4, 10));
        QVERIFY(Sel::combine(ma, ma, Sel::Op::Subtract).isNull());
    }

    void mergeDownAndFlatten()
    {
        std::unique_ptr<Document> doc(makeDoc());
        Ops::newLayer(doc.get());
        QVERIFY(Ops::fill(doc.get(), Qt::green, BlendMode::Normal, 1.f, false));
        QVERIFY(Ops::mergeDown(doc.get()));
        QCOMPARE(doc->layerCount(), 1);
        QVERIFY(doc->layerAt(0).isBackground);
        QCOMPARE(QColor(doc->composite().pixel(3, 3)), QColor(Qt::green));
    }

    void cropAndRotate()
    {
        std::unique_ptr<Document> doc(makeDoc(QSize(40, 20)));
        Ops::rotate(doc.get(), Ops::Rotation::Rotate90CW);
        QCOMPARE(doc->size(), QSize(20, 40));
        Ops::crop(doc.get(), QRect(5, 5, 10, 10), true);
        QCOMPARE(doc->size(), QSize(10, 10));
        QCOMPARE(doc->layerAt(0).image.size(), QSize(10, 10));
        doc->undoStack()->undo();
        doc->undoStack()->undo();
        QCOMPARE(doc->size(), QSize(40, 20));
    }

    void nativeRoundTrip()
    {
        QTemporaryDir dir;
        std::unique_ptr<Document> doc(makeDoc());
        Ops::newLayer(doc.get(), "Paint", BlendMode::Multiply, 0.5f);
        QVERIFY(Ops::fill(doc.get(), Qt::red, BlendMode::Normal, 1.f, false));
        QString path = dir.filePath("t.pslop");
        QString err;
        QVERIFY2(DocumentIO::saveNative(doc.get(), path, &err), qPrintable(err));
        std::unique_ptr<Document> loaded(DocumentIO::load(path, &err));
        QVERIFY2(loaded, qPrintable(err));
        QCOMPARE(loaded->layerCount(), 2);
        QCOMPARE(loaded->layerAt(1).name, QStringLiteral("Paint"));
        QCOMPARE(loaded->layerAt(1).mode, BlendMode::Multiply);
        QCOMPARE(loaded->layerAt(1).opacity, 0.5f);
        QVERIFY(loaded->layerAt(0).isBackground);
        QCOMPARE(loaded->composite().pixel(2, 2), doc->composite().pixel(2, 2));
    }

    void pngExportRoundTrip()
    {
        QTemporaryDir dir;
        std::unique_ptr<Document> doc(makeDoc());
        QVERIFY(Ops::fill(doc.get(), QColor(10, 200, 30), BlendMode::Normal, 1.f, false));
        QString path = dir.filePath("t.png"), err;
        QVERIFY2(DocumentIO::exportFlat(doc.get(), path, "png", -1, &err), qPrintable(err));
        QImage img(path);
        QCOMPARE(img.size(), doc->size());
        QCOMPARE(QColor(img.pixel(5, 5)), QColor(10, 200, 30));
    }

    void floodFillContiguous()
    {
        QImage img(10, 10, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::white);
        for (int y = 0; y < 10; ++y) img.setPixel(5, y, qRgb(0, 0, 0));
        QImage m = ImageOps::floodMask(img, QPoint(1, 1), 0, true, false);
        QCOMPARE(Sel::bounds(m), QRect(0, 0, 5, 10));
        QImage all = ImageOps::floodMask(img, QPoint(1, 1), 0, false, false);
        QCOMPARE(all.constScanLine(0)[7], uchar(255));
    }
};

QTEST_GUILESS_MAIN(TestCore)
#include "test_core.moc"
#include <algorithm>
#include <iterator>
