
#ifndef SNES9X_WASM_MODULE_H
#define SNES9X_WASM_MODULE_H

#include <cstdint>
#include <memory>
#include <utility>
#include <thread>
#include <vector>
#include <string>

#include "snes9x.h"

// WAMR:
#include "wasm_export.h"

#include "wasi_types.h"
#include "wasm_vfs.h"
#include "wasm_ppux.h"
#include "wasm_host.h"

class module : public std::enable_shared_from_this<module> {
public:
    module(std::string name_p, wasm_module_t mod_p, wasm_module_inst_t mi_p, wasm_exec_env_t exec_env_p);

    ~module();

    [[nodiscard]] static std::shared_ptr<module>
    create(std::string name_p, wasm_module_t mod_p, wasm_module_inst_t mi_p);

    void runMain();

    void start();

    std::string name;
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

public:
    bool wait_for_events(uint32_t &events_p);

    void notify_events(uint32_t events_p);

private:
    wasi_iovec create_iovec(const iovec_app_t *iovec_app, uint32_t iovs_len);

private:
    wasm_module_t mod;
    wasm_module_inst_t module_inst;

    wasm_exec_env_t exec_env;
    std::unordered_map<wasi_fd_t, std::shared_ptr<fd_inst>> fds;

    wasi_fd_t fd_free = 3;
    std::mutex events_cv_mtx;
    std::condition_variable events_cv;

    std::atomic<uint32_t> events = wasm_event_kind::none;

public:
    ppux ppux;

private:
    wasm_thread_t thread_id;
};

extern std::vector<std::shared_ptr<module>> modules;

template<typename ITER>
static void for_each_module(ITER iter) {
    for (auto & m : modules) {
        if (!m) {
            continue;
        }

        iter(m);
    }
}

#endif //SNES9X_WASM_MODULE_H
