// catportrait.h -- a cat's face composed the way glaiel::CatParts builds it.
//
// swfs/catparts.swf `CatHeadPlacements`, frame = head index - 1, holds the head
// (fill mask + `tex` texture slot + outline) and named markers. The game puts
// its parts on them (sub_1407393E0):
//   lear / rear   CatEar     full marker matrix (rear is mirrored)
//   leye / reye   CatEye     marker position only, scale 1 (reye mirrored)
//   mouth         CatMouth   marker position only
//   ahead / aface HeadItemF / FaceItemF  (equipped hat / face item)
//   tex           texture frame (CatData texture index - 1), also inside ears
// Colour: the paletted shader maps greys through a row of textures/palette.png;
// in battle the row is the cat's class palette (classes.gon graphics.palette).
#pragma once

#include "swf.h"

#include <string>

namespace cr {

struct CatLook {
    int head = 0, ear = 0, eye = 0, brow = 0, mouth = 0, tex = 0;   // 1-based part indices
    int palette = 0;                                                 // palette.png row
    int hat_frame = 0, face_frame = 0;                               // item icon frames (1-based), 0 = none

    std::string key() const;
};

// Render options for CatHeadPlacements at frame look.head - 1.
SwfRenderOpts cat_face_opts(const CatLook& look, const uint8_t* palette_row /* 16 RGB */);

}  // namespace cr
