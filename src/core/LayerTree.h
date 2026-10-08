#pragma once

#include "core/Layer.h"

#include <QList>

// Navigation of the layer tree stored in a flat list (index 0 is the bottom). A group's
// descendants form a contiguous block directly below it; each layer names its group in
// `parent`.
namespace Tree {

// Index of the layer with `id`, or -1.
int indexOf(const QList<Layer>& layers, quint64 id);
// Index of the containing group, or -1 at the top level.
int parentIndex(const QList<Layer>& layers, int index);
// Nesting depth (0 at the top level).
int depth(const QList<Layer>& layers, int index);
// Lowest index of the layer's subtree (the layer itself unless it is a non-empty group).
int subtreeStart(const QList<Layer>& layers, int index);
// True if `index` lies inside the group at `ancestor` (at any depth).
bool isInside(const QList<Layer>& layers, int index, int ancestor);
// Direct children of the group at `group` (or the top level for -1), bottom to top.
QList<int> children(const QList<Layer>& layers, int group);
// The layer and all its ancestors are visible.
bool effectivelyVisible(const QList<Layer>& layers, int index);
// All ancestors are expanded, so the Layers panel shows the row.
bool shownInPanel(const QList<Layer>& layers, int index);
// The clipping base for a clipped layer: the nearest non-clipped sibling below, or -1.
int clipBase(const QList<Layer>& layers, int index);
// The layer is clipped and has a base to clip to.
bool isClipped(const QList<Layer>& layers, int index);

// Moves the subtree rooted at `index` into group `parent`, inserting it at position `dest` of
// the list without it (its bottom layer lands at `dest`). Returns the subtree's new top index.
int moveSubtree(QList<Layer>& layers, int index, int dest, quint64 parent);

// Gives every layer (and mask) a fresh id, keeping the parent links.
void renewIds(QList<Layer>& layers);

} // namespace Tree
