import os

root = os.path.join(os.path.dirname(__file__), '..', 'src')

p = os.path.join(root, 'swf.h')
s = open(p, encoding='utf-8').read()
if 'struct SwfOverride' not in s:
    s = s.replace('''class SwfDoc {
public:''', '''// Per-render tweaks for composed art (the game's cat portraits).
struct SwfOverride {
    enum Mode { Frame, Replace, Hide } mode = Frame;
    std::string symbol;      // Replace: draw this exported symbol instead
    int frame = 0;           // frame for the (replacement) sprite
    bool pos_only = false;   // keep only the marker's position (and its mirror sign)
};
struct SwfRenderOpts {
    std::unordered_map<std::string, SwfOverride> overrides;   // by instance name
    const uint8_t* palette = nullptr;   // 16 RGB triplets: greys are remapped like the game's paletted shader
    std::string bounds_symbol;          // size the image by this symbol's frame instead
    int bounds_frame = 0;
    float margin = 0.6f;                // canvas grows by this fraction around those bounds
};

class SwfDoc {
public:
    // Like render(), with overrides/palette/masks; output cropped to its opaque area.
    bool render_ex(const std::string& symbol, int frame, int size, const SwfRenderOpts& opts, SwfImage& out) const;
''')
    open(p, 'w', encoding='utf-8').write(s)

p = os.path.join(root, 'swf.cpp')
s = open(p, encoding='utf-8').read()
s = s.replace('''struct Canvas {
    int w, h;''', '''struct Canvas {
    const float* mask = nullptr;   // multiplies coverage (clip layers)
    int w, h;''')
s = s.replace('''                float cv = std::min(1.0f, cov[(size_t)y * w + x]);
                if (cv <= 0.0f) continue;''', '''                float cv = std::min(1.0f, cov[(size_t)y * w + x]);
                if (mask) cv *= mask[(size_t)y * w + x];
                if (cv <= 0.0f) continue;''')
s = s.replace('''// --- document ---------------------------------------------------------------------------''', '''// --- per-render context (one render at a time per thread) ----------------------------------

struct ResolvedOverride { SwfOverride::Mode mode; uint16_t cid; int frame; bool pos_only; };
struct RenderCtx {
    std::unordered_map<std::string, ResolvedOverride> ov;
    const uint8_t* palette = nullptr;
};
thread_local const RenderCtx* t_ctx = nullptr;

// The game's paletted shader: a grey (r==g==b) source colour picks entry
// round(r*15) of the cat's palette row; anything coloured is left alone.
void apply_col(const CX& cx, const uint8_t c[4], float out[4]) {
    if (t_ctx && t_ctx->palette && std::abs(c[0] - c[1]) <= 1 && std::abs(c[0] - c[2]) <= 1) {
        int i = (int)(c[0] / 255.0f * 15.0f + 0.5f);
        const uint8_t* p = t_ctx->palette + i * 3;
        uint8_t m[4] = {p[0], p[1], p[2], c[3]};
        cx.apply(m, out);
        return;
    }
    cx.apply(c, out);
}

// --- document ---------------------------------------------------------------------------''', 1)
a = s.index('void SwfDoc::Impl::draw_shape(')
b = s.index('void SwfDoc::Impl::draw(Canvas& cv')
body = s[a:b]
for old, new in (('cx.apply(f.color, c);', 'apply_col(cx, f.color, c);'),
                 ('cx.apply(col, o);', 'apply_col(cx, col, o);'),
                 ('cx.apply(grey, c);', 'apply_col(cx, grey, c);'),
                 ('cx.apply(&bm->rgba[((size_t)iv * bm->w + iu) * 4], o);', 'apply_col(cx, &bm->rgba[((size_t)iv * bm->w + iu) * 4], o);'),
                 ('cx.apply(ln.color, c);', 'apply_col(cx, ln.color, c);')):
    body = body.replace(old, new)
s = s[:a] + body + s[b:]

old_draw = '''    } else if (c->second.kind == 2) {
        for (auto& [d, pl] : display_list(id, frame)) {
            if (!pl.cid || pl.clip) continue;
            if (hidden && !pl.name.empty() &&
                std::find(hidden->begin(), hidden->end(), pl.name) != hidden->end())
                continue;
            draw(cv, pl.cid, m * pl.m, cx * pl.cx, 0, depth + 1);
        }
    }
}'''
new_draw = '''    } else if (c->second.kind == 2) {
        const float* inherited = cv.mask;
        const size_t n = (size_t)cv.w * cv.h;
        std::vector<std::pair<uint16_t, std::vector<float>>> masks;   // (clip depth, coverage)
        std::vector<float> eff;
        for (auto& [d, pl] : display_list(id, frame)) {
            while (!masks.empty() && d > masks.back().first) masks.pop_back();
            if (!pl.cid) continue;
            if (hidden && !pl.name.empty() &&
                std::find(hidden->begin(), hidden->end(), pl.name) != hidden->end())
                continue;
            uint16_t cid = pl.cid;
            int cframe = 0;
            Mat pm = pl.m;
            if (t_ctx && !pl.name.empty()) {
                auto o = t_ctx->ov.find(pl.name);
                if (o != t_ctx->ov.end()) {
                    if (o->second.mode == SwfOverride::Hide) continue;
                    if (o->second.mode == SwfOverride::Replace) cid = o->second.cid;
                    cframe = o->second.frame;
                    if (o->second.pos_only) pm = Mat{pl.m.a < 0 ? -1.0 : 1.0, 0, 0, 1.0, pl.m.tx, pl.m.ty};
                }
            }
            if (pl.clip) {
                // A mask layer: its coverage clips every depth up to pl.clip.
                Canvas mc(cv.w, cv.h);
                mc.mask = inherited;
                draw(mc, cid, m * pm, CX{}, cframe, depth + 1);
                std::vector<float> cov(n);
                for (size_t i = 0; i < n; ++i) cov[i] = mc.px[i * 4 + 3];
                masks.emplace_back(pl.clip, std::move(cov));
                continue;
            }
            const float* use = inherited;
            if (!masks.empty()) {
                eff.assign(n, 1.0f);
                if (inherited) for (size_t i = 0; i < n; ++i) eff[i] = inherited[i];
                for (auto& mk : masks) for (size_t i = 0; i < n; ++i) eff[i] *= mk.second[i];
                use = eff.data();
            }
            cv.mask = use;
            draw(cv, cid, m * pm, cx * pl.cx, cframe, depth + 1);
            cv.mask = inherited;
        }
    }
}'''
assert old_draw in s
s = s.replace(old_draw, new_draw)

render_ex = '''bool SwfDoc::render_ex(const std::string& symbol, int frame, int size, const SwfRenderOpts& opts, SwfImage& out) const {
    auto it = symbols_.find(symbol);
    if (it == symbols_.end() || size <= 0) return false;
    RenderCtx ctx;
    ctx.palette = opts.palette;
    for (auto& [name, o] : opts.overrides) {
        ResolvedOverride r{o.mode, 0, o.frame, o.pos_only};
        if (o.mode == SwfOverride::Replace) {
            auto s2 = symbols_.find(o.symbol);
            if (s2 == symbols_.end()) r.mode = SwfOverride::Hide;
            else r.cid = s2->second;
        }
        ctx.ov.emplace(name, r);
    }
    // Size by the reference symbol (e.g. the bare head), then give the parts room.
    auto ref = opts.bounds_symbol.empty() ? it : symbols_.find(opts.bounds_symbol);
    if (ref == symbols_.end()) ref = it;
    double x0 = 1e30, y0 = 1e30, x1 = -1e30, y1 = -1e30;
    impl_->bounds(ref->second, Mat{}, opts.bounds_symbol.empty() ? frame : opts.bounds_frame, 0, x0, y0, x1, y1);
    if (x1 <= x0 || y1 <= y0) return false;
    double mw = (x1 - x0) * opts.margin, mh = (y1 - y0) * opts.margin;
    x0 -= mw; x1 += mw; y0 -= mh; y1 += mh;
    double sc = size * 20.0 / std::max(x1 - x0, y1 - y0);
    int w = (int)std::ceil((x1 - x0) * sc / 20) + 2, h = (int)std::ceil((y1 - y0) * sc / 20) + 2;
    if (w > 2048 || h > 2048) return false;
    Canvas cv(w, h);
    t_ctx = &ctx;
    impl_->draw(cv, it->second, Mat{sc, 0, 0, sc, -x0 * sc + 20, -y0 * sc + 20}, CX{}, frame, 0);
    t_ctx = nullptr;
    int cx0 = w, cy0 = h, cx1 = -1, cy1 = -1;   // crop to the opaque area
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            if (cv.px[((size_t)y * w + x) * 4 + 3] > 0.02f) {
                cx0 = std::min(cx0, x); cy0 = std::min(cy0, y); cx1 = std::max(cx1, x); cy1 = std::max(cy1, y);
            }
    if (cx1 < 0) return false;
    out.w = cx1 - cx0 + 1;
    out.h = cy1 - cy0 + 1;
    out.rgba.resize((size_t)out.w * out.h * 4);
    for (int y = 0; y < out.h; ++y)
        for (int x = 0; x < out.w; ++x) {
            const float* src = &cv.px[((size_t)(y + cy0) * w + (x + cx0)) * 4];
            uint8_t* d = &out.rgba[((size_t)y * out.w + x) * 4];
            float a = src[3];
            for (int k = 0; k < 3; ++k) d[k] = (uint8_t)std::clamp(a > 0 ? src[k] / a * 255.0f : 0.0f, 0.0f, 255.0f);
            d[3] = (uint8_t)std::clamp(a * 255.0f, 0.0f, 255.0f);
        }
    return true;
}

'''
if 'bool SwfDoc::render_ex(' not in s:
    s = s.replace('int SwfDoc::frame_of_label(', render_ex + 'int SwfDoc::frame_of_label(', 1)
open(p, 'w', encoding='utf-8').write(s)
print('patched')
