
#ifndef SNES9X_WASM_HOST_H
#define SNES9X_WASM_HOST_H

// bit flags for events raised from emulator that wasm modules would want to monitor
enum wasm_event_kind : uint32_t {
    none = 0UL,
    rom_loaded = (1UL << 0),
    rom_closed = (1UL << 1),
    irq = (1UL << 2),
    nmi = (1UL << 3),
    frame_start = (1UL << 4),
    frame_end = (1UL << 5),
};

bool wasm_host_init();

bool wasm_host_load_module(const std::string& name, uint8_t *module_binary, uint32_t module_size);

void wasm_host_notify_events(wasm_event_kind events);

void wasm_ppux_render_obj_lines(bool sub, uint8_t zstart);
void wasm_ppux_render_bg_lines(int layer, bool sub, uint8_t zh, uint8_t zl);

void wasm_ppux_start_screen();
void wasm_ppux_end_screen();

#endif //SNES9X_WASM_HOST_H
