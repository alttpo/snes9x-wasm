
#ifndef SNES9X_WASM_HOST_H
#define SNES9X_WASM_HOST_H

// bit flags for events raised from emulator that wasm modules would want to monitor
enum wasm_event_kind : uint32_t {
    ev_none = 0UL,

    // reserved up to 8 net sockets:
    ev_net_received_0 = (1UL << 0),
    ev_net_received_1 = (1UL << 1),
    ev_net_received_2 = (1UL << 2),
    ev_net_received_3 = (1UL << 3),
    ev_net_received_4 = (1UL << 4),
    ev_net_received_5 = (1UL << 5),
    ev_net_received_6 = (1UL << 6),
    ev_net_received_7 = (1UL << 7),

    ev_snes_irq = (1UL << 27),
    ev_snes_nmi = (1UL << 28),
    ev_ppu_frame_start = (1UL << 29),
    ev_ppu_frame_end = (1UL << 30),
    ev_shutdown = (1UL << 31),
};

bool wasm_host_init();

void wasm_host_unload_all_modules();
bool wasm_host_load_module(const std::string& name, uint8_t *module_binary, uint32_t module_size);

void wasm_host_notify_events(wasm_event_kind events);

void wasm_ppux_render_obj_lines(bool sub, uint8_t zstart);
void wasm_ppux_render_bg_lines(int layer, bool sub, uint8_t zh, uint8_t zl);

void wasm_ppux_start_screen();
void wasm_ppux_end_screen();

#endif //SNES9X_WASM_HOST_H
