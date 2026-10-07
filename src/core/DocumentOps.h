#pragma once

#include "core/BlendMode.h"
#include "core/Selection.h"

#include <QColor>
#include <QImage>
#include <QString>

class Document;

// Undoable document-level operations behind the menus and panels.
// Each returns false (and sets `error` when given) if the operation is not possible,
// mirroring the alerts Photoshop shows.
namespace Ops {

enum class Arrange { BringToFront, BringForward, SendBackward, SendToBack };
enum class Resample { NearestNeighbor, Bilinear, Bicubic };
enum class Modify { Border, Smooth, Expand, Contract };
enum class Rotation { Rotate180, Rotate90CW, Rotate90CCW, FlipHorizontal, FlipVertical };

// Layers
void newLayer(Document* doc, const QString& name = QString(), BlendMode mode = BlendMode::Normal,
              float opacity = 1.0f);
bool duplicateLayer(Document* doc, const QString& name = QString());
bool deleteLayer(Document* doc, QString* error = nullptr);
bool moveLayer(Document* doc, int from, int to);
bool arrange(Document* doc, Arrange how);
void setVisible(Document* doc, int index, bool visible);
void soloVisibility(Document* doc, int index);
void setOpacity(Document* doc, int index, float opacity);
void setFill(Document* doc, int index, float fill);
void setBlendMode(Document* doc, int index, BlendMode mode);
void rename(Document* doc, int index, const QString& name);
void setLocks(Document* doc, int index, bool transparency, bool pixels, bool position, bool all);
bool layerFromBackground(Document* doc, int index, const QString& name = QStringLiteral("Layer 0"));
bool mergeDown(Document* doc, QString* error = nullptr);
bool mergeVisible(Document* doc, QString* error = nullptr);
void flatten(Document* doc);
bool layerViaCopy(Document* doc, bool cut, QString* error = nullptr);

// Selection
void selectAll(Document* doc);
void deselect(Document* doc);
void reselect(Document* doc);
void inverse(Document* doc);
void loadSelectionFromLayer(Document* doc, int index, Sel::Op op = Sel::Op::Replace);
bool modifySelection(Document* doc, Modify how, int amount, bool atCanvasBounds = false);
// Select > Grow (contiguous) and Select > Similar, using the Magic Wand tolerance.
bool growSelection(Document* doc, int tolerance, bool contiguous);
// Edit in Quick Mask Mode: the selection becomes a paintable mask, and back.
void setQuickMask(Document* doc, bool on);

// Pixels
bool fill(Document* doc, const QColor& color, BlendMode mode, float opacity,
          bool preserveTransparency, QString* error = nullptr);
bool clear(Document* doc, const QColor& backgroundColor, QString* error = nullptr);
QImage copy(Document* doc, bool merged, QPoint* topLeft = nullptr, QString* error = nullptr);
void paste(Document* doc, const QImage& image, const QPoint* preferredTopLeft = nullptr);

// Canvas
void crop(Document* doc, const QRect& rect, bool deleteCroppedPixels);
void cropToSelection(Document* doc);
void imageSize(Document* doc, const QSize& newSize, double dpi, Resample method);
void canvasSize(Document* doc, const QSize& newSize, const QPoint& anchorOffset,
                const QColor& extensionColor);
void rotate(Document* doc, Rotation how);

} // namespace Ops
