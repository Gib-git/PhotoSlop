#include "core/Layer.h"

#include "core/LayerStyle.h"
#include "core/VectorLayers.h"

#include <QPainter>
#include <cstring>
#include <atomic>
#include <algorithm>
#include <iterator>

quint64 Layer::nextId()
{
    static std::atomic<quint64> counter{1};
    return counter++;
}

Layer Layer::create(const QString& name)
{
    Layer l;
    l.id = nextId();
    l.name = name;
    return l;
}

void Layer::ensureCovers(const QRect& canvasRect)
{
    if (canvasRect.isEmpty()) return;
    QRect cur = rect();
    if (cur.contains(canvasRect)) return;
    setGeometry(cur.isNull() ? canvasRect : cur.united(canvasRect));
}

void Layer::setGeometry(const QRect& canvasRect)
{
    if (canvasRect == rect()) return;
    if (canvasRect.isEmpty()) {
        image = QImage();
        offset = QPoint();
        return;
    }
    QImage next(canvasRect.size(), QImage::Format_ARGB32_Premultiplied);
    next.fill(Qt::transparent);
    QRect overlap = rect().intersected(canvasRect);
    if (!overlap.isEmpty()) {
        const int bytes = overlap.width() * 4;
        for (int y = overlap.top(); y <= overlap.bottom(); ++y) {
            const uchar* s = image.constScanLine(y - offset.y()) + (overlap.left() - offset.x()) * 4;
            uchar* d = next.scanLine(y - canvasRect.top()) + (overlap.left() - canvasRect.left()) * 4;
            memcpy(d, s, bytes);
        }
    }
    image = next;
    offset = canvasRect.topLeft();
}

QRgb Layer::pixelAt(const QPoint& canvasPt) const
{
    QPoint p = canvasPt - offset;
    if (image.isNull() || !image.rect().contains(p)) return 0;
    return reinterpret_cast<const QRgb*>(image.constScanLine(p.y()))[p.x()];
}

QImage Layer::toCanvasImage(const QSize& canvasSize) const
{
    QImage out(canvasSize, QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);
    if (!image.isNull()) {
        QPainter p(&out);
        p.setCompositionMode(QPainter::CompositionMode_Source);
        p.drawImage(offset, image);
    }
    return out;
}

void Layer::trimToContent()
{
    if (image.isNull()) return;
    int minX = image.width(), minY = image.height(), maxX = -1, maxY = -1;
    for (int y = 0; y < image.height(); ++y) {
        const QRgb* row = reinterpret_cast<const QRgb*>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            if (qAlpha(row[x])) {
                minX = std::min(minX, x);
                maxX = std::max(maxX, x);
                minY = std::min(minY, y);
                maxY = y;
            }
        }
    }
    if (maxX < 0) {
        image = QImage();
        offset = QPoint();
        return;
    }
    QRect r(minX, minY, maxX - minX + 1, maxY - minY + 1);
    if (r == image.rect()) return;
    image = image.copy(r);
    offset += r.topLeft();
}

bool Layer::hasStyle() const { return style && style->active(); }

int Layer::maskAt(const QPoint& canvasPt) const
{
    if (!mask || !maskEnabled) return 255;
    return maskValue(mask->pixelAt(canvasPt), maskDefault);
}

void Layer::maskRow(int y, int x0, int count, uint8_t* out) const
{
    if (!mask || !maskEnabled) {
        memset(out, 255, size_t(count));
        return;
    }
    const Layer& m = *mask;
    const int def = maskDefault;
    const int my = y - m.offset.y();
    if (m.image.isNull() || my < 0 || my >= m.image.height()) {
        memset(out, def, size_t(count));
        return;
    }
    const auto* row = reinterpret_cast<const QRgb*>(m.image.constScanLine(my));
    const int w = m.image.width();
    for (int i = 0; i < count; ++i) {
        const int mx = x0 + i - m.offset.x();
        out[i] = uint8_t(mx < 0 || mx >= w ? def : maskValue(row[mx], def));
    }
}

void Layer::translate(const QPoint& delta)
{
    if (delta.isNull()) return;
    offset += delta;
    if (mask && maskLinked) mask->offset += delta;
    if (isVector()) Vector::translateData(*this, QPointF(delta));
}

void Layer::renewIds()
{
    id = nextId();
    if (mask) mask->id = nextId();
}

bool sameLayer(const Layer& a, const Layer& b)
{
    if (a.id != b.id || a.image.cacheKey() != b.image.cacheKey() || a.offset != b.offset || a.visible != b.visible
        || a.opacity != b.opacity || a.fill != b.fill || a.mode != b.mode || a.isBackground != b.isBackground
        || a.kind != b.kind || a.parent != b.parent || a.clipped != b.clipped || a.adjustment != b.adjustment
        || a.style != b.style || a.text != b.text || a.shape != b.shape)
        return false;
    if (bool(a.mask) != bool(b.mask)) return false;
    if (a.mask
        && (a.maskDefault != b.maskDefault || a.maskEnabled != b.maskEnabled || !sameLayer(*a.mask, *b.mask)))
        return false;
    return true;
}
