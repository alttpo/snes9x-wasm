
#ifndef SNES9X_WASM_HOST_H
#define SNES9X_WASM_HOST_H

bool wasm_host_init();

bool wasm_host_load_module(const std::string& name, uint8_t *module_binary, uint32_t module_size);

void wasm_host_notify_nmi();

void wasm_ppux_render_obj_lines(bool sub, uint8_t zstart);
void wasm_ppux_render_bg_lines(int layer, bool sub, uint8_t zh, uint8_t zl);

void wasm_ppux_start_screen();
void wasm_ppux_end_screen();

#endif //SNES9X_WASM_HOST_H
