#pragma once

#include "core/Document.h"
#include "core/Transform.h"
#include "tools/Tool.h"

#include <QPointer>
#include <QPolygonF>

class QAbstractButton;
class QDoubleSpinBox;
class QStackedWidget;

// Free Transform (Ctrl+T), Edit > Transform and Select > Transform Selection. Not in the
// toolbox: it takes over as a modal tool until Enter commits or Escape cancels.
class FreeTransformTool : public Tool {
    Q_OBJECT
public:
    enum class Mode { Free, Scale, Rotate, Skew, Distort, Perspective, Warp };

    using Tool::Tool;
    QString id() const override { return QStringLiteral("transform"); }
    QString name() const override { return QStringLiteral("Free Transform"); }
    QString iconName() const override { return QStringLiteral("tool-move"); }
    QChar shortcut() const override { return QChar(); }
    QWidget* createOptions(QWidget* parent) override;

    // Starts transforming the selected pixels of the target layer (the whole layer when
    // nothing is selected), or only the selection outline. Alerts and returns false when
    // that is not possible.
    bool begin(CanvasView* v, bool selectionOnly, Mode mode = Mode::Free);
    bool isActive() const { return !m_doc.isNull(); }
    void setMode(Mode mode);
    Mode mode() const { return m_mode; }
    // Rotate 180° / 90° and Flip, about the reference point.
    void rotateBy(double degrees, const QString& historyName);
    void flip(bool horizontal, const QString& historyName);
    // Edit > Transform > Again repeats the last committed transform.
    static bool hasLastTransform() { return s_hasLast; }
    void applyLastTransform();

    QPolygonF quad() const { return m_quad; }
    void setQuad(const QPolygonF& quad); // for numeric entry and tests
    const Xform::Patch& patch() const { return m_patch; }
    void setPatch(const Xform::Patch& patch);

    void mousePress(CanvasView* v, const ToolEvent& e) override;
    void mouseMove(CanvasView* v, const ToolEvent& e) override;
    void mouseRelease(CanvasView* v, const ToolEvent& e) override;
    void mouseDoubleClick(CanvasView* v, const ToolEvent& e) override;
    bool keyPress(CanvasView* v, QKeyEvent* e) override;
    void paintOverlay(QPainter& p, CanvasView* v) override;
    QCursor cursor(CanvasView* v, Qt::KeyboardModifiers mods) const override;
    bool commit(CanvasView* v) override;
    bool cancel(CanvasView* v) override;

private:
    enum class Hit { None, Move, Rotate, Corner, Edge, Pivot, WarpPoint, WarpSurface };
    struct HitResult {
        Hit hit = Hit::None;
        int index = -1;
    };

    QTransform xf() const; // source canvas coords -> transformed canvas coords
    QTransform srcToDest() const;
    QRectF srcRectF() const { return QRectF(m_srcRect); }
    QPointF handlePos(int handle) const; // 0 TL, 1 T, 2 TR, 3 R, 4 BR, 5 B, 6 BL, 7 L
    QPointF handleSource(int handle) const;
    HitResult hitTest(CanvasView* v, const QPointF& viewPos) const;
    void dragHandle(CanvasView* v, const ToolEvent& e);
    void dragWarp(CanvasView* v, const ToolEvent& e);
    bool acceptQuad(const QPolygonF& q);
    void beginPreview();
    void render();
    void end();
    QString historyName() const;
    void syncParams();
    void applyParams();
    void updateFields();

    // Session
    QPointer<Document> m_doc;
    QPointer<CanvasView> m_view;
    bool m_selectionOnly = false;
    bool m_vectorTarget = false; // a text or shape layer, re-rendered on commit
    Mode m_mode = Mode::Free;
    quint64 m_layerId = 0;
    DocState m_before;
    QImage m_src;        // floating pixels, premultiplied
    QRect m_srcRect;     // where they came from (canvas)
    QImage m_selSrc;     // selection inside m_srcRect, transformed along with the pixels
    QImage m_cleared;    // the layer with the floating pixels removed
    QRect m_clearedRect;
    QPolygonF m_quad;
    QPointF m_pivot; // reference point, in source coordinates
    bool m_warp = false;
    Xform::Patch m_patch;
    bool m_previewing = false;
    QString m_historyOverride;

    // Drag
    HitResult m_drag;
    HitResult m_hover;
    QPointF m_pressPos;
    QPolygonF m_pressQuad;
    Xform::Patch m_pressPatch;
    QTransform m_pressXf;
    QPointF m_pressPivot;
    double m_warpU = 0, m_warpV = 0;

    // Options
    Xform::Interp m_interp = Xform::Interp::Bicubic;
    bool m_link = true;
    bool m_showPivot = true;
    double m_px = 0, m_py = 0, m_sx = 1, m_sy = 1, m_angle = 0, m_skewH = 0, m_skewV = 0;
    QPointer<QDoubleSpinBox> m_fx, m_fy, m_fw, m_fh, m_fa, m_fsh, m_fsv;
    QPointer<QAbstractButton> m_warpButton;
    QPointer<QStackedWidget> m_optionPages;

    static inline QTransform s_last;
    static inline bool s_hasLast = false;
};
