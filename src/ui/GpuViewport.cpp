#include "ui/GpuViewport.h"

#include "core/Document.h"

#include <QGuiApplication>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QOpenGLPaintDevice>
#include <QOpenGLShaderProgram>
#include <QPainter>
#include <algorithm>

namespace {
constexpr int kTile = 1024;

const char* kVertex = R"(
attribute highp vec2 pos;
attribute highp vec2 uv;
varying highp vec2 v;
void main() { v = uv; gl_Position = vec4(pos, 0.0, 1.0); }
)";

const char* kFragment = R"(
varying highp vec2 v;
uniform sampler2D tex;
void main() { gl_FragColor = texture2D(tex, v); }
)";
} // namespace

namespace Gpu {

bool available(QString* description)
{
    static int state = -1;
    static QString renderer;
    if (state < 0) {
        state = 0;
        const QString platform = QGuiApplication::platformName();
        if (!qEnvironmentVariableIsSet("PHOTOSLOP_NO_GPU") && platform != QLatin1String("offscreen")
            && platform != QLatin1String("minimal")) {
            QOpenGLContext ctx;
            QOffscreenSurface surface;
            surface.create();
            // OpenGL 2.0 with shaders: older drivers (Windows' built-in GDI OpenGL 1.1) keep the
            // raster canvas.
            if (ctx.create() && ctx.makeCurrent(&surface)) {
                if ((ctx.isOpenGLES() || ctx.format().majorVersion() >= 2) && QOpenGLShaderProgram::hasOpenGLShaderPrograms(&ctx)) {
                    const auto* text = reinterpret_cast<const char*>(ctx.functions()->glGetString(GL_RENDERER));
                    renderer = text ? QString::fromLatin1(text) : QStringLiteral("OpenGL");
                    state = 1;
                }
                ctx.doneCurrent();
            }
        }
    }
    if (description) *description = state == 1 ? renderer : QString();
    return state == 1;
}

} // namespace Gpu

GpuViewport::GpuViewport(Document* doc, QWidget* parent)
    : QOpenGLWidget(parent)
    , m_doc(doc)
{
}

GpuViewport::~GpuViewport()
{
    if (context()) {
        makeCurrent();
        releaseTextures();
        delete m_program;
        m_program = nullptr;
        doneCurrent();
    }
}

void GpuViewport::paintGL()
{
    if (!m_paint) return;
    // Paint into the bound framebuffer directly: a QPainter on the widget itself would be
    // redirected while the window is grabbed, losing everything but the native drawing.
    const qreal dpr = devicePixelRatioF();
    QOpenGLPaintDevice device(size() * dpr);
    device.setDevicePixelRatio(dpr);
    QPainter p(&device);
    m_paint(p);
}

void GpuViewport::releaseTextures()
{
    QOpenGLContext* ctx = QOpenGLContext::currentContext();
    if (!ctx) return;
    QOpenGLFunctions* f = ctx->functions();
    for (Level& l : m_levels)
        for (Tile& t : l.tiles)
            if (t.texture) f->glDeleteTextures(1, &t.texture);
    m_levels.clear();
}

void GpuViewport::invalidate(const QRect& canvasRect)
{
    for (int i = 0; i < m_levels.size(); ++i) {
        Level& l = m_levels[i];
        QRect r = canvasRect.isNull() ? QRect(QPoint(), l.size)
                                      : QRect(QPoint(canvasRect.left() >> i, canvasRect.top() >> i),
                                              QPoint(canvasRect.right() >> i, canvasRect.bottom() >> i));
        for (Tile& t : l.tiles) {
            const QRect part = r & t.rect;
            if (!part.isEmpty()) t.dirty |= part;
        }
    }
}

bool GpuViewport::ensureProgram()
{
    if (m_program) return true;
    if (m_failed) return false;
    m_program = new QOpenGLShaderProgram;
    if (!m_program->addShaderFromSourceCode(QOpenGLShader::Vertex, kVertex)
        || !m_program->addShaderFromSourceCode(QOpenGLShader::Fragment, kFragment) || !m_program->link()) {
        delete m_program;
        m_program = nullptr;
        m_failed = true;
        return false;
    }
    return true;
}

bool GpuViewport::drawImage(const QRectF& target, const QRect& src, int level, bool smooth)
{
    QOpenGLContext* ctx = QOpenGLContext::currentContext();
    if (!m_doc || !ctx || !ensureProgram()) return false;
    QOpenGLFunctions* f = ctx->functions();
    const QImage& img = m_doc->pyramidLevel(level);
    if (img.isNull()) return true;

    // (Re)build the tile grid when the level appears or changes size.
    if (m_levels.size() <= level) m_levels.resize(level + 1);
    Level& L = m_levels[level];
    if (L.size != img.size()) {
        for (Tile& t : L.tiles)
            if (t.texture) f->glDeleteTextures(1, &t.texture);
        L.tiles.clear();
        L.size = img.size();
        for (int y = 0; y < img.height(); y += kTile)
            for (int x = 0; x < img.width(); x += kTile) {
                Tile t;
                t.rect = QRect(x, y, std::min(kTile, img.width() - x), std::min(kTile, img.height() - y));
                t.dirty = t.rect;
                L.tiles.append(t);
            }
    }

    const double scale = 1 << level;
    const QRectF srcL(src.x() / scale, src.y() / scale, src.width() / scale, src.height() / scale);
    const QRect need = srcL.toAlignedRect() & img.rect();
    if (need.isEmpty() || srcL.isEmpty()) return true;
    const double sx = target.width() / srcL.width(), sy = target.height() / srcL.height();
    const qreal dpr = devicePixelRatioF();
    const double fbw = width() * dpr, fbh = height() * dpr;

    f->glViewport(0, 0, int(fbw), int(fbh));
    f->glDisable(GL_SCISSOR_TEST);
    f->glDisable(GL_DEPTH_TEST);
    f->glDisable(GL_STENCIL_TEST);
    f->glEnable(GL_BLEND);
    // Premultiplied pixels over the checkerboard.
    f->glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    f->glBindBuffer(GL_ARRAY_BUFFER, 0);
    f->glActiveTexture(GL_TEXTURE0);
    m_program->bind();
    m_program->setUniformValue("tex", 0);
    const int posLoc = m_program->attributeLocation("pos");
    const int uvLoc = m_program->attributeLocation("uv");
    m_program->enableAttributeArray(posLoc);
    m_program->enableAttributeArray(uvLoc);

    for (Tile& t : L.tiles) {
        const QRect part = t.rect & need;
        if (part.isEmpty()) continue;
        if (!t.texture) {
            f->glGenTextures(1, &t.texture);
            f->glBindTexture(GL_TEXTURE_2D, t.texture);
            f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            f->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, t.rect.width(), t.rect.height(), 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            t.dirty = t.rect;
        } else {
            f->glBindTexture(GL_TEXTURE_2D, t.texture);
        }
        if (!t.dirty.isEmpty()) {
            const QImage up = img.copy(t.dirty).convertToFormat(QImage::Format_RGBA8888_Premultiplied);
            f->glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
            f->glTexSubImage2D(GL_TEXTURE_2D, 0, t.dirty.x() - t.rect.x(), t.dirty.y() - t.rect.y(), up.width(), up.height(),
                               GL_RGBA, GL_UNSIGNED_BYTE, up.constBits());
            t.dirty = QRect();
        }
        const GLint filter = smooth ? GL_LINEAR : GL_NEAREST;
        f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
        f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);

        // Corners in framebuffer pixels, then normalized device coordinates.
        const double x0 = (target.x() + (part.left() - srcL.x()) * sx) * dpr;
        const double x1 = (target.x() + (part.left() + part.width() - srcL.x()) * sx) * dpr;
        const double y0 = (target.y() + (part.top() - srcL.y()) * sy) * dpr;
        const double y1 = (target.y() + (part.top() + part.height() - srcL.y()) * sy) * dpr;
        auto nx = [fbw](double x) { return GLfloat(2.0 * x / fbw - 1.0); };
        auto ny = [fbh](double y) { return GLfloat(1.0 - 2.0 * y / fbh); };
        const GLfloat u0 = GLfloat(double(part.left() - t.rect.left()) / t.rect.width());
        const GLfloat u1 = GLfloat(double(part.left() + part.width() - t.rect.left()) / t.rect.width());
        const GLfloat v0 = GLfloat(double(part.top() - t.rect.top()) / t.rect.height());
        const GLfloat v1 = GLfloat(double(part.top() + part.height() - t.rect.top()) / t.rect.height());
        const GLfloat pos[] = {nx(x0), ny(y0), nx(x1), ny(y0), nx(x0), ny(y1), nx(x1), ny(y1)};
        const GLfloat uv[] = {u0, v0, u1, v0, u0, v1, u1, v1};
        m_program->setAttributeArray(posLoc, pos, 2);
        m_program->setAttributeArray(uvLoc, uv, 2);
        f->glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }
    m_program->disableAttributeArray(posLoc);
    m_program->disableAttributeArray(uvLoc);
    m_program->release();
    f->glBindTexture(GL_TEXTURE_2D, 0);
    return true;
}
