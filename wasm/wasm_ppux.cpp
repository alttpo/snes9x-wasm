
#include "wasm_module.h"
#include "wasm_vfs.h"
#include "wasm_ppux.h"

#include "snes9x.h"
#include "gfx.h"
#include "tileimpl.h"

ppux::ppux() {
    // initialize ppux layers:
    for (auto &layer: main) {
        layer.resize(ppux::pitch * MAX_SNES_HEIGHT);
    }
    for (auto &layer: sub) {
        layer.resize(ppux::pitch * MAX_SNES_HEIGHT);
    }
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
            // map priority of pixel to depth value:
            auto prio = (xp[x] >> 16) & 3;
            auto xd = priority_depth_map[prio];

            if ((xp[x] & PX_ENABLE) == PX_ENABLE && xd >= md[x]) {
                mc[x] = MATH::Calc(xp[x] & 0x7fff, sc[x], sd[x]);
                md[x] = xd;
            }
        }
    }
}

void ppux::lines_sub(
    int    left,
    int    right,
    uint8  layer,
    uint16 *c,
    uint8  *d
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
            if ((xp[x] & PX_ENABLE) == PX_ENABLE && xd >= d[x]) {
                c[x] = xp[x] & 0x7fff;
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

void wasm_ppux_render_obj_lines(bool sub, uint8_t zstart) {
    for (const auto &m_w: modules) {
        auto m = m_w.lock();
        if (!m) {
            continue;
        }

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
}

void wasm_ppux_render_bg_lines(int layer, bool sub, uint8_t zh, uint8_t zl) {
    for (const auto &m_w: modules) {
        auto m = m_w.lock();
        if (!m) {
            continue;
        }

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
}

fd_ppux_cmd::fd_ppux_cmd(std::weak_ptr<module> m_p, wasi_fd_t fd_p)
    : fd_inst(fd_p), m_w(std::move(m_p)) {}

wasi_errno_t fd_ppux_cmd::write(const iovec &iov, uint32_t &nwritten) {
    auto m = m_w.lock();
    if (!m) {
        return EBADF;
    }

    nwritten = 0;

    ppux &ppux = m->ppux;
    auto &cmdNext = ppux.cmdNext;
    for (const auto &io: iov) {
        if (io.second & 3) {
            // size must be multiple of 4
            return EINVAL;
        }

        // append the uint32_ts:
        auto data = (uint32_t *) io.first;
        auto size = io.second / 4;
        cmdNext.insert(cmdNext.end(), data, data + size);
    }

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
            fprintf(stderr, "enqueued cmd list malformed at index %ld; opcode must have MSB set\n", it - cmdNext.begin());
            cmdNext.erase(cmdNext.begin(), cmdNext.end());
            return EINVAL;
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
    if (endit != cmdNext.end()) {
        // atomically copy cmdNext to cmd and clear cmdNext:
        std::unique_lock<std::mutex> lk(ppux.cmd_m);
        ppux.cmd = cmdNext;
        cmdNext.erase(cmdNext.begin(), cmdNext.end());
        return 0;
    }

    return 0;
}

void ppux::render_cmd() {
    std::unique_lock<std::mutex> lk(cmd_m);

    // clear all layers:
    for (auto &layer: main) {
        layer.assign(ppux::pitch * MAX_SNES_HEIGHT, 0);
    }
    for (auto &layer: sub) {
        layer.assign(ppux::pitch * MAX_SNES_HEIGHT, 0);
    }

    // process draw commands:
    auto opit = cmd.begin();
    while (opit != cmd.end()) {
        // NOTE: the bits of adjacent fields are intentionally not packed to allow
        // for trivial rewriting of values that are aligned at byte boundaries.

        auto it = opit;

        //   MSB                                             LSB
        //   1111 1111     1111 1111     0000 0000     0000 0000
        // [ fedc ba98 ] [ 7654 3210 ] [ fedc ba98 ] [ 7654 3210 ]
        //   oooo oooo     ---- ----     ssss ssss     ssss ssss    o = opcode
        //                                                          s = size of packet in uint32_ts
        if ((*it & (1 << 31)) == 0) {
            fprintf(stderr, "cmd list malformed at index %ld; opcode must have MSB set\n", it - cmd.begin());
            cmd.erase(cmd.begin(), cmd.end());
            return;
        }

        auto size = *it & 0xffff;
        if (size == 0) {
            // stop:
            break;
        }

        auto o = (*it >> 24) & 0x7F;
        it++;

        // find start of next opcode:
        opit = it + size;
        if (opit > cmd.end()) {
            opit = cmd.end();
        }

        if (o == 1) {
            // draw horizontal runs of 16bpp pixels starting at x,y. wrap at
            // width and proceed to next line until `size-2` pixels in total are
            // drawn.

            //   MSB                                             LSB
            //   1111 1111     1111 1111     0000 0000     0000 0000
            // [ fedc ba98 ] [ 7654 3210 ] [ fedc ba98 ] [ 7654 3210 ]
            //   ---- ----     ---- slll     ---- --ww     wwww wwww
            //                                                          w = width in pixels
            //                                                          l = PPU layer
            //                                                          s = main or sub screen; main=0, sub=1

            // width up to 1024 where 0 represents 1024 and 1..1023 are normal:
            auto width = *it & 0x03ff;
            if (width == 0) { width = 1024; }

            // which ppux layer to render to: (BG1..4, OBJ)
            auto layer = (*it >> 16) & 7;

            // main or sub screen (main=0, sub=1):
            auto is_sub = ((*it >> 16) >> 4) & 1;

            //   MSB                                             LSB
            //   1111 1111     1111 1111     0000 0000     0000 0000
            // [ fedc ba98 ] [ 7654 3210 ] [ fedc ba98 ] [ 7654 3210 ]
            //   ---- --yy     yyyy yyyy     ---- --xx     xxxx xxxx
            it++;
            if (it == cmd.end()) break;

            auto x0 = *it & 0x03ff;
            auto y0 = (*it >> 16) & 0x03ff;
            auto x1 = x0 + width;

            it++;

            // copy pixels in:
            // each pixel is represented by a 4-byte little-endian uint32:
            //   MSB                                             LSB
            //   1111 1111     1111 1111     0000 0000     0000 0000
            // [ fedc ba98 ] [ 7654 3210 ] [ fedc ba98 ] [ 7654 3210 ]
            //   ---- ----     ---- --pp     Errr rrgg     gggb bbbb    E = enable pixel
            //                                                          r = red (5-bits)
            //                                                          g = green (5-bits)
            //                                                          b = blue (5-bits)
            //                                                          p = priority [0..3] for OBJ, [0..1] for BG
            auto offs = (y0 * pitch);
            std::vector<uint32_t> &vec = is_sub ? sub[layer] : main[layer];
            for (uint32_t x = x0; it != opit && it != cmd.end(); it++) {
                vec[offs + x] = *it;
                // wrap at width and move down a line:
                if (++x >= x1) {
                    x = x0;
                    offs += pitch;
                }
            }
        }
    }
}

void wasm_ppux_start_screen() {
    // notify all wasm module threads that NMI is occurring:
    for (auto it = modules.begin(); it != modules.end(); it++) {
        auto &m_w = *it;
        auto m = m_w.lock();
        if (!m) {
            modules.erase(it);
            continue;
        }

        m->notify_events(module::event_kind::frame_start);
        m->ppux.render_cmd();
    }
}

void wasm_ppux_end_screen() {
    // notify all wasm module threads that NMI is occurring:
    for (auto it = modules.begin(); it != modules.end(); it++) {
        auto &m_w = *it;
        auto m = m_w.lock();
        if (!m) {
            modules.erase(it);
            continue;
        }

        m->notify_events(module::event_kind::frame_end);
    }
}
