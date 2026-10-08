#include "io/DocumentIO.h"

#include "core/Adjustments.h"
#include "core/Document.h"
#include "core/ImageOps.h"
#include "core/LayerStyle.h"
#include "core/VectorLayers.h"

#include <QBuffer>
#include <QColorSpace>
#include <QDataStream>
#include <QFileInfo>
#include <QHash>
#include <QImageReader>
#include <QImageWriter>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <cmath>
#include <algorithm>
#include <iterator>

namespace DocumentIO {

namespace {
constexpr quint32 kMagic = 0x50534C50; // "PSLP"
constexpr quint32 kVersion = 3; // 2 added guides, 3 groups, masks, adjustment/text/shape layers and styles
QByteArray toPng(const QImage& img)
{
    QByteArray png;
    if (!img.isNull()) {
        QBuffer buf(&png);
        buf.open(QIODevice::WriteOnly);
        img.convertToFormat(QImage::Format_ARGB32).save(&buf, "PNG");
    }
    return png;
}

QImage fromPng(const QByteArray& png)
{
    if (png.isEmpty()) return QImage();
    QImage img;
    img.loadFromData(png, "PNG");
    return img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
}

// Everything a version 3 layer adds, as JSON. Parents are stored as list indices.
QByteArray layerExtra(const Layer& l, const QHash<quint64, int>& indexOf)
{
    QJsonObject o;
    o.insert(QStringLiteral("kind"), int(l.kind));
    o.insert(QStringLiteral("parent"), l.parent ? indexOf.value(l.parent, -1) : -1);
    o.insert(QStringLiteral("expanded"), l.expanded);
    o.insert(QStringLiteral("clipped"), l.clipped);
    if (l.mask)
        o.insert(QStringLiteral("mask"), QJsonObject{{QStringLiteral("default"), int(l.maskDefault)},
                                                     {QStringLiteral("enabled"), l.maskEnabled},
                                                     {QStringLiteral("linked"), l.maskLinked}});
    if (l.adjustment) o.insert(QStringLiteral("adjustment"), l.adjustment->toJson());
    if (l.style) o.insert(QStringLiteral("style"), l.style->toJson());
    if (l.text) o.insert(QStringLiteral("text"), l.text->toJson());
    if (l.shape) o.insert(QStringLiteral("shape"), l.shape->toJson());
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

// Applies the JSON extras; returns the parent's list index (or -1).
int applyExtra(Layer& l, const QByteArray& json)
{
    const QJsonObject o = QJsonDocument::fromJson(json).object();
    l.kind = LayerKind(std::clamp(o.value(QStringLiteral("kind")).toInt(), 0, int(LayerKind::Shape)));
    l.expanded = o.value(QStringLiteral("expanded")).toBool(true);
    l.clipped = o.value(QStringLiteral("clipped")).toBool();
    if (o.contains(QStringLiteral("mask"))) {
        const QJsonObject m = o.value(QStringLiteral("mask")).toObject();
        l.maskDefault = quint8(std::clamp(m.value(QStringLiteral("default")).toInt(255), 0, 255));
        l.maskEnabled = m.value(QStringLiteral("enabled")).toBool(true);
        l.maskLinked = m.value(QStringLiteral("linked")).toBool(true);
        l.mask.set(Layer::create(QStringLiteral("Layer Mask")));
    }
    if (o.contains(QStringLiteral("adjustment")))
        l.adjustment = Adjust::LayerSettings::fromJson(o.value(QStringLiteral("adjustment")).toObject());
    if (o.contains(QStringLiteral("style")))
        l.style = std::make_shared<const LayerStyle>(LayerStyle::fromJson(o.value(QStringLiteral("style")).toObject()));
    if (o.contains(QStringLiteral("text")))
        l.text = std::make_shared<const TextData>(TextData::fromJson(o.value(QStringLiteral("text")).toObject()));
    if (o.contains(QStringLiteral("shape")))
        l.shape = std::make_shared<const ShapeData>(ShapeData::fromJson(o.value(QStringLiteral("shape")).toObject()));
    if (l.kind == LayerKind::Adjustment && !l.adjustment) l.adjustment = Adjust::LayerSettings::make(Adjust::Kind::Levels);
    if (l.kind == LayerKind::Text && !l.text) l.kind = LayerKind::Pixel;
    if (l.kind == LayerKind::Shape && !l.shape) l.kind = LayerKind::Pixel;
    return o.value(QStringLiteral("parent")).toInt(-1);
}

} // namespace

QString openFilter()
{
    return QStringLiteral(
        "All Formats (*.pslop *.png *.jpg *.jpeg *.bmp *.gif *.tif *.tiff *.webp);;"
        "PhotoSlop (*.pslop);;PNG (*.png);;JPEG (*.jpg *.jpeg);;BMP (*.bmp);;GIF (*.gif);;"
        "TIFF (*.tif *.tiff);;WebP (*.webp)");
}

QString saveFilter()
{
    return QStringLiteral(
        "PhotoSlop (*.pslop);;PNG (*.png);;JPEG (*.jpg *.jpeg);;BMP (*.bmp);;TIFF (*.tif *.tiff);;"
        "WebP (*.webp)");
}

QStringList exportFormats()
{
    QStringList out;
    const auto supported = QImageWriter::supportedImageFormats();
    for (const char* f : {"png", "jpeg", "gif", "webp", "tiff", "bmp"})
        if (supported.contains(f)) out << QString::fromLatin1(f).toUpper();
    return out;
}

bool isNativePath(const QString& path) { return path.endsWith(QStringLiteral(".pslop"), Qt::CaseInsensitive); }

QByteArray formatForPath(const QString& path)
{
    QString ext = QFileInfo(path).suffix().toLower();
    if (ext == QLatin1String("jpg")) return "jpeg";
    if (ext == QLatin1String("tif")) return "tiff";
    return ext.toLatin1();
}

Document* fromImage(const QImage& src, const QString& title)
{
    QImage img = src;
    if (img.colorSpace().isValid() && img.colorSpace() != QColorSpace::SRgb)
        img.convertToColorSpace(QColorSpace::SRgb);
    const bool hasAlpha = img.hasAlphaChannel();
    img = img.convertToFormat(QImage::Format_ARGB32_Premultiplied);

    auto* doc = new Document(img.size());
    DocState s;
    s.size = img.size();
    s.dpi = img.dotsPerMeterX() > 0 ? std::round(img.dotsPerMeterX() * 0.0254) : 72.0;
    // Photoshop opens flat opaque images as a Background layer, and images with
    // transparency as "Layer 0".
    Layer l = Layer::create(hasAlpha ? QStringLiteral("Layer 0") : QStringLiteral("Background"));
    l.isBackground = !hasAlpha;
    l.image = img;
    s.layers = {l};
    doc->initialize(s);
    doc->setTitle(title);
    return doc;
}

static Document* loadNative(const QString& path, QString* error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = f.errorString();
        return nullptr;
    }
    QDataStream in(&f);
    in.setVersion(QDataStream::Qt_6_0);
    quint32 magic = 0, version = 0;
    in >> magic >> version;
    if (magic != kMagic || version > kVersion) {
        if (error) *error = QStringLiteral("This is not a valid PhotoSlop document.");
        return nullptr;
    }
    DocState s;
    qint32 active = 0, count = 0;
    in >> s.size >> s.dpi >> active >> count;
    if (in.status() != QDataStream::Ok || s.size.isEmpty() || count < 0 || count > 100000) {
        if (error) *error = QStringLiteral("The document is damaged.");
        return nullptr;
    }
    QList<int> parents;
    for (int i = 0; i < count; ++i) {
        Layer l = Layer::create(QString());
        QString modeId;
        QByteArray png;
        in >> l.name >> l.offset >> l.visible >> l.opacity >> l.fill >> modeId >> l.lockTransparency
            >> l.lockPixels >> l.lockPosition >> l.lockAll >> l.isBackground >> png;
        l.mode = Blend::fromId(modeId);
        l.image = fromPng(png);
        int parent = -1;
        if (version >= 3) {
            QByteArray extra, maskPng;
            QPoint maskOffset;
            in >> extra >> maskPng >> maskOffset;
            parent = applyExtra(l, extra);
            if (l.mask) {
                l.mask->image = fromPng(maskPng);
                l.mask->offset = maskOffset;
            }
        }
        parents.append(parent);
        s.layers.append(l);
    }
    for (int i = 0; i < s.layers.size(); ++i) {
        const int p = parents[i];
        if (p > i && p < s.layers.size() && s.layers[p].isGroup()) s.layers[i].parent = s.layers[p].id;
    }
    if (version >= 2) {
        qint32 guideCount = 0;
        in >> guideCount;
        for (int i = 0; i < guideCount && in.status() == QDataStream::Ok; ++i) {
            bool vertical = false;
            Guide g;
            in >> vertical >> g.position;
            g.orientation = vertical ? Qt::Vertical : Qt::Horizontal;
            s.guides.append(g);
        }
    }
    if (in.status() != QDataStream::Ok) {
        if (error) *error = QStringLiteral("The document is damaged.");
        return nullptr;
    }
    s.active = active;
    auto* doc = new Document(s.size);
    doc->initialize(s);
    doc->setTitle(QFileInfo(path).fileName());
    doc->setFilePath(path);
    return doc;
}

Document* load(const QString& path, QString* error)
{
    if (isNativePath(path)) return loadNative(path, error);
    QImageReader reader(path);
    reader.setAutoTransform(true);
    QImage img = reader.read();
    if (img.isNull()) {
        if (error)
            *error = QStringLiteral("Could not complete your request because %1.").arg(reader.errorString());
        return nullptr;
    }
    Document* doc = fromImage(img, QFileInfo(path).fileName());
    doc->setFilePath(path);
    return doc;
}

bool saveNative(Document* doc, const QString& path, QString* error)
{
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        if (error) *error = f.errorString();
        return false;
    }
    QDataStream out(&f);
    out.setVersion(QDataStream::Qt_6_0);
    out << kMagic << kVersion << doc->size() << doc->dpi() << qint32(doc->activeIndex())
        << qint32(doc->layerCount());
    QHash<quint64, int> indexOf;
    for (int i = 0; i < doc->layerCount(); ++i) indexOf.insert(doc->layerAt(i).id, i);
    for (const Layer& l : doc->layers()) {
        out << l.name << l.offset << l.visible << l.opacity << l.fill << Blend::id(l.mode)
            << l.lockTransparency << l.lockPixels << l.lockPosition << l.lockAll << l.isBackground
            << toPng(l.image);
        out << layerExtra(l, indexOf) << (l.mask ? toPng(l.mask->image) : QByteArray())
            << (l.mask ? l.mask->offset : QPoint());
    }
    out << qint32(doc->guides().size());
    for (const Guide& g : doc->guides()) out << (g.orientation == Qt::Vertical) << g.position;
    if (!f.commit()) {
        if (error) *error = f.errorString();
        return false;
    }
    return true;
}

bool exportFlat(Document* doc, const QString& path, const QByteArray& format, int quality,
                QString* error)
{
    const bool noAlpha = format == "jpeg" || format == "bmp";
    QImage img = ImageOps::toExportImage(doc->composite(), noAlpha ? QColor(Qt::white) : QColor());
    const int dpm = int(std::lround(doc->dpi() / 0.0254));
    img.setDotsPerMeterX(dpm);
    img.setDotsPerMeterY(dpm);
    img.setColorSpace(QColorSpace::SRgb);
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        if (error) *error = f.errorString();
        return false;
    }
    QImageWriter writer(&f, format);
    if (quality >= 0) writer.setQuality(quality);
    if (!writer.write(img)) {
        if (error) *error = writer.errorString();
        f.cancelWriting();
        return false;
    }
    if (!f.commit()) {
        if (error) *error = f.errorString();
        return false;
    }
    return true;
}

} // namespace DocumentIO
