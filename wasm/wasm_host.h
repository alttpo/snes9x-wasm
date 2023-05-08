#ifndef SNES9X_GTK_WASM_HOST_H
#define SNES9X_GTK_WASM_HOST_H

bool wasm_host_init();

bool wasm_host_load_module(const std::string& name, uint8_t *module_binary, uint32_t module_size);

#endif //SNES9X_GTK_WASM_HOST_H
