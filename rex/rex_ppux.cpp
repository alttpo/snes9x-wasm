
#include <cassert>

#include "snes9x.h"
#include "gfx.h"
#include "tileimpl.h"

#include "rex.h"

#include "rex_ppux.h"

ppux::ppux() {
    // initialize ppux layers:
    for (auto &layer: main) {
        layer.resize(ppux::pitch * MAX_SNES_HEIGHT);
    }
    for (auto &layer: sub) {
        layer.resize(ppux::pitch * MAX_SNES_HEIGHT);
    }
    vram.resize(vram_max_size);
    cgram.resize(cgram_max_size);
    priority_depth_map[0] = 0;
    priority_depth_map[1] = 0;
    priority_depth_map[2] = 0;
    priority_depth_map[3] = 0;
}

template<class MATH>
void ppux::lines_math_main(
    int left,
    int right,
    uint8 layer,
    uint16 *mc,
    uint8 *md,
    const uint16 *sc,
    const uint8 *sd
) {
    uint32_t *xp = main[layer].data() + (GFX.StartY * pitch);
    for (uint32 l = GFX.StartY; l <= GFX.EndY; l++,
        mc += GFX.PPL, md += GFX.PPL,
        sc += GFX.PPL, sd += GFX.PPL,
        xp += pitch
        ) {
        for (int x = left; x < right; x++) {
            if ((xp[x] & PX_ENABLE) == 0) {
                continue;
            }

            // map priority of pixel to depth value:
            auto prio = (xp[x] >> 16) & 3;
            auto xd = priority_depth_map[prio];

            if (xd <= md[x]) {
                continue;
            }

            uint32_t r, g, b;
            auto rgb555 = xp[x] & 0x7fff;
            DECOMPOSE_PIXEL_RGB555(rgb555, b, g, r);
            auto pix = BUILD_PIXEL(r, g, b);

            mc[x] = MATH::Calc(pix, sc[x], sd[x]);
            md[x] = xd;
        }
    }
}

void ppux::lines_sub(
    int left,
    int right,
    uint8 layer,
    uint16 *c,
    uint8 *d
) {
    uint32_t *xp = sub[layer].data() + (GFX.StartY * pitch);
    for (uint32 l = GFX.StartY; l <= GFX.EndY; l++,
        c += GFX.PPL, d += GFX.PPL,
        xp += pitch
        ) {
        for (int x = left; x < right; x++) {
            // map priority of pixel to depth value:
            auto prio = (xp[x] >> 16) & 3;
            auto xd = priority_depth_map[prio];
            if (((xp[x] & PX_ENABLE) == PX_ENABLE) && (xd > d[x])) {
                uint32_t r, g, b;
                auto rgb555 = xp[x] & 0x7fff;
                DECOMPOSE_PIXEL_RGB555(rgb555, b, g, r);
                auto pix = BUILD_PIXEL(r, g, b);
                c[x] = pix;
                d[x] = xd;
            }
        }
    }
}

void ppux::render_line_main(ppux::layer layer) {
    struct ClipData *p = IPPU.Clip[0];
    uint32 ppl = GFX.StartY * GFX.PPL;

    // draw into the main or sub screen:
    for (int clip = 0; clip < p[layer].Count; clip++) {
        if (p[layer].DrawMode[clip] == 0) {
            continue;
        }

        uint16 lc = p[layer].Left[clip];
        uint16 rc = p[layer].Right[clip];

        // main:
        bool enableMath = (Memory.FillRAM[0x2131] & (1 << layer));

        uint16 *mc = GFX.Screen + ppl;
        uint8 *md = GFX.ZBuffer + ppl;
        uint16 *sc = GFX.SubScreen + ppl;
        uint8 *sd = GFX.SubZBuffer + ppl;

        //printf("  %s[%3d] clip[%1d] Y:%3d..%3d, X:%3d..%3d\n", layerNames[layer], IPPU.CurrentLine, clip, GFX.StartY, GFX.EndY, p[layer].Left[clip], p[layer].Right[clip]);

        // select color math function:
        int i = 0;
        if (enableMath) {
            if (!Settings.Transparency)
                i = 0;
            else {
                //bool colorSub  = (Memory.FillRAM[0x2131] & 0x80);
                i = (Memory.FillRAM[0x2131] & 0x80) ? 4 : 1;

                //bool colorHalf = (Memory.FillRAM[0x2131] & 0x40);
                if (Memory.FillRAM[0x2131] & 0x40) {
                    i++;
                    if (Memory.FillRAM[0x2130] & 2)
                        i++;
                }
                if (IPPU.MaxBrightness != 0xf) {
                    if (i == 1)
                        i = 7;
                    else if (i == 3)
                        i = 8;
                }
            }
        }

        switch (i) {
            case 0:
                lines_math_main<TileImpl::Blend_None>(lc, rc, layer, mc, md, sc, sd);
                break;
            case 1:
                lines_math_main<TileImpl::Blend_Add>(lc, rc, layer, mc, md, sc, sd);
                break;
            case 2:
                lines_math_main<TileImpl::Blend_AddF1_2>(lc, rc, layer, mc, md, sc, sd);
                break;
            case 3:
                lines_math_main<TileImpl::Blend_AddS1_2>(lc, rc, layer, mc, md, sc, sd);
                break;
            case 4:
                lines_math_main<TileImpl::Blend_Sub>(lc, rc, layer, mc, md, sc, sd);
                break;
            case 5:
                lines_math_main<TileImpl::Blend_SubF1_2>(lc, rc, layer, mc, md, sc, sd);
                break;
            case 6:
                lines_math_main<TileImpl::Blend_SubS1_2>(lc, rc, layer, mc, md, sc, sd);
                break;
            case 7:
                lines_math_main<TileImpl::Blend_AddBrightness>(lc, rc, layer, mc, md, sc, sd);
                break;
            case 8:
                lines_math_main<TileImpl::Blend_AddS1_2Brightness>(lc, rc, layer, mc, md, sc, sd);
                break;
            default:
                assert(false);
        }
    }
}

void ppux::render_line_sub(ppux::layer layer) {
    struct ClipData *p = IPPU.Clip[1];
    uint32 ppl = GFX.StartY * GFX.PPL;

    // draw into the sub screen:
    for (int clip = 0; clip < p[layer].Count; clip++) {
        if (p[layer].DrawMode[clip] == 0) {
            continue;
        }

        uint16 lc = p[layer].Left[clip];
        uint16 rc = p[layer].Right[clip];

        // sub:
        uint16 *c = GFX.SubScreen + ppl;
        uint8 *d = GFX.SubZBuffer + ppl;

        lines_sub(lc, rc, layer, c, d);
    }
}

ppux_error ppux::cmd_upload(uint32_t *data, uint32_t size) {
    // size is counted in uint32_t units.

    // append the uint32_ts:
    cmdNext.insert(cmdNext.end(), data, data + size);

    // skip through opcode frames to find end-of-list:
    auto endit = cmdNext.cend();
    auto opit = cmdNext.cbegin();
    while (opit != cmdNext.cend()) {
        auto it = opit;

        //   MSB                                             LSB
        //   1111 1111     1111 1111     0000 0000     0000 0000
        // [ fedc ba98 ] [ 7654 3210 ] [ fedc ba98 ] [ 7654 3210 ]
        //   1ooo oooo     ---- ----     ssss ssss     ssss ssss    o = opcode
        //                                                          s = size of packet in uint32_ts
        if ((*it & (1 << 31)) == 0) {
            // MSB must be 1 to indicate opcode/size start of frame:
            fprintf(
                stderr,
                "enqueued cmd list malformed at index %td; opcode must have MSB set\n",
                it - cmdNext.cbegin());
            cmdNext.erase(cmdNext.begin(), cmdNext.end());
            return PPUX_INVALID_OPCODE;
        }

        auto size = *it & 0xffff;
        if (size == 0) {
            // end of list:
            endit = opit;
            break;
        }

        it++;

        // jump to next opcode:
        opit = it + size;
        if (opit > cmdNext.cend()) {
            opit = cmdNext.cend();
        }
    }

    // did we find the end?
    if (endit == cmdNext.cend()) {
        return PPUX_MISSING_END;
    }

    // atomically copy cmdNext to cmd and clear cmdNext:
    {
        std::lock_guard lk(cmd_m);
        cmd = cmdNext;
        cmdNext.erase(cmdNext.begin(), cmdNext.end());
    }

    return PPUX_SUCCESS;
}

ppux_error ppux::vram_upload(uint32_t addr, const uint8_t *data, uint32_t size) {
    uint64_t maxaddr = (uint64_t) addr + (uint64_t) size;
    if (maxaddr >= vram_max_size) {
        return PPUX_ADDRESS_OUT_OF_RANGE;
    }

    std::copy_n(data, size, vram.begin() + addr);
    return PPUX_SUCCESS;
}

ppux_error ppux::cgram_upload(uint32_t addr, const uint8_t *data, uint32_t size) {
    uint64_t maxaddr = (uint64_t) addr + (uint64_t) size;
    if (maxaddr >= vram_max_size) {
        return PPUX_ADDRESS_OUT_OF_RANGE;
    }

    std::copy_n(data, size, cgram.begin() + addr);
    return PPUX_SUCCESS;
}

void ppux::render_cmd() {
    std::lock_guard lk(cmd_m);

    if (dirty_bottom >= dirty_top) {
        // clear all dirty lines in each layer:
        for (auto &layer: main) {
            std::fill_n(layer.begin() + dirty_top * ppux::pitch, (dirty_bottom - dirty_top + 1) * ppux::pitch, 0);
            //layer.assign(ppux::pitch * MAX_SNES_HEIGHT, 0);
        }
        for (auto &layer: sub) {
            std::fill_n(layer.begin() + dirty_top * ppux::pitch, (dirty_bottom - dirty_top + 1) * ppux::pitch, 0);
            //layer.assign(ppux::pitch * MAX_SNES_HEIGHT, 0);
        }
    }

    // reset to clean state:
    dirty_top = MAX_SNES_HEIGHT;
    dirty_bottom = -1;

    // process draw commands:
    opit = cmd.cbegin();
    while (opit != cmd.cend()) {
        // NOTE: the bits of adjacent fields are intentionally not packed to allow
        // for trivial rewriting of values that are aligned at byte boundaries.

        auto it = opit;

        //   MSB                                             LSB
        //   1111 1111     1111 1111     0000 0000     0000 0000
        // [ fedc ba98 ] [ 7654 3210 ] [ fedc ba98 ] [ 7654 3210 ]
        //   1ooo oooo     ---- ----     ssss ssss     ssss ssss    o = opcode
        //                                                          s = size of command data in uint32_ts
        if ((*it & (1 << 31)) == 0) {
            fprintf(stderr, "cmd list malformed at index %td; opcode must have MSB set\n", it - cmd.cbegin());
            cmd.erase(cmd.begin(), cmd.end());
            return;
        }

        auto size = *it & 0xffff;
        auto o = (*it >> 24) & 0x7F;
        it++;

        // end drawing:
        if (o == 0) {
            // stop:
            break;
        }

        // find start of next opcode:
        opit = it + size;
        if (opit > cmd.cend()) {
            opit = cmd.cend();
        }

        // execute opcode handler if in range:
        if (o >= sizeof(opcode_handlers) / sizeof(opcode_handlers[0])) {
            continue;
        }

        std::invoke(opcode_handlers[o], this, it);
    }
}

void ppux::cmd_bitmap_15bpp(std::vector<uint32_t>::const_iterator it) {
    // draw horizontal runs of 15bpp RGB555 pixels starting at x,y. wrap at width and proceed to next line until
    // `size-2` pixels in total are drawn.

    //   MSB                                             LSB
    //   1111 1111     1111 1111     0000 0000     0000 0000
    // [ fedc ba98 ] [ 7654 3210 ] [ fedc ba98 ] [ 7654 3210 ]
    //   yyyy yyyy     yyyy yyyy     xxxx xxxx     xxxx xxxx    x = x-coordinate (0..65535) of top-left
    //   -o-- slll     jjjj iiii     ---- --ww     wwww wwww    y = y-coordinate (0..65535) of top-left
    //                                                          w = width in pixels (1..1024)
    //                                                          l = PPU layer
    //                                                          s = main or sub screen; main=0, sub=1
    //                                                          o = per pixel overlay = 0, replace = 1
    //                                                       iiii = if bit[n]=1, subtract offsx[n] from x coord
    //                                                       jjjj = if bit[n]=1, subtract offsy[n] from y coord

    auto x0 = (int)(*it & 65535);
    auto y0 = (int)((*it >> 16) & 65535);
    it++;
    if (it == cmd.cend()) return;

    // width up to 1024 where 0 represents 1024 and 1..1023 are normal:
    auto width = *it & 0x03ff;
    if (width == 0) { width = 1024; }

    if (*it & (1 << 0x10)) x0 -= offsx[0];
    if (*it & (1 << 0x11)) x0 -= offsx[1];
    if (*it & (1 << 0x12)) x0 -= offsx[2];
    if (*it & (1 << 0x13)) x0 -= offsx[3];

    if (*it & (1 << 0x14)) y0 -= offsy[0];
    if (*it & (1 << 0x15)) y0 -= offsy[1];
    if (*it & (1 << 0x16)) y0 -= offsy[2];
    if (*it & (1 << 0x17)) y0 -= offsy[3];

    // which ppux layer to render to: (BG1..4, OBJ)
    auto layer = (*it >> 24) & 7;

    // main or sub screen (main=0, sub=1):
    auto is_sub = ((*it >> 24) >> 4) & 1;

    // overlay pixels (0) or overwrite pixels (1) based on PX_ENABLE flag (1<<31) per pixel:
    auto is_replace = (((*it >> 24) >> 7) & 1);

    it++;

    // copy pixels in; see comment in rex_ppux.h for uint32_t bits representation:
    auto x1 = x0 + (int)width;
    auto offs = y0 * (int)pitch;
    std::vector<uint32_t> &vec = is_sub ? sub[layer] : main[layer];
    auto y = y0;
    bool dirty = false;
    for (auto x = x0; it != opit && it != cmd.cend(); it++) {
        if (x >= 0 && x < SNES_WIDTH && y >= 0 && y < SNES_HEIGHT) {
            auto p = *it;
            if (is_replace) {
                // replace all pixels:
                vec[offs + (x & 511)] = p;
                dirty = true;
            } else {
                // overlay: only replace pixels where PX_ENABLE is set:
                if ((p & PX_ENABLE) != 0) {
                    vec[offs + (x & 511)] = p;
                    dirty = true;
                }
            }
        }
        // wrap at width and move down a line:
        if (++x >= x1) {
            x = x0;
            offs += pitch;
            y++;
        }
    }

    if (dirty) {
        if (dirty_top > (int) y0) {
            dirty_top = (int) y0;
        }
        if (dirty_bottom < (int) y) {
            dirty_bottom = (int) y;
        }
    }
}

template<unsigned bpp, typename PLOT>
void ppux::draw_vram_tile(
    unsigned x0, unsigned y0,
    unsigned w, unsigned h,
    bool hflip, bool vflip,

    const uint8_t *vram,
    const uint8_t *cgram,

    PLOT plot
) {
    // draw tile:
    unsigned sy = y0;
    for (int ty = 0; ty < h; ty++, sy++) {
        if (sy < 0 || sy >= SNES_HEIGHT) {
            continue;
        }

        unsigned sx = x0;
        unsigned y = !vflip ? (ty) : (h - 1 - ty);

        for (int tx = 0; tx < w; tx++, sx++) {
            if (sx < 0 || sx >= SNES_WIDTH) {
                continue;
            }

            unsigned x = (!hflip ? tx : (w - 1 - tx));

            uint8_t col, d0, d1, d2, d3, d4, d5, d6, d7;
            uint8_t mask = 1 << (7 - (x & 7));
            const uint8_t *tile_ptr = vram;

            switch (bpp) {
                case 2:
                    // 16 bytes per 8x8 tile
                    tile_ptr += ((x >> 3) << 4);
                    tile_ptr += ((y >> 3) << 8);
                    tile_ptr += (y & 7) << 1;
                    d0 = *(tile_ptr);
                    d1 = *(tile_ptr + 1);
                    col = !!(d0 & mask) << 0;
                    col += !!(d1 & mask) << 1;
                    break;
                case 4:
                    // 32 bytes per 8x8 tile
                    tile_ptr += ((x >> 3) << 5);
                    tile_ptr += ((y >> 3) << 9);
                    tile_ptr += (y & 7) << 1;
                    d0 = *(tile_ptr);
                    d1 = *(tile_ptr + 1);
                    d2 = *(tile_ptr + 16);
                    d3 = *(tile_ptr + 17);
                    col = !!(d0 & mask) << 0;
                    col += !!(d1 & mask) << 1;
                    col += !!(d2 & mask) << 2;
                    col += !!(d3 & mask) << 3;
                    break;
                case 8:
                    // 64 bytes per 8x8 tile
                    tile_ptr += ((x >> 3) << 6);
                    tile_ptr += ((y >> 3) << 10);
                    tile_ptr += (y & 7) << 1;
                    d0 = *(tile_ptr);
                    d1 = *(tile_ptr + 1);
                    d2 = *(tile_ptr + 16);
                    d3 = *(tile_ptr + 17);
                    d4 = *(tile_ptr + 32);
                    d5 = *(tile_ptr + 33);
                    d6 = *(tile_ptr + 48);
                    d7 = *(tile_ptr + 49);
                    col = !!(d0 & mask) << 0;
                    col += !!(d1 & mask) << 1;
                    col += !!(d2 & mask) << 2;
                    col += !!(d3 & mask) << 3;
                    col += !!(d4 & mask) << 4;
                    col += !!(d5 & mask) << 5;
                    col += !!(d6 & mask) << 6;
                    col += !!(d7 & mask) << 7;
                    break;
                default:
                    // TODO: warn
                    break;
            }

            // color 0 is always transparent:
            if (col == 0)
                continue;

            // look up color in cgram:
            //uint16_t bgr = cgram[col];
            uint16_t bgr = *(cgram + (col << 1)) + (*(cgram + (col << 1) + 1) << 8);

            plot(sx, sy, bgr);
        }
    }
}

void ppux::cmd_vram_tiles(std::vector<uint32_t>::const_iterator it) {
    //   MSB                                             LSB
    //   1111 1111     1111 1111     0000 0000     0000 0000
    // [ fedc ba98 ] [ 7654 3210 ] [ fedc ba98 ] [ 7654 3210 ]
    //   yyyy yyyy     yyyy yyyy     xxxx xxxx     xxxx xxxx    x = x coordinate (0..65535)
    //   vfpp slll     jjjj iiii     ---- ----     --bb hhww    y = y coordinate (0..65535)
    //   ---- ----     dddd dddd     dddd dddd     dddd dddd    d = bitmap data address in extra ram
    //   ---- ----     cccc cccc     cccc cccc     cccc cccc    c = cgram/palette address in extra ram (points to color 0 of palette)
    //                                                          w = width in pixels  = 8 << w  (8, 16, 32, 64)
    //                                                          h = height in pixels = 8 << h  (8, 16, 32, 64)
    //                                                          b = bits per pixel   = 2 << b  (2, 4, 8)
    //                                                          f = horizontal flip
    //                                                          v = vertical flip
    //                                                          l = PPU layer
    //                                                          s = main or sub screen; main=0, sub=1
    //                                                          p = priority (0..3 for OBJ, 0..1 for BG)
    //                                                       iiii = if bit[n]=1, subtract offsx[n] from x coord
    //                                                       jjjj = if bit[n]=1, subtract offsy[n] from y coord

    auto x0 = (int) (*it & 65535);
    auto y0 = (int) ((*it >> 0x10) & 65535);
    it++;
    if (it == cmd.cend()) return;

    auto width = 8 << (*it & 3);
    auto height = 8 << ((*it >> 2) & 3);
    auto bpp = 2 << ((*it >> 4) & 3);

    if (*it & (1 << 0x10)) x0 -= offsx[0];
    if (*it & (1 << 0x11)) x0 -= offsx[1];
    if (*it & (1 << 0x12)) x0 -= offsx[2];
    if (*it & (1 << 0x13)) x0 -= offsx[3];

    if (*it & (1 << 0x14)) y0 -= offsy[0];
    if (*it & (1 << 0x15)) y0 -= offsy[1];
    if (*it & (1 << 0x16)) y0 -= offsy[2];
    if (*it & (1 << 0x17)) y0 -= offsy[3];

    // which ppux layer to render to: (BG1..4, OBJ)
    auto layer = (*it >> 0x18) & 7;

    // main or sub screen (main=0, sub=1):
    auto is_sub = (*it >> 0x1b) & 1;

    auto prio = ((*it >> 0x1c) & 3) << 16;

    auto hflip = ((*it >> 0x1e) & 1) == 1;
    auto vflip = ((*it >> 0x1f) & 1) == 1;

    it++;
    if (it == cmd.cend()) return;

    auto bitmap_addr = *it;
    it++;
    if (it == cmd.cend()) return;

    auto cgram_addr = *it;
    it++;

    // early clipping:
    if (x0 + (int) width < 0) {
        return;
    }
    if (x0 >= SNES_WIDTH) {
        return;
    }
    if (y0 + (int) height < 0) {
        return;
    }
    if (y0 >= SNES_HEIGHT) {
        return;
    }

    auto bitmap = vram.data() + bitmap_addr;
    auto palette = cgram.data() + cgram_addr;

    std::vector<uint32_t> &vec = is_sub ? sub[layer] : main[layer];

    auto plot = [&](unsigned sx, unsigned sy, uint16_t color) {
        dirty_top = std::min((int) sy, dirty_top);
        dirty_bottom = std::max((int) (sy + height - 1), dirty_bottom);
        vec[sy * ppux::pitch + sx] = (uint32_t) color | PX_ENABLE | prio;
    };

    switch (bpp) {
        case 2:
            draw_vram_tile<2>(
                x0, y0, width, height, hflip, vflip, bitmap, palette,
                plot
            );
            break;
        case 4:
            draw_vram_tile<4>(
                x0, y0, width, height, hflip, vflip, bitmap, palette,
                plot
            );
            break;
        case 8:
            draw_vram_tile<8>(
                x0, y0, width, height, hflip, vflip, bitmap, palette,
                plot
            );
            break;
    }
}

static auto readu16(uint8_t *p) -> uint16_t {
    return (uint16_t) (((uint16_t) p[1] << 8) | (uint16_t) p[0]);
}

static auto rex_readu16(uint8_t tgt, uint32_t addr, uint16_t &result) {
    auto mt = rex_memory_target(tgt);
    if (!mt.readable) return;
    if (!mt.p) return;
    if (addr >= mt.size) return;
    result = readu16(mt.p + addr);
}

void ppux::cmd_set_offs_ptr(std::vector<uint32_t>::const_iterator it) {
    //   MSB                                             LSB
    //   1111 1111     1111 1111     0000 0000     0000 0000
    // [ fedc ba98 ] [ 7654 3210 ] [ fedc ba98 ] [ 7654 3210 ]
    //   ---- ----     ---- ----     ---- ----     ---- --ii    i = index (0..3)
    //   --tt tttt     aaaa aaaa     aaaa aaaa     aaaa aaaa    a = 24-bit address of X
    //                                                          t = memory target identifier
    //   --tt tttt     aaaa aaaa     aaaa aaaa     aaaa aaaa    a = 24-bit address of Y
    //                                                          t = memory target identifier

    unsigned i = (*it & 3);
    it++;

    uint8_t tgt_x = (*it >> 0x18) & 63;
    uint32_t addr_x = *it & ((1 << 0x18) - 1);
    it++;

    uint8_t tgt_y = (*it >> 0x18) & 63;
    uint32_t addr_y = *it & ((1 << 0x18) - 1);
    it++;

    rex_readu16(tgt_x, addr_x, offsx[i]);
    rex_readu16(tgt_y, addr_y, offsy[i]);
}
