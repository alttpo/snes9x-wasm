
#ifndef SNES9X_REX_H
#define SNES9X_REX_H

#include <cstdint>

void rex_host_init();

void rex_rom_loaded();

void rex_rom_unloaded();

void rex_set_last_cycles(int32_t last);

void rex_set_curr_cycles(int32_t curr);

void rex_on_pc(uint32_t pc);

void rex_host_frame_start();

void rex_ppux_render_obj_lines(bool sub, uint8_t zstart);

void rex_ppux_render_bg_lines(int layer, bool sub, uint8_t zh, uint8_t zl);

void rex_host_frame_end();

void rex_host_frame_skip();

struct rex_memory_chip_desc {
    uint8_t *p;
    size_t size;
    bool readable;
    bool writable;
};

rex_memory_chip_desc rex_memory_chip(uint8_t c);

void rex_append_rom_filename(std::vector<uint8_t> &o);

#endif //SNES9X_REX_H
