#include "io/Psd.h"

#include "core/Adjustments.h"
#include "core/ColorModes.h"
#include "core/Compositor.h"
#include "core/Document.h"
#include "core/LayerTree.h"

#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSaveFile>
#include <QSet>
#include <QtEndian>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace Psd {

namespace {

// Header colour modes.
enum PsdMode { kBitmap = 0, kGray = 1, kIndexed = 2, kRGB = 3, kCMYK = 4, kMultichannel = 7, kDuotone = 8, kLab = 9 };
constexpr int kMaxPsdSide = 30000;

struct BlendKey {
    const char* key;
    BlendMode mode;
};
constexpr BlendKey kBlendKeys[] = {
    {"norm", BlendMode::Normal},      {"diss", BlendMode::Dissolve},     {"dark", BlendMode::Darken},
    {"mul ", BlendMode::Multiply},    {"idiv", BlendMode::ColorBurn},    {"lbrn", BlendMode::LinearBurn},
    {"dkCl", BlendMode::DarkerColor}, {"lite", BlendMode::Lighten},      {"scrn", BlendMode::Screen},
    {"div ", BlendMode::ColorDodge},  {"lddg", BlendMode::LinearDodge},  {"lgCl", BlendMode::LighterColor},
    {"over", BlendMode::Overlay},     {"sLit", BlendMode::SoftLight},    {"hLit", BlendMode::HardLight},
    {"vLit", BlendMode::VividLight},  {"lLit", BlendMode::LinearLight},  {"pLit", BlendMode::PinLight},
    {"hMix", BlendMode::HardMix},     {"diff", BlendMode::Difference},   {"smud", BlendMode::Exclusion},
    {"fsub", BlendMode::Subtract},    {"fdiv", BlendMode::Divide},       {"hue ", BlendMode::Hue},
    {"sat ", BlendMode::Saturation},  {"colr", BlendMode::Color},        {"lum ", BlendMode::Luminosity},
    {"pass", BlendMode::PassThrough},
};

BlendMode blendFromKey(const QByteArray& key)
{
    for (const BlendKey& b : kBlendKeys)
        if (key == QByteArray(b.key, 4)) return b.mode;
    return BlendMode::Normal;
}

QByteArray keyFromBlend(BlendMode mode)
{
    for (const BlendKey& b : kBlendKeys)
        if (b.mode == mode) return QByteArray(b.key, 4);
    return QByteArrayLiteral("norm");
}

// Adjustment and fill layer keys PhotoSlop cannot read, for the warning.
QString unsupportedLayerType(const QHash<QByteArray, QByteArray>& info)
{
    static const QList<QPair<QByteArray, QString>> keys = {
        {"blwh", QStringLiteral("Black & White")},   {"vibA", QStringLiteral("Vibrance")},
        {"expA", QStringLiteral("Exposure")},        {"mixr", QStringLiteral("Channel Mixer")},
        {"selc", QStringLiteral("Selective Color")}, {"grdm", QStringLiteral("Gradient Map")},
        {"phfl", QStringLiteral("Photo Filter")},    {"clrL", QStringLiteral("Color Lookup")},
        {"SoCo", QStringLiteral("Solid Color")},     {"GdFl", QStringLiteral("Gradient")},
        {"PtFl", QStringLiteral("Pattern")},
    };
    for (const auto& k : keys)
        if (info.contains(k.first)) return k.second;
    return QString();
}

// ================================ Reading ================================

class Reader {
public:
    explicit Reader(const QByteArray& d)
        : m_p(reinterpret_cast<const uchar*>(d.constData()))
        , m_size(d.size())
    {
    }

    bool psb = false;
    bool ok() const { return m_ok; }
    void fail() { m_ok = false; }
    qint64 pos() const { return m_pos; }
    qint64 size() const { return m_size; }
    const uchar* data() const { return m_p; }

    bool need(qint64 n)
    {
        if (n < 0 || m_pos + n > m_size) {
            m_ok = false;
            return false;
        }
        return true;
    }
    void seek(qint64 p)
    {
        if (p < 0 || p > m_size) m_ok = false;
        else m_pos = p;
    }
    void skip(qint64 n)
    {
        if (need(n)) m_pos += n;
    }
    quint8 u8() { return need(1) ? m_p[m_pos++] : 0; }
    quint16 u16()
    {
        if (!need(2)) return 0;
        const quint16 v = qFromBigEndian<quint16>(m_p + m_pos);
        m_pos += 2;
        return v;
    }
    qint16 i16() { return qint16(u16()); }
    quint32 u32()
    {
        if (!need(4)) return 0;
        const quint32 v = qFromBigEndian<quint32>(m_p + m_pos);
        m_pos += 4;
        return v;
    }
    qint32 i32() { return qint32(u32()); }
    quint64 u64()
    {
        if (!need(8)) return 0;
        const quint64 v = qFromBigEndian<quint64>(m_p + m_pos);
        m_pos += 8;
        return v;
    }
    // Section lengths that grow to 64 bits in large-document (PSB) files.
    quint64 length() { return psb ? u64() : u32(); }
    QByteArray bytes(qint64 n)
    {
        if (!need(n)) return QByteArray();
        QByteArray b(reinterpret_cast<const char*>(m_p + m_pos), n);
        m_pos += n;
        return b;
    }

private:
    const uchar* m_p;
    qint64 m_size;
    qint64 m_pos = 0;
    bool m_ok = true;
};

// Small big-endian reader over one tagged block.
struct BlockReader {
    const QByteArray& b;
    int pos = 0;
    bool ok = true;
    int u16()
    {
        if (pos + 2 > b.size()) {
            ok = false;
            return 0;
        }
        const int v = qFromBigEndian<quint16>(b.constData() + pos);
        pos += 2;
        return v;
    }
    int i16() { return qint16(quint16(u16())); }
    int u8()
    {
        if (pos + 1 > b.size()) {
            ok = false;
            return 0;
        }
        return quint8(b[pos++]);
    }
    quint32 u32()
    {
        if (pos + 4 > b.size()) {
            ok = false;
            return 0;
        }
        const quint32 v = qFromBigEndian<quint32>(b.constData() + pos);
        pos += 4;
        return v;
    }
    QByteArray bytes(int n)
    {
        if (pos + n > b.size()) {
            ok = false;
            return QByteArray();
        }
        const QByteArray out = b.mid(pos, n);
        pos += n;
        return out;
    }
};

int rowBytes(int w, int depth) { return depth == 1 ? (w + 7) / 8 : w * (depth / 8); }

// PackBits: n >= 0 copies n + 1 literal bytes, -127..-1 repeats the next byte 1 - n times.
void unpackBits(const uchar* s, qint64 n, uchar* d, int dn)
{
    qint64 i = 0;
    int o = 0;
    while (o < dn && i < n) {
        const int c = qint8(s[i++]);
        if (c >= 0) {
            const int len = std::min<qint64>({qint64(c) + 1, n - i, qint64(dn - o)});
            std::memcpy(d + o, s + i, size_t(len));
            i += c + 1;
            o += len;
        } else if (c != -128) {
            if (i >= n) break;
            const int len = std::min(1 - c, dn - o);
            std::memset(d + o, s[i++], size_t(len));
            o += len;
        }
    }
}

QByteArray inflate(const uchar* z, qint64 n, qint64 expected)
{
    // qUncompress expects zlib data behind a big-endian size word.
    QByteArray buf(4 + n, Qt::Uninitialized);
    qToBigEndian<quint32>(quint32(std::min<qint64>(expected, 0x7fffffff)), buf.data());
    std::memcpy(buf.data() + 4, z, size_t(n));
    return qUncompress(buf);
}

// Undoes ZIP-with-prediction on one row.
void unpredict(uchar* row, int w, int depth)
{
    if (depth == 8) {
        for (int x = 1; x < w; ++x) row[x] = uchar(row[x] + row[x - 1]);
    } else if (depth == 16) {
        quint16 prev = qFromBigEndian<quint16>(row);
        for (int x = 1; x < w; ++x) {
            const quint16 v = quint16(qFromBigEndian<quint16>(row + 2 * x) + prev);
            qToBigEndian<quint16>(v, row + 2 * x);
            prev = v;
        }
    } else if (depth == 32) {
        // Bytes are delta coded across the row, with each float's bytes split into planes.
        const int n = w * 4;
        for (int i = 1; i < n; ++i) row[i] = uchar(row[i] + row[i - 1]);
        QByteArray tmp(reinterpret_cast<const char*>(row), n);
        for (int x = 0; x < w; ++x)
            for (int b = 0; b < 4; ++b) row[4 * x + b] = uchar(tmp[b * w + x]);
    }
}

// Raw rows to 16-bit samples. `linear` colour channels of 32-bit files are converted to sRGB.
void toSamples(const uchar* raw, int w, int h, int depth, bool linear, QVector<quint16>& out)
{
    out.resize(qsizetype(w) * h);
    const int rb = rowBytes(w, depth);
    for (int y = 0; y < h; ++y) {
        const uchar* row = raw + qint64(y) * rb;
        quint16* o = out.data() + qint64(y) * w;
        switch (depth) {
        case 1:
            for (int x = 0; x < w; ++x) o[x] = (row[x >> 3] & (0x80 >> (x & 7))) ? 0 : 65535;
            break;
        case 8:
            for (int x = 0; x < w; ++x) o[x] = quint16(row[x] * 257);
            break;
        case 16:
            for (int x = 0; x < w; ++x) o[x] = qFromBigEndian<quint16>(row + 2 * x);
            break;
        case 32:
            for (int x = 0; x < w; ++x) {
                const quint32 bits = qFromBigEndian<quint32>(row + 4 * x);
                float f;
                std::memcpy(&f, &bits, 4);
                double v = std::isfinite(f) ? double(f) : 0.0;
                if (linear) v = ColorModes::linearToSrgb(v);
                o[x] = quint16(std::clamp(std::lround(v * 65535.0), 0L, 65535L));
            }
            break;
        default: break;
        }
    }
}

// One layer channel: a compression word followed by the pixels.
bool decodeChannel(const uchar* p, qint64 len, int w, int h, int depth, bool psb, QByteArray& raw)
{
    const int rb = rowBytes(w, depth);
    raw = QByteArray(qint64(rb) * h, 0);
    if (w <= 0 || h <= 0) return true;
    if (len < 2) return false;
    const int comp = qFromBigEndian<quint16>(p);
    const uchar* d = p + 2;
    const qint64 n = len - 2;
    auto* out = reinterpret_cast<uchar*>(raw.data());
    switch (comp) {
    case 0: std::memcpy(out, d, size_t(std::min<qint64>(raw.size(), n))); return true;
    case 1: {
        const int cw = psb ? 4 : 2;
        if (n < qint64(h) * cw) return false;
        qint64 at = qint64(h) * cw;
        for (int y = 0; y < h; ++y) {
            const qint64 c = psb ? qFromBigEndian<quint32>(d + 4 * y) : qFromBigEndian<quint16>(d + 2 * y);
            if (at + c > n) return false;
            unpackBits(d + at, c, out + qint64(y) * rb, rb);
            at += c;
        }
        return true;
    }
    case 2:
    case 3: {
        const QByteArray z = inflate(d, n, raw.size());
        if (z.size() < raw.size()) return false;
        std::memcpy(out, z.constData(), size_t(raw.size()));
        if (comp == 3)
            for (int y = 0; y < h; ++y) unpredict(out + qint64(y) * rb, w, depth);
        return true;
    }
    default: return false;
    }
}

struct Context {
    int mode = kRGB;
    int depth = 8;
    int width = 0, height = 0;
    bool psb = false;
    QVector<QRgb> palette;
    const uchar* data = nullptr;
};

int colorChannels(int mode)
{
    switch (mode) {
    case kRGB:
    case kLab: return 3;
    case kCMYK: return 4;
    default: return 1;
    }
}

inline int to8(quint16 v) { return (v + 128) / 257; }

// Builds a premultiplied image from colour planes (in the file's mode) and an optional alpha plane.
QImage compose(const Context& c, int w, int h, const std::array<const quint16*, 4>& col, const quint16* alpha)
{
    QImage img(w, h, QImage::Format_ARGB32_Premultiplied);
    static const quint16 zero = 0;
    for (int y = 0; y < h; ++y) {
        auto* row = reinterpret_cast<QRgb*>(img.scanLine(y));
        for (int x = 0; x < w; ++x) {
            const qint64 i = qint64(y) * w + x;
            auto s = [&](int ch) { return col[size_t(ch)] ? col[size_t(ch)][i] : zero; };
            const int a = alpha ? to8(alpha[i]) : 255;
            QRgb rgb;
            switch (c.mode) {
            case kRGB: rgb = qRgb(to8(s(0)), to8(s(1)), to8(s(2))); break;
            case kIndexed: {
                const int idx = to8(s(0));
                rgb = idx < c.palette.size() ? c.palette[idx] : qRgb(0, 0, 0);
                break;
            }
            case kCMYK:
                // Stored inverted: 65535 is no ink.
                rgb = ColorModes::cmykToRgb(1.0 - s(0) / 65535.0, 1.0 - s(1) / 65535.0, 1.0 - s(2) / 65535.0,
                                            1.0 - s(3) / 65535.0);
                break;
            case kLab:
                rgb = ColorModes::labToRgb(s(0) / 65535.0 * 100.0, s(1) / 257.0 - 128.0, s(2) / 257.0 - 128.0);
                break;
            default: {
                const int v = to8(s(0));
                rgb = qRgb(v, v, v);
                break;
            }
            }
            row[x] = qPremultiply((rgb & 0x00ffffff) | (QRgb(a) << 24));
        }
    }
    return img;
}

QImage greyImage(const QVector<quint16>& plane, int w, int h)
{
    QImage img(w, h, QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < h; ++y) {
        auto* row = reinterpret_cast<QRgb*>(img.scanLine(y));
        const quint16* s = plane.constData() + qint64(y) * w;
        for (int x = 0; x < w; ++x) {
            const int v = to8(s[x]);
            row[x] = qRgb(v, v, v);
        }
    }
    return img;
}

struct ChannelRef {
    int id = 0;
    qint64 offset = 0;
    quint64 length = 0;
};

struct Record {
    QRect rect;
    QList<ChannelRef> channels;
    QByteArray blend;
    int opacity = 255;
    int clipping = 0;
    int flags = 0;
    bool hasMask = false;
    QRect maskRect;
    int maskDefault = 0;
    int maskFlags = 0;
    bool hasRealMask = false;
    QRect realMaskRect;
    int realMaskDefault = 0;
    int realMaskFlags = 0;
    QString name;
    QHash<QByteArray, QByteArray> info;

    const ChannelRef* channel(int id) const
    {
        for (const ChannelRef& c : channels)
            if (c.id == id) return &c;
        return nullptr;
    }
};

bool isLargeKey(const QByteArray& key)
{
    static const QSet<QByteArray> keys = {"LMsk", "Lr16", "Lr32", "Layr", "Mt16", "Mt32", "Mtrn",
                                          "Alph", "FMsk", "lnk2", "FEid", "FXid", "PxSD"};
    return keys.contains(key);
}

// Tagged blocks ("8BIM" + key + length + data) up to `end`.
void readTaggedBlocks(Reader& r, qint64 end, QHash<QByteArray, QByteArray>& out)
{
    while (r.ok() && r.pos() + 12 <= end) {
        const QByteArray sig = QByteArray(reinterpret_cast<const char*>(r.data() + r.pos()), 4);
        if (sig != "8BIM" && sig != "8B64") {
            // Some writers pad blocks to four bytes without counting it.
            r.skip(1);
            continue;
        }
        r.skip(4);
        const QByteArray key = r.bytes(4);
        const quint64 len = (r.psb && isLargeKey(key)) ? r.u64() : r.u32();
        if (len > quint64(end - r.pos())) break;
        out.insert(key, r.bytes(qint64(len)));
    }
    r.seek(end);
}

bool readRecord(Reader& r, Record& rec)
{
    const int top = r.i32(), left = r.i32(), bottom = r.i32(), right = r.i32();
    rec.rect = QRect(left, top, std::max(0, right - left), std::max(0, bottom - top));
    const int n = r.u16();
    if (n > 56) return false;
    for (int i = 0; i < n; ++i) {
        ChannelRef c;
        c.id = r.i16();
        c.length = r.length();
        rec.channels.append(c);
    }
    if (r.bytes(4) != "8BIM") return false;
    rec.blend = r.bytes(4);
    rec.opacity = r.u8();
    rec.clipping = r.u8();
    rec.flags = r.u8();
    r.skip(1);
    // Read the length first: the order operands of + are evaluated in is unspecified (MSVC differs).
    const quint32 extraLen = r.u32();
    const qint64 extraEnd = r.pos() + extraLen;
    if (!r.ok() || extraEnd > r.size()) return false;

    // Layer mask data.
    const quint32 maskLen = r.u32();
    const qint64 maskEnd = r.pos() + maskLen;
    if (maskLen >= 18) {
        const int t = r.i32(), l = r.i32(), b = r.i32(), rt = r.i32();
        rec.maskRect = QRect(l, t, std::max(0, rt - l), std::max(0, b - t));
        rec.maskDefault = r.u8();
        rec.maskFlags = r.u8();
        rec.hasMask = true;
        if (rec.maskFlags & 0x10) {
            // Mask parameters: densities (1 byte) and feathers (8 bytes).
            const int params = r.u8();
            if (params & 1) r.skip(1);
            if (params & 2) r.skip(8);
            if (params & 4) r.skip(1);
            if (params & 8) r.skip(8);
        }
        if (maskEnd - r.pos() >= 18) {
            // The pixel mask of a layer that also has a vector mask.
            rec.realMaskFlags = r.u8();
            rec.realMaskDefault = r.u8();
            const int t2 = r.i32(), l2 = r.i32(), b2 = r.i32(), r2 = r.i32();
            rec.realMaskRect = QRect(l2, t2, std::max(0, r2 - l2), std::max(0, b2 - t2));
            rec.hasRealMask = true;
        }
    }
    r.seek(maskEnd);
    r.skip(r.u32()); // blending ranges
    const int nameLen = r.u8();
    rec.name = QString::fromLatin1(r.bytes(nameLen));
    r.skip((4 - (1 + nameLen) % 4) % 4);
    readTaggedBlocks(r, extraEnd, rec.info);
    return r.ok();
}

// The layer count, records and channel data of a layer info section ending at `end`.
bool readLayerInfo(Reader& r, qint64 end, QList<Record>& records, bool* mergedAlpha)
{
    const int count = r.i16();
    if (mergedAlpha) *mergedAlpha = count < 0;
    const int n = std::abs(count);
    for (int i = 0; i < n && r.ok(); ++i) {
        Record rec;
        if (!readRecord(r, rec)) return false;
        records.append(rec);
    }
    for (Record& rec : records) {
        for (ChannelRef& c : rec.channels) {
            c.offset = r.pos();
            if (c.length > quint64(end - r.pos())) return false;
            r.skip(qint64(c.length));
        }
    }
    r.seek(end);
    return r.ok();
}

// ---- Adjustment layers ----

std::shared_ptr<const Adjust::LayerSettings> readAdjustment(const QHash<QByteArray, QByteArray>& info)
{
    using namespace Adjust;
    LayerSettings s;
    if (info.contains("levl")) {
        BlockReader b{info["levl"]};
        if (b.u16() != 2) return nullptr;
        s.kind = Kind::Levels;
        for (int i = 0; i < 4; ++i) {
            LevelsChannel& c = s.levels.channels[size_t(i)];
            c.inBlack = std::clamp(b.u16(), 0, 253);
            c.inWhite = std::clamp(b.u16(), c.inBlack + 2, 255);
            c.outBlack = std::clamp(b.u16(), 0, 255);
            c.outWhite = std::clamp(b.u16(), 0, 255);
            const int g = b.u16();
            c.gamma = g > 0 ? std::clamp(g / 100.0, 0.1, 9.99) : 1.0;
        }
        return b.ok ? s.finalized() : nullptr;
    }
    if (info.contains("curv")) {
        BlockReader b{info["curv"]};
        const int isMap = b.u8();
        const int version = b.u16();
        const quint32 map = b.u32();
        if (isMap || (version != 1 && version != 4)) return nullptr;
        s.kind = Kind::Curves;
        // Version 1 lists the channels in a bit mask (0 = composite, then R, G, B).
        QList<int> channels;
        if (version == 1) {
            for (int bit = 0; bit < 32; ++bit)
                if (map & (1u << bit)) channels << bit;
        } else {
            for (quint32 i = 0; i < map; ++i) channels << int(i);
        }
        for (int ch : channels) {
            const int count = b.u16();
            if (count < 2 || count > 19) return nullptr;
            CurvePoints pts;
            for (int i = 0; i < count; ++i) {
                const int out = b.u16(), in = b.u16();
                pts.append(QPointF(std::clamp(in, 0, 255), std::clamp(out, 0, 255)));
            }
            std::sort(pts.begin(), pts.end(), [](const QPointF& a, const QPointF& c) { return a.x() < c.x(); });
            if (ch < 4) s.curves.channels[size_t(ch)] = pts;
        }
        return b.ok ? s.finalized() : nullptr;
    }
    if (info.contains("hue2")) {
        BlockReader b{info["hue2"]};
        if (b.u16() != 2) return nullptr;
        s.kind = Kind::HueSaturation;
        auto& hs = s.hueSaturation;
        hs.colorize = b.u8() != 0;
        b.u8();
        const int ch = b.i16(), cs = b.i16(), cl = b.i16();
        const int mh = b.i16(), ms = b.i16(), ml = b.i16();
        if (hs.colorize) hs.ranges[0] = {std::clamp(ch, 0, 360), std::clamp(cs, 0, 100), std::clamp(cl, -100, 100)};
        else hs.ranges[0] = {std::clamp(mh, -180, 180), std::clamp(ms, -100, 100), std::clamp(ml, -100, 100)};
        for (int i = 1; i <= 6; ++i) {
            for (int k = 0; k < 4; ++k) b.i16(); // range limits: PhotoSlop uses the default ranges
            const int h = b.i16(), sa = b.i16(), l = b.i16();
            hs.ranges[size_t(i)] = {std::clamp(h, -180, 180), std::clamp(sa, -100, 100), std::clamp(l, -100, 100)};
        }
        return b.ok ? s.finalized() : nullptr;
    }
    if (info.contains("blnc")) {
        BlockReader b{info["blnc"]};
        s.kind = Kind::ColorBalance;
        for (int tone = 0; tone < 3; ++tone)
            for (int axis = 0; axis < 3; ++axis) s.colorBalance.values[size_t(tone)][size_t(axis)] = std::clamp(b.i16(), -100, 100);
        s.colorBalance.preserveLuminosity = b.u8() != 0;
        return b.ok ? s.finalized() : nullptr;
    }
    if (info.contains("brit")) {
        BlockReader b{info["brit"]};
        s.kind = Kind::BrightnessContrast;
        s.brightness = std::clamp(b.i16(), -150, 150);
        s.contrast = std::clamp(b.i16(), -100, 100);
        // Newer files describe the modern adjustment in "CgEd"; without it the values are legacy.
        s.legacy = !info.contains("CgEd");
        if (!s.legacy) s.contrast = std::clamp(s.contrast, -50, 100);
        return b.ok ? s.finalized() : nullptr;
    }
    if (info.contains("nvrt")) {
        s.kind = Kind::Invert;
        return s.finalized();
    }
    if (info.contains("post")) {
        BlockReader b{info["post"]};
        s.kind = Kind::Posterize;
        s.posterizeLevels = std::clamp(b.u16(), 2, 255);
        return s.finalized();
    }
    if (info.contains("thrs")) {
        BlockReader b{info["thrs"]};
        s.kind = Kind::Threshold;
        s.thresholdLevel = std::clamp(b.u16(), 1, 255);
        return s.finalized();
    }
    return nullptr;
}

QString unicodeName(const QByteArray& luni)
{
    BlockReader b{luni};
    const quint32 n = b.u32();
    if (!b.ok || n > quint32((luni.size() - 4) / 2)) return QString();
    QString s;
    s.reserve(int(n));
    for (quint32 i = 0; i < n; ++i) s.append(QChar(char16_t(b.u16())));
    while (s.endsWith(QChar(0))) s.chop(1);
    return s;
}

// Decodes one channel of a record to 16-bit samples over `size`.
bool channelSamples(const Context& c, const ChannelRef& ch, const QSize& size, bool linear, QVector<quint16>& out)
{
    QByteArray raw;
    if (!decodeChannel(c.data + ch.offset, qint64(ch.length), size.width(), size.height(), c.depth, c.psb, raw)) return false;
    toSamples(reinterpret_cast<const uchar*>(raw.constData()), size.width(), size.height(), c.depth, linear, out);
    return true;
}

Layer buildLayer(const Context& c, const Record& rec, QSet<QString>& problems)
{
    Layer l = Layer::create(rec.name);
    const QString uname = unicodeName(rec.info.value("luni"));
    if (!uname.isEmpty()) l.name = uname;
    l.visible = !(rec.flags & 2);
    l.opacity = rec.opacity / 255.f;
    if (rec.info.contains("iOpa") && !rec.info["iOpa"].isEmpty()) l.fill = quint8(rec.info["iOpa"][0]) / 255.f;
    l.mode = blendFromKey(rec.blend);
    if (l.mode == BlendMode::PassThrough) l.mode = BlendMode::Normal; // only groups pass through
    l.lockTransparency = rec.flags & 1;
    if (rec.info.contains("lspf")) {
        BlockReader b{rec.info["lspf"]};
        const quint32 v = b.u32();
        l.lockTransparency = l.lockTransparency || (v & 1);
        l.lockPixels = v & 2;
        l.lockPosition = v & 4;
        l.lockAll = v & 0x80000000u;
    }
    l.clipped = rec.clipping == 1;

    const bool linear = c.depth == 32 && (c.mode == kRGB || c.mode == kGray);
    if (auto adj = readAdjustment(rec.info)) {
        l.kind = LayerKind::Adjustment;
        l.adjustment = adj;
    } else if (!rec.rect.isEmpty()) {
        const int nc = colorChannels(c.mode);
        std::array<QVector<quint16>, 4> planes;
        std::array<const quint16*, 4> ptrs{};
        QVector<quint16> alpha;
        bool ok = true;
        for (int i = 0; i < nc; ++i)
            if (const ChannelRef* ch = rec.channel(i)) {
                ok = ok && channelSamples(c, *ch, rec.rect.size(), linear, planes[size_t(i)]);
                ptrs[size_t(i)] = planes[size_t(i)].constData();
            }
        const ChannelRef* a = rec.channel(-1);
        if (a) ok = ok && channelSamples(c, *a, rec.rect.size(), false, alpha);
        if (!ok) problems << QStringLiteral("Some layer pixels were damaged and could not be read.");
        l.image = compose(c, rec.rect.width(), rec.rect.height(), ptrs, a ? alpha.constData() : nullptr);
        l.offset = rec.rect.topLeft();
    }
    const QString unsupported = unsupportedLayerType(rec.info);
    if (!unsupported.isEmpty() && l.kind != LayerKind::Adjustment)
        problems << QStringLiteral("%1 layers are not supported and open as plain layers.").arg(unsupported);
    if (rec.info.contains("TySh")) problems << QStringLiteral("Type layers open as pixels.");
    if (rec.info.contains("SoLd") || rec.info.contains("PlLd")) problems << QStringLiteral("Smart objects open as pixels.");
    if (rec.info.contains("lfx2") || rec.info.contains("lmfx")) problems << QStringLiteral("Layer styles are not imported.");

    // The layer mask: the "real" user mask when the layer also has a vector mask.
    const bool real = rec.hasRealMask && rec.channel(-3);
    const ChannelRef* mc = real ? rec.channel(-3) : rec.channel(-2);
    if (rec.hasMask && mc) {
        const QRect mr = real ? rec.realMaskRect : rec.maskRect;
        const int mflags = real ? rec.realMaskFlags : rec.maskFlags;
        Layer m = Layer::create(QStringLiteral("Layer Mask"));
        if (!mr.isEmpty()) {
            QVector<quint16> plane;
            if (channelSamples(c, *mc, mr.size(), false, plane)) {
                m.image = greyImage(plane, mr.width(), mr.height());
                m.offset = mr.topLeft();
            }
        }
        l.maskDefault = quint8((real ? rec.realMaskDefault : rec.maskDefault) >= 128 ? 255 : 0);
        l.maskEnabled = !(mflags & 2);
        l.maskLinked = !(mflags & 1);
        l.mask.set(m);
    }
    if (l.kind == LayerKind::Adjustment && !l.mask) {
        // Adjustment layers always carry a mask in PhotoSlop.
        l.mask.set(Layer::create(QStringLiteral("Layer Mask")));
        l.maskDefault = 255;
    }
    return l;
}

void readResources(const QByteArray& res, DocState& s)
{
    BlockReader b{res};
    while (b.ok && b.pos + 12 <= res.size()) {
        const QByteArray sig = b.bytes(4);
        if (sig != "8BIM" && sig != "MeSa" && sig != "AgHg" && sig != "PHUT" && sig != "DCSR") break;
        const int id = b.u16();
        const int nameLen = b.u8();
        b.bytes(nameLen + ((1 + nameLen) % 2));
        const quint32 size = b.u32();
        if (size > quint32(res.size() - b.pos)) break;
        const QByteArray data = b.bytes(int(size));
        if (size % 2) b.u8();
        BlockReader d{data};
        if (id == 1005 && size >= 16) {
            const double dpi = d.u32() / 65536.0;
            if (dpi > 0.5 && dpi < 100000) s.dpi = dpi;
        } else if (id == 1032 && size >= 16) {
            d.u32(); // version
            d.u32();
            d.u32(); // grid cycles
            const quint32 count = d.u32();
            for (quint32 i = 0; i < count && d.ok; ++i) {
                const qint32 loc = qint32(d.u32());
                const int dir = d.u8();
                if (!d.ok) break;
                Guide g;
                g.orientation = dir == 0 ? Qt::Vertical : Qt::Horizontal;
                g.position = loc / 32.0;
                s.guides.append(g);
            }
        }
    }
}

// The flattened image stored after the layers.
QImage readMerged(Reader& r, const Context& c, int channels, bool mergedAlpha, bool* ok)
{
    *ok = false;
    const int w = c.width, h = c.height, rb = rowBytes(w, c.depth);
    const int comp = r.u16();
    const int nc = std::min(channels, colorChannels(c.mode) + (mergedAlpha ? 1 : 0));
    QList<QByteArray> raws;
    if (comp == 0) {
        for (int i = 0; i < nc; ++i) raws << r.bytes(qint64(rb) * h);
    } else if (comp == 1) {
        QVector<quint32> counts(qsizetype(channels) * h);
        for (quint32& v : counts) v = c.psb ? r.u32() : r.u16();
        for (int i = 0; i < nc && r.ok(); ++i) {
            QByteArray raw(qint64(rb) * h, 0);
            for (int y = 0; y < h && r.ok(); ++y) {
                const quint32 n = counts[qsizetype(i) * h + y];
                if (!r.need(n)) break;
                unpackBits(r.data() + r.pos(), n, reinterpret_cast<uchar*>(raw.data()) + qint64(y) * rb, rb);
                r.skip(n);
            }
            raws << raw;
        }
    } else if (comp == 2 || comp == 3) {
        const QByteArray all = inflate(r.data() + r.pos(), r.size() - r.pos(), qint64(rb) * h * channels);
        for (int i = 0; i < nc; ++i) {
            QByteArray raw = all.mid(qint64(i) * rb * h, qint64(rb) * h);
            raw.resize(qint64(rb) * h);
            if (comp == 3)
                for (int y = 0; y < h; ++y) unpredict(reinterpret_cast<uchar*>(raw.data()) + qint64(y) * rb, w, c.depth);
            raws << raw;
        }
    } else {
        return QImage();
    }
    if (!r.ok() || raws.size() < nc) return QImage();
    const bool linear = c.depth == 32 && (c.mode == kRGB || c.mode == kGray);
    std::array<QVector<quint16>, 5> planes;
    std::array<const quint16*, 4> ptrs{};
    const int ncol = std::min(nc, colorChannels(c.mode));
    for (int i = 0; i < ncol; ++i) {
        toSamples(reinterpret_cast<const uchar*>(raws[i].constData()), w, h, c.depth, linear, planes[size_t(i)]);
        ptrs[size_t(i)] = planes[size_t(i)].constData();
    }
    const quint16* alpha = nullptr;
    if (mergedAlpha && nc > ncol) {
        toSamples(reinterpret_cast<const uchar*>(raws[ncol].constData()), w, h, c.depth, false, planes[4]);
        alpha = planes[4].constData();
    }
    *ok = true;
    return compose(c, w, h, ptrs, alpha);
}

// ================================ Writing ================================

class Writer {
public:
    QByteArray buf;
    void u8(int v) { buf.append(char(v)); }
    void u16(int v)
    {
        char b[2];
        qToBigEndian<quint16>(quint16(v), b);
        buf.append(b, 2);
    }
    void u32(quint32 v)
    {
        char b[4];
        qToBigEndian<quint32>(v, b);
        buf.append(b, 4);
    }
    void i32(qint32 v) { u32(quint32(v)); }
    void raw(const QByteArray& b) { buf.append(b); }
    void raw(const char* s) { buf.append(s, qsizetype(std::strlen(s))); }
    // Starts a 4-byte length; end() writes the byte count since then.
    qsizetype beginLength()
    {
        const qsizetype at = buf.size();
        u32(0);
        return at;
    }
    void endLength(qsizetype at, int align = 1)
    {
        while ((buf.size() - at - 4) % align) u8(0);
        qToBigEndian<quint32>(quint32(buf.size() - at - 4), buf.data() + at);
    }
};

void packBits(const uchar* s, int n, QByteArray& out)
{
    int i = 0;
    while (i < n) {
        int run = 1;
        while (i + run < n && run < 128 && s[i + run] == s[i]) ++run;
        if (run >= 2) {
            out.append(char(1 - run));
            out.append(char(s[i]));
            i += run;
            continue;
        }
        const int start = i;
        int len = 0;
        while (i < n && len < 128) {
            if (i + 2 < n && s[i] == s[i + 1] && s[i + 1] == s[i + 2]) break;
            ++i;
            ++len;
        }
        out.append(char(len - 1));
        out.append(reinterpret_cast<const char*>(s + start), len);
    }
}

// RLE rows of one 8-bit plane: the row byte counts, then the packed rows.
void rlePlane(const QByteArray& plane, int w, int h, QByteArray& counts, QByteArray& data)
{
    for (int y = 0; y < h; ++y) {
        const qsizetype before = data.size();
        packBits(reinterpret_cast<const uchar*>(plane.constData()) + qint64(y) * w, w, data);
        char b[2];
        qToBigEndian<quint16>(quint16(data.size() - before), b);
        counts.append(b, 2);
    }
}

// A layer channel block: compression word and RLE data.
QByteArray channelBlock(const QByteArray& plane, int w, int h)
{
    QByteArray out;
    if (w <= 0 || h <= 0) {
        out.append(2, '\0'); // raw, no pixels
        return out;
    }
    QByteArray counts, data;
    rlePlane(plane, w, h, counts, data);
    out.append(char(0));
    out.append(char(1));
    out.append(counts);
    out.append(data);
    return out;
}

// Splits a premultiplied image into 8-bit planes in the document's mode, plus alpha.
QList<QByteArray> splitPlanes(const QImage& img, ColorMode mode, bool withAlpha)
{
    const int nc = ColorModes::channelCount(mode);
    const int w = img.width(), h = img.height();
    QList<QByteArray> planes;
    for (int i = 0; i < nc + (withAlpha ? 1 : 0); ++i) planes << QByteArray(qint64(w) * h, '\0');
    int v[4];
    for (int y = 0; y < h; ++y) {
        const auto* row = reinterpret_cast<const QRgb*>(img.constScanLine(y));
        for (int x = 0; x < w; ++x) {
            const QRgb s = qUnpremultiply(row[x]);
            ColorModes::toChannels(mode, s, v);
            const qint64 i = qint64(y) * w + x;
            for (int c = 0; c < nc; ++c) {
                // CMYK is stored inverted (255 = no ink).
                const int val = mode == ColorMode::CMYK ? 255 - v[c] : v[c];
                planes[c][i] = char(val);
            }
            if (withAlpha) planes[nc][i] = char(qAlpha(row[x]));
        }
    }
    return planes;
}

QByteArray maskPlane(const Layer& mask, int maskDefault)
{
    const QImage& img = mask.image;
    QByteArray plane(qint64(img.width()) * img.height(), '\0');
    for (int y = 0; y < img.height(); ++y) {
        const auto* row = reinterpret_cast<const QRgb*>(img.constScanLine(y));
        for (int x = 0; x < img.width(); ++x) plane[qint64(y) * img.width() + x] = char(maskValue(row[x], maskDefault));
    }
    return plane;
}

QByteArray block(const char* key, const QByteArray& data)
{
    Writer w;
    w.raw("8BIM");
    w.raw(key);
    QByteArray d = data;
    while (d.size() % 4) d.append('\0');
    w.u32(quint32(d.size()));
    w.raw(d);
    return w.buf;
}

QByteArray adjustmentBlock(const Adjust::LayerSettings& s, QByteArray* key)
{
    using namespace Adjust;
    Writer w;
    switch (s.kind) {
    case Kind::Levels:
        *key = "levl";
        w.u16(2);
        for (int i = 0; i < 29; ++i) {
            const LevelsChannel c = i < 4 ? s.levels.channels[size_t(i)] : LevelsChannel();
            w.u16(c.inBlack);
            w.u16(c.inWhite);
            w.u16(c.outBlack);
            w.u16(c.outWhite);
            w.u16(int(std::lround(c.gamma * 100.0)));
        }
        break;
    case Kind::Curves: {
        *key = "curv";
        w.u8(0);
        w.u16(1);
        w.u32(0x0f); // composite, red, green, blue
        auto points = [&](const CurvePoints& pts) {
            const CurvePoints p = pts.size() > 19 ? pts.mid(0, 19) : pts;
            w.u16(int(p.size()));
            for (const QPointF& pt : p) {
                w.u16(int(std::lround(pt.y()))); // output first
                w.u16(int(std::lround(pt.x())));
            }
        };
        for (int ch = 0; ch < 4; ++ch) points(s.curves.channels[size_t(ch)]);
        w.raw("Crv ");
        w.u16(4);
        w.u32(4);
        for (int ch = 0; ch < 4; ++ch) {
            w.u16(ch);
            points(s.curves.channels[size_t(ch)]);
        }
        break;
    }
    case Kind::HueSaturation: {
        *key = "hue2";
        const auto& hs = s.hueSaturation;
        w.u16(2);
        w.u8(hs.colorize ? 1 : 0);
        w.u8(0);
        const auto& m = hs.ranges[0];
        for (int v : {hs.colorize ? m.hue : 0, hs.colorize ? m.saturation : 25, hs.colorize ? m.lightness : 0}) w.u16(v);
        for (int v : {hs.colorize ? 0 : m.hue, hs.colorize ? 0 : m.saturation, hs.colorize ? 0 : m.lightness}) w.u16(v);
        static const int ranges[6][4] = {{315, 345, 15, 45},   {15, 45, 75, 105},    {75, 105, 135, 165},
                                         {135, 165, 195, 225}, {195, 225, 255, 285}, {255, 285, 315, 345}};
        for (int i = 0; i < 6; ++i) {
            for (int v : ranges[i]) w.u16(v);
            const auto& r = hs.ranges[size_t(i + 1)];
            w.u16(r.hue);
            w.u16(r.saturation);
            w.u16(r.lightness);
        }
        break;
    }
    case Kind::ColorBalance:
        *key = "blnc";
        for (int tone = 0; tone < 3; ++tone)
            for (int axis = 0; axis < 3; ++axis) w.u16(s.colorBalance.values[size_t(tone)][size_t(axis)]);
        w.u8(s.colorBalance.preserveLuminosity ? 1 : 0);
        break;
    case Kind::BrightnessContrast:
        *key = "brit";
        w.u16(s.brightness);
        w.u16(s.contrast);
        w.u16(127); // mean
        w.u8(0);    // Lab only
        break;
    case Kind::Invert: *key = "nvrt"; break;
    case Kind::Posterize:
        *key = "post";
        w.u16(s.posterizeLevels);
        w.u16(0);
        break;
    case Kind::Threshold:
        *key = "thrs";
        w.u16(s.thresholdLevel);
        w.u16(0);
        break;
    default: return QByteArray();
    }
    return w.buf;
}

struct OutRecord {
    QRect rect;
    QList<QPair<int, QByteArray>> channels; // id, block
    QByteArray blend = "norm";
    int opacity = 255;
    bool clipped = false;
    int flags = 0x08;
    bool hasMask = false;
    QRect maskRect;
    int maskDefault = 255;
    int maskFlags = 0;
    QString name;
    QByteArray extra; // tagged blocks
};

QByteArray pascalName(const QString& name)
{
    QByteArray n = name.toLatin1().left(255);
    for (char& ch : n)
        if (ch == 0) ch = '?';
    QByteArray out;
    out.append(char(n.size()));
    out.append(n);
    while (out.size() % 4) out.append('\0');
    return out;
}

QByteArray unicodeBlock(const QString& name)
{
    Writer w;
    w.u32(quint32(name.size()));
    for (QChar ch : name) w.u16(ch.unicode());
    return block("luni", w.buf);
}

void writeRecord(Writer& w, const OutRecord& r)
{
    w.i32(r.rect.top());
    w.i32(r.rect.left());
    w.i32(r.rect.top() + r.rect.height());
    w.i32(r.rect.left() + r.rect.width());
    w.u16(int(r.channels.size()));
    for (const auto& c : r.channels) {
        w.u16(c.first);
        w.u32(quint32(c.second.size()));
    }
    w.raw("8BIM");
    w.raw(r.blend);
    w.u8(r.opacity);
    w.u8(r.clipped ? 1 : 0);
    w.u8(r.flags);
    w.u8(0);
    const qsizetype extra = w.beginLength();
    if (r.hasMask) {
        w.u32(20);
        w.i32(r.maskRect.top());
        w.i32(r.maskRect.left());
        w.i32(r.maskRect.top() + r.maskRect.height());
        w.i32(r.maskRect.left() + r.maskRect.width());
        w.u8(r.maskDefault);
        w.u8(r.maskFlags);
        w.u16(0);
    } else {
        w.u32(0);
    }
    w.u32(0); // blending ranges
    w.raw(pascalName(r.name));
    w.raw(r.extra);
    w.endLength(extra);
}

QByteArray sectionBlock(int type, BlendMode mode)
{
    Writer w;
    w.u32(quint32(type));
    if (type != 3) {
        w.raw("8BIM");
        w.raw(keyFromBlend(mode));
    }
    return block("lsct", w.buf);
}

OutRecord dividerRecord(ColorMode mode)
{
    OutRecord r;
    r.name = QStringLiteral("</Layer group>");
    r.channels.append({-1, channelBlock(QByteArray(), 0, 0)});
    for (int c = 0; c < ColorModes::channelCount(mode); ++c) r.channels.append({c, channelBlock(QByteArray(), 0, 0)});
    r.extra = unicodeBlock(r.name) + sectionBlock(3, BlendMode::Normal);
    return r;
}

} // namespace

// ================================ API ================================

bool isPsdPath(const QString& path)
{
    const QString ext = QFileInfo(path).suffix().toLower();
    return ext == QLatin1String("psd") || ext == QLatin1String("psb") || ext == QLatin1String("pdd");
}

Document* decode(const QByteArray& data, const QString& title, QString* error, QStringList* warnings)
{
    auto fail = [error](const QString& msg) -> Document* {
        if (error) *error = msg;
        return nullptr;
    };
    Reader r(data);
    if (r.bytes(4) != "8BPS") return fail(QStringLiteral("it is not a valid Photoshop document."));
    const int version = r.u16();
    if (version != 1 && version != 2) return fail(QStringLiteral("this Photoshop document version is not supported."));
    r.psb = version == 2;
    r.skip(6);
    Context c;
    c.psb = r.psb;
    const int channels = r.u16();
    c.height = int(r.u32());
    c.width = int(r.u32());
    c.depth = r.u16();
    c.mode = r.u16();
    c.data = r.data();
    if (!r.ok() || c.width <= 0 || c.height <= 0 || c.width > 300000 || c.height > 300000 || channels < 1 || channels > 56)
        return fail(QStringLiteral("the document is damaged."));
    if (c.depth != 1 && c.depth != 8 && c.depth != 16 && c.depth != 32)
        return fail(QStringLiteral("its bit depth is not supported."));
    if (c.mode == kMultichannel) return fail(QStringLiteral("Multichannel documents are not supported."));
    if (c.mode != kBitmap && c.mode != kGray && c.mode != kIndexed && c.mode != kRGB && c.mode != kCMYK && c.mode != kDuotone
        && c.mode != kLab)
        return fail(QStringLiteral("its color mode is not supported."));

    // Colour mode data: the palette of Indexed documents.
    const quint32 cmLen = r.u32();
    const QByteArray cm = r.bytes(cmLen);
    if (c.mode == kIndexed && cm.size() >= 768)
        for (int i = 0; i < 256; ++i) c.palette << qRgb(quint8(cm[i]), quint8(cm[256 + i]), quint8(cm[512 + i]));

    DocState s;
    s.size = QSize(c.width, c.height);
    s.dpi = 72.0;
    switch (c.mode) {
    case kBitmap:
    case kGray:
    case kDuotone: s.mode = ColorMode::Grayscale; break;
    case kCMYK: s.mode = ColorMode::CMYK; break;
    case kLab: s.mode = ColorMode::Lab; break;
    default: s.mode = ColorMode::RGB; break;
    }

    const quint32 resLen = r.u32();
    readResources(r.bytes(resLen), s);

    // Layer and mask information.
    const quint64 lmLen = r.length();
    const qint64 lmStart = r.pos();
    const qint64 lmEnd = lmStart + qint64(lmLen);
    if (!r.ok() || lmEnd > r.size()) return fail(QStringLiteral("the document is damaged."));
    QList<Record> records;
    bool mergedAlpha = false;
    if (lmLen > 0) {
        const quint64 liLen = r.length();
        const qint64 liEnd = r.pos() + qint64(liLen);
        if (liLen > 0 && !readLayerInfo(r, liEnd, records, &mergedAlpha))
            return fail(QStringLiteral("its layers are damaged."));
        r.seek(liEnd);
        if (r.pos() + 4 <= lmEnd) r.skip(r.u32()); // global layer mask info
        // 16- and 32-bit documents keep their layers in a tagged block here.
        while (r.ok() && r.pos() + 12 <= lmEnd) {
            const QByteArray sig(reinterpret_cast<const char*>(r.data() + r.pos()), 4);
            if (sig != "8BIM" && sig != "8B64") {
                r.skip(1); // unrecorded padding
                continue;
            }
            r.skip(4);
            const QByteArray key = r.bytes(4);
            const quint64 len = (r.psb && isLargeKey(key)) ? r.u64() : r.u32();
            const qint64 end = r.pos() + qint64(len);
            if (len > quint64(lmEnd - r.pos())) break;
            if (records.isEmpty() && (key == "Lr16" || key == "Lr32" || key == "Layr") && len > 0
                && !readLayerInfo(r, end, records, &mergedAlpha))
                return fail(QStringLiteral("its layers are damaged."));
            r.seek(end);
        }
    }
    r.seek(lmEnd);

    QSet<QString> problems;
    if (c.depth > 8)
        problems << QStringLiteral("The document was converted from %1 to 8 bits per channel.").arg(c.depth);

    // Build the layer list bottom to top, turning section dividers into groups.
    QList<Layer>& layers = s.layers;
    QList<int> openGroups;
    for (const Record& rec : std::as_const(records)) {
        const QByteArray sec = rec.info.contains("lsct") ? rec.info["lsct"] : rec.info.value("lsdk");
        int type = 0;
        if (sec.size() >= 4) type = int(qFromBigEndian<quint32>(sec.constData()));
        if (type == 3) {
            openGroups.append(int(layers.size()));
            continue;
        }
        Layer l = buildLayer(c, rec, problems);
        if (type == 1 || type == 2) {
            l.kind = LayerKind::Group;
            l.image = QImage();
            l.adjustment.reset();
            l.expanded = type == 1;
            l.mode = sec.size() >= 12 ? blendFromKey(sec.mid(8, 4)) : blendFromKey(rec.blend);
            l.fill = 1.f;
            const int start = openGroups.isEmpty() ? int(layers.size()) : openGroups.takeLast();
            layers.append(l);
            for (int k = start; k < layers.size() - 1; ++k)
                if (layers[k].parent == 0) layers[k].parent = l.id;
            continue;
        }
        layers.append(l);
    }

    // Photoshop's Background layer has no transparency channel.
    if (!records.isEmpty() && !layers.isEmpty()) {
        const Record& bottom = records.first();
        Layer& l = layers.first();
        if (l.kind == LayerKind::Pixel && l.parent == 0 && !bottom.channel(-1) && l.rect() == QRect(QPoint(), s.size)) {
            l.isBackground = true;
            l.lockTransparency = false;
        }
    }

    if (layers.isEmpty()) {
        // A flat file: the merged image is the document.
        bool ok = false;
        const QImage merged = readMerged(r, c, channels, mergedAlpha, &ok);
        if (!ok) return fail(QStringLiteral("its image data is damaged."));
        Layer l = Layer::create(mergedAlpha ? QStringLiteral("Layer 0") : QStringLiteral("Background"));
        l.isBackground = !mergedAlpha;
        l.image = merged;
        layers.append(l);
    }
    s.active = int(layers.size()) - 1;
    if (warnings) {
        QStringList list = problems.values();
        list.sort();
        *warnings = list;
    }
    auto* doc = new Document(s.size);
    doc->initialize(s);
    doc->setTitle(title);
    return doc;
}

Document* load(const QString& path, QString* error, QStringList* warnings)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = f.errorString();
        return nullptr;
    }
    const QByteArray data = f.readAll();
    Document* doc = decode(data, QFileInfo(path).fileName(), error, warnings);
    if (doc) doc->setFilePath(path);
    return doc;
}

QByteArray encode(Document* doc, QString* error, QStringList* warnings)
{
    if (doc->width() > kMaxPsdSide || doc->height() > kMaxPsdSide) {
        if (error) *error = QStringLiteral("Photoshop documents are limited to 30,000 pixels in each direction.");
        return QByteArray();
    }
    const ColorMode mode = doc->colorMode();
    const int nc = ColorModes::channelCount(mode);
    const QList<Layer>& layers = doc->layers();
    QStringList baked, rasterized, dropped;

    QList<OutRecord> out;
    for (int i = 0; i < layers.size(); ++i) {
        const Layer& l = layers[i];
        // Dividers for the groups whose contents start here, outermost first.
        QList<int> starting;
        if (l.isGroup() && Tree::subtreeStart(layers, i) == i) starting << i;
        for (int g = i, p = Tree::parentIndex(layers, g); p >= 0 && Tree::subtreeStart(layers, p) == i;
             g = p, p = Tree::parentIndex(layers, g))
            starting << p;
        for (int k = int(starting.size()) - 1; k >= 0; --k) out.append(dividerRecord(mode));

        OutRecord r;
        r.name = l.name;
        r.opacity = std::clamp(int(std::lround(l.opacity * 255.f)), 0, 255);
        r.clipped = l.clipped && !l.isBackground;
        r.blend = keyFromBlend(l.mode == BlendMode::PassThrough ? BlendMode::Normal : l.mode);
        r.flags = 0x08 | (l.visible ? 0 : 2) | (l.lockTransparency ? 1 : 0);
        QByteArray extra = unicodeBlock(l.name);
        int fill = std::clamp(int(std::lround(l.fill * 255.f)), 0, 255);

        QImage img;
        QPoint at;
        if (l.isGroup()) {
            extra += sectionBlock(l.expanded ? 1 : 2, l.mode);
        } else if (l.kind == LayerKind::Adjustment) {
            QByteArray key;
            const QByteArray data = l.adjustment ? adjustmentBlock(*l.adjustment, &key) : QByteArray();
            if (key.isEmpty()) {
                dropped << l.name;
                continue;
            }
            extra += block(key.constData(), data);
        } else if (l.hasStyle()) {
            // Effects become pixels: render the layer alone with its fill, at full opacity.
            QList<Layer> tmp = layers;
            Layer& t = tmp[i];
            t.opacity = 1.f;
            t.mode = BlendMode::Normal;
            t.clipped = false;
            t.mask.reset();
            t.visible = true;
            t.parent = 0;
            const QRect area = Compositor::extent(tmp, i, doc->bounds() | l.rect());
            img = Compositor::renderSingle(tmp, i, area);
            at = area.topLeft();
            fill = 255;
            baked << l.name;
        } else {
            img = l.image;
            at = l.offset;
            if (l.isVector()) rasterized << l.name;
        }
        extra += block("iOpa", QByteArray(1, char(fill)));
        quint32 locks = (l.lockTransparency ? 1u : 0u) | (l.lockPixels ? 2u : 0u) | (l.lockPosition ? 4u : 0u)
            | (l.lockAll ? 0x80000000u : 0u);
        {
            Writer w;
            w.u32(locks);
            extra += block("lspf", w.buf);
        }

        // Pixels.
        if (!img.isNull()) {
            r.rect = QRect(at, img.size());
            const QList<QByteArray> planes = splitPlanes(img, mode, !l.isBackground);
            if (!l.isBackground) r.channels.append({-1, channelBlock(planes[nc], img.width(), img.height())});
            for (int ch = 0; ch < nc; ++ch) r.channels.append({ch, channelBlock(planes[ch], img.width(), img.height())});
        } else {
            r.channels.append({-1, channelBlock(QByteArray(), 0, 0)});
            for (int ch = 0; ch < nc; ++ch) r.channels.append({ch, channelBlock(QByteArray(), 0, 0)});
        }
        if (l.mask) {
            r.hasMask = true;
            r.maskDefault = l.maskDefault >= 128 ? 255 : 0;
            r.maskFlags = (l.maskEnabled ? 0 : 2) | (l.maskLinked ? 0 : 1);
            const Layer& m = *l.mask;
            r.maskRect = m.image.isNull() ? QRect() : m.rect();
            r.channels.append({-2, channelBlock(maskPlane(m, l.maskDefault), m.image.width(), m.image.height())});
        }
        r.extra = extra;
        out.append(r);
    }

    // The composite, for readers that do not composite layers themselves.
    const QImage& composite = doc->composite();
    bool transparent = false;
    for (int y = 0; y < composite.height() && !transparent; ++y) {
        const auto* row = reinterpret_cast<const QRgb*>(composite.constScanLine(y));
        for (int x = 0; x < composite.width(); ++x)
            if (qAlpha(row[x]) != 255) {
                transparent = true;
                break;
            }
    }

    Writer w;
    w.raw("8BPS");
    w.u16(1);
    w.buf.append(6, '\0');
    w.u16(nc + (transparent ? 1 : 0));
    w.u32(quint32(doc->height()));
    w.u32(quint32(doc->width()));
    w.u16(8);
    switch (mode) {
    case ColorMode::Grayscale: w.u16(kGray); break;
    case ColorMode::CMYK: w.u16(kCMYK); break;
    case ColorMode::Lab: w.u16(kLab); break;
    default: w.u16(kRGB); break;
    }
    w.u32(0); // colour mode data

    // Image resources: resolution and guides.
    const qsizetype res = w.beginLength();
    {
        w.raw("8BIM");
        w.u16(1005);
        w.u16(0); // empty name, padded
        w.u32(16);
        const quint32 fixed = quint32(std::lround(doc->dpi() * 65536.0));
        w.u32(fixed);
        w.u16(1); // pixels per inch
        w.u16(1); // width shown in inches
        w.u32(fixed);
        w.u16(1);
        w.u16(1);
    }
    if (!doc->guides().isEmpty()) {
        w.raw("8BIM");
        w.u16(1032);
        w.u16(0);
        const QList<Guide>& guides = doc->guides();
        const qsizetype len = 16 + 5 * guides.size();
        w.u32(quint32(len));
        w.u32(1);
        w.u32(576);
        w.u32(576);
        w.u32(quint32(guides.size()));
        for (const Guide& g : guides) {
            w.i32(qint32(std::lround(g.position * 32.0)));
            w.u8(g.orientation == Qt::Vertical ? 0 : 1);
        }
        if (len % 2) w.u8(0);
    }
    w.endLength(res);

    // Layer and mask information.
    const qsizetype lm = w.beginLength();
    const qsizetype li = w.beginLength();
    w.u16(quint16(transparent ? -int(out.size()) : int(out.size())));
    for (const OutRecord& r : std::as_const(out)) writeRecord(w, r);
    for (const OutRecord& r : std::as_const(out))
        for (const auto& c : r.channels) w.raw(c.second);
    w.endLength(li, 4);
    w.u32(0); // global layer mask info
    w.endLength(lm);

    // Merged image data, RLE.
    const QList<QByteArray> planes = splitPlanes(composite, mode, transparent);
    QByteArray counts, data;
    for (const QByteArray& p : planes) rlePlane(p, doc->width(), doc->height(), counts, data);
    w.u16(1);
    w.raw(counts);
    w.raw(data);

    if (warnings) {
        warnings->clear();
        if (!rasterized.isEmpty())
            *warnings << QStringLiteral("Type and shape layers were saved as pixels: %1.").arg(rasterized.join(QStringLiteral(", ")));
        if (!baked.isEmpty())
            *warnings << QStringLiteral("Layer styles were merged into their layers: %1.").arg(baked.join(QStringLiteral(", ")));
        if (!dropped.isEmpty())
            *warnings << QStringLiteral("Black & White adjustment layers cannot be saved in Photoshop format and were left out: %1.")
                             .arg(dropped.join(QStringLiteral(", ")));
    }
    return w.buf;
}

bool save(Document* doc, const QString& path, QString* error, QStringList* warnings)
{
    const QByteArray data = encode(doc, error, warnings);
    if (data.isEmpty()) return false;
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        if (error) *error = f.errorString();
        return false;
    }
    f.write(data);
    if (!f.commit()) {
        if (error) *error = f.errorString();
        return false;
    }
    return true;
}

} // namespace Psd
