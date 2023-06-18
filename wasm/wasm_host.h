
#ifndef SNES9X_WASM_HOST_H
#define SNES9X_WASM_HOST_H

#include <string>

// bit flags for events raised from emulator that wasm modules would want to monitor
enum wasm_event_kind : uint32_t {
    ev_none = 0UL,

    ev_shutdown,
    ev_snes_nmi,
    ev_snes_irq,
    ev_ppu_frame_start,
    ev_ppu_frame_end,
    ev_ppu_frame_skip,
};

typedef size_t (*wasi_write_cb)(const char *text_begin, const char *text_end);

void wasm_host_wasi_stdout(wasi_write_cb cb);
void wasm_host_wasi_stderr(wasi_write_cb cb);

bool wasm_host_init();
void wasm_host_unload_all_modules();
int wasm_host_load_module(const std::string& name, uint8_t *module_binary, uint32_t module_size);

void wasm_host_notify_events(wasm_event_kind events);

void wasm_host_debugger_enable(bool enabled);

void wasm_ppux_render_obj_lines(bool sub, uint8_t zstart);
void wasm_ppux_render_bg_lines(int layer, bool sub, uint8_t zh, uint8_t zl);

void wasm_host_frame_start();
void wasm_host_frame_end();
void wasm_host_frame_skip();

#endif //SNES9X_WASM_HOST_H
