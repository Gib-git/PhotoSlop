#include "core/BlendMode.h"
#include "core/Commands.h"
#include "core/Document.h"
#include "core/DocumentOps.h"
#include "core/ImageOps.h"
#include "core/Selection.h"
#include "core/Transform.h"
#include "io/DocumentIO.h"

#include <QTemporaryDir>
#include <QUndoStack>
#include <QtTest>

static QImage rectMask(QSize size, QRect r)
{
    QPainterPath path;
    path.addRect(r);
    return Sel::pathMask(size, path, false);
}

// A test pattern with distinct pixels, so resampling errors show up.
static QImage pattern(int w, int h)
{
    QImage img(w, h, QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) img.setPixel(x, y, qRgb(x * 37 % 256, y * 53 % 256, (x * y) % 256));
    return img;
}

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

    // ---------------- Stage 2 ----------------

    void selectionExpandContract()
    {
        const QSize s(40, 40);
        const QImage m = rectMask(s, QRect(10, 10, 10, 10));
        QCOMPARE(Sel::bounds(Sel::expanded(m, 3)), QRect(7, 7, 16, 16));
        // Straight edges grow by exactly the radius; corners round off.
        QCOMPARE(Sel::expanded(m, 3).constScanLine(15)[7], uchar(255));
        QVERIFY(Sel::expanded(m, 3).constScanLine(7)[7] < 128);
        QCOMPARE(Sel::bounds(Sel::contracted(m, 2, false)), QRect(12, 12, 6, 6));
        QVERIFY(Sel::contracted(m, 5, false).isNull());
    }

    void contractAtCanvasBounds()
    {
        const QImage all = Sel::full(QSize(20, 20));
        // Without the option, the canvas edge does not eat into the selection.
        QCOMPARE(Sel::bounds(Sel::contracted(all, 3, false)), QRect(0, 0, 20, 20));
        QCOMPARE(Sel::bounds(Sel::contracted(all, 3, true)), QRect(3, 3, 14, 14));
    }

    void selectionBorderAndSmooth()
    {
        const QSize s(40, 40);
        const QImage m = rectMask(s, QRect(10, 10, 20, 20));
        const QImage b = Sel::border(m, 4);
        QCOMPARE(b.constScanLine(20)[20], uchar(0)); // centre is no longer selected
        QCOMPARE(b.constScanLine(20)[10], uchar(255)); // the edge is
        QCOMPARE(b.constScanLine(20)[8], uchar(255));
        QCOMPARE(b.constScanLine(20)[5], uchar(0));
        // A one-pixel spike disappears when smoothed.
        QImage spiky = m.copy();
        spiky.scanLine(5)[20] = 255;
        const QImage sm = Sel::smoothed(spiky, 2, false);
        QCOMPARE(sm.constScanLine(5)[20], uchar(0));
        QCOMPARE(sm.constScanLine(20)[20], uchar(255));
    }

    void growAndSimilar()
    {
        QImage img(30, 10, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::white);
        for (int y = 0; y < 10; ++y) {
            img.setPixel(10, y, qRgb(0, 0, 0)); // a wall
            img.setPixel(5, y, qRgb(250, 250, 250));
        }
        const QImage seed = rectMask(img.size(), QRect(0, 0, 2, 2));
        const QImage grown = Sel::grown(seed, img, 32, true);
        QCOMPARE(Sel::bounds(grown), QRect(0, 0, 10, 10));
        const QImage similar = Sel::grown(seed, img, 32, false);
        QCOMPARE(similar.constScanLine(5)[20], uchar(255));
        QCOMPARE(similar.constScanLine(5)[10], uchar(0));
    }

    void modifySelectionUndo()
    {
        std::unique_ptr<Document> doc(makeDoc());
        doc->changeSelection(rectMask(doc->size(), QRect(10, 10, 10, 10)), "Rectangular Marquee");
        QVERIFY(Ops::modifySelection(doc.get(), Ops::Modify::Expand, 2));
        QCOMPARE(doc->selectionBounds(), QRect(8, 8, 14, 14));
        QCOMPARE(doc->undoStack()->text(doc->undoStack()->index() - 1), QStringLiteral("Expand"));
        doc->undoStack()->undo();
        QCOMPARE(doc->selectionBounds(), QRect(10, 10, 10, 10));
    }

    void wandSampleSizeAveragesSeed()
    {
        QImage img(10, 10, QImage::Format_ARGB32_Premultiplied);
        img.fill(qRgb(100, 100, 100));
        img.setPixel(5, 5, qRgb(255, 255, 255)); // a speck under the cursor
        // Point sample picks the speck; a 3x3 average sees the grey around it.
        QCOMPARE(Sel::bounds(ImageOps::floodMask(img, QPoint(5, 5), 10, true, false, 1)), QRect(5, 5, 1, 1));
        QCOMPARE(Sel::bounds(ImageOps::floodMask(img, QPoint(5, 5), 30, true, false, 3)).width(), 10);
    }

    void transformIdentityAndTranslationAreLossless()
    {
        const QImage src = pattern(16, 12);
        for (auto interp : {Xform::Interp::NearestNeighbor, Xform::Interp::Bilinear, Xform::Interp::Bicubic}) {
            QRect r;
            const QImage out = Xform::transformed(src, QTransform::fromTranslate(5, 3), interp, QRect(0, 0, 100, 100), &r);
            QCOMPARE(r, QRect(5, 3, 16, 12));
            QCOMPARE(out, src);
        }
    }

    void transformRotate90IsExact()
    {
        const QImage src = pattern(8, 5);
        QTransform t;
        QVERIFY(Xform::quadTransform(QRectF(0, 0, 8, 5), QPolygonF{{5, 0}, {5, 8}, {0, 8}, {0, 0}}, &t));
        QRect r;
        const QImage out = Xform::transformed(src, t, Xform::Interp::Bicubic, QRect(0, 0, 100, 100), &r);
        QCOMPARE(r, QRect(0, 0, 5, 8));
        QCOMPARE(out.pixel(4, 0), src.pixel(0, 0));
        QCOMPARE(out.pixel(0, 7), src.pixel(7, 4));
        QCOMPARE(out, src.transformed(QTransform().rotate(90)).convertToFormat(QImage::Format_ARGB32_Premultiplied));
    }

    void transformScaleAndPerspective()
    {
        QImage src(10, 10, QImage::Format_ARGB32_Premultiplied);
        src.fill(Qt::red);
        QRect r;
        const QImage up = Xform::transformed(src, QTransform::fromScale(2, 2), Xform::Interp::NearestNeighbor, QRect(0, 0, 100, 100), &r);
        QCOMPARE(r, QRect(0, 0, 20, 20));
        QCOMPARE(QColor(up.pixel(10, 10)), QColor(Qt::red));
        // A strong shrink takes the pre-filtered path and keeps colour and coverage.
        const QImage down = Xform::transformed(pattern(64, 64), QTransform::fromScale(0.25, 0.25), Xform::Interp::Bilinear, QRect(0, 0, 100, 100), &r);
        QCOMPARE(r, QRect(0, 0, 16, 16));
        QCOMPARE(qAlpha(down.pixel(8, 8)), 255);
        QTransform p;
        QVERIFY(Xform::quadTransform(QRectF(0, 0, 10, 10), QPolygonF{{2, 0}, {8, 0}, {10, 10}, {0, 10}}, &p));
        const QImage persp = Xform::transformed(src, p, Xform::Interp::Bilinear, QRect(0, 0, 100, 100), &r);
        QCOMPARE(qAlpha(persp.pixel(5 - r.left(), 5 - r.top())), 255);
        QCOMPARE(qAlpha(persp.pixel(0 - r.left(), 0 - r.top())), 0);
        QVERIFY(Xform::isConvex(QPolygonF{{2, 0}, {8, 0}, {10, 10}, {0, 10}}));
        QVERIFY(!Xform::isConvex(QPolygonF{{0, 0}, {10, 10}, {10, 0}, {0, 10}}));
    }

    void warpIdentityMatchesSource()
    {
        const QImage src = pattern(24, 16);
        const auto patch = Xform::Patch::fromTransform(QRectF(0, 0, 24, 16), QTransform());
        QCOMPARE(patch.eval(0.5, 0.5), QPointF(12, 8));
        QRect r;
        const QImage out = Xform::warped(src, patch, Xform::Interp::Bilinear, QRect(0, 0, 100, 100), &r);
        QVERIFY(r.contains(QRect(0, 0, 24, 16)));
        int maxDiff = 0;
        for (int y = 0; y < 16; ++y)
            for (int x = 0; x < 24; ++x) {
                const QRgb a = src.pixel(x, y), b = out.pixel(x - r.left(), y - r.top());
                maxDiff = std::max({maxDiff, std::abs(qRed(a) - qRed(b)), std::abs(qGreen(a) - qGreen(b)), std::abs(qAlpha(a) - qAlpha(b))});
            }
        QVERIFY2(maxDiff <= 1, qPrintable(QString::number(maxDiff)));
    }

    void quickMaskRoundTrip()
    {
        std::unique_ptr<Document> doc(makeDoc());
        doc->changeSelection(rectMask(doc->size(), QRect(10, 10, 20, 20)), "Rectangular Marquee");
        Ops::setQuickMask(doc.get(), true);
        QVERIFY(doc->inQuickMask());
        QVERIFY(!doc->hasSelection());
        QCOMPARE(doc->editIndex(), Document::kQuickMaskIndex);
        // Masked areas are tinted red; selected ones are not.
        QVERIFY(qAlpha(doc->quickMaskOverlay().pixel(2, 2)) > 100);
        QCOMPARE(qAlpha(doc->quickMaskOverlay().pixel(15, 15)), 0);
        // Painting white on the mask adds to the selection; the layers are untouched.
        QVERIFY(Ops::fill(doc.get(), Qt::white, BlendMode::Normal, 1.f, false));
        QCOMPARE(QColor(doc->composite().pixel(2, 2)), QColor(Qt::white));
        doc->undoStack()->undo();
        doc->changeSelection(rectMask(doc->size(), QRect(40, 0, 10, 10)), "Rectangular Marquee");
        QVERIFY(Ops::fill(doc.get(), Qt::white, BlendMode::Normal, 1.f, false));
        doc->changeSelection(QImage(), "Deselect");
        Ops::setQuickMask(doc.get(), false);
        QVERIFY(!doc->inQuickMask());
        QCOMPARE(doc->selection().constScanLine(15)[15], uchar(255));
        QCOMPARE(doc->selection().constScanLine(5)[45], uchar(255));
        QCOMPARE(doc->selection().constScanLine(2)[2], uchar(0));
        // Undo walks back into Quick Mask mode with the painted mask.
        doc->undoStack()->undo();
        QVERIFY(doc->inQuickMask());
        doc->undoStack()->undo();
        doc->undoStack()->undo();
        QCOMPARE(qGray(doc->quickMaskLayer().pixelAt(QPoint(45, 5))), 0);
    }

    void guidesUndoAndSave()
    {
        QTemporaryDir dir;
        std::unique_ptr<Document> doc(makeDoc());
        doc->changeGuides({Guide{Qt::Vertical, 12}, Guide{Qt::Horizontal, 30}}, "New Guide");
        QCOMPARE(doc->guides().size(), 2);
        doc->undoStack()->undo();
        QVERIFY(doc->guides().isEmpty());
        doc->undoStack()->redo();
        const QString path = dir.filePath("g.pslop");
        QString err;
        QVERIFY2(DocumentIO::saveNative(doc.get(), path, &err), qPrintable(err));
        std::unique_ptr<Document> loaded(DocumentIO::load(path, &err));
        QVERIFY2(loaded, qPrintable(err));
        QCOMPARE(loaded->guides(), doc->guides());
    }
};

QTEST_GUILESS_MAIN(TestCore)
#include "test_core.moc"
#include <algorithm>
#include <iterator>
