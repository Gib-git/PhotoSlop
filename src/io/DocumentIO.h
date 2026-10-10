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
// Layered formats PhotoSlop can save to and keep working in (.pslop and .psd).
bool isLayeredPath(const QString& path);

// Loads any supported file into a new document (caller owns it). `warnings` lists what a
// Photoshop file lost on the way in.
Document* load(const QString& path, QString* error, QStringList* warnings = nullptr);
// Creates a document from a single image (used by Open and by File > New from clipboard).
Document* fromImage(const QImage& image, const QString& title);

bool saveNative(Document* doc, const QString& path, QString* error);
// Saves to a layered format chosen by extension (.pslop or .psd).
bool saveLayered(Document* doc, const QString& path, QString* error, QStringList* warnings = nullptr);
// Writes the flattened image. `quality` is 0..100 for lossy formats.
bool exportFlat(Document* doc, const QString& path, const QByteArray& format, int quality,
                QString* error);
QByteArray formatForPath(const QString& path);

} // namespace DocumentIO
