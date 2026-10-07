#pragma once

#include <QImage>
#include <QPoint>
#include <QRect>
#include <QString>
#include <atomic>
#include <functional>
#include <memory>

class Document;
class PixelEdit;

// Image filters (Filter menu) and the machinery that applies them, and the colour
// adjustments, to the edit layer inside the selection.
namespace Filters {

// Set from another thread to stop a running filter early; its result is then discarded.
using CancelFlag = std::atomic<bool>;

inline bool cancelled(const CancelFlag* c) { return c && c->load(std::memory_order_relaxed); }

// Runs fn(begin, end) over [0, count) in parallel chunks. Returns false if cancelled.
bool parallelFor(int count, const std::function<void(int begin, int end)>& fn, const CancelFlag* cancel = nullptr);

// A filter maps a premultiplied ARGB32 image to a result of the same size, or returns a null
// image when cancelled. `origin` is the canvas position of the image's top-left corner, for
// effects aligned to the canvas (Mosaic, noise).
using Fn = std::function<QImage(const QImage& src, const QPoint& origin, const CancelFlag* cancel)>;

struct Spec {
    QString name;          // history and Last Filter text, e.g. "Gaussian Blur"
    Fn fn;
    int margin = 0;        // how far beyond the changed area the filter reads
    bool spreads = false;  // can make transparent pixels opaque (blurs), so it may grow the layer
};

// ---- Filters: premultiplied ARGB32 in and out, edge pixels repeat ----
QImage gaussianBlur(const QImage& src, double radius, const CancelFlag* cancel = nullptr);
QImage boxBlur(const QImage& src, int radius, const CancelFlag* cancel = nullptr);
// `angle` in degrees, counter-clockwise from the x axis; `distance` is the streak length in pixels.
QImage motionBlur(const QImage& src, double angle, int distance, const CancelFlag* cancel = nullptr);
// `amount` in percent; channels that differ from the blur by less than `threshold` are left alone.
QImage unsharpMask(const QImage& src, double amount, double radius, int threshold, const CancelFlag* cancel = nullptr);
QImage highPass(const QImage& src, double radius, const CancelFlag* cancel = nullptr);
// `amount` in percent. The noise depends only on the canvas position and `seed`, so a preview
// and the final result match.
QImage addNoise(const QImage& src, double amount, bool gaussian, bool monochromatic, const QPoint& origin,
                quint32 seed, const CancelFlag* cancel = nullptr);
QImage median(const QImage& src, int radius, const CancelFlag* cancel = nullptr);
// Square cells of `cellSize` pixels, aligned to the canvas origin.
QImage mosaic(const QImage& src, int cellSize, const QPoint& origin, const CancelFlag* cancel = nullptr);

// Specs for the Filter menu.
Spec gaussianBlurSpec(double radius);
Spec boxBlurSpec(int radius);
Spec motionBlurSpec(double angle, int distance);
Spec blurSpec();
Spec blurMoreSpec();
Spec unsharpMaskSpec(double amount, double radius, int threshold);
Spec sharpenSpec();
Spec sharpenMoreSpec();
Spec highPassSpec(double radius);
Spec addNoiseSpec(double amount, bool gaussian, bool monochromatic, quint32 seed);
Spec medianSpec(int radius);
Spec mosaicSpec(int cellSize);

// Edits the edit layer (or the Quick Mask) inside the selection: holds the original pixels,
// renders specs against them (on any thread), shows results on the canvas, and commits one
// undo step. Destroying an uncommitted session restores the original pixels.
class Session {
public:
    // Everything a worker thread needs. Images are implicitly shared and only read.
    struct Input {
        QImage layer;          // original pixels of the edit layer
        QPoint layerOffset;
        QRect canvas;
        QRect target;          // canvas area that changes
        QImage selection;      // canvas-sized coverage, null = everything
        bool keepAlpha = false; // transparency locked
    };

    // `target` overrides the changed area (used by Fade, which must match the faded command).
    Session(Document* doc, const QString& name, bool spreads, const QRect& target = QRect());
    ~Session();

    bool isValid() const { return m_edit != nullptr; }
    quint64 layerId() const { return m_layerId; }
    QString error() const { return m_error; }
    QRect target() const { return m_input.target; }
    const Input& input() const { return m_input; }
    // Original pixels of the target area, and the selection coverage over it (null = all).
    QImage originalTarget() const;
    QImage selectionTarget() const;

    // Target-sized final pixels: the spec's output limited by the selection and transparency
    // lock. Thread-safe. Null when cancelled.
    static QImage render(const Input& input, const Spec& spec, const CancelFlag* cancel = nullptr);

    // Fade blends with the faded command's own pixels, so the selection must not apply twice.
    void ignoreSelection() { m_input.selection = QImage(); }

    void show(const QImage& result);
    void showOriginal();
    void commit(const QString& name);
    QImage shownResult() const { return m_shown; }

private:
    std::unique_ptr<PixelEdit> m_edit;
    Input m_input;
    QString m_error;
    QImage m_shown;
    quint64 m_layerId = 0;
};

// What a committed filter changed, for Edit > Fade.
struct Applied {
    quint64 layerId = 0;
    QRect target;
    QImage before;
    QImage after;
};

// Applies a spec at once (Last Filter, Invert, Auto Tone...). Returns false with an alert
// message in `error` when the edit layer cannot be changed.
bool apply(Document* doc, const Spec& spec, QString* error = nullptr, Applied* applied = nullptr);

// Edit > Fade: blends `after` back over `before` with `mode` at `opacity` (0..1).
Spec fadeSpec(const QString& name, const QImage& before, int mode, double opacity);

} // namespace Filters
