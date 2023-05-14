
#ifndef SNES9X_WASM_PPUX_H
#define SNES9X_WASM_PPUX_H

#include <cstdint>
#include <vector>
#include "snes9x.h"

struct ppux {
    ppux();

    enum layer {
        BG1 = 0,
        BG2,
        BG3,
        BG4,
        OBJ,
        cardinality
    };

    // ppux allows integrating custom colors into PPU's 5 layers:
    //   0 = BG1, 1 = BG2, 2 = BG3, 3 = BG4, 4 = OBJ
    // each layer is MAX_SNES_WIDTH x MAX_SNES_HEIGHT in dimensions
    // each pixel is 4 bytes:
    //   byte 0        byte 1        byte 2        byte 3
    // [ 7654 3210 ] [ 7654 3210 ] [ 7654 3210 ] [ 7654 3210 ]
    //   gggr rrrr     -bbb bbgg     E--- --pp     ---- ----    E = enable pixel
    //                                                          r = red (5-bits)
    //                                                          g = green (5-bits)
    //                                                          b = blue (5-bits)
    //                                                          p = priority [0..3]
    std::vector<uint32_t> main[layer::cardinality];
    std::vector<uint32_t> sub[layer::cardinality];

    static const long bpp = 4;
    static const long pitch = MAX_SNES_WIDTH;

    static const uint32_t PX_ENABLE = (1UL << 23);  // `E`

    template<class MATH>
    void lines_math_main(
        int left,
        int right,
        uint8 layer,
        uint16 *mc,
        uint8 *md,
        const uint16 *sc,
        const uint8 *sd
    );

    void lines_sub(
        int    left,
        int    right,
        uint8  layer,
        uint16 *c,
        uint8  *d
    );

    uint8_t priority_depth_map[4];

public:
    void render_line_main(layer layer);

    void render_line_sub(layer layer);
};

#endif //SNES9X_WASM_PPUX_H
