#include "core/Adjustments.h"
#include "core/Compositor.h"
#include "core/LayerStyle.h"
#include "core/LayerTree.h"
#include "core/VectorLayers.h"
#include "core/BlendMode.h"
#include "core/Commands.h"
#include "core/Document.h"
#include "core/DocumentOps.h"
#include "core/Filters.h"
#include "core/Healing.h"
#include "core/ImageOps.h"
#include "core/Selection.h"
#include "core/Transform.h"
#include "io/DocumentIO.h"

#include <QPainter>
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
    // ---------------- Stage 3: adjustments and filters ----------------

    static QRgb mapped(const Adjust::PixelMap& map, QRgb c)
    {
        map(&c, 1);
        return c;
    }

    static QImage solid(QSize size, QRgb c)
    {
        QImage img(size, QImage::Format_ARGB32_Premultiplied);
        img.fill(qPremultiply(c));
        return img;
    }

    void levelsLut()
    {
        Adjust::LevelsChannel c;
        QCOMPARE(Adjust::levelsLut(c), Adjust::identityLut());
        c.inBlack = 50;
        c.inWhite = 200;
        Adjust::Lut l = Adjust::levelsLut(c);
        QCOMPARE(int(l[50]), 0);
        QCOMPARE(int(l[30]), 0);
        QCOMPARE(int(l[200]), 255);
        QCOMPARE(int(l[125]), 128);
        c = {};
        c.gamma = 2.0; // brightens the midtones
        QCOMPARE(int(Adjust::levelsLut(c)[64]), 128);
        c = {};
        c.outBlack = 20;
        c.outWhite = 220;
        l = Adjust::levelsLut(c);
        QCOMPARE(int(l[0]), 20);
        QCOMPARE(int(l[255]), 220);
        // The RGB channel applies after the colour channels.
        Adjust::Levels lv;
        lv.channels[1].inWhite = 128; // red doubles
        lv.channels[0].outWhite = 128; // then everything halves
        QCOMPARE(mapped(Adjust::levelsMap(lv), qRgb(100, 100, 100)), qRgb(100, 50, 50));
    }

    void autoLevelsStretches()
    {
        QImage img(100, 1, QImage::Format_ARGB32_Premultiplied);
        for (int x = 0; x < 100; ++x) img.setPixel(x, 0, qRgb(50 + x, 60 + x, 70 + x));
        const Adjust::Histogram h = Adjust::histogram(img);
        QCOMPARE(h.count, quint64(100));
        Adjust::Levels tone = Adjust::autoLevels(h, Adjust::AutoMode::Tone, 0.0);
        QCOMPARE(tone.channels[1].inBlack, 50);
        QCOMPARE(tone.channels[1].inWhite, 149);
        QCOMPARE(tone.channels[3].inBlack, 70);
        Adjust::Levels contrast = Adjust::autoLevels(h, Adjust::AutoMode::Contrast, 0.0);
        QCOMPARE(contrast.channels[0].inBlack, 50);
        QCOMPARE(contrast.channels[0].inWhite, 169);
        QCOMPARE(contrast.channels[1], Adjust::LevelsChannel());
        // Clipping ignores the extreme pixels.
        Adjust::Levels clipped = Adjust::autoLevels(h, Adjust::AutoMode::Tone, 0.05);
        QCOMPARE(clipped.channels[1].inBlack, 55);
        QCOMPARE(clipped.channels[1].inWhite, 144);
    }

    void histogramHonoursCoverage()
    {
        QImage img = solid(QSize(10, 10), qRgb(200, 100, 0));
        QImage mask(10, 10, QImage::Format_Grayscale8);
        mask.fill(0);
        for (int y = 0; y < 5; ++y) memset(mask.scanLine(y), 255, 10);
        const Adjust::Histogram h = Adjust::histogram(img, mask);
        QCOMPARE(h.count, quint64(50));
        QCOMPARE(h.channels[1][200], 50u);
        QCOMPARE(h.channels[3][0], 50u);
    }

    void curvesLut()
    {
        QCOMPARE(Adjust::curveLut(Adjust::identityCurve()), Adjust::identityLut());
        const Adjust::Lut l = Adjust::curveLut({QPointF(0, 0), QPointF(128, 200), QPointF(255, 255)});
        QCOMPARE(int(l[128]), 200);
        QCOMPARE(int(l[0]), 0);
        QCOMPARE(int(l[255]), 255);
        for (int v = 1; v < 256; ++v) QVERIFY(l[v] >= l[v - 1]);
        // Flat beyond the end points.
        const Adjust::Lut cut = Adjust::curveLut({QPointF(50, 0), QPointF(200, 255)});
        QCOMPARE(int(cut[20]), 0);
        QCOMPARE(int(cut[230]), 255);
        QCOMPARE(int(cut[125]), 128);
        // An inverted curve inverts.
        Adjust::Curves c;
        c.channels[0] = {QPointF(0, 255), QPointF(255, 0)};
        QCOMPARE(mapped(Adjust::curvesMap(c), qRgb(10, 20, 30)), qRgb(245, 235, 225));
    }

    void simpleAdjustments()
    {
        QCOMPARE(mapped(Adjust::invertMap(), qRgba(10, 20, 30, 40)), qRgba(245, 235, 225, 40));
        QCOMPARE(mapped(Adjust::desaturateMap(), qRgb(200, 100, 0)), qRgb(100, 100, 100));
        QCOMPARE(mapped(Adjust::posterizeMap(2), qRgb(100, 130, 255)), qRgb(0, 255, 255));
        QCOMPARE(mapped(Adjust::thresholdMap(128), qRgb(200, 200, 200)), qRgb(255, 255, 255));
        QCOMPARE(mapped(Adjust::thresholdMap(128), qRgb(255, 0, 0)), qRgb(0, 0, 0));
        // Brightness/Contrast at zero changes nothing; modern brightness keeps black and white.
        QCOMPARE(mapped(Adjust::brightnessContrastMap(0, 0, false), qRgb(12, 130, 250)), qRgb(12, 130, 250));
        const auto bright = Adjust::brightnessContrastMap(100, 0, false);
        QCOMPARE(mapped(bright, qRgb(0, 255, 128)), qRgb(0, 255, qBlue(mapped(bright, qRgb(0, 0, 128)))));
        QVERIFY(qBlue(mapped(bright, qRgb(0, 0, 128))) > 170);
        QCOMPARE(mapped(Adjust::brightnessContrastMap(20, 0, true), qRgb(0, 100, 250)), qRgb(20, 120, 255));
        const QRgb contrasty = mapped(Adjust::brightnessContrastMap(0, 80, false), qRgb(40, 128, 215));
        QVERIFY(qRed(contrasty) < 40 && qBlue(contrasty) > 215);
    }

    void hueSaturation()
    {
        Adjust::HueSaturation hs;
        QCOMPARE(mapped(Adjust::hueSaturationMap(hs), qRgb(12, 34, 56)), qRgb(12, 34, 56));
        hs.ranges[0].hue = 120;
        QCOMPARE(mapped(Adjust::hueSaturationMap(hs), qRgb(255, 0, 0)), qRgb(0, 255, 0));
        hs = {};
        hs.ranges[0].saturation = -100;
        const QRgb grey = mapped(Adjust::hueSaturationMap(hs), qRgb(200, 50, 50));
        QCOMPARE(qRed(grey), qGreen(grey));
        hs = {};
        hs.ranges[0].lightness = 100;
        QCOMPARE(mapped(Adjust::hueSaturationMap(hs), qRgb(10, 200, 30)), qRgb(255, 255, 255));
        // Editing the Reds leaves blues and greys alone.
        hs = {};
        hs.ranges[1].hue = 120;
        QCOMPARE(mapped(Adjust::hueSaturationMap(hs), qRgb(255, 0, 0)), qRgb(0, 255, 0));
        QCOMPARE(mapped(Adjust::hueSaturationMap(hs), qRgb(0, 0, 255)), qRgb(0, 0, 255));
        QCOMPARE(mapped(Adjust::hueSaturationMap(hs), qRgb(90, 90, 90)), qRgb(90, 90, 90));
        // Colorize tints by lightness.
        hs = {};
        hs.colorize = true;
        hs.ranges[0].hue = 240;
        hs.ranges[0].saturation = 100;
        const QRgb tinted = mapped(Adjust::hueSaturationMap(hs), qRgb(128, 128, 128));
        QVERIFY(qBlue(tinted) > 250 && qRed(tinted) < 5);
    }

    void colorBalance()
    {
        Adjust::ColorBalance cb;
        QCOMPARE(mapped(Adjust::colorBalanceMap(cb), qRgb(30, 128, 220)), qRgb(30, 128, 220));
        cb.values[1][0] = 100; // midtones toward red
        cb.preserveLuminosity = false;
        const QRgb warm = mapped(Adjust::colorBalanceMap(cb), qRgb(128, 128, 128));
        QVERIFY(qRed(warm) > 180);
        QCOMPARE(qGreen(warm), 128);
        cb.preserveLuminosity = true;
        double h, s, l0, l1;
        Adjust::rgbToHsl(128, 128, 128, h, s, l0);
        const QRgb kept = mapped(Adjust::colorBalanceMap(cb), qRgb(128, 128, 128));
        Adjust::rgbToHsl(qRed(kept), qGreen(kept), qBlue(kept), h, s, l1);
        QVERIFY(std::abs(l0 - l1) < 0.01);
        QVERIFY(qRed(kept) > qGreen(kept));
    }

    void blackAndWhite()
    {
        Adjust::BlackWhite bw;
        const auto map = Adjust::blackWhiteMap(bw);
        QCOMPARE(mapped(map, qRgb(255, 255, 255)), qRgb(255, 255, 255));
        QCOMPARE(mapped(map, qRgb(255, 0, 0)), qRgb(102, 102, 102));   // reds 40%
        QCOMPARE(mapped(map, qRgb(255, 255, 0)), qRgb(153, 153, 153)); // yellows 60%
        QCOMPARE(mapped(map, qRgb(0, 0, 255)), qRgb(51, 51, 51));      // blues 20%
        bw.tint = true;
        const QRgb t = mapped(Adjust::blackWhiteMap(bw), qRgb(128, 128, 128));
        QVERIFY(qRed(t) > qBlue(t)); // the default tint is warm
    }

    void gaussianBlurPreservesFlatAndSpreads()
    {
        // A flat image stays flat (edges repeat) for both the kernel and box-pass paths.
        const QImage flat = solid(QSize(40, 30), qRgb(90, 120, 200));
        for (double r : {1.0, 8.0}) QCOMPARE(Filters::gaussianBlur(flat, r), flat);
        QImage dot = solid(QSize(41, 41), qRgb(0, 0, 0));
        dot.setPixel(20, 20, qRgb(255, 255, 255));
        for (double r : {1.0, 5.0}) {
            const QImage b = Filters::gaussianBlur(dot, r);
            QVERIFY(qRed(b.pixel(20, 20)) < 255);
            QVERIFY(qRed(b.pixel(21, 20)) > 0);
            QCOMPARE(qRed(b.pixel(19, 20)), qRed(b.pixel(21, 20)));
            QCOMPARE(qRed(b.pixel(20, 17)), qRed(b.pixel(23, 20)));
            QCOMPARE(qRed(b.pixel(0, 0)), 0);
        }
        // Transparent pixels pick up coverage, and premultiplied colour stays within alpha.
        QImage half(20, 20, QImage::Format_ARGB32_Premultiplied);
        half.fill(Qt::transparent);
        for (int y = 0; y < 20; ++y)
            for (int x = 0; x < 10; ++x) half.setPixel(x, y, qRgb(255, 0, 0));
        const QImage hb = Filters::gaussianBlur(half, 2.0);
        const QRgb edge = hb.pixel(10, 10);
        QVERIFY(qAlpha(edge) > 0 && qAlpha(edge) < 255);
        QCOMPARE(qRed(edge), qAlpha(edge));
    }

    void blursAndSharpen()
    {
        QImage bar = solid(QSize(31, 31), qRgb(0, 0, 0));
        for (int y = 0; y < 31; ++y) bar.setPixel(15, y, qRgb(255, 255, 255));
        // A vertical motion blur leaves a vertical bar alone; a horizontal one spreads it.
        QCOMPARE(Filters::motionBlur(bar, 90, 10), bar);
        const QImage h = Filters::motionBlur(bar, 0, 10);
        QVERIFY(qRed(h.pixel(18, 15)) > 0);
        QCOMPARE(qRed(h.pixel(25, 15)), 0);
        QCOMPARE(Filters::boxBlur(solid(QSize(9, 9), qRgb(7, 8, 9)), 3), solid(QSize(9, 9), qRgb(7, 8, 9)));
        // Unsharp mask raises contrast at an edge; a high threshold leaves it alone.
        QImage edge = solid(QSize(20, 20), qRgb(100, 100, 100));
        for (int y = 0; y < 20; ++y)
            for (int x = 10; x < 20; ++x) edge.setPixel(x, y, qRgb(150, 150, 150));
        const QImage sharp = Filters::unsharpMask(edge, 100, 2, 0);
        QVERIFY(qRed(sharp.pixel(9, 5)) < 100);
        QVERIFY(qRed(sharp.pixel(10, 5)) > 150);
        QCOMPARE(qRed(sharp.pixel(0, 5)), 100);
        QCOMPARE(Filters::unsharpMask(edge, 100, 2, 60), edge);
        const QImage hp = Filters::highPass(edge, 3);
        QCOMPARE(qRed(hp.pixel(0, 5)), 128);
        QVERIFY(qRed(hp.pixel(10, 5)) > 128);
    }

    void noiseMedianMosaic()
    {
        const QImage grey = solid(QSize(32, 32), qRgb(128, 128, 128));
        const QImage n1 = Filters::addNoise(grey, 25, false, true, QPoint(), 7);
        QCOMPARE(Filters::addNoise(grey, 25, false, true, QPoint(), 7), n1); // deterministic
        QVERIFY(Filters::addNoise(grey, 25, false, true, QPoint(), 8) != n1);
        bool changed = false;
        for (int x = 0; x < 32; ++x) {
            const QRgb p = n1.pixel(x, 3);
            QCOMPARE(qRed(p), qBlue(p)); // monochromatic
            changed |= qRed(p) != 128;
        }
        QVERIFY(changed);
        // The same canvas position gets the same noise, whatever the crop.
        QCOMPARE(Filters::addNoise(grey.copy(4, 4, 8, 8), 25, true, false, QPoint(4, 4), 7).pixel(0, 0),
                 Filters::addNoise(grey, 25, true, false, QPoint(), 7).pixel(4, 4));
        QImage clear(4, 4, QImage::Format_ARGB32_Premultiplied);
        clear.fill(Qt::transparent);
        QCOMPARE(Filters::addNoise(clear, 100, true, false, QPoint(), 1), clear);

        QImage speck = grey;
        speck.setPixel(10, 10, qRgb(255, 255, 255));
        QCOMPARE(Filters::median(speck, 1), grey);

        QImage ramp(8, 4, QImage::Format_ARGB32_Premultiplied);
        for (int y = 0; y < 4; ++y)
            for (int x = 0; x < 8; ++x) ramp.setPixel(x, y, qRgb(x * 10, 0, 0));
        const QImage m = Filters::mosaic(ramp, 4, QPoint());
        QCOMPARE(qRed(m.pixel(0, 0)), 15);
        QCOMPARE(qRed(m.pixel(3, 3)), 15);
        QCOMPARE(qRed(m.pixel(4, 0)), 55);
        // Cells line up with the canvas, not the crop.
        const QImage shifted = Filters::mosaic(ramp.copy(2, 0, 6, 4), 4, QPoint(2, 0));
        QCOMPARE(qRed(shifted.pixel(0, 0)), 25); // canvas x 2..3
        QCOMPARE(qRed(shifted.pixel(2, 0)), 55); // canvas x 4..7
    }

    void cancelStopsFilters()
    {
        Filters::CancelFlag cancel{true};
        QVERIFY(Filters::gaussianBlur(solid(QSize(64, 64), qRgb(1, 2, 3)), 5, &cancel).isNull());
        QVERIFY(Filters::median(solid(QSize(64, 64), qRgb(1, 2, 3)), 2, &cancel).isNull());
    }

    void sessionAppliesInsideSelection()
    {
        std::unique_ptr<Document> doc(makeDoc());
        doc->changeSelection(rectMask(doc->size(), QRect(10, 10, 20, 10)), "Rectangular Marquee");
        const int before = doc->undoStack()->count();
        QString err;
        QVERIFY(Filters::apply(doc.get(), Adjust::spec("Invert", Adjust::invertMap()), &err));
        QCOMPARE(doc->undoStack()->count(), before + 1);
        QCOMPARE(doc->undoStack()->text(before), QStringLiteral("Invert"));
        QCOMPARE(doc->layers()[0].pixelAt(QPoint(15, 15)), qRgb(0, 0, 0));
        QCOMPARE(doc->layers()[0].pixelAt(QPoint(5, 5)), qRgb(255, 255, 255));
        doc->undoStack()->undo();
        QCOMPARE(doc->layers()[0].pixelAt(QPoint(15, 15)), qRgb(255, 255, 255));
    }

    void sessionPreviewAndCancel()
    {
        std::unique_ptr<Document> doc(makeDoc());
        {
            Filters::Session s(doc.get(), "Levels", false);
            QVERIFY(s.isValid());
            QCOMPARE(s.target(), doc->bounds());
            const Filters::Spec spec = Adjust::spec("Invert", Adjust::invertMap());
            s.show(Filters::Session::render(s.input(), spec));
            QCOMPARE(doc->layers()[0].pixelAt(QPoint(3, 3)), qRgb(0, 0, 0)); // previewed
            s.showOriginal();
            QCOMPARE(doc->layers()[0].pixelAt(QPoint(3, 3)), qRgb(255, 255, 255));
            s.show(Filters::Session::render(s.input(), spec));
        } // destroyed without commit
        QCOMPARE(doc->layers()[0].pixelAt(QPoint(3, 3)), qRgb(255, 255, 255));
        QCOMPARE(doc->undoStack()->count(), 0);
    }

    void sessionSpreadsAndLocks()
    {
        std::unique_ptr<Document> doc(makeDoc());
        Ops::newLayer(doc.get());
        doc->changeSelection(rectMask(doc->size(), QRect(20, 20, 10, 10)), "Rectangular Marquee");
        QVERIFY(Ops::fill(doc.get(), Qt::black, BlendMode::Normal, 1.f, false));
        Ops::deselect(doc.get());
        // Adjustments refuse an empty area; blurs spread past the layer's pixels.
        Ops::newLayer(doc.get());
        QString err;
        QVERIFY(!Filters::apply(doc.get(), Adjust::spec("Invert", Adjust::invertMap()), &err));
        QVERIFY(err.contains("empty"));
        Ops::deleteLayer(doc.get());
        const QRect before = doc->activeLayer()->rect();
        QVERIFY(Filters::apply(doc.get(), Filters::gaussianBlurSpec(2.0), &err));
        QVERIFY(doc->activeLayer()->rect().contains(before.adjusted(-3, -3, 3, 3)));
        QVERIFY(qAlpha(doc->activeLayer()->pixelAt(QPoint(18, 25))) > 0);
        doc->undoStack()->undo();
        QCOMPARE(qAlpha(doc->activeLayer()->pixelAt(QPoint(18, 25))), 0);
        // Locked transparency keeps the alpha.
        Ops::setLocks(doc.get(), doc->activeIndex(), true, false, false, false);
        QVERIFY(Filters::apply(doc.get(), Filters::gaussianBlurSpec(2.0), &err));
        QCOMPARE(qAlpha(doc->activeLayer()->pixelAt(QPoint(18, 25))), 0);
        QCOMPARE(qAlpha(doc->activeLayer()->pixelAt(QPoint(20, 25))), 255);
        // Locked pixels refuse.
        Ops::setLocks(doc.get(), doc->activeIndex(), false, true, false, false);
        QVERIFY(!Filters::apply(doc.get(), Filters::gaussianBlurSpec(2.0), &err));
        QVERIFY(err.contains("locked"));
    }

    void sessionFeatheredSelectionBlends()
    {
        std::unique_ptr<Document> doc(makeDoc(QSize(8, 8)));
        QImage mask(8, 8, QImage::Format_Grayscale8);
        mask.fill(128);
        doc->changeSelection(mask, "Feather");
        QVERIFY(Filters::apply(doc.get(), Adjust::spec("Invert", Adjust::invertMap())));
        const int v = qRed(doc->layers()[0].pixelAt(QPoint(4, 4)));
        QVERIFY(v > 120 && v < 135);
    }

    // ---------------- Stage 4: layer power features ----------------

    // A transparent layer filled with `color` inside `r`, added above the active layer.
    static void colorLayer(Document* doc, const QColor& color, QRect r, const QString& name = QString())
    {
        Ops::newLayer(doc, name);
        QPainterPath path;
        path.addRect(r);
        doc->changeSelection(Sel::pathMask(doc->size(), path, false), "Marquee");
        QVERIFY(Ops::fill(doc, color, BlendMode::Normal, 1.f, false));
        Ops::deselect(doc);
    }

    static QColor at(Document* doc, int x, int y) { return QColor::fromRgba(qUnpremultiply(doc->composite().pixel(x, y))); }

    void groupsNestAndUngroup()
    {
        std::unique_ptr<Document> doc(makeDoc());
        colorLayer(doc.get(), Qt::red, QRect(0, 0, 10, 10), "A");
        QVERIFY(Ops::groupLayer(doc.get()));
        QCOMPARE(doc->layerCount(), 3);
        const Layer& g = doc->layerAt(2);
        QVERIFY(g.isGroup());
        QCOMPARE(g.mode, BlendMode::PassThrough);
        QCOMPARE(doc->layerAt(1).parent, g.id);
        QCOMPARE(Tree::children(doc->layers(), 2), QList<int>{1});
        // A new layer with the group selected goes inside it, at the top.
        Ops::newLayer(doc.get(), "B");
        QCOMPARE(doc->activeIndex(), 2);
        QCOMPARE(doc->layerAt(2).parent, doc->layerAt(3).id);
        QCOMPARE(Tree::depth(doc->layers(), 2), 1);
        // Hiding the group hides its contents.
        QCOMPARE(at(doc.get(), 5, 5), QColor(Qt::red));
        Ops::setVisible(doc.get(), 3, false);
        QCOMPARE(at(doc.get(), 5, 5), QColor(Qt::white));
        Ops::setVisible(doc.get(), 3, true);
        // Ungroup keeps the order and drops the group.
        doc->setActiveIndex(3);
        QVERIFY(Ops::ungroup(doc.get()));
        QCOMPARE(doc->layerCount(), 3);
        QCOMPARE(doc->layerAt(1).parent, quint64(0));
        QCOMPARE(doc->layerAt(2).parent, quint64(0));
        QCOMPARE(doc->layerAt(2).name, QStringLiteral("B"));
        doc->undoStack()->undo();
        QVERIFY(doc->layerAt(3).isGroup());
    }

    void groupBlendIsolatesChildren()
    {
        // A Multiply layer inside a Normal group multiplies with the group's transparency, so
        // the group shows its colour unchanged; in a Pass Through group it multiplies with the
        // background.
        std::unique_ptr<Document> doc(makeDoc());
        doc->modify("Gray", [&] { doc->layerRef(0).image.fill(QColor(200, 200, 200)); });
        colorLayer(doc.get(), QColor(128, 128, 128), QRect(0, 0, 10, 10));
        Ops::setBlendMode(doc.get(), 1, BlendMode::Multiply);
        QVERIFY(Ops::groupLayer(doc.get()));
        const QColor pass = at(doc.get(), 5, 5);
        QVERIFY(std::abs(pass.red() - 100) <= 2);
        Ops::setBlendMode(doc.get(), 2, BlendMode::Normal);
        QVERIFY(std::abs(at(doc.get(), 5, 5).red() - 128) <= 1);
        // Group opacity applies once to the whole group.
        Ops::setOpacity(doc.get(), 2, 0.5f);
        QVERIFY(std::abs(at(doc.get(), 5, 5).red() - 164) <= 2);
    }

    void moveIntoAndOutOfGroups()
    {
        std::unique_ptr<Document> doc(makeDoc());
        colorLayer(doc.get(), Qt::red, QRect(0, 0, 4, 4), "A");   // 1
        colorLayer(doc.get(), Qt::green, QRect(0, 0, 4, 4), "B"); // 2
        Ops::newGroup(doc.get());                                 // 3, empty
        QVERIFY(Ops::moveLayerInto(doc.get(), 1, 3));
        // A moved below the group's layer, inside it: B, A, Group order bottom-up becomes B, A(in), Group.
        QCOMPARE(doc->layerAt(1).name, QStringLiteral("B"));
        QCOMPARE(doc->layerAt(2).name, QStringLiteral("A"));
        QCOMPARE(doc->layerAt(2).parent, doc->layerAt(3).id);
        // Send Backward on the only child steps out below the group.
        doc->setActiveIndex(2);
        QVERIFY(Ops::arrange(doc.get(), Ops::Arrange::SendBackward));
        QCOMPARE(doc->layerAt(doc->activeIndex()).parent, quint64(0));
        // The Background never moves and nothing goes below it.
        QVERIFY(!Ops::moveLayer(doc.get(), 0, 2));
        doc->setActiveIndex(1);
        QVERIFY(!Ops::arrange(doc.get(), Ops::Arrange::SendBackward) || doc->layerAt(0).isBackground);
        QVERIFY(doc->layerAt(0).isBackground);
        // A group cannot go inside itself.
        Ops::newGroup(doc.get());
        const int outer = doc->activeIndex();
        QVERIFY(!Ops::moveLayerInto(doc.get(), outer, outer));
    }

    void duplicateAndDeleteGroup()
    {
        std::unique_ptr<Document> doc(makeDoc());
        colorLayer(doc.get(), Qt::red, QRect(0, 0, 4, 4), "A");
        QVERIFY(Ops::groupLayer(doc.get()));
        QVERIFY(Ops::duplicateLayer(doc.get()));
        QCOMPARE(doc->layerCount(), 5);
        const Layer& copy = doc->layerAt(4);
        QVERIFY(copy.isGroup());
        QVERIFY(copy.id != doc->layerAt(2).id);
        QCOMPARE(doc->layerAt(3).parent, copy.id);
        QCOMPARE(doc->layerAt(1).parent, doc->layerAt(2).id);
        QVERIFY(Ops::deleteLayer(doc.get()));
        QCOMPARE(doc->layerCount(), 3);
    }

    void layerMaskHidesAndIsPaintable()
    {
        std::unique_ptr<Document> doc(makeDoc());
        colorLayer(doc.get(), Qt::red, QRect(0, 0, 20, 20));
        QPainterPath left;
        left.addRect(0, 0, 10, 48);
        doc->changeSelection(Sel::pathMask(doc->size(), left, false), "Marquee");
        QVERIFY(Ops::addMask(doc.get(), Ops::MaskFill::RevealSelection));
        QVERIFY(!doc->hasSelection());
        QVERIFY(doc->editingMask());
        QCOMPARE(at(doc.get(), 5, 5), QColor(Qt::red));
        QCOMPARE(at(doc.get(), 15, 5), QColor(Qt::white));
        // Filling the mask white reveals; the fill goes to the mask, not the pixels.
        QVERIFY(Ops::fill(doc.get(), Qt::white, BlendMode::Normal, 1.f, false));
        QCOMPARE(at(doc.get(), 15, 5), QColor(Qt::red));
        doc->undoStack()->undo();
        QCOMPARE(at(doc.get(), 15, 5), QColor(Qt::white));
        // Disabling shows everything; deleting with apply bakes the mask in.
        Ops::setMaskEnabled(doc.get(), 1, false);
        QCOMPARE(at(doc.get(), 15, 5), QColor(Qt::red));
        Ops::setMaskEnabled(doc.get(), 1, true);
        QVERIFY(Ops::deleteMask(doc.get(), true));
        QVERIFY(!doc->layerAt(1).hasMask());
        QCOMPARE(qAlpha(doc->layerAt(1).pixelAt(QPoint(15, 5))), 0);
        QCOMPARE(qAlpha(doc->layerAt(1).pixelAt(QPoint(5, 5))), 255);
        // Hide All starts fully hidden.
        QVERIFY(Ops::addMask(doc.get(), Ops::MaskFill::HideAll));
        QCOMPARE(at(doc.get(), 5, 5), QColor(Qt::white));
    }

    void linkedMaskMovesWithLayer()
    {
        std::unique_ptr<Document> doc(makeDoc());
        colorLayer(doc.get(), Qt::red, QRect(0, 0, 10, 10));
        QPainterPath box;
        box.addRect(0, 0, 5, 10);
        doc->changeSelection(Sel::pathMask(doc->size(), box, false), "Marquee");
        QVERIFY(Ops::addMask(doc.get(), Ops::MaskFill::RevealSelection));
        Layer& l = doc->layerRef(1);
        l.translate(QPoint(20, 0));
        QCOMPARE(l.maskAt(QPoint(22, 5)), 255);
        QCOMPARE(l.maskAt(QPoint(27, 5)), 0);
        l.maskLinked = false;
        l.translate(QPoint(-20, 0));
        QCOMPARE(l.maskAt(QPoint(2, 5)), 0);
    }

    void clippingMaskLimitsToBase()
    {
        std::unique_ptr<Document> doc(makeDoc());
        colorLayer(doc.get(), Qt::blue, QRect(10, 10, 10, 10));
        colorLayer(doc.get(), Qt::red, QRect(0, 0, 40, 40));
        QVERIFY(Ops::toggleClippingMask(doc.get()));
        QVERIFY(Tree::isClipped(doc->layers(), 2));
        QCOMPARE(at(doc.get(), 15, 15), QColor(Qt::red));
        QCOMPARE(at(doc.get(), 5, 5), QColor(Qt::white));
        // Hiding the base hides the clipped layer too.
        Ops::setVisible(doc.get(), 1, false);
        QCOMPARE(at(doc.get(), 5, 5), QColor(Qt::white));
        QCOMPARE(at(doc.get(), 15, 15), QColor(Qt::white));
        Ops::setVisible(doc.get(), 1, true);
        QVERIFY(Ops::toggleClippingMask(doc.get()));
        QCOMPARE(at(doc.get(), 5, 5), QColor(Qt::red));
        // The Background cannot be clipped.
        doc->setActiveIndex(0);
        QVERIFY(!Ops::toggleClippingMask(doc.get()));
    }

    void adjustmentLayerIsNonDestructive()
    {
        std::unique_ptr<Document> doc(makeDoc());
        colorLayer(doc.get(), QColor(255, 0, 0), QRect(0, 0, 20, 20));
        QPainterPath left;
        left.addRect(0, 0, 10, 48);
        doc->changeSelection(Sel::pathMask(doc->size(), left, false), "Marquee");
        Ops::newAdjustmentLayer(doc.get(), Adjust::LayerSettings::make(Adjust::Kind::Invert));
        QCOMPARE(doc->layerAt(2).name, QStringLiteral("Invert 1"));
        QCOMPARE(doc->layerAt(2).kind, LayerKind::Adjustment);
        QVERIFY(doc->editingMask()); // adjustment layers always target their mask
        // Inverted inside the old selection only.
        QCOMPARE(at(doc.get(), 5, 5), QColor(0, 255, 255));
        QCOMPARE(at(doc.get(), 15, 5), QColor(255, 0, 0));
        QCOMPARE(at(doc.get(), 5, 30), QColor(0, 0, 0));
        // The pixels below are untouched.
        QCOMPARE(QColor(doc->layerAt(1).pixelAt(QPoint(5, 5))), QColor(255, 0, 0));
        // Changing the settings re-renders; hiding the layer restores.
        Adjust::LayerSettings t = *doc->layerAt(2).adjustment;
        t.kind = Adjust::Kind::Threshold;
        t.thresholdLevel = 128;
        Ops::setAdjustment(doc.get(), 2, t.finalized());
        QCOMPARE(at(doc.get(), 5, 5), QColor(Qt::black));
        Ops::setVisible(doc.get(), 2, false);
        QCOMPARE(at(doc.get(), 5, 5), QColor(255, 0, 0));
        // Half opacity mixes halfway.
        Ops::setVisible(doc.get(), 2, true);
        Ops::setAdjustment(doc.get(), 2, Adjust::LayerSettings::make(Adjust::Kind::Invert));
        Ops::setOpacity(doc.get(), 2, 0.5f);
        QVERIFY(std::abs(at(doc.get(), 5, 30).red() - 128) <= 1);
    }

    void adjustmentInsideGroupOnlyAffectsGroup()
    {
        std::unique_ptr<Document> doc(makeDoc());
        colorLayer(doc.get(), QColor(255, 0, 0), QRect(0, 0, 10, 10));
        QVERIFY(Ops::groupLayer(doc.get()));
        Ops::setBlendMode(doc.get(), 2, BlendMode::Normal);
        Ops::newAdjustmentLayer(doc.get(), Adjust::LayerSettings::make(Adjust::Kind::Invert));
        QCOMPARE(doc->layerAt(doc->activeIndex()).parent, doc->layerAt(3).id);
        QCOMPARE(at(doc.get(), 5, 5), QColor(0, 255, 255));
        QCOMPARE(at(doc.get(), 30, 30), QColor(Qt::white)); // the background is outside the group
        // As Pass Through, the adjustment reaches everything below.
        Ops::setBlendMode(doc.get(), 3, BlendMode::PassThrough);
        QCOMPARE(at(doc.get(), 30, 30), QColor(Qt::black));
    }

    void layerStyleEffects()
    {
        std::unique_ptr<Document> doc(makeDoc());
        colorLayer(doc.get(), Qt::red, QRect(20, 15, 10, 10));
        LayerStyle st;
        st.stroke.enabled = true;
        st.stroke.size = 3;
        st.stroke.color = Qt::blue;
        Ops::setStyle(doc.get(), 1, std::make_shared<const LayerStyle>(st), "Layer Style");
        QVERIFY(doc->layerAt(1).hasStyle());
        QCOMPARE(at(doc.get(), 18, 20), QColor(Qt::blue)); // outside stroke
        QCOMPARE(at(doc.get(), 25, 20), QColor(Qt::red));  // content untouched
        QCOMPARE(at(doc.get(), 10, 20), QColor(Qt::white));
        // Inside stroke draws over the content's edge instead.
        st.stroke.position = StrokeEffect::Position::Inside;
        Ops::setStyle(doc.get(), 1, std::make_shared<const LayerStyle>(st), "Layer Style");
        QCOMPARE(at(doc.get(), 18, 20), QColor(Qt::white));
        QCOMPARE(at(doc.get(), 21, 20), QColor(Qt::blue));
        // Colour overlay survives Fill 0; drop shadow lands down-right.
        st = LayerStyle();
        st.colorOverlay.enabled = true;
        st.colorOverlay.color = Qt::green;
        st.dropShadow.enabled = true;
        st.dropShadow.opacity = 100;
        st.dropShadow.size = 0;
        st.dropShadow.distance = 6;
        st.dropShadow.angle = 90; // light from above: shadow straight down
        Ops::setStyle(doc.get(), 1, std::make_shared<const LayerStyle>(st), "Layer Style");
        Ops::setFill(doc.get(), 1, 0.f);
        QCOMPARE(at(doc.get(), 25, 20), QColor(Qt::green));
        QCOMPARE(at(doc.get(), 25, 28), QColor(Qt::black));
        QCOMPARE(at(doc.get(), 25, 33), QColor(Qt::white));
        // Rasterizing bakes the effects into the pixels.
        Ops::setFill(doc.get(), 1, 1.f);
        const QRgb before = doc->composite().pixel(25, 28);
        QVERIFY(Ops::rasterizeStyle(doc.get(), 1));
        QVERIFY(!doc->layerAt(1).style);
        QCOMPARE(doc->composite().pixel(25, 28), before);
    }

    void textAndShapeLayers()
    {
        std::unique_ptr<Document> doc(makeDoc(QSize(200, 100)));
        TextData t;
        t.text = QStringLiteral("Hi");
        t.size = 40;
        t.color = Qt::black;
        t.position = QPointF(20, 60);
        const int ti = Ops::newTextLayer(doc.get(), t);
        QCOMPARE(doc->layerAt(ti).kind, LayerKind::Text);
        QCOMPARE(doc->layerAt(ti).name, QStringLiteral("Hi"));
        QVERIFY(!doc->layerAt(ti).image.isNull());
        const QRect r1 = doc->layerAt(ti).rect();
        QVERIFY(r1.contains(QPoint(30, 50)));
        // Editing re-renders; moving keeps the text editable.
        t.text = QStringLiteral("Hi there");
        Ops::setText(doc.get(), ti, t, "Edit Type Layer");
        QVERIFY(doc->layerAt(ti).rect().width() > r1.width());
        doc->layerRef(ti).translate(QPoint(10, 0));
        QCOMPARE(doc->layerAt(ti).text->position, QPointF(30, 60));
        QVERIFY(Ops::editTargetError(doc.get(), "Could not use the brush tool").contains("rasterized"));
        QVERIFY(Ops::rasterizeLayer(doc.get(), ti));
        QCOMPARE(doc->layerAt(ti).kind, LayerKind::Pixel);
        QVERIFY(Ops::editTargetError(doc.get(), "x").isEmpty());

        ShapeData s;
        s.path = Vector::rectanglePath(QRectF(100, 10, 50, 30), 0);
        s.fillColor = Qt::red;
        const int si = Ops::newShapeLayer(doc.get(), s, "Rectangle");
        QCOMPARE(doc->layerAt(si).name, QStringLiteral("Rectangle 1"));
        QCOMPARE(at(doc.get(), 120, 20), QColor(Qt::red));
        s.fillColor = Qt::blue;
        Ops::setShape(doc.get(), si, s, "Edit Shape");
        QCOMPARE(at(doc.get(), 120, 20), QColor(Qt::blue));
        // Canvas rotation keeps shapes as vectors.
        Ops::rotate(doc.get(), Ops::Rotation::Rotate180);
        QCOMPARE(doc->layerAt(si).kind, LayerKind::Shape);
        QCOMPARE(at(doc.get(), 200 - 120, 100 - 20), QColor(Qt::blue));
    }

    void mergeGroupAndAdjustmentDown()
    {
        std::unique_ptr<Document> doc(makeDoc());
        colorLayer(doc.get(), QColor(255, 0, 0), QRect(0, 0, 10, 10));
        Ops::newAdjustmentLayer(doc.get(), Adjust::LayerSettings::make(Adjust::Kind::Invert));
        const QRgb shown = doc->composite().pixel(5, 5);
        // Merging an adjustment layer down applies it to the pixels below.
        QVERIFY(Ops::mergeDown(doc.get()));
        QCOMPARE(doc->layerCount(), 2);
        QCOMPARE(doc->layerAt(1).kind, LayerKind::Pixel);
        QCOMPARE(doc->composite().pixel(5, 5), shown);
        // Merging a group (Ctrl+E) gives one pixel layer that looks the same.
        colorLayer(doc.get(), QColor(0, 0, 255), QRect(5, 5, 10, 10));
        doc->setActiveIndex(2);
        QVERIFY(Ops::groupLayer(doc.get()));
        const QImage before = doc->composite().copy();
        QVERIFY(Ops::mergeDown(doc.get()));
        QCOMPARE(doc->layerAt(doc->activeIndex()).kind, LayerKind::Pixel);
        QCOMPARE(doc->composite(), before);
        // Merge Visible and Flatten use the same compositing.
        QVERIFY(Ops::mergeVisible(doc.get()));
        QCOMPARE(doc->composite(), before);
    }

    void stage4RoundTrip()
    {
        QTemporaryDir dir;
        std::unique_ptr<Document> doc(makeDoc(QSize(120, 80)));
        colorLayer(doc.get(), Qt::red, QRect(0, 0, 30, 30), "Base");
        QVERIFY(Ops::groupLayer(doc.get()));
        Ops::setBlendMode(doc.get(), 2, BlendMode::Normal);
        QVERIFY(Ops::addMask(doc.get(), Ops::MaskFill::HideAll));
        QVERIFY(Ops::fill(doc.get(), Qt::white, BlendMode::Normal, 1.f, false)); // reveal through the mask
        Adjust::LayerSettings lv;
        lv.kind = Adjust::Kind::Levels;
        lv.levels.channels[0].inWhite = 200;
        Ops::newAdjustmentLayer(doc.get(), lv.finalized());
        colorLayer(doc.get(), Qt::blue, QRect(0, 0, 60, 60), "Clip");
        QVERIFY(Ops::toggleClippingMask(doc.get()));
        LayerStyle st;
        st.dropShadow.enabled = true;
        Ops::setStyle(doc.get(), doc->activeIndex(), std::make_shared<const LayerStyle>(st), "Layer Style");
        TextData t;
        t.text = "A";
        t.position = QPointF(70, 50);
        const int ti = Ops::newTextLayer(doc.get(), t);
        ShapeData s;
        s.path = Vector::ellipsePath(QRectF(80, 10, 20, 20));
        Ops::newShapeLayer(doc.get(), s, "Ellipse");

        const QString path = dir.filePath("s4.pslop");
        QString err;
        QVERIFY2(DocumentIO::saveNative(doc.get(), path, &err), qPrintable(err));
        std::unique_ptr<Document> loaded(DocumentIO::load(path, &err));
        QVERIFY2(loaded, qPrintable(err));
        QCOMPARE(loaded->layerCount(), doc->layerCount());
        for (int i = 0; i < doc->layerCount(); ++i) {
            const Layer& a = doc->layerAt(i);
            const Layer& b = loaded->layerAt(i);
            QCOMPARE(b.kind, a.kind);
            QCOMPARE(b.clipped, a.clipped);
            QCOMPARE(b.hasMask(), a.hasMask());
            QCOMPARE(bool(b.style), bool(a.style));
            QCOMPARE(Tree::parentIndex(loaded->layers(), i), Tree::parentIndex(doc->layers(), i));
        }
        QVERIFY(loaded->layerAt(ti).text);
        QCOMPARE(loaded->layerAt(ti).text->text, QStringLiteral("A"));
        QCOMPARE(loaded->composite(), doc->composite());
    }

    void healingBlendsAndFindsTexture()
    {
        // A flat source healed into a target whose edges are a different flat colour takes the
        // target's colour, keeping the source's (absent) texture.
        QImage target(20, 20, QImage::Format_ARGB32_Premultiplied), source(20, 20, QImage::Format_ARGB32_Premultiplied);
        target.fill(QColor(200, 100, 50));
        source.fill(QColor(20, 20, 20));
        std::vector<uint8_t> region(400, 0);
        for (int y = 5; y < 15; ++y)
            for (int x = 5; x < 15; ++x) region[size_t(y * 20 + x)] = 1;
        const QImage healed = Heal::blend(target, source, region);
        const QColor c = QColor(healed.pixel(10, 10));
        QVERIFY(std::abs(c.red() - 200) <= 2 && std::abs(c.green() - 100) <= 2 && std::abs(c.blue() - 50) <= 2);
        // Texture survives: a bright source dot stays brighter than its surroundings.
        source.setPixel(10, 10, qRgb(120, 120, 120));
        const QImage dotted = Heal::blend(target, source, region);
        QVERIFY(qRed(dotted.pixel(10, 10)) > qRed(dotted.pixel(8, 8)) + 50);

        // Proximity match: around a dark spot on a two-tone image, the best source is on the
        // same side of the split.
        QImage img(200, 100, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::white);
        QPainter(&img).fillRect(0, 50, 200, 50, Qt::blue);
        std::vector<uint8_t> spot(36, 1);
        const QPoint off = Heal::findSource(img, img.rect(), spot, QRect(97, 20, 6, 6));
        QVERIFY(20 + off.y() >= 0 && 26 + off.y() < 50); // stays in the white half
    }

    void parallelCompositeMatchesSingleThreaded()
    {
        std::unique_ptr<Document> doc(makeDoc(QSize(300, 300)));
        colorLayer(doc.get(), QColor(10, 200, 30), QRect(20, 20, 200, 200));
        LayerStyle st;
        st.outerGlow.enabled = true;
        st.outerGlow.size = 20;
        st.stroke.enabled = true;
        Ops::setStyle(doc.get(), 1, std::make_shared<const LayerStyle>(st), "Layer Style");
        Ops::setBlendMode(doc.get(), 1, BlendMode::Dissolve);
        Ops::setOpacity(doc.get(), 1, 0.7f);
        const QImage parallel = doc->composite().copy();
        const QImage single = Compositor::flatten(doc->layers(), doc->bounds());
        QCOMPARE(parallel, single);
    }
};

// A GUI application: text layers need fonts.
QTEST_MAIN(TestCore)
#include "test_core.moc"
#include <algorithm>
#include <iterator>
