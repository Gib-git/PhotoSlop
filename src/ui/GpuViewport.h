#pragma once

#include <QList>
#include <QOpenGLWidget>
#include <QPointer>
#include <QRect>
#include <functional>

class Document;
class QOpenGLShaderProgram;
class QPainter;

namespace Gpu {
// True when an OpenGL context can be created; `description` gets the renderer name.
bool available(QString* description = nullptr);
} // namespace Gpu

// The OpenGL viewport CanvasView uses when Preferences > Performance > Use Graphics Processor
// is on. The document's image pyramid lives in tiled textures; only the parts that changed are
// uploaded again. Everything else on the canvas is still drawn with QPainter.
class GpuViewport : public QOpenGLWidget {
    Q_OBJECT
public:
    explicit GpuViewport(Document* doc, QWidget* parent = nullptr);
    ~GpuViewport() override;

    // Marks a canvas area as changed in every pyramid level.
    void invalidate(const QRect& canvasRect = QRect());
    // Draws canvas rectangle `src` from pyramid `level` into `target` (logical viewport
    // coordinates). Call inside QPainter::beginNativePainting(). False when OpenGL failed, so the
    // caller can fall back to QPainter.
    bool drawImage(const QRectF& target, const QRect& src, int level, bool smooth);
    // Paints the whole canvas; called from paintGL(), so grabs and repaints go through it.
    void setPainter(std::function<void(QPainter&)> paint) { m_paint = std::move(paint); }

protected:
    void paintGL() override;

private:
    struct Tile {
        unsigned int texture = 0;
        QRect rect;  // in level pixels
        QRect dirty; // still to upload
    };
    struct Level {
        QSize size;
        QList<Tile> tiles;
    };
    void releaseTextures();
    bool ensureProgram();

    QPointer<Document> m_doc;
    QList<Level> m_levels;
    QOpenGLShaderProgram* m_program = nullptr;
    bool m_failed = false;
    std::function<void(QPainter&)> m_paint;
};
