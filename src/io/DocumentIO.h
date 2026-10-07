#pragma once

#include <QColor>
#include <QImage>
#include <QString>
#include <QStringList>

class Document;

namespace DocumentIO {

// File dialog filters.
QString openFilter();
QString saveFilter();
QStringList exportFormats(); // "PNG", "JPEG", ...

bool isNativePath(const QString& path);

// Loads any supported file into a new document (caller owns it).
Document* load(const QString& path, QString* error);
// Creates a document from a single image (used by Open and by File > New from clipboard).
Document* fromImage(const QImage& image, const QString& title);

bool saveNative(Document* doc, const QString& path, QString* error);
// Writes the flattened image. `quality` is 0..100 for lossy formats.
bool exportFlat(Document* doc, const QString& path, const QByteArray& format, int quality,
                QString* error);
QByteArray formatForPath(const QString& path);

} // namespace DocumentIO
