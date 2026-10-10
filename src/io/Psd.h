#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

class Document;

// Photoshop documents (.psd, and .psb for reading), with PhotoSlop's own reader and writer.
//
// Reading: Bitmap, Grayscale, Duotone (as grey), Indexed, RGB, CMYK and Lab files at 1, 8, 16
// and 32 bits per channel, raw, RLE and ZIP compressed. Layers keep their names, blend modes,
// opacity, fill, visibility, locks and clipping; groups, layer masks, guides and resolution are
// read, as are Levels, Curves, Hue/Saturation, Color Balance, Brightness/Contrast, Invert,
// Posterize and Threshold adjustment layers. Deeper files are converted to 8 bits per channel.
//
// Writing: 8 bits per channel in the document's colour mode, RLE compressed, with a full
// composite for other readers. Type and shape layers are written as pixels and layer styles are
// merged into their layer; Black & White adjustment layers cannot be written and are left out.
namespace Psd {

bool isPsdPath(const QString& path);

// Reads a file. `warnings` lists what could not be kept (unsupported layer types, depth).
Document* load(const QString& path, QString* error, QStringList* warnings = nullptr);
Document* decode(const QByteArray& data, const QString& title, QString* error, QStringList* warnings = nullptr);

// Writes the document. `warnings` lists what was simplified on the way.
bool save(Document* doc, const QString& path, QString* error, QStringList* warnings = nullptr);
QByteArray encode(Document* doc, QString* error, QStringList* warnings = nullptr);

} // namespace Psd
