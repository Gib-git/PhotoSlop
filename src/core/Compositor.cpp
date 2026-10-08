#include "core/Compositor.h"

#include "core/Adjustments.h"
#include "core/Filters.h"
#include "core/LayerStyle.h"

#include <QHash>
#include <QtMath>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

namespace Compositor {

namespace {

constexpr int kHuge = 1 << 28;
const QRect kEverywhere(QPoint(-kHuge, -kHuge), QPoint(kHuge, kHuge));

// A zeroed pixel buffer with a surface over it.
struct Buffer {
    std::vector<uint32_t> px;
    Surface s;
    explicit Buffer(const QRect& area)
        : px(size_t(std::max(0, area.width())) * size_t(std::max(0, area.height())), 0)
    {
        s = {px.data(), area.width(), area};
    }
};

// Parent/child links and extents, computed once per render.
struct Tree {
    const QList<Layer>& L;
    std::vector<QList<int>> kids; // per group; the last entry is the top level
    std::vector<QRect> bounds;

    explicit Tree(const QList<Layer>& layers)
        : L(layers)
        , kids(size_t(layers.size()) + 1)
        , bounds(size_t(layers.size()))
    {
        const int n = int(layers.size());
        QHash<quint64, int> byId;
        for (int i = 0; i < n; ++i) byId.insert(layers[i].id, i);
        for (int i = 0; i < n; ++i) {
            const int p = layers[i].parent ? byId.value(layers[i].parent, -1) : -1;
            const bool valid = p > i && layers[p].isGroup();
            kids[valid ? size_t(p) : size_t(n)].append(i); // ascending: bottom to top
        }
        std::vector<char> done(size_t(n), 0);
        for (int i = 0; i < n; ++i) extentOf(i, done);
    }

    const QList<int>& top() const { return kids.back(); }

    QRect extentOf(int i, std::vector<char>& done)
    {
        if (done[size_t(i)]) return bounds[size_t(i)];
        done[size_t(i)] = 1;
        const Layer& l = L[i];
        QRect r;
        switch (l.kind) {
        case LayerKind::Adjustment: r = kEverywhere; break;
        case LayerKind::Group:
            for (int k : kids[size_t(i)])
                if (L[k].visible) r |= extentOf(k, done);
            break;
        default:
            r = l.rect();
            if (!r.isEmpty() && l.hasStyle()) {
                const int m = l.style->margin();
                r.adjust(-m, -m, m, m);
            }
            break;
        }
        bounds[size_t(i)] = r;
        return r;
    }
};

inline uint32_t premulColor(const QColor& c) { return qPremultiply(c.rgba()); }

void copyRow(uint32_t* d, const uint32_t* s, int n) { memcpy(d, s, size_t(n) * 4); }

// Layer pixels times its mask, written into `dst` (which must be zero beforehand).
void copyMaskedContent(const Layer& l, const Surface& dst)
{
    const QRect r = l.rect() & dst.area;
    if (r.isEmpty()) return;
    const bool masked = l.hasMask() && l.maskEnabled;
    std::vector<uint8_t> m(size_t(r.width()));
    for (int y = r.top(); y <= r.bottom(); ++y) {
        auto* d = dst.row(y) + (r.left() - dst.area.left());
        auto* s = reinterpret_cast<const uint32_t*>(l.image.constScanLine(y - l.offset.y())) + (r.left() - l.offset.x());
        if (!masked) {
            copyRow(d, s, r.width());
            continue;
        }
        l.maskRow(y, r.left(), r.width(), m.data());
        for (int x = 0; x < r.width(); ++x) d[x] = m[size_t(x)] == 255 ? s[x] : Blend::byteMul(s[x], m[size_t(x)]);
    }
}

// Multiplies a surface by a layer's mask.
void applyMask(const Layer& l, const Surface& s)
{
    if (!l.hasMask() || !l.maskEnabled) return;
    const int w = s.area.width();
    std::vector<uint8_t> m(static_cast<size_t>(w));
    for (int y = s.area.top(); y <= s.area.bottom(); ++y) {
        l.maskRow(y, s.area.left(), w, m.data());
        uint32_t* d = s.row(y);
        for (int x = 0; x < w; ++x)
            if (m[size_t(x)] != 255) d[x] = Blend::byteMul(d[x], m[size_t(x)]);
    }
}

// Composites `src` (covering at least `r`) onto `dst` within `r`.
void compositeSurface(const Surface& dst, const Surface& src, const QRect& r, BlendMode mode, float opacity,
                      const Layer* maskOwner = nullptr)
{
    if (r.isEmpty() || opacity <= 0.f) return;
    std::vector<uint8_t> m(size_t(r.width()));
    const bool masked = maskOwner && maskOwner->hasMask() && maskOwner->maskEnabled;
    for (int y = r.top(); y <= r.bottom(); ++y) {
        if (masked) maskOwner->maskRow(y, r.left(), r.width(), m.data());
        Blend::compositeRow(dst.row(y) + (r.left() - dst.area.left()), src.row(y) + (r.left() - src.area.left()), r.width(),
                            mode == BlendMode::PassThrough ? BlendMode::Normal : mode, opacity,
                            masked ? m.data() : nullptr, r.left(), y);
    }
}

// ---------------- Effects ----------------

// Three box blurs approximate a Gaussian whose visible reach is about `size` pixels.
void blurPlane(std::vector<uint8_t>& a, int w, int h, int size)
{
    if (size <= 0 || w <= 0 || h <= 0) return;
    const int r = std::max(1, int(std::ceil(size / 3.0)));
    std::vector<uint8_t> tmp(std::max(w, h));
    const int div = 2 * r + 1;
    for (int pass = 0; pass < 3; ++pass) {
        for (int y = 0; y < h; ++y) {
            uint8_t* row = a.data() + size_t(y) * size_t(w);
            int sum = 0;
            for (int x = -r; x <= r; ++x) sum += (x >= 0 && x < w) ? row[x] : 0;
            for (int x = 0; x < w; ++x) {
                tmp[size_t(x)] = uint8_t((sum + div / 2) / div);
                const int out = x - r, in = x + r + 1;
                sum += (in < w ? row[in] : 0) - (out >= 0 ? row[out] : 0);
            }
            memcpy(row, tmp.data(), size_t(w));
        }
        for (int x = 0; x < w; ++x) {
            int sum = 0;
            for (int y = -r; y <= r; ++y) sum += (y >= 0 && y < h) ? a[size_t(y) * size_t(w) + size_t(x)] : 0;
            for (int y = 0; y < h; ++y) {
                tmp[size_t(y)] = uint8_t((sum + div / 2) / div);
                const int out = y - r, in = y + r + 1;
                sum += (in < h ? a[size_t(in) * size_t(w) + size_t(x)] : 0) - (out >= 0 ? a[size_t(out) * size_t(w) + size_t(x)] : 0);
            }
            for (int y = 0; y < h; ++y) a[size_t(y) * size_t(w) + size_t(x)] = tmp[size_t(y)];
        }
    }
}

// Spread / choke: scales coverage up so the matte grows before it fades.
void amplify(std::vector<uint8_t>& a, double factor)
{
    if (factor <= 1.0) return;
    for (uint8_t& v : a) v = uint8_t(std::min(255.0, v * factor));
}

// 1D squared distance transform (Felzenszwalb & Huttenlocher) of f into d.
void edt1d(const float* f, float* d, int n, std::vector<int>& v, std::vector<float>& z)
{
    constexpr float kInf = std::numeric_limits<float>::max();
    int k = 0;
    v[0] = 0;
    z[0] = -kInf;
    z[1] = kInf;
    for (int q = 1; q < n; ++q) {
        float s;
        for (;;) {
            const int p = v[size_t(k)];
            s = ((f[q] + float(q) * q) - (f[p] + float(p) * p)) / (2.f * (q - p));
            if (s <= z[size_t(k)] && k > 0) {
                --k;
                continue;
            }
            break;
        }
        if (s <= z[size_t(k)]) { // k == 0
            v[0] = q;
            z[0] = -kInf;
            z[1] = kInf;
            continue;
        }
        ++k;
        v[size_t(k)] = q;
        z[size_t(k)] = s;
        z[size_t(k) + 1] = kInf;
    }
    k = 0;
    for (int q = 0; q < n; ++q) {
        while (z[size_t(k) + 1] < q) ++k;
        const int p = v[size_t(k)];
        d[q] = float(q - p) * float(q - p) + f[p];
    }
}

// Squared distance from every pixel to the nearest pixel where `inside` is true.
std::vector<float> distanceTo(const std::vector<uint8_t>& inside, int w, int h)
{
    constexpr float kFar = 1e12f;
    std::vector<float> g(size_t(w) * size_t(h));
    for (size_t i = 0; i < g.size(); ++i) g[i] = inside[i] ? 0.f : kFar;
    const int n = std::max(w, h);
    std::vector<float> f(static_cast<size_t>(n)), d(static_cast<size_t>(n));
    std::vector<int> v(static_cast<size_t>(n));
    std::vector<float> z(size_t(n) + 1);
    for (int x = 0; x < w; ++x) {
        for (int y = 0; y < h; ++y) f[size_t(y)] = g[size_t(y) * size_t(w) + size_t(x)];
        edt1d(f.data(), d.data(), h, v, z);
        for (int y = 0; y < h; ++y) g[size_t(y) * size_t(w) + size_t(x)] = d[size_t(y)];
    }
    for (int y = 0; y < h; ++y) {
        float* row = g.data() + size_t(y) * size_t(w);
        edt1d(row, d.data(), w, v, z);
        memcpy(row, d.data(), size_t(w) * sizeof(float));
    }
    return g;
}

// Composites a solid colour through a coverage plane covering `plane` onto `dst` within `r`.
void compositeColor(const Surface& dst, const QRect& r, const std::vector<uint8_t>& cov, const QRect& plane,
                    const QColor& color, BlendMode mode, float opacity)
{
    if (r.isEmpty() || opacity <= 0.f) return;
    std::vector<uint32_t> src(size_t(r.width()), premulColor(color));
    for (int y = r.top(); y <= r.bottom(); ++y) {
        const uint8_t* c = cov.data() + size_t(y - plane.top()) * size_t(plane.width()) + size_t(r.left() - plane.left());
        Blend::compositeRow(dst.row(y) + (r.left() - dst.area.left()), src.data(), r.width(), mode, opacity, c, r.left(), y);
    }
}

// Composites layer content (mask applied, covering at least `r` plus the style margin) and
// the layer's effects onto `dst` within `r`.
void finishContent(const Layer& l, const Surface& content, const Surface& dst, const QRect& r)
{
    const float contentOpacity = l.opacity * l.fill;
    const LayerStyle* st = l.hasStyle() ? l.style.get() : nullptr;
    if (!st) {
        compositeSurface(dst, content, r, l.mode, contentOpacity);
        return;
    }
    const QRect E = content.area;
    const int W = E.width(), H = E.height();
    const size_t N = size_t(W) * size_t(H);
    std::vector<uint8_t> A(N);
    for (int y = 0; y < H; ++y) {
        const uint32_t* s = content.row(E.top() + y);
        uint8_t* a = A.data() + size_t(y) * size_t(W);
        for (int x = 0; x < W; ++x) a[x] = uint8_t(s[x] >> 24);
    }
    const float layerOpacity = l.opacity;

    if (st->dropShadow.enabled) {
        const DropShadow& ds = st->dropShadow;
        const double rad = ds.angle * M_PI / 180.0;
        const int dx = int(std::lround(-std::cos(rad) * ds.distance));
        const int dy = int(std::lround(std::sin(rad) * ds.distance));
        std::vector<uint8_t> S(N, 0);
        for (int y = 0; y < H; ++y) {
            const int sy = y - dy;
            if (sy < 0 || sy >= H) continue;
            for (int x = 0; x < W; ++x) {
                const int sx = x - dx;
                if (sx >= 0 && sx < W) S[size_t(y) * size_t(W) + size_t(x)] = A[size_t(sy) * size_t(W) + size_t(sx)];
            }
        }
        blurPlane(S, W, H, ds.size);
        if (ds.spread > 0) amplify(S, ds.spread >= 100 ? 255.0 : 100.0 / (100.0 - ds.spread));
        compositeColor(dst, r, S, E, ds.color, ds.mode, ds.opacity / 100.f * layerOpacity);
    }
    if (st->outerGlow.enabled) {
        const OuterGlow& og = st->outerGlow;
        std::vector<uint8_t> G = A;
        blurPlane(G, W, H, og.size);
        // Full strength at the edge, fading out over the size.
        amplify(G, og.size > 0 ? 2.0 : 1.0);
        if (og.spread > 0) amplify(G, og.spread >= 100 ? 255.0 : 100.0 / (100.0 - og.spread));
        compositeColor(dst, r, G, E, og.color, og.mode, og.opacity / 100.f * layerOpacity);
    }

    compositeSurface(dst, content, r, l.mode, contentOpacity);

    if (st->colorOverlay.enabled) {
        const ColorOverlay& co = st->colorOverlay;
        compositeColor(dst, r, A, E, co.color, co.mode, co.opacity / 100.f * layerOpacity);
    }
    if (st->stroke.enabled && st->stroke.size > 0) {
        const StrokeEffect& sk = st->stroke;
        std::vector<uint8_t> inside(N), outside(N);
        for (size_t i = 0; i < N; ++i) {
            inside[i] = A[i] >= 128;
            outside[i] = !inside[i];
        }
        const bool wantOut = sk.position != StrokeEffect::Position::Inside;
        const bool wantIn = sk.position != StrokeEffect::Position::Outside;
        const double reach = sk.position == StrokeEffect::Position::Center ? sk.size / 2.0 : sk.size;
        std::vector<float> dOut, dIn;
        if (wantOut) dOut = distanceTo(inside, W, H);
        if (wantIn) dIn = distanceTo(outside, W, H);
        std::vector<uint8_t> K(N, 0);
        for (size_t i = 0; i < N; ++i) {
            double k = 0.0;
            if (inside[i]) {
                if (wantOut) k = (255 - A[i]) / 255.0; // antialiased edge pixels
                if (wantIn) k = std::max(k, std::clamp(reach + 1.0 - std::sqrt(dIn[i]), 0.0, 1.0) * A[i] / 255.0);
            } else if (wantOut) {
                k = std::clamp(reach + 1.0 - std::sqrt(dOut[i]), 0.0, 1.0);
            }
            K[i] = uint8_t(std::lround(k * 255.0));
        }
        compositeColor(dst, r, K, E, sk.color, sk.mode, sk.opacity / 100.f * layerOpacity);
    }
}

void renderList(const Tree& t, const QList<int>& list, const Surface& dst);
void drawLayer(const Tree& t, int i, const Surface& dst);

// Applies an adjustment layer to `dst` within `r`; alpha is never changed.
void drawAdjustment(const Layer& l, const Surface& dst, const QRect& r)
{
    if (!l.adjustment || !l.adjustment->map || r.isEmpty()) return;
    const float op = std::clamp(l.opacity * l.fill, 0.f, 1.f);
    if (op <= 0.f) return;
    const uint32_t op255 = uint32_t(std::lround(op * 255.f));
    const int w = r.width();
    std::vector<QRgb> tmp(static_cast<size_t>(w));
    std::vector<uint32_t> opaque(static_cast<size_t>(w));
    std::vector<uint8_t> m(size_t(w), 255);
    const bool masked = l.hasMask() && l.maskEnabled;
    const bool normal = l.mode == BlendMode::Normal || l.mode == BlendMode::PassThrough;
    for (int y = r.top(); y <= r.bottom(); ++y) {
        uint32_t* d = dst.row(y) + (r.left() - dst.area.left());
        for (int x = 0; x < w; ++x) tmp[size_t(x)] = qUnpremultiply(d[x]);
        l.adjustment->map(tmp.data(), w);
        if (masked) l.maskRow(y, r.left(), w, m.data());
        if (normal) {
            for (int x = 0; x < w; ++x) {
                const uint32_t a = d[x] >> 24;
                if (!a) continue;
                const uint32_t cov = (m[size_t(x)] * op255 + 127) / 255;
                if (!cov) continue;
                const uint32_t adj = qPremultiply((tmp[size_t(x)] & 0x00ffffffu) | (a << 24));
                d[x] = cov == 255 ? adj : Blend::byteMul(adj, cov) + Blend::byteMul(d[x], 255 - cov);
            }
        } else {
            for (int x = 0; x < w; ++x) opaque[size_t(x)] = tmp[size_t(x)] | 0xff000000u;
            Blend::compositeRowPreserveAlpha(d, opaque.data(), w, l.mode, op, masked ? m.data() : nullptr, r.left(), y);
        }
    }
}

// A group's contents blended on their own, with the group's mask applied.
void renderGroupIsolated(const Tree& t, int i, const Surface& out)
{
    renderList(t, t.kids[size_t(i)], out);
    applyMask(t.L[i], out);
}

void drawGroup(const Tree& t, int i, const Surface& dst, const QRect& r)
{
    const Layer& g = t.L[i];
    const float op = std::clamp(g.opacity * g.fill, 0.f, 1.f);
    if (g.mode == BlendMode::PassThrough) {
        // The children blend straight into the backdrop; opacity and mask mix the result back.
        Buffer tmp(r);
        for (int y = r.top(); y <= r.bottom(); ++y) copyRow(tmp.s.row(y), dst.row(y) + (r.left() - dst.area.left()), r.width());
        renderList(t, t.kids[size_t(i)], tmp.s);
        const bool masked = g.hasMask() && g.maskEnabled;
        const uint32_t op255 = uint32_t(std::lround(op * 255.f));
        std::vector<uint8_t> m(size_t(r.width()), 255);
        for (int y = r.top(); y <= r.bottom(); ++y) {
            uint32_t* d = dst.row(y) + (r.left() - dst.area.left());
            const uint32_t* s = tmp.s.row(y);
            if (!masked && op255 == 255) {
                copyRow(d, s, r.width());
                continue;
            }
            if (masked) g.maskRow(y, r.left(), r.width(), m.data());
            for (int x = 0; x < r.width(); ++x) {
                const uint32_t cov = (m[size_t(x)] * op255 + 127) / 255;
                if (cov == 255) d[x] = s[x];
                else if (cov) d[x] = Blend::byteMul(s[x], cov) + Blend::byteMul(d[x], 255 - cov);
            }
        }
        return;
    }
    Buffer tmp(r);
    renderList(t, t.kids[size_t(i)], tmp.s);
    compositeSurface(dst, tmp.s, r, g.mode, op, &g);
}

void drawLayer(const Tree& t, int i, const Surface& dst)
{
    const Layer& l = t.L[i];
    if (!l.visible) return;
    const QRect r = t.bounds[size_t(i)] & dst.area;
    if (r.isEmpty()) return;
    switch (l.kind) {
    case LayerKind::Group: drawGroup(t, i, dst, r); return;
    case LayerKind::Adjustment: drawAdjustment(l, dst, r); return;
    default: break;
    }
    if (l.hasStyle()) {
        const int m = l.style->margin();
        Buffer content(r.adjusted(-m, -m, m, m));
        copyMaskedContent(l, content.s);
        finishContent(l, content.s, dst, r);
        return;
    }
    // Fast path: blend straight from the layer.
    const QRect pr = r & l.rect();
    if (pr.isEmpty()) return;
    const bool masked = l.hasMask() && l.maskEnabled;
    std::vector<uint8_t> m(masked ? size_t(pr.width()) : 0);
    const float op = l.opacity * l.fill;
    for (int y = pr.top(); y <= pr.bottom(); ++y) {
        if (masked) l.maskRow(y, pr.left(), pr.width(), m.data());
        auto* s = reinterpret_cast<const uint32_t*>(l.image.constScanLine(y - l.offset.y())) + (pr.left() - l.offset.x());
        Blend::compositeRow(dst.row(y) + (pr.left() - dst.area.left()), s, pr.width(), l.mode, op,
                            masked ? m.data() : nullptr, pr.left(), y);
    }
}

// A clipping group: the base layer's content, the clipped layers painted inside its pixels,
// then the whole blended with the base's mode, opacity and effects.
void drawClipGroup(const Tree& t, int base, const QList<int>& clips, const Surface& dst)
{
    const Layer& b = t.L[base];
    const QRect r = t.bounds[size_t(base)] & dst.area;
    if (r.isEmpty()) return;
    const int margin = b.hasStyle() ? b.style->margin() : 0;
    Buffer content(r.adjusted(-margin, -margin, margin, margin));
    if (b.isGroup()) renderGroupIsolated(t, base, content.s);
    else copyMaskedContent(b, content.s);

    for (int c : clips) {
        const Layer& cl = t.L[c];
        const QRect cr = content.s.area;
        if (cl.kind == LayerKind::Adjustment) {
            drawAdjustment(cl, content.s, cr);
            continue;
        }
        const float op = cl.opacity * cl.fill;
        if (op <= 0.f) continue;
        const bool masked = cl.hasMask() && cl.maskEnabled;
        std::vector<uint8_t> m(size_t(cr.width()));
        Buffer group(cl.isGroup() ? cr : QRect());
        if (cl.isGroup()) renderList(t, t.kids[size_t(c)], group.s);
        const QRect sr = cl.isGroup() ? cr : (cl.rect() & cr);
        for (int y = sr.top(); y <= sr.bottom(); ++y) {
            if (masked) cl.maskRow(y, sr.left(), sr.width(), m.data());
            const uint32_t* s = cl.isGroup()
                ? group.s.row(y) + (sr.left() - cr.left())
                : reinterpret_cast<const uint32_t*>(cl.image.constScanLine(y - cl.offset.y())) + (sr.left() - cl.offset.x());
            Blend::compositeRowPreserveAlpha(content.s.row(y) + (sr.left() - cr.left()), s, sr.width(),
                                             cl.mode == BlendMode::PassThrough ? BlendMode::Normal : cl.mode, op,
                                             masked ? m.data() : nullptr, sr.left(), y);
        }
    }
    if (b.isGroup() && !b.hasStyle()) {
        compositeSurface(dst, content.s, r, b.mode, std::clamp(b.opacity * b.fill, 0.f, 1.f));
        return;
    }
    finishContent(b, content.s, dst, r);
}

void renderList(const Tree& t, const QList<int>& list, const Surface& dst)
{
    for (int k = 0; k < list.size();) {
        const int base = list[k];
        int e = k + 1;
        while (e < list.size() && t.L[list[e]].clipped) ++e;
        const Layer& b = t.L[base];
        if (b.visible) {
            QList<int> clips;
            for (int j = k + 1; j < e; ++j)
                if (t.L[list[j]].visible) clips.append(list[j]);
            if (clips.isEmpty() || b.kind == LayerKind::Adjustment) {
                drawLayer(t, base, dst);
                for (int c : clips) drawLayer(t, c, dst); // nothing to clip to
            } else {
                drawClipGroup(t, base, clips, dst);
            }
        }
        // Clipped layers hide with their base.
        k = e;
    }
}

} // namespace

Surface surfaceOf(QImage& canvasImage, const QRect& area)
{
    Surface s;
    const QRect a = area & canvasImage.rect();
    s.stride = canvasImage.bytesPerLine() / 4;
    s.bits = reinterpret_cast<uint32_t*>(canvasImage.bits()) + a.top() * s.stride + a.left();
    s.area = a;
    return s;
}

void render(const QList<Layer>& layers, const Surface& dst)
{
    if (dst.area.isEmpty()) return;
    Tree t(layers);
    renderList(t, t.top(), dst);
}

void renderParallel(const QList<Layer>& layers, QImage& canvasImage, const QRect& area)
{
    const QRect a = area & canvasImage.rect();
    if (a.isEmpty()) return;
    canvasImage.detach(); // worker threads write through raw pointers
    uint32_t* bits = reinterpret_cast<uint32_t*>(canvasImage.bits());
    const qsizetype stride = canvasImage.bytesPerLine() / 4;
    const Tree t(layers);
    constexpr int kBand = 64;
    const int bands = (a.height() + kBand - 1) / kBand;
    auto renderBand = [&](int b) {
        const QRect band(a.left(), a.top() + b * kBand, a.width(), std::min(kBand, a.bottom() + 1 - (a.top() + b * kBand)));
        Surface s{bits + band.top() * stride + band.left(), stride, band};
        for (int y = band.top(); y <= band.bottom(); ++y) memset(s.row(y), 0, size_t(band.width()) * 4);
        renderList(t, t.top(), s);
    };
    // Small updates (brush dabs) are cheaper on this thread.
    if (bands == 1 || qint64(a.width()) * a.height() < 64 * 64 * 4) {
        for (int b = 0; b < bands; ++b) renderBand(b);
        return;
    }
    Filters::parallelFor(bands, [&](int b0, int b1) {
        for (int b = b0; b < b1; ++b) renderBand(b);
    });
}

QImage flatten(const QList<Layer>& layers, const QRect& area)
{
    QImage out(area.size(), QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);
    if (area.isEmpty()) return out;
    Surface s{reinterpret_cast<uint32_t*>(out.bits()), out.bytesPerLine() / 4, area};
    render(layers, s);
    return out;
}

QImage renderSingle(const QList<Layer>& layers, int index, const QRect& area)
{
    QImage out(area.size(), QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);
    if (area.isEmpty() || index < 0 || index >= layers.size()) return out;
    Surface s{reinterpret_cast<uint32_t*>(out.bits()), out.bytesPerLine() / 4, area};
    Tree t(layers);
    drawLayer(t, index, s);
    return out;
}

QRect extent(const QList<Layer>& layers, int index, const QRect& everywhere)
{
    if (index < 0 || index >= layers.size()) return QRect();
    Tree t(layers);
    const QRect r = t.bounds[size_t(index)];
    return r == kEverywhere ? everywhere : r;
}

} // namespace Compositor
