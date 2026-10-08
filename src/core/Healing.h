#pragma once

#include <QImage>
#include <QPoint>
#include <QRect>
#include <vector>

// The maths behind the Healing Brush and Spot Healing Brush.
namespace Heal {

// Blends `source` into `target` where `region` (one byte per pixel) is non-zero: the result
// keeps the source's texture but takes on the target's colours at the region's edge
// (a Poisson blend). Both images are premultiplied ARGB32 of the same size.
QImage blend(const QImage& target, const QImage& source, const std::vector<uint8_t>& region);

// Offset (source minus destination) to nearby texture whose surroundings best match the
// pixels around `region` (covering `regionRect`). `image` covers canvas rect `imageRect`.
QPoint findSource(const QImage& image, const QRect& imageRect, const std::vector<uint8_t>& region,
                  const QRect& regionRect);

} // namespace Heal
