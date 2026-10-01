#include "swf.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <mutex>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG   // we only need its zlib inflate
#define STBI_NO_STDIO
#include "stb_image.h"

namespace cr {
namespace {

// --- bit reader ---------------------------------------------------------------------

struct Bits {
    const uint8_t* b;
    size_t n, p;
    int bit = 0;
    Bits(const uint8_t* b_, size_t n_, size_t p_) : b(b_), n(n_), p(p_) {}
    uint32_t ub(int k) {
        uint32_t v = 0;
        for (int i = 0; i < k; ++i) {
            uint8_t byte = p < n ? b[p] : 0;
            v = (v << 1) | ((byte >> (7 - bit)) & 1);
            if (++bit == 8) { bit = 0; ++p; }
        }
        return v;
    }
    int32_t sb(int k) {
        if (!k) return 0;
        uint32_t v = ub(k);
        return (v & (1u << (k - 1))) ? (int32_t)(v - (1ull << k)) : (int32_t)v;
    }
    double fb(int k) { return sb(k) / 65536.0; }
    void align() { if (bit) { bit = 0; ++p; } }
    uint8_t u8() { align(); return p < n ? b[p++] : 0; }
    uint16_t u16() { align(); uint16_t v = p + 1 < n ? (uint16_t)(b[p] | b[p + 1] << 8) : 0; p += 2; return v; }
    void rect() { int k = ub(5); for (int i = 0; i < 4; ++i) sb(k); align(); }
    void rect(int32_t r[4]) { int k = ub(5); for (int i = 0; i < 4; ++i) r[i] = sb(k); align(); }
    void cstr() { align(); while (p < n && b[p]) ++p; ++p; }
};

struct Mat {  // x' = a x + c y + tx ; y' = b x + d y + ty   (twips)
    double a = 1, b = 0, c = 0, d = 1, tx = 0, ty = 0;
    Mat operator*(const Mat& n) const {  // this applied after n
        return {a * n.a + c * n.b, b * n.a + d * n.b, a * n.c + c * n.d, b * n.c + d * n.d,
                a * n.tx + c * n.ty + tx, b * n.tx + d * n.ty + ty};
    }
    void apply(double x, double y, double& ox, double& oy) const { ox = a * x + c * y + tx; oy = b * x + d * y + ty; }
    bool inverse(Mat& o) const {
        double det = a * d - b * c;
        if (std::fabs(det) < 1e-12) return false;
        o.a = d / det; o.b = -b / det; o.c = -c / det; o.d = a / det;
        o.tx = -(o.a * tx + o.c * ty); o.ty = -(o.b * tx + o.d * ty);
        return true;
    }
};

Mat read_matrix(Bits& bs) {
    bs.align();
    Mat m;
    if (bs.ub(1)) { int k = bs.ub(5); m.a = bs.fb(k); m.d = bs.fb(k); }
    if (bs.ub(1)) { int k = bs.ub(5); m.b = bs.fb(k); m.c = bs.fb(k); }
    int k = bs.ub(5);
    m.tx = bs.sb(k);
    m.ty = bs.sb(k);
    bs.align();
    return m;
}

struct CX {
    float mul[4] = {1, 1, 1, 1};
    float add[4] = {0, 0, 0, 0};
    CX operator*(const CX& in) const {  // this applied after `in`
        CX o;
        for (int i = 0; i < 4; ++i) { o.mul[i] = mul[i] * in.mul[i]; o.add[i] = mul[i] * in.add[i] + add[i]; }
        return o;
    }
    void apply(const uint8_t c[4], float out[4]) const {
        for (int i = 0; i < 4; ++i) out[i] = std::clamp((c[i] * mul[i] + add[i]) / 255.0f, 0.0f, 1.0f);
    }
};

CX read_cxform(Bits& bs, bool alpha) {
    bs.align();
    CX cx;
    int has_add = bs.ub(1), has_mul = bs.ub(1), k = bs.ub(4), m = alpha ? 4 : 3;
    if (has_mul) for (int i = 0; i < m; ++i) cx.mul[i] = bs.sb(k) / 256.0f;
    if (has_add) for (int i = 0; i < m; ++i) cx.add[i] = (float)bs.sb(k);
    bs.align();
    return cx;
}

// --- shapes -------------------------------------------------------------------------

struct Fill {
    int type = 0;            // 0 solid, 1 linear, 2 radial, 3 bitmap
    uint8_t color[4] = {0, 0, 0, 255};
    Mat m;
    std::vector<std::pair<uint8_t, std::array<uint8_t, 4>>> stops;
    uint16_t bitmap = 0;
    bool smooth_repeat = false;
};

void read_color(Bits& bs, bool alpha, uint8_t c[4]) {
    bs.align();
    c[0] = bs.u8(); c[1] = bs.u8(); c[2] = bs.u8(); c[3] = alpha ? bs.u8() : 255;
}

Fill read_fill(Bits& bs, int ver) {
    Fill f;
    uint8_t t = bs.u8();
    if (t == 0x00) { read_color(bs, ver >= 3, f.color); }
    else if (t == 0x10 || t == 0x12 || t == 0x13) {
        f.type = t == 0x10 ? 1 : 2;
        f.m = read_matrix(bs);
        bs.align();
        bs.ub(2); bs.ub(2);
        int ng = bs.ub(4);
        for (int i = 0; i < ng; ++i) {
            uint8_t r = bs.u8();
            std::array<uint8_t, 4> c;
            read_color(bs, ver >= 3, c.data());
            f.stops.push_back({r, c});
        }
        if (t == 0x13) bs.u16();
        if (!f.stops.empty()) memcpy(f.color, f.stops[f.stops.size() / 2].second.data(), 4);
    } else if (t >= 0x40 && t <= 0x43) {
        f.type = 3;
        f.bitmap = bs.u16();
        f.m = read_matrix(bs);
        f.smooth_repeat = t == 0x40 || t == 0x42;
    } else {
        f.type = -1;
    }
    return f;
}

struct Line { double width; uint8_t color[4]; };

struct Edge {
    int f0, f1, l;
    std::vector<std::pair<double, double>> pts;   // twips
};

struct Group {
    std::vector<Fill> fills;
    std::vector<Line> lines;
    std::vector<Edge> edges;
};

struct Shape {
    int32_t bounds[4] = {};  // xmin xmax ymin ymax
    std::vector<Group> groups;
};

void read_styles(Bits& bs, int ver, Group& g) {
    int n = bs.u8();
    if (n == 0xFF && ver >= 2) n = bs.u16();
    for (int i = 0; i < n; ++i) g.fills.push_back(read_fill(bs, ver));
    n = bs.u8();
    if (n == 0xFF) n = bs.u16();
    for (int i = 0; i < n; ++i) {
        Line l;
        l.width = bs.u16();
        if (ver == 4) {
            bs.align();
            bs.ub(2);
            int join = bs.ub(2), has_fill = bs.ub(1);
            bs.ub(3); bs.ub(5); bs.ub(1); bs.ub(2);
            if (join == 2) bs.u16();
            if (has_fill) { Fill f = read_fill(bs, ver); memcpy(l.color, f.color, 4); }
            else read_color(bs, true, l.color);
        } else {
            read_color(bs, ver >= 3, l.color);
        }
        g.lines.push_back(l);
    }
}

bool parse_shape(const uint8_t* b, size_t n, size_t p, int code, Shape& s) {
    int ver = code == 2 ? 1 : code == 22 ? 2 : code == 32 ? 3 : 4;
    Bits bs(b, n, p);
    bs.u16();
    bs.rect(s.bounds);
    if (ver == 4) { bs.rect(); bs.u8(); }
    s.groups.emplace_back();
    read_styles(bs, ver, s.groups.back());
    bs.align();
    int nf = bs.ub(4), nl = bs.ub(4);
    double x = 0, y = 0;
    int f0 = 0, f1 = 0, l = 0;
    for (int guard = 0; guard < 2000000; ++guard) {
        if (bs.p >= n) return false;
        if (bs.ub(1) == 0) {
            int nw = bs.ub(1), ls = bs.ub(1), fs1 = bs.ub(1), fs0 = bs.ub(1), mv = bs.ub(1);
            if (!(nw | ls | fs1 | fs0 | mv)) break;
            if (mv) { int k = bs.ub(5); x = bs.sb(k); y = bs.sb(k); }
            if (fs0) f0 = bs.ub(nf);
            if (fs1) f1 = bs.ub(nf);
            if (ls) l = bs.ub(nl);
            if (nw) {
                s.groups.emplace_back();
                read_styles(bs, ver, s.groups.back());
                bs.align();
                nf = bs.ub(4); nl = bs.ub(4);
            }
        } else {
            Edge e{f0, f1, l, {}};
            e.pts.push_back({x, y});
            if (bs.ub(1)) {
                int k = bs.ub(4) + 2;
                double dx = 0, dy = 0;
                if (bs.ub(1)) { dx = bs.sb(k); dy = bs.sb(k); }
                else if (bs.ub(1)) dy = bs.sb(k);
                else dx = bs.sb(k);
                x += dx; y += dy;
                e.pts.push_back({x, y});
            } else {
                int k = bs.ub(4) + 2;
                double cx = x + bs.sb(k), cy = y + bs.sb(k);
                double ex = cx + bs.sb(k), ey = cy + bs.sb(k);
                for (int i = 1; i <= 8; ++i) {
                    double t = i / 8.0, u = 1 - t;
                    e.pts.push_back({u * u * x + 2 * u * t * cx + t * t * ex, u * u * y + 2 * u * t * cy + t * t * ey});
                }
                x = ex; y = ey;
            }
            s.groups.back().edges.push_back(std::move(e));
        }
    }
    return true;
}

// --- rasteriser ---------------------------------------------------------------------

struct Seg { double x0, y0, x1, y1; };

struct Canvas {
    int w, h;
    std::vector<float> px;   // premultiplied RGBA
    std::vector<float> cov;
    Canvas(int w_, int h_) : w(w_), h(h_), px((size_t)w_ * h_ * 4, 0.0f), cov((size_t)w_ * h_) {}

    // Nonzero-winding coverage: 5 sub-rows per pixel, exact horizontal coverage.
    void coverage(const std::vector<Seg>& segs) {
        std::fill(cov.begin(), cov.end(), 0.0f);
        constexpr int SS = 5;
        std::vector<std::pair<double, int>> xs;
        double ymin = 1e30, ymax = -1e30;
        for (auto& s : segs) { ymin = std::min({ymin, s.y0, s.y1}); ymax = std::max({ymax, s.y0, s.y1}); }
        int r0 = std::max(0, (int)std::floor(ymin)), r1 = std::min(h - 1, (int)std::ceil(ymax));
        for (int row = r0; row <= r1; ++row) {
            float* c = &cov[(size_t)row * w];
            for (int sr = 0; sr < SS; ++sr) {
                double y = row + (sr + 0.5) / SS;
                xs.clear();
                for (auto& s : segs) {
                    if ((s.y0 <= y && s.y1 > y) || (s.y1 <= y && s.y0 > y)) {
                        double t = (y - s.y0) / (s.y1 - s.y0);
                        xs.push_back({s.x0 + t * (s.x1 - s.x0), s.y1 > s.y0 ? 1 : -1});
                    }
                }
                if (xs.size() < 2) continue;
                std::sort(xs.begin(), xs.end());
                int wind = 0;
                for (size_t k = 0; k + 1 < xs.size(); ++k) {
                    wind += xs[k].second;
                    if (!wind) continue;
                    double xa = std::max(0.0, xs[k].first), xb = std::min((double)w, xs[k + 1].first);
                    if (xb <= xa) continue;
                    int pa = (int)xa, pb = std::min(w - 1, (int)xb);
                    for (int pxi = pa; pxi <= pb; ++pxi) {
                        double ov = std::min(xb, pxi + 1.0) - std::max(xa, (double)pxi);
                        if (ov > 0) c[pxi] += (float)(ov / SS);
                    }
                }
            }
        }
    }

    template <class ColorFn>
    void composite(ColorFn color_at) {
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                float cv = std::min(1.0f, cov[(size_t)y * w + x]);
                if (cv <= 0.0f) continue;
                float c[4];
                color_at(x + 0.5, y + 0.5, c);
                float a = c[3] * cv;
                float* d = &px[((size_t)y * w + x) * 4];
                for (int i = 0; i < 3; ++i) d[i] = c[i] * a + d[i] * (1 - a);
                d[3] = a + d[3] * (1 - a);
            }
    }
};

void add_circle(std::vector<Seg>& out, double cx, double cy, double r) {
    constexpr int N = 16;
    for (int i = 0; i < N; ++i) {
        double a0 = 2 * 3.14159265358979 * i / N, a1 = 2 * 3.14159265358979 * (i + 1) / N;
        out.push_back({cx + r * cos(a0), cy + r * sin(a0), cx + r * cos(a1), cy + r * sin(a1)});
    }
}

// A thick segment as a quad, wound the same way as add_circle so the nonzero
// union of all of them is the stroke.
void add_quad(std::vector<Seg>& out, double x0, double y0, double x1, double y1, double r) {
    double dx = x1 - x0, dy = y1 - y0, len = std::sqrt(dx * dx + dy * dy);
    if (len < 1e-9) return;
    double nx = -dy / len * r, ny = dx / len * r;
    double p[4][2] = {{x0 - nx, y0 - ny}, {x1 - nx, y1 - ny}, {x1 + nx, y1 + ny}, {x0 + nx, y0 + ny}};
    double area = 0;
    for (int i = 0; i < 4; ++i) area += p[i][0] * p[(i + 1) % 4][1] - p[(i + 1) % 4][0] * p[i][1];
    for (int i = 0; i < 4; ++i) {
        int a = area > 0 ? i : 3 - i, b = area > 0 ? (i + 1) % 4 : (6 - i) % 4;
        out.push_back({p[a][0], p[a][1], p[b][0], p[b][1]});
    }
}

}  // namespace

// --- document ---------------------------------------------------------------------------

struct Char { int kind; size_t p; size_t len; int code; };   // kind: 1 shape, 2 sprite, 3 bitmap

struct Bitmap { int w = 0, h = 0; std::vector<uint8_t> rgba; };

struct SwfDoc::Impl {
    const std::vector<uint8_t>* body = nullptr;
    std::unordered_map<uint16_t, Char> chars;
    mutable std::mutex mu;
    mutable std::unordered_map<uint16_t, std::shared_ptr<Shape>> shapes;
    mutable std::unordered_map<uint16_t, std::shared_ptr<Bitmap>> bitmaps;

    struct Place { uint16_t cid = 0; Mat m; CX cx; uint16_t clip = 0; };

    void scan(size_t pos, size_t end);
    std::shared_ptr<Shape> shape(uint16_t id) const;
    std::shared_ptr<Bitmap> bitmap(uint16_t id) const;
    std::map<uint16_t, Place> display_list(uint16_t id, int frame) const;
    int frames(uint16_t id) const;
    void bounds(uint16_t id, const Mat& m, int frame, int depth, double& x0, double& y0, double& x1, double& y1) const;
    void draw(Canvas& cv, uint16_t id, const Mat& m, const CX& cx, int frame, int depth) const;
    void draw_shape(Canvas& cv, const Shape& s, const Mat& m, const CX& cx) const;
};

namespace {
template <class F>
void for_tags(const std::vector<uint8_t>& b, size_t pos, size_t end, F fn) {
    while (pos + 2 <= end) {
        uint16_t h = (uint16_t)(b[pos] | b[pos + 1] << 8);
        pos += 2;
        int code = h >> 6;
        size_t len = h & 0x3F;
        if (len == 0x3F) {
            if (pos + 4 > end) return;
            len = b[pos] | b[pos + 1] << 8 | b[pos + 2] << 16 | (size_t)b[pos + 3] << 24;
            pos += 4;
        }
        if (pos + len > end) return;
        if (!fn(code, pos, len)) return;
        pos += len;
        if (code == 0) return;
    }
}
uint16_t rd16(const std::vector<uint8_t>& b, size_t p) { return (uint16_t)(b[p] | b[p + 1] << 8); }
}  // namespace

void SwfDoc::Impl::scan(size_t pos, size_t end) {
    const auto& b = *body;
    for_tags(b, pos, end, [&](int code, size_t p, size_t len) {
        if (len < 2) return true;
        uint16_t id = rd16(b, p);
        if (code == 2 || code == 22 || code == 32 || code == 83) chars[id] = {1, p, len, code};
        else if (code == 39) chars[id] = {2, p + 4, len - 4, code};
        else if (code == 20 || code == 36) chars[id] = {3, p, len, code};
        return true;
    });
}

std::shared_ptr<Shape> SwfDoc::Impl::shape(uint16_t id) const {
    std::lock_guard<std::mutex> lk(mu);
    auto it = shapes.find(id);
    if (it != shapes.end()) return it->second;
    auto c = chars.find(id);
    std::shared_ptr<Shape> s;
    if (c != chars.end() && c->second.kind == 1) {
        s = std::make_shared<Shape>();
        if (!parse_shape(body->data(), c->second.p + c->second.len, c->second.p, c->second.code, *s)) s->groups.clear();
    }
    shapes[id] = s;
    return s;
}

std::shared_ptr<Bitmap> SwfDoc::Impl::bitmap(uint16_t id) const {
    std::lock_guard<std::mutex> lk(mu);
    auto it = bitmaps.find(id);
    if (it != bitmaps.end()) return it->second;
    std::shared_ptr<Bitmap> bm;
    auto c = chars.find(id);
    if (c != chars.end() && c->second.kind == 3 && c->second.len > 7) {
        const uint8_t* p = body->data() + c->second.p;
        int fmt = p[2], w = p[3] | p[4] << 8, h = p[5] | p[6] << 8;
        if (fmt == 5 && w > 0 && h > 0) {   // 32-bit ARGB (premultiplied for Lossless2)
            int outlen = 0;
            char* raw = stbi_zlib_decode_malloc_guesssize_headerflag((const char*)p + 7, (int)c->second.len - 7,
                                                                     w * h * 4, &outlen, 1);
            if (raw && outlen >= w * h * 4) {
                bm = std::make_shared<Bitmap>();
                bm->w = w; bm->h = h;
                bm->rgba.resize((size_t)w * h * 4);
                bool alpha = c->second.code == 36;
                for (int i = 0; i < w * h; ++i) {
                    uint8_t a = alpha ? (uint8_t)raw[i * 4] : 255;
                    for (int k = 0; k < 3; ++k) {
                        int v = (uint8_t)raw[i * 4 + 1 + k];
                        if (alpha && a) v = std::min(255, v * 255 / a);   // un-premultiply
                        bm->rgba[i * 4 + k] = (uint8_t)v;
                    }
                    bm->rgba[i * 4 + 3] = a;
                }
            }
            if (raw) free(raw);
        }
    }
    bitmaps[id] = bm;
    return bm;
}

std::map<uint16_t, SwfDoc::Impl::Place> SwfDoc::Impl::display_list(uint16_t id, int frame) const {
    std::map<uint16_t, Place> dl;
    auto c = chars.find(id);
    if (c == chars.end() || c->second.kind != 2) return dl;
    const auto& b = *body;
    int f = 0;
    for_tags(b, c->second.p, c->second.p + c->second.len, [&](int code, size_t p, size_t len) {
        if (code == 26 || code == 70) {
            Bits bs(b.data(), p + len, p);
            uint8_t flags = bs.u8(), flags2 = code == 70 ? bs.u8() : 0;
            uint16_t depth = bs.u16();
            if (code == 70 && (flags2 & 0x08)) bs.cstr();   // class name
            Place pl = (flags & 0x01) && dl.count(depth) ? dl[depth] : Place{};
            if (flags & 0x02) pl.cid = bs.u16();
            if (flags & 0x04) pl.m = read_matrix(bs);
            if (flags & 0x08) pl.cx = read_cxform(bs, true);
            if (flags & 0x10) bs.u16();
            if (flags & 0x20) bs.cstr();
            if (flags & 0x40) pl.clip = bs.u16();
            dl[depth] = pl;
        } else if (code == 28 && len >= 2) {
            dl.erase(rd16(b, p));
        } else if (code == 5 && len >= 4) {
            dl.erase(rd16(b, p + 2));
        } else if (code == 1) {
            if (f == frame) return false;
            ++f;
        }
        return true;
    });
    return dl;
}

int SwfDoc::Impl::frames(uint16_t id) const {
    auto c = chars.find(id);
    if (c == chars.end() || c->second.kind != 2) return 1;
    int n = 0;
    for_tags(*body, c->second.p, c->second.p + c->second.len, [&](int code, size_t, size_t) {
        if (code == 1) ++n;
        return true;
    });
    return std::max(1, n);
}

void SwfDoc::Impl::bounds(uint16_t id, const Mat& m, int frame, int depth, double& x0, double& y0, double& x1,
                          double& y1) const {
    auto c = chars.find(id);
    if (c == chars.end() || depth > 24) return;
    if (c->second.kind == 1) {
        auto s = shape(id);
        if (!s) return;
        double xs[2] = {(double)s->bounds[0], (double)s->bounds[1]}, ys[2] = {(double)s->bounds[2], (double)s->bounds[3]};
        for (double x : xs)
            for (double y : ys) {
                double ox, oy;
                m.apply(x, y, ox, oy);
                x0 = std::min(x0, ox); y0 = std::min(y0, oy); x1 = std::max(x1, ox); y1 = std::max(y1, oy);
            }
    } else if (c->second.kind == 2) {
        for (auto& [d, pl] : display_list(id, frame))
            if (pl.cid && !pl.clip) bounds(pl.cid, m * pl.m, 0, depth + 1, x0, y0, x1, y1);
    }
}

void SwfDoc::Impl::draw_shape(Canvas& cv, const Shape& s, const Mat& m, const CX& cx) const {
    double scale = std::sqrt(std::fabs(m.a * m.d - m.b * m.c));
    std::vector<Seg> segs;
    for (const Group& g : s.groups) {
        // Fills: collect every edge bordering a style, oriented so the style is on
        // the same side (fill1 as-is, fill0 reversed); then nonzero fill.
        std::map<int, std::vector<Seg>> per;
        for (const Edge& e : g.edges) {
            for (size_t i = 0; i + 1 < e.pts.size(); ++i) {
                double ax, ay, bx, by;
                m.apply(e.pts[i].first, e.pts[i].second, ax, ay);
                m.apply(e.pts[i + 1].first, e.pts[i + 1].second, bx, by);
                ax /= 20; ay /= 20; bx /= 20; by /= 20;
                if (e.f1) per[e.f1].push_back({ax, ay, bx, by});
                if (e.f0) per[e.f0].push_back({bx, by, ax, ay});
            }
        }
        for (auto& [fi, sg] : per) {
            if (fi - 1 >= (int)g.fills.size()) continue;
            const Fill& f = g.fills[fi - 1];
            if (f.type < 0) continue;
            cv.coverage(sg);
            if (f.type == 0) {
                float c[4];
                cx.apply(f.color, c);
                cv.composite([&](double, double, float* o) { memcpy(o, c, sizeof(c)); });
            } else if (f.type == 1 || f.type == 2) {
                Mat inv;
                if (!(m * f.m).inverse(inv)) continue;
                cv.composite([&](double x, double y, float* o) {
                    double gx, gy;
                    inv.apply(x * 20, y * 20, gx, gy);
                    double t = f.type == 1 ? (gx + 16384) / 32768.0 : std::sqrt(gx * gx + gy * gy) / 16384.0;
                    double r = std::clamp(t, 0.0, 1.0) * 255.0;
                    const auto& st = f.stops;
                    size_t k = 0;
                    while (k + 1 < st.size() && st[k + 1].first < r) ++k;
                    uint8_t col[4];
                    if (k + 1 >= st.size() || r <= st[k].first) memcpy(col, st[k].second.data(), 4);
                    else {
                        double u = (r - st[k].first) / std::max(1, st[k + 1].first - st[k].first);
                        for (int i = 0; i < 4; ++i) col[i] = (uint8_t)(st[k].second[i] * (1 - u) + st[k + 1].second[i] * u);
                    }
                    cx.apply(col, o);
                });
            } else if (f.type == 3) {
                auto bm = bitmap(f.bitmap);
                Mat inv;
                if (!bm || !(m * f.m).inverse(inv)) {
                    uint8_t grey[4] = {128, 128, 128, 255};
                    float c[4];
                    cx.apply(grey, c);
                    cv.composite([&](double, double, float* o) { memcpy(o, c, sizeof(c)); });
                    continue;
                }
                cv.composite([&](double x, double y, float* o) {
                    double u, v;
                    inv.apply(x * 20, y * 20, u, v);   // bitmap fill matrices map bitmap pixels to twips
                    int iu = (int)std::floor(u), iv = (int)std::floor(v);
                    if (f.smooth_repeat) { iu = ((iu % bm->w) + bm->w) % bm->w; iv = ((iv % bm->h) + bm->h) % bm->h; }
                    else { iu = std::clamp(iu, 0, bm->w - 1); iv = std::clamp(iv, 0, bm->h - 1); }
                    cx.apply(&bm->rgba[((size_t)iv * bm->w + iu) * 4], o);
                });
            }
        }
        // Strokes: round joins and caps, as a nonzero union of quads and discs.
        std::map<int, std::vector<const Edge*>> per_line;
        for (const Edge& e : g.edges) if (e.l) per_line[e.l].push_back(&e);
        for (auto& [li, edges] : per_line) {
            if (li - 1 >= (int)g.lines.size()) continue;
            const Line& ln = g.lines[li - 1];
            double r = std::max(0.5, ln.width * scale / 20.0) / 2.0;
            segs.clear();
            for (const Edge* e : edges) {
                for (size_t i = 0; i < e->pts.size(); ++i) {
                    double x, y;
                    m.apply(e->pts[i].first, e->pts[i].second, x, y);
                    x /= 20; y /= 20;
                    add_circle(segs, x, y, r);
                    if (i + 1 < e->pts.size()) {
                        double x2, y2;
                        m.apply(e->pts[i + 1].first, e->pts[i + 1].second, x2, y2);
                        add_quad(segs, x, y, x2 / 20, y2 / 20, r);
                    }
                }
            }
            cv.coverage(segs);
            float c[4];
            cx.apply(ln.color, c);
            cv.composite([&](double, double, float* o) { memcpy(o, c, sizeof(c)); });
        }
    }
}

void SwfDoc::Impl::draw(Canvas& cv, uint16_t id, const Mat& m, const CX& cx, int frame, int depth) const {
    auto c = chars.find(id);
    if (c == chars.end() || depth > 24) return;
    if (c->second.kind == 1) {
        if (auto s = shape(id)) draw_shape(cv, *s, m, cx);
    } else if (c->second.kind == 2) {
        for (auto& [d, pl] : display_list(id, frame))
            if (pl.cid && !pl.clip) draw(cv, pl.cid, m * pl.m, cx * pl.cx, 0, depth + 1);
    }
}

// --- public -------------------------------------------------------------------------------

bool SwfDoc::load(std::vector<uint8_t>&& file) {
    if (file.size() < 8 || file[0] != 'F' || file[1] != 'W' || file[2] != 'S') return false;   // game ships FWS
    body_ = std::move(file);
    body_.erase(body_.begin(), body_.begin() + 8);
    impl_ = std::make_shared<Impl>();
    impl_->body = &body_;
    int nbits = body_[0] >> 3;
    size_t pos = (5 + 4 * nbits + 7) / 8 + 4;
    impl_->scan(pos, body_.size());
    for_tags(body_, pos, body_.size(), [&](int code, size_t p, size_t len) {
        if (code == 76 || code == 56) {
            uint16_t n = rd16(body_, p);
            size_t q = p + 2;
            for (int i = 0; i < n && q + 2 < p + len; ++i) {
                uint16_t id = rd16(body_, q);
                q += 2;
                size_t e = q;
                while (e < p + len && body_[e]) ++e;
                symbols_[std::string((const char*)&body_[q], e - q)] = id;
                q = e + 1;
            }
        }
        return true;
    });
    return !symbols_.empty();
}

int SwfDoc::frame_count(const std::string& symbol) const {
    auto it = symbols_.find(symbol);
    return it == symbols_.end() ? 0 : impl_->frames(it->second);
}

bool SwfDoc::render(const std::string& symbol, int frame, int size, SwfImage& out) const {
    auto it = symbols_.find(symbol);
    if (it == symbols_.end() || size <= 0) return false;
    double x0 = 1e30, y0 = 1e30, x1 = -1e30, y1 = -1e30;
    impl_->bounds(it->second, Mat{}, frame, 0, x0, y0, x1, y1);
    if (x1 <= x0 || y1 <= y0) return false;
    double sc = size * 20.0 / std::max(x1 - x0, y1 - y0);
    int w = (int)std::ceil((x1 - x0) * sc / 20) + 2, h = (int)std::ceil((y1 - y0) * sc / 20) + 2;
    if (w > 2048 || h > 2048) return false;
    Mat m{sc, 0, 0, sc, -x0 * sc + 20, -y0 * sc + 20};
    Canvas cv(w, h);
    impl_->draw(cv, it->second, m, CX{}, frame, 0);
    out.w = w;
    out.h = h;
    out.rgba.resize((size_t)w * h * 4);
    for (size_t i = 0; i < (size_t)w * h; ++i) {
        float a = cv.px[i * 4 + 3];
        for (int k = 0; k < 3; ++k)
            out.rgba[i * 4 + k] = (uint8_t)std::clamp(a > 0 ? cv.px[i * 4 + k] / a * 255.0f : 0.0f, 0.0f, 255.0f);
        out.rgba[i * 4 + 3] = (uint8_t)std::clamp(a * 255.0f, 0.0f, 255.0f);
    }
    return true;
}

}  // namespace cr
