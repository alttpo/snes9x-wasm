
#ifndef SNES9X_REX_PPUX_H
#define SNES9X_REX_PPUX_H

#include <cstdint>
#include <vector>
#include <mutex>

#include "snes9x.h"

enum ppux_error {
    PPUX_SUCCESS,
    PPUX_INVALID_OPCODE,
    PPUX_MISSING_END,
    PPUX_ADDRESS_OUT_OF_RANGE,
};

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
    // each pixel is represented by a 4-byte little-endian uint32:
    //   MSB                                             LSB
    //   1111 1111     1111 1111     0000 0000     0000 0000
    // [ fedc ba98 ] [ 7654 3210 ] [ fedc ba98 ] [ 7654 3210 ]
    //   E--- ----     ---- --pp     -rrr rrgg     gggb bbbb    E = enable pixel
    //                                                          r = red (5-bits)
    //                                                          g = green (5-bits)
    //                                                          b = blue (5-bits)
    //                                                          p = priority [0..3]
    std::vector<uint32_t> main[layer::cardinality];
    std::vector<uint32_t> sub[layer::cardinality];

    static const long pitch = MAX_SNES_WIDTH;

    static const uint32_t PX_ENABLE = (1UL << 0x1f);  // `E`

    // draw commands:
    std::recursive_mutex cmd_m;
    std::vector<uint32_t> cmd;
    std::vector<uint32_t> cmdNext;

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
        int left,
        int right,
        uint8 layer,
        uint16 *c,
        uint8 *d
    );

    uint8_t priority_depth_map[4];
    int dirty_top = 0;
    int dirty_bottom = MAX_SNES_HEIGHT - 1;

public:
    ppux_error cmd_upload(uint32_t *data, uint32_t size);
    ppux_error vram_upload(uint32_t addr, const uint8_t *data, uint32_t size);
    ppux_error cgram_upload(uint32_t addr, const uint8_t *data, uint32_t size);

    void render_cmd();

    void render_line_main(layer layer);

    void render_line_sub(layer layer);

private:
    typedef void (ppux::*opcode_handler)(std::vector<uint32_t>::const_iterator it);

    // ppux opcode functions, starting from opcode 1:
    void cmd_bitmap_15bpp(std::vector<uint32_t>::const_iterator it);

    void cmd_vram_tiles(std::vector<uint32_t>::const_iterator it);

    void cmd_set_offs_ptr(std::vector<uint32_t>::const_iterator it);

    static constexpr opcode_handler opcode_handlers[] = {
        // 0 is the terminate opcode:
        nullptr,
        &ppux::cmd_bitmap_15bpp,
        &ppux::cmd_vram_tiles,
        &ppux::cmd_set_offs_ptr
    };

    static const uint32_t vram_max_size = 65536 * 1024;
    static const uint32_t cgram_max_size = 256 * 2 * 1024;
    std::vector<uint8_t> vram;
    std::vector<uint8_t> cgram;

    uint16_t offsx[4]{};
    uint16_t offsy[4]{};

    std::vector<uint32_t>::const_iterator opit;

    template<unsigned bpp, typename PLOT>
    void draw_vram_tile(int x0, int y0, unsigned w, unsigned h, bool hflip, bool vflip, const uint8_t *vram, const uint8_t *cgram, PLOT plot);
};

#endif //SNES9X_REX_PPUX_H
