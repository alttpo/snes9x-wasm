
#ifndef SNES9X_WASI_IMPL_H
#define SNES9X_WASI_IMPL_H

void wasm_host_register_wasi();

extern wasi_write_cb stdout_write_cb;
extern wasi_write_cb stderr_write_cb;

#endif //SNES9X_WASI_IMPL_H
