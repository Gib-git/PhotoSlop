#pragma once

#include "core/Layer.h"

#include <QImage>
#include <QList>
#include <QRect>

// Blends the layer tree: groups (isolated or Pass Through), clipping masks, layer masks,
// adjustment layers and layer styles.
namespace Compositor {

// A view of premultiplied ARGB32 pixels covering canvas rectangle `area`.
struct Surface {
    uint32_t* bits = nullptr;
    qsizetype stride = 0; // pixels per row
    QRect area;
    uint32_t* row(int canvasY) const { return bits + (canvasY - area.top()) * stride; }
};

// A surface over part of a canvas-sized image (detach the image first when sharing it
// between threads).
Surface surfaceOf(QImage& canvasImage, const QRect& area);

// Composites the visible layers onto `dst`, which holds the backdrop (normally transparent).
void render(const QList<Layer>& layers, const Surface& dst);
// Same, split into bands rendered on all cores.
void renderParallel(const QList<Layer>& layers, QImage& canvasImage, const QRect& area);
// The visible layers flattened over `area`, on transparency.
QImage flatten(const QList<Layer>& layers, const QRect& area);
// One layer (a group with its contents) composited alone onto transparency, with its mask,
// effects, opacity and blend mode, as merging it into an empty layer would give.
QImage renderSingle(const QList<Layer>& layers, int index, const QRect& area);
// Canvas area a layer can change, including its effects; adjustment layers return `everywhere`.
QRect extent(const QList<Layer>& layers, int index, const QRect& everywhere);

} // namespace Compositor
