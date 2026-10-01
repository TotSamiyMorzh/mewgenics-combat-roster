#include "catportrait.h"

#include <cstdio>

namespace cr {

std::string CatLook::key() const {
    char b[96];
    sprintf_s(b, "cat:%d,%d,%d,%d,%d,%d,%d,%d,%d", head, ear, eye, brow, mouth, tex, palette, hat_frame, face_frame);
    return b;
}

SwfRenderOpts cat_face_opts(const CatLook& c, const uint8_t* palette_row) {
    SwfRenderOpts o;
    o.palette = palette_row;
    o.bounds_symbol = "CatHead";
    o.bounds_frame = c.head - 1;
    o.margin = 0.7f;   // ears/eyes stick out of the bare head
    auto part = [](const char* sym, int idx, bool pos_only) {
        SwfOverride v;
        v.mode = idx > 0 ? SwfOverride::Replace : SwfOverride::Hide;
        v.symbol = sym;
        v.frame = idx - 1;
        v.pos_only = pos_only;
        return v;
    };
    o.overrides["tex"] = {SwfOverride::Frame, {}, c.tex - 1};
    o.overrides["scars"] = {SwfOverride::Hide};
    o.overrides["lear"] = part("CatEar", c.ear, false);
    o.overrides["rear"] = part("CatEar", c.ear, false);
    SwfOverride le = part("CatEye", c.eye, true), re = part("CatEye", c.eye, true);
    if (c.brow > 0) {
        le.extra_symbol = re.extra_symbol = "CatEyebrow";
        le.extra_frame = re.extra_frame = c.brow - 1;
    }
    o.overrides["leye"] = le;
    o.overrides["reye"] = re;
    o.overrides["mouth"] = part("CatMouth", c.mouth, true);
    o.overrides["ahead"] = part("HeadItemF", c.hat_frame, false);
    o.overrides["aface"] = part("FaceItemF", c.face_frame, false);
    o.overrides["aneck"] = {SwfOverride::Hide};
    return o;
}

}  // namespace cr
