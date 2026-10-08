#pragma once

#include "core/Layer.h"

#include <QImage>
#include <QList>
#include <QObject>
#include <QVector>
#include <QLine>

class QUndoStack;

// A ruler guide. Horizontal guides sit at a y position, vertical guides at an x position
// (canvas pixels).
struct Guide {
    Qt::Orientation orientation = Qt::Horizontal;
    double position = 0.0;
    bool operator==(const Guide& o) const { return orientation == o.orientation && position == o.position; }
};

// Everything needed to restore a document for undo. Images are implicitly
// shared, so taking a snapshot is cheap.
struct DocState {
    QSize size;
    double dpi = 72.0;
    QList<Layer> layers;
    int active = 0;
    QImage selection;
    QList<Guide> guides;
    bool quickMask = false;
    Layer quickMaskLayer; // opaque grey: white = selected
};

class Document : public QObject {
    Q_OBJECT
public:
    explicit Document(const QSize& size, QObject* parent = nullptr);
    ~Document() override;

    QSize size() const { return m_size; }
    QRect bounds() const { return QRect(QPoint(), m_size); }
    int width() const { return m_size.width(); }
    int height() const { return m_size.height(); }
    double dpi() const { return m_dpi; }
    void setDpi(double dpi) { m_dpi = dpi; }
    // Changes the canvas size without touching layers; call inside modify().
    void setSizeRaw(const QSize& size) { m_size = size; }

    QString title() const { return m_title; }
    void setTitle(const QString& title);
    QString filePath() const { return m_filePath; }
    void setFilePath(const QString& path) { m_filePath = path; }
    bool isModified() const;
    void setClean();

    // ---- Layers (index 0 is the bottom of the stack) ----
    int layerCount() const { return int(m_layers.size()); }
    const QList<Layer>& layers() const { return m_layers; }
    // `index` may also be kQuickMaskIndex or a maskIndex().
    const Layer& layerAt(int index) const;
    Layer& layerRef(int index); // direct, un-undoable access
    QList<Layer>& layersRef() { return m_layers; }
    int activeIndex() const { return m_active; }
    void setActiveIndex(int index);
    Layer* activeLayer();
    int indexOfId(quint64 id) const;
    QString nextLayerName();
    // "Group 1", "Levels 1"...: the first free name with this prefix.
    QString nextName(const QString& prefix) const;
    bool hasBackground() const { return !m_layers.isEmpty() && m_layers.first().isBackground; }

    // ---- Edit target ----
    // Painting and pixel commands go to the Quick Mask while it is on, then to the active
    // layer's mask when it is targeted (adjustment layers always target theirs), otherwise
    // to the active layer.
    static constexpr int kQuickMaskIndex = -2;
    static constexpr int kMaskIndexBase = -1000;
    static int maskIndex(int layerIndex) { return kMaskIndexBase - layerIndex; }
    static bool isMaskIndex(int index) { return index <= kMaskIndexBase; }
    static int maskOwner(int index) { return kMaskIndexBase - index; }
    int editIndex() const;
    Layer* editLayer();
    // True when painting goes to the active layer's mask.
    bool editingMask() const { return isMaskIndex(editIndex()); }
    // Targets the active layer's mask (when it has one) or its pixels.
    void setMaskTargeted(bool on);
    bool maskTargeted() const { return m_maskTarget; }

    // ---- History Brush source ----
    // The state the History Brush paints from: the document as opened, unless the History
    // panel picked another state.
    const DocState& historySource() const { return m_historySource; }
    void setHistorySource(const DocState& s) { m_historySource = s; }
    // The document as it was after `undoIndex` history steps (0 = as opened).
    DocState stateAt(int undoIndex);

    // ---- Quick Mask ----
    bool inQuickMask() const { return m_quickMask; }
    // Red overlay over masked (unselected) areas, canvas sized; null when Quick Mask is off.
    const QImage& quickMaskOverlay();
    // Switches Quick Mask on or off without recording history; call inside modify().
    void setQuickMaskRaw(bool on, const Layer& maskLayer = Layer());
    const Layer& quickMaskLayer() const { return m_qmLayer; }

    // ---- Guides ----
    const QList<Guide>& guides() const { return m_guides; }
    void setGuidesRaw(const QList<Guide>& guides);
    void changeGuides(const QList<Guide>& guides, const QString& undoText);

    // ---- Selection ----
    const QImage& selection() const { return m_selection; }
    bool hasSelection() const { return !m_selection.isNull(); }
    QRect selectionBounds() const;
    const QVector<QLine>& selectionEdges() const;
    void setSelectionRaw(const QImage& mask);
    void changeSelection(const QImage& mask, const QString& undoText);
    const QImage& lastSelection() const { return m_lastSelection; }

    // ---- Compositing ----
    void invalidate(const QRect& canvasRect = QRect());
    void flush();
    const QImage& composite();
    int pyramidLevelCount() const { return int(m_pyramid.size()) + 1; }
    const QImage& pyramidLevel(int level); // 0 = full resolution
    QRgb compositePixel(const QPoint& pt);

    // ---- Undo ----
    QUndoStack* undoStack() const { return m_undo; }
    DocState state() const;
    void restoreState(const DocState& s);
    void pushSnapshot(const QString& text, const DocState& before, int mergeId = -1);
    template <typename F>
    void modify(const QString& text, F&& fn, int mergeId = -1)
    {
        DocState before = state();
        fn();
        pushSnapshot(text, before, mergeId);
    }

    // Notifications for code that edits layers directly.
    void notifyLayersChanged();
    void notifyLayerPixels(int index, const QRect& canvasRect);

    // Restores a full state and replaces the history with a fresh one (used by loaders).
    void initialize(const DocState& s);

signals:
    void imageChanged(const QRect& canvasRect);
    void layersChanged();
    void layerPixelsChanged(int index);
    void activeLayerChanged();
    void editTargetChanged();
    void selectionChanged();
    void guidesChanged();
    void quickMaskChanged();
    void sizeChanged();
    void modifiedChanged();
    void titleChanged();

private:
    void recomposite(const QRect& r);
    void updateQuickMaskOverlay(const QRect& r);
    void restoreQuickMask(bool on, const Layer& maskLayer);
    void updatePyramid(const QRect& r);
    void reallocate();

    QSize m_size;
    double m_dpi = 72.0;
    QString m_title;
    QString m_filePath;
    QList<Layer> m_layers;
    int m_active = 0;
    bool m_maskTarget = false;
    DocState m_historySource;
    int m_layerCounter = 0;

    QImage m_selection;
    QImage m_lastSelection;
    mutable bool m_selCacheValid = false;
    mutable QRect m_selBounds;
    mutable QVector<QLine> m_selEdges;

    QList<Guide> m_guides;
    bool m_quickMask = false;
    Layer m_qmLayer;
    QImage m_qmOverlay;

    QImage m_composite;
    QVector<QImage> m_pyramid; // levels 1..n
    QRect m_dirty;
    bool m_flushScheduled = false;

    QUndoStack* m_undo = nullptr;
};
