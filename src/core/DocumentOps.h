#pragma once

#include "core/BlendMode.h"
#include "core/ColorModes.h"
#include "core/Selection.h"

#include <QColor>
#include <QImage>
#include <QString>
#include <memory>

class Document;
struct Layer;
struct LayerStyle;
struct ShapeData;
struct TextData;
namespace Adjust { struct LayerSettings; }

// Undoable document-level operations behind the menus and panels.
// Each returns false (and sets `error` when given) if the operation is not possible,
// mirroring the alerts Photoshop shows.
namespace Ops {

enum class Arrange { BringToFront, BringForward, SendBackward, SendToBack };
enum class Resample { NearestNeighbor, Bilinear, Bicubic };
enum class Modify { Border, Smooth, Expand, Contract };
enum class Rotation { Rotate180, Rotate90CW, Rotate90CCW, FlipHorizontal, FlipVertical };
enum class MaskFill { RevealAll, HideAll, RevealSelection, HideSelection };

// Why the edit target cannot take pixel changes, as "<prefix> because ...", or an empty
// string when it can. `prefix` is e.g. "Could not use the brush tool".
QString editTargetError(Document* doc, const QString& prefix);

// Inserts a layer where a new layer goes (above the active layer, or at the top of the active
// group) and selects it, without recording history: call inside Document::modify().
int insertLayerRaw(Document* doc, Layer layer);

// Layers
void newLayer(Document* doc, const QString& name = QString(), BlendMode mode = BlendMode::Normal,
              float opacity = 1.0f);
bool duplicateLayer(Document* doc, const QString& name = QString());
bool deleteLayer(Document* doc, QString* error = nullptr);
// Moves layer `from` (with its contents) to where layer `to` is, among `to`'s siblings.
bool moveLayer(Document* doc, int from, int to);
// Places a layer directly above or below `target` in `target`'s group, or at the top of a group.
bool moveLayerAbove(Document* doc, int index, int target);
bool moveLayerBelow(Document* doc, int index, int target);
bool moveLayerInto(Document* doc, int index, int group);
bool arrange(Document* doc, Arrange how);
void setVisible(Document* doc, int index, bool visible);
void soloVisibility(Document* doc, int index);
void setOpacity(Document* doc, int index, float opacity);
void setFill(Document* doc, int index, float fill);
void setBlendMode(Document* doc, int index, BlendMode mode);
void rename(Document* doc, int index, const QString& name);
void setLocks(Document* doc, int index, bool transparency, bool pixels, bool position, bool all);
bool layerFromBackground(Document* doc, int index, const QString& name = QStringLiteral("Layer 0"));
// Merges the active layer into the one below; a group merges into one layer.
bool mergeDown(Document* doc, QString* error = nullptr);
bool mergeVisible(Document* doc, QString* error = nullptr);
void flatten(Document* doc);
bool layerViaCopy(Document* doc, bool cut, QString* error = nullptr);
// Shows or hides a group's contents in the Layers panel (not recorded in the history).
void setExpanded(Document* doc, int index, bool expanded);

// Groups
void newGroup(Document* doc, const QString& name = QString());
// Puts the active layer into a new group (Ctrl+G).
bool groupLayer(Document* doc, QString* error = nullptr);
// Moves a group's contents out and removes it (Ctrl+Shift+G).
bool ungroup(Document* doc, QString* error = nullptr);

// Clipping masks: toggles whether the active layer is clipped to the layer below.
bool toggleClippingMask(Document* doc, QString* error = nullptr);

// Layer masks
bool addMask(Document* doc, MaskFill fill, QString* error = nullptr);
// Removes the active layer's mask, first applying it to the pixels when `apply` is set.
bool deleteMask(Document* doc, bool apply, QString* error = nullptr);
void setMaskEnabled(Document* doc, int index, bool enabled);
void setMaskLinked(Document* doc, int index, bool linked);
void loadSelectionFromMask(Document* doc, int index, Sel::Op op = Sel::Op::Replace);

// Adjustment layers. A new one gets a mask from the selection (or a blank one).
void newAdjustmentLayer(Document* doc, std::shared_ptr<const Adjust::LayerSettings> settings,
                        const QString& name = QString());
void setAdjustment(Document* doc, int index, std::shared_ptr<const Adjust::LayerSettings> settings);

// Layer styles (null clears).
void setStyle(Document* doc, int index, std::shared_ptr<const LayerStyle> style, const QString& undoText);

// Text and shape layers
int newTextLayer(Document* doc, const TextData& text);
void setText(Document* doc, int index, const TextData& text, const QString& undoText, int mergeId = -1);
int newShapeLayer(Document* doc, const ShapeData& shape, const QString& baseName);
void setShape(Document* doc, int index, const ShapeData& shape, const QString& undoText, int mergeId = -1);
// Layer > Rasterize: text and shape layers become pixel layers.
bool rasterizeLayer(Document* doc, int index);
// Layer > Rasterize > Layer Style: bakes the effects into the pixels.
bool rasterizeStyle(Document* doc, int index);

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
// Image > Mode. Grayscale turns every layer's pixels grey; the other modes keep the pixels.
void setColorMode(Document* doc, ColorMode mode);

} // namespace Ops
