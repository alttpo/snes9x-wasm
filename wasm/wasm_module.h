
#ifndef SNES9X_WASM_MODULE_H
#define SNES9X_WASM_MODULE_H

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>
#include <string>

#include "snes9x.h"

// WAMR:
#include "wasm_export.h"

#include "wasi_types.h"

#include "wasm_vfs.h"

class module : public std::enable_shared_from_this<module> {
public:
    module(std::string name_p, wasm_module_t mod_p, wasm_module_inst_t mi_p, wasm_exec_env_t exec_env_p);

    ~module();

    [[nodiscard]] static std::shared_ptr<module>
    create(std::string name_p, wasm_module_t mod_p, wasm_module_inst_t mi_p);

    void runMain();

    void start();

public:
    wasi_errno_t path_open(
        wasi_fd_t dirfd,
        wasi_lookupflags_t dirflags, const char *path, uint32_t path_len,
        wasi_oflags_t oflags, wasi_rights_t fs_rights_base,
        wasi_rights_t fs_rights_inheriting, wasi_fdflags_t fs_flags,
        wasi_fd_t *fd_app
    );

    wasi_errno_t fd_close(wasi_fd_t fd);

    wasi_errno_t fd_read(wasi_fd_t fd, const iovec_app_t *iovec_app, uint32_t iovs_len, uint32_t *nread_app);

    wasi_errno_t fd_write(wasi_fd_t fd, const iovec_app_t *iovec_app, uint32_t iovs_len, uint32_t *nwritten_app);

    wasi_errno_t
    fd_pread(wasi_fd_t fd, const iovec_app_t *iovec_app, uint32_t iovs_len, wasi_filesize_t offset,
             uint32_t *nread_app);

    wasi_errno_t fd_pwrite(wasi_fd_t fd, const iovec_app_t *iovec_app, uint32_t iovs_len, wasi_filesize_t offset,
                           uint32_t *nwritten_app);

    bool wait_for_nmi();

    void notify_nmi();

private:
    iovec create_iovec(const iovec_app_t *iovec_app, uint32_t iovs_len);

private:
    std::string name;
    wasm_module_t mod;
    wasm_module_inst_t module_inst;
    wasm_exec_env_t exec_env;

    std::unordered_map<wasi_fd_t, std::shared_ptr<fd_inst>> fds;
    wasi_fd_t fd_free = 3;

    std::mutex nmi_cv_m;
    std::condition_variable nmi_cv;
    bool nmi_triggered = false;

public:
    // ppux allows integrating custom colors into PPU's 5 layers:
    //   0 = BG1, 1 = BG2, 2 = BG3, 3 = BG4, 4 = OBJ
    // each layer is MAX_SNES_WIDTH x MAX_SNES_HEIGHT in dimensions
    // each pixel is 4 bytes:
    //   byte 0        byte 1        byte 2        byte 3
    // [ 7654 3210 ] [ fedc ba98 ] [ 7654 3210 ] [ 7654 3210 ]
    //   gggr rrrr     Ebbb bbgg     ---- --pp     ---- ----    E = enable pixel
    //                                                          r = red (5-bits)
    //                                                          g = green (5-bits)
    //                                                          b = blue (5-bits)
    //                                                          p = priority [0..3]
    std::vector<uint8_t> ppux[5];

    static const int ppux_pitch = 4 * MAX_SNES_WIDTH;
};

#endif //SNES9X_WASM_MODULE_H
