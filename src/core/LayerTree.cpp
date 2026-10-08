#include "core/LayerTree.h"

#include <QHash>
#include <algorithm>

namespace Tree {

int indexOf(const QList<Layer>& layers, quint64 id)
{
    if (id == 0) return -1;
    for (int i = 0; i < layers.size(); ++i)
        if (layers[i].id == id) return i;
    return -1;
}

int parentIndex(const QList<Layer>& layers, int index)
{
    if (index < 0 || index >= layers.size()) return -1;
    const quint64 p = layers[index].parent;
    if (p == 0) return -1;
    // The parent is above its children.
    for (int i = index + 1; i < layers.size(); ++i)
        if (layers[i].id == p) return i;
    return -1;
}

int depth(const QList<Layer>& layers, int index)
{
    int d = 0;
    for (int p = parentIndex(layers, index); p >= 0; p = parentIndex(layers, p)) ++d;
    return d;
}

bool isInside(const QList<Layer>& layers, int index, int ancestor)
{
    for (int p = parentIndex(layers, index); p >= 0; p = parentIndex(layers, p))
        if (p == ancestor) return true;
    return false;
}

int subtreeStart(const QList<Layer>& layers, int index)
{
    if (index < 0 || index >= layers.size() || !layers[index].isGroup()) return index;
    int s = index;
    while (s > 0 && isInside(layers, s - 1, index)) --s;
    return s;
}

QList<int> children(const QList<Layer>& layers, int group)
{
    QList<int> out;
    int lo = 0, hi = int(layers.size()) - 1;
    quint64 pid = 0;
    if (group >= 0) {
        lo = subtreeStart(layers, group);
        hi = group - 1;
        pid = layers[group].id;
    }
    // Walk down from the top of the range, skipping each child's own subtree.
    for (int i = hi; i >= lo;) {
        if (layers[i].parent == pid) {
            out.prepend(i);
            i = subtreeStart(layers, i) - 1;
        } else {
            --i; // malformed entry; skip it
        }
    }
    return out;
}

bool effectivelyVisible(const QList<Layer>& layers, int index)
{
    for (int i = index; i >= 0; i = parentIndex(layers, i))
        if (!layers[i].visible) return false;
    return index >= 0;
}

bool shownInPanel(const QList<Layer>& layers, int index)
{
    for (int p = parentIndex(layers, index); p >= 0; p = parentIndex(layers, p))
        if (!layers[p].expanded) return false;
    return true;
}

int clipBase(const QList<Layer>& layers, int index)
{
    if (index < 0 || index >= layers.size() || !layers[index].clipped) return -1;
    const QList<int> siblings = children(layers, parentIndex(layers, index));
    const int pos = int(siblings.indexOf(index));
    for (int k = pos - 1; k >= 0; --k)
        if (!layers[siblings[k]].clipped) return siblings[k];
    return -1;
}

bool isClipped(const QList<Layer>& layers, int index) { return clipBase(layers, index) >= 0; }

int moveSubtree(QList<Layer>& layers, int index, int dest, quint64 parent)
{
    const int s = subtreeStart(layers, index);
    QList<Layer> block = layers.mid(s, index - s + 1);
    layers.remove(s, block.size());
    dest = std::clamp(dest, 0, int(layers.size()));
    block.last().parent = parent;
    for (int i = 0; i < block.size(); ++i) layers.insert(dest + i, block[i]);
    return dest + int(block.size()) - 1;
}

void renewIds(QList<Layer>& layers)
{
    QHash<quint64, quint64> ids;
    for (Layer& l : layers) {
        const quint64 old = l.id;
        l.renewIds();
        ids.insert(old, l.id);
    }
    for (Layer& l : layers) l.parent = ids.value(l.parent, l.parent);
}

} // namespace Tree
