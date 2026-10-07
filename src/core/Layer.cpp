#include "core/Layer.h"

#include <QPainter>
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

bool sameLayer(const Layer& a, const Layer& b)
{
    return a.id == b.id && a.image.cacheKey() == b.image.cacheKey() && a.offset == b.offset
        && a.visible == b.visible && a.opacity == b.opacity && a.fill == b.fill
        && a.mode == b.mode && a.isBackground == b.isBackground;
}
