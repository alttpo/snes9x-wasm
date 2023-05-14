
#include "wasm_module.h"
#include "wasm_vfs.h"
#include "wasm_ppux.h"

#include "snes9x.h"
#include "gfx.h"
#include "tileimpl.h"

fd_ppux::fd_ppux(std::weak_ptr<module> m_p, ppux::layer layer_p, bool sub_p, wasi_fd_t fd_p)
    : fd_inst(fd_p), m_w(std::move(m_p)), layer(layer_p), sub(sub_p) {}

wasi_errno_t fd_ppux::pwrite(const iovec &iov, wasi_filesize_t offset, uint32_t &nwritten) {
    auto m = m_w.lock();
    if (!m) {
        return EBADF;
    }

    nwritten = 0;

    auto &vec = m->ppux.main[layer];
    auto vec_size = vec.size() * ppux::bpp;
    for (const auto &io: iov) {
        size_t size = io.second;
        if (offset + size > vec_size) {
            size = vec_size - offset;
        }
        if (size == 0) {
            continue;
        }

        memcpy((uint8_t *) vec.data() + (long) offset, io.first, size);

        offset += size;
        nwritten += size;
    }

    return 0;
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
    for (const auto &io: iov) {
        if (io.second & 3) {
            // size must be multiple of 4
            return EINVAL;
        }

        auto data = (uint32_t *) io.first;
        auto size = io.second / 4;
        for (auto p = data; p < data + size; p++) {
            if (*p == 0) {
                std::unique_lock<std::mutex> lk(ppux.cmd_m);
                ppux.cmd = ppux.cmdNext;
                ppux.cmdNext.erase(ppux.cmdNext.begin(), ppux.cmdNext.end());
                break;
            }

            ppux.cmdNext.push_back(*p);
        }
    }

    return 0;
}

ppux::ppux() {
    // initialize ppux layers:
    for (auto &layer: main) {
        layer.resize(ppux::pitch * MAX_SNES_HEIGHT);
    }
    for (auto &layer: sub) {
        layer.resize(ppux::pitch * MAX_SNES_HEIGHT);
    }
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
                break;
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
