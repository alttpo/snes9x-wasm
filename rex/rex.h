
#ifndef SNES9X_REX_H
#define SNES9X_REX_H

#include "rex_ppux.h"
#include "rex_iovm.h"

struct rex {
    struct ppux ppux;
    std::array<struct vm_inst, 2> vms;

    int32_t vm_init(unsigned n);

    int32_t vm_load(unsigned n, const uint8_t *vmprog, uint32_t vmprog_len);

    iovm1_state vm_getstate(unsigned n);

    int32_t vm_reset(unsigned n);

    int32_t vm_read_data(unsigned n, uint8_t *dst, uint32_t dst_len, uint32_t *o_read, uint32_t *o_addr, uint8_t *o_target);

    friend void iovm1_opcode_cb(struct iovm1_t *vm, struct iovm1_callback_state_t *cbs);

    void on_pc(uint32_t pc);
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
