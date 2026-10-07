#include "io/DocumentIO.h"

#include "core/Document.h"
#include "core/ImageOps.h"

#include <QBuffer>
#include <QColorSpace>
#include <QDataStream>
#include <QFileInfo>
#include <QImageReader>
#include <QImageWriter>
#include <QSaveFile>
#include <cmath>
#include <algorithm>
#include <iterator>

namespace DocumentIO {

namespace {
constexpr quint32 kMagic = 0x50534C50; // "PSLP"
constexpr quint32 kVersion = 1;
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
    for (int i = 0; i < count; ++i) {
        Layer l = Layer::create(QString());
        QString modeId;
        QByteArray png;
        in >> l.name >> l.offset >> l.visible >> l.opacity >> l.fill >> modeId >> l.lockTransparency
            >> l.lockPixels >> l.lockPosition >> l.lockAll >> l.isBackground >> png;
        l.mode = Blend::fromId(modeId);
        if (!png.isEmpty()) {
            QImage img;
            img.loadFromData(png, "PNG");
            l.image = img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        }
        s.layers.append(l);
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
    for (const Layer& l : doc->layers()) {
        QByteArray png;
        if (!l.image.isNull()) {
            QBuffer buf(&png);
            buf.open(QIODevice::WriteOnly);
            l.image.convertToFormat(QImage::Format_ARGB32).save(&buf, "PNG");
        }
        out << l.name << l.offset << l.visible << l.opacity << l.fill << Blend::id(l.mode)
            << l.lockTransparency << l.lockPixels << l.lockPosition << l.lockAll << l.isBackground
            << png;
    }
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
