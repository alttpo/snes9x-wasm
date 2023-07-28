
#ifndef SNES9X_REX_H
#define SNES9X_REX_H

#include <cstdint>
#include <algorithm>
#include <utility>

#ifdef __WIN32__
#define WIN32_LEAN_AND_MEAN
#endif

#include "snes9x.h"
#include "memmap.h"

#include "rex_ppux.h"
#include "rex_iovm.h"
#include "rex_client.h"

struct rex {
    void on_pc(uint32_t pc);

    void start();

    void shutdown();

    void handle_net();

    void frame_start();

    void ppux_render_obj_lines(bool sub, uint8_t zstart);

    void ppux_render_bg_lines(int layer, bool sub, uint8_t zh, uint8_t zl);

private:
    std::vector<rex_client_sp> clients;

    sock_sp listener;
    std::vector<sock_wp> all_socks;
};

void rex_host_init();

void rex_rom_loaded();

void rex_rom_unloaded();

void rex_on_pc(uint32_t pc);

void rex_host_frame_start();

void rex_ppux_render_obj_lines(bool sub, uint8_t zstart);

void rex_ppux_render_bg_lines(int layer, bool sub, uint8_t zh, uint8_t zl);

void rex_host_frame_end();

void rex_host_frame_skip();

extern struct rex rex;

#endif //SNES9X_REX_H
