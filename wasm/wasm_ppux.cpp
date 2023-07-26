
#include "wasm_module.h"
#include "wasm_ppux.h"

#include "snes9x.h"
#include "gfx.h"
#include "tileimpl.h"
#include <cassert>

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

bool ppux::cmd_write(uint32_t *data, uint32_t size) {
    // size is counted in uint32_t units.

    // append the uint32_ts:
    cmdNext.insert(cmdNext.end(), data, data + size);

    // skip through opcode frames to find end-of-list:
    auto endit = cmdNext.end();
    auto opit = cmdNext.begin();
    while (opit != cmdNext.end()) {
        auto it = opit;

        //   MSB                                             LSB
        //   1111 1111     1111 1111     0000 0000     0000 0000
        // [ fedc ba98 ] [ 7654 3210 ] [ fedc ba98 ] [ 7654 3210 ]
        //   1ooo oooo     ---- ----     ssss ssss     ssss ssss    o = opcode
        //                                                          s = size of packet in uint32_ts
        if ((*it & (1 << 31)) == 0) {
            // MSB must be 1 to indicate opcode/size start of frame:
            wasm_host_stderr_printf(
                "enqueued cmd list malformed at index %td; opcode must have MSB set\n",
                it - cmdNext.begin());
            cmdNext.erase(cmdNext.begin(), cmdNext.end());
            return false;
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
        if (opit > cmdNext.end()) {
            opit = cmdNext.end();
        }
    }

    // did we find the end?
    if (endit == cmdNext.end()) {
        return false;
    }

    // atomically copy cmdNext to cmd and clear cmdNext:
    {
        std::unique_lock<std::mutex> lk(cmd_m);
        cmd = cmdNext;
        cmdNext.erase(cmdNext.begin(), cmdNext.end());
    }

    return true;
}

bool ppux::vram_upload(uint32_t addr, uint8_t *data, uint32_t size) {
    uint64_t maxaddr = (uint64_t) addr + (uint64_t) size;
    if (maxaddr >= vram_max_size) {
        return false;
    }

    std::copy_n(data, size, vram.begin() + addr);
    return true;
}

bool ppux::cgram_upload(uint32_t addr, uint8_t *data, uint32_t size) {
    uint64_t maxaddr = (uint64_t) addr + (uint64_t) size;
    if (maxaddr >= vram_max_size) {
        return false;
    }

    std::copy_n(data, size, cgram.begin() + addr);
    return true;
}

void ppux::render_cmd() {
    std::unique_lock<std::mutex> lk(cmd_m);

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
    auto opit = cmd.begin();
    while (opit != cmd.end()) {
        // NOTE: the bits of adjacent fields are intentionally not packed to allow
        // for trivial rewriting of values that are aligned at byte boundaries.

        auto it = opit;

        //   MSB                                             LSB
        //   1111 1111     1111 1111     0000 0000     0000 0000
        // [ fedc ba98 ] [ 7654 3210 ] [ fedc ba98 ] [ 7654 3210 ]
        //   1ooo oooo     ---- ----     ssss ssss     ssss ssss    o = opcode
        //                                                          s = size of command data in uint32_ts
        if ((*it & (1 << 31)) == 0) {
            wasm_host_stderr_printf("cmd list malformed at index %td; opcode must have MSB set\n", it - cmd.begin());
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
        if (opit > cmd.end()) {
            opit = cmd.end();
        }

        // execute opcode handler if in range:
        if (o >= sizeof(opcode_handlers) / sizeof(opcode_handlers[0])) {
            continue;
        }

        std::invoke(opcode_handlers[o], this, it, opit);
    }
}

void ppux::cmd_bitmap_15bpp(std::vector<uint32_t>::iterator it, std::vector<uint32_t>::iterator opit) {
    // draw horizontal runs of 15bpp RGB555 pixels starting at x,y. wrap at width and proceed to next line until
    // `size-2` pixels in total are drawn.

    //   MSB                                             LSB
    //   1111 1111     1111 1111     0000 0000     0000 0000
    // [ fedc ba98 ] [ 7654 3210 ] [ fedc ba98 ] [ 7654 3210 ]
    //   ---- --yy     yyyy yyyy     ---- --xx     xxxx xxxx    x = x-coordinate (0..1023) of top-left
    //   -o-- slll     ---- ----     ---- --ww     wwww wwww    y = y-coordinate (0..1023) of top-left
    //                                                          w = width in pixels (1..1024)
    //                                                          l = PPU layer
    //                                                          s = main or sub screen; main=0, sub=1
    //                                                          o = per pixel overlay = 0, replace = 1

    auto x0 = *it & 0x03ff;
    auto y0 = (*it >> 16) & 0x03ff;
    it++;
    if (it == cmd.end()) return;

    // width up to 1024 where 0 represents 1024 and 1..1023 are normal:
    auto width = *it & 0x03ff;
    if (width == 0) { width = 1024; }

    // which ppux layer to render to: (BG1..4, OBJ)
    auto layer = (*it >> 24) & 7;

    // main or sub screen (main=0, sub=1):
    auto is_sub = ((*it >> 24) >> 4) & 1;

    // overlay pixels (0) or overwrite pixels (1) based on PX_ENABLE flag (1<<31) per pixel:
    auto is_replace = (((*it >> 24) >> 7) & 1);

    it++;

    // copy pixels in; see comment in wasm_ppux.h for uint32_t bits representation:
    auto x1 = x0 + width;
    auto offs = (y0 * pitch);
    std::vector<uint32_t> &vec = is_sub ? sub[layer] : main[layer];
    auto y = y0;
    bool dirty = false;
    for (auto x = x0; it != opit && it != cmd.end(); it++) {
        auto p = *it;
        if (is_replace) {
            // replace all pixels:
            vec[offs + x] = p;
            dirty = true;
        } else {
            // overlay: only replace pixels where PX_ENABLE is set:
            if ((p & PX_ENABLE) != 0) {
                vec[offs + x] = p;
                dirty = true;
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

template<unsigned bpp, bool hflip, bool vflip, typename PLOT>
void ppux::draw_vram_tile(
    unsigned x0, unsigned y0,
    unsigned w, unsigned h,

    const uint8_t *vram,
    const uint8_t *cgram,

    PLOT plot
) {
    // draw tile:
    unsigned sy = y0;
    for (int ty = 0; ty < h; ty++, sy++) {
        sy &= 255;

        unsigned sx = x0;
        unsigned y = !vflip ? (ty) : (h - 1 - ty);

        for (int tx = 0; tx < w; tx++, sx++) {
            sx &= 511;

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

void ppux::cmd_vram_tiles_4bpp(std::vector<uint32_t>::iterator it, std::vector<uint32_t>::iterator opit) {
    //   MSB                                             LSB
    //   1111 1111     1111 1111     0000 0000     0000 0000
    // [ fedc ba98 ] [ 7654 3210 ] [ fedc ba98 ] [ 7654 3210 ]
    //  ---- --yy     yyyy yyyy     ---- --xx     xxxx xxxx    x = x coordinate (0..1023)
    //  --pp slll     ---- --vf     hhhh hhhh     wwww wwww    y = y coordinate (0..1023)
    //  ---- ----     dddd dddd     dddd dddd     dddd dddd    d = bitmap data address in extra ram
    //  ---- ----     cccc cccc     cccc cccc     cccc cccc    c = cgram/palette address in extra ram (points to color 0 of palette)
    //                                                         w = width in pixels
    //                                                         h = height in pixels
    //                                                         f = horizontal flip
    //                                                         v = vertical flip
    //                                                         l = PPU layer
    //                                                         s = main or sub screen; main=0, sub=1
    //                                                         p = priority (0..3 for OBJ, 0..1 for BG)

    auto x0 = *it & 1023;
    auto y0 = (*it >> 16) & 1023;
    it++;
    if (it == cmd.end()) return;

    auto width = (*it & 255);
    auto height = ((*it >> 8) & 255);
    auto hflip = ((*it >> 16) & 1) == 1;
    auto vflip = ((*it >> 17) & 1) == 1;

    // which ppux layer to render to: (BG1..4, OBJ)
    auto layer = (*it >> 24) & 7;

    // main or sub screen (main=0, sub=1):
    auto is_sub = ((*it >> 24) >> 3) & 1;

    auto prio = (((*it >> 24) >> 4) & 3) << 16;
    it++;
    if (it == cmd.end()) return;

    auto bitmap_addr = *it;
    it++;
    if (it == cmd.end()) return;
    auto cgram_addr = *it;
    it++;

    auto bitmap = vram.data() + bitmap_addr;
    auto palette = cgram.data() + cgram_addr;

    std::vector<uint32_t> &vec = is_sub ? sub[layer] : main[layer];

    if (!hflip) {
        if (!vflip) {
            draw_vram_tile<4, false, false>(
                x0, y0, width, height, bitmap, palette,
                [&](unsigned sx, unsigned sy, uint16_t color) {
                    vec[sy * ppux::pitch + sx] = (uint32_t) color | PX_ENABLE | prio;
                }
            );
        } else {
            draw_vram_tile<4, false, true>(
                x0, y0, width, height, bitmap, palette,
                [&](unsigned sx, unsigned sy, uint16_t color) {
                    vec[sy * ppux::pitch + sx] = (uint32_t) color | PX_ENABLE | prio;
                }
            );
        }
    } else {
        if (!vflip) {
            draw_vram_tile<4, true, false>(
                x0, y0, width, height, bitmap, palette,
                [&](unsigned sx, unsigned sy, uint16_t color) {
                    vec[sy * ppux::pitch + sx] = (uint32_t) color | PX_ENABLE | prio;
                }
            );
        } else {
            draw_vram_tile<4, true, true>(
                x0, y0, width, height, bitmap, palette,
                [&](unsigned sx, unsigned sy, uint16_t color) {
                    vec[sy * ppux::pitch + sx] = (uint32_t) color | PX_ENABLE | prio;
                }
            );
        }
    }

    dirty_top = std::min((int) y0, dirty_top);
    dirty_bottom = std::max((int) (y0 + height), dirty_bottom);
}

void wasm_host_frame_start() {
    for_each_module(
        [=](const std::shared_ptr<module>& m) {
            m->notify_event(wasm_event_kind::ev_ppu_frame_start);
        }
    );
    for_each_module(
        [=](const std::shared_ptr<module>& m) {
            //m->wait_for_ack_last_event(std::chrono::microseconds(2000));
            m->ppux.render_cmd();
        }
    );
}

void wasm_ppux_render_obj_lines(bool sub, uint8_t zstart) {
    for_each_module(
        [=](const std::shared_ptr<module>& m) {
            ppux &ppux = m->ppux;
            ppux.priority_depth_map[0] = zstart;
            ppux.priority_depth_map[1] = zstart + 4;
            ppux.priority_depth_map[2] = zstart + 8;
            ppux.priority_depth_map[3] = zstart + 12;

            if (sub) {
                ppux.render_line_sub(ppux::layer::OBJ);
            } else {
                ppux.render_line_main(ppux::layer::OBJ);
            }
        }
    );
}

void wasm_ppux_render_bg_lines(int layer, bool sub, uint8_t zh, uint8_t zl) {
    for_each_module(
        [=](const std::shared_ptr<module>& m) {
            ppux &ppux = m->ppux;
            ppux.priority_depth_map[0] = zl;
            ppux.priority_depth_map[1] = zh;
            ppux.priority_depth_map[2] = zl;
            ppux.priority_depth_map[3] = zh;

            if (sub) {
                ppux.render_line_sub((ppux::layer) layer);
            } else {
                ppux.render_line_main((ppux::layer) layer);
            }
        }
    );
}

void wasm_host_frame_end() {
    for_each_module(
        [=](const std::shared_ptr<module>& m) {
            m->notify_event(wasm_event_kind::ev_ppu_frame_end);
        }
    );
}

void wasm_host_frame_skip() {
    for_each_module(
        [=](const std::shared_ptr<module>& m) {
            m->notify_event(wasm_event_kind::ev_ppu_frame_skip);
        }
    );
}
