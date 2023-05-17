
#include <memory>
#include <thread>
#include <utility>
#include <vector>

// WAMR:
#include "wasm_export.h"

#include "wasi_types.h"
#include "wasm_host.h"
#include "wasm_module.h"

// snes9x:
#include "snes9x.h"
#include "memmap.h"

// TODO: destructor of thread on exit() throws error
std::vector<std::shared_ptr<module>> modules;

bool wasm_host_init() {
    // initialize wasm runtime
    RuntimeInitArgs init;

    init.mem_alloc_type = Alloc_With_Pool;
    init.mem_alloc_option.pool.heap_size = 1048576 * 64;
    init.mem_alloc_option.pool.heap_buf = new uint8_t[init.mem_alloc_option.pool.heap_size];

    init.running_mode = Mode_Interp;

    init.native_module_name = "snes";
    init.n_native_symbols = 0;
    init.native_symbols = {};

    if (!wasm_runtime_full_init(&init)) {
        fprintf(stderr, "wasm_runtime_full_init failed\n");
        return false;
    }

    auto *wasi = new std::vector<NativeSymbol>();
    wasi->push_back({
        "path_open",
        (void *) (wasi_errno_t (*)(wasm_exec_env_t, wasi_fd_t dirfd,
                                   wasi_lookupflags_t dirflags, const char *path, uint32 path_len,
                                   wasi_oflags_t oflags, wasi_rights_t fs_rights_base,
                                   wasi_rights_t fs_rights_inheriting, wasi_fdflags_t fs_flags,
                                   wasi_fd_t *fd_app)) (
            [](wasm_exec_env_t exec_env,
               wasi_fd_t dirfd,
               wasi_lookupflags_t dirflags, const char *path, uint32 path_len,
               wasi_oflags_t oflags, wasi_rights_t fs_rights_base,
               wasi_rights_t fs_rights_inheriting, wasi_fdflags_t fs_flags,
               wasi_fd_t *fd_app
            ) -> wasi_errno_t {
                auto m = static_cast<module *>(wasm_runtime_get_user_data(exec_env));
                return m->path_open(dirfd, dirflags, path, path_len, oflags, fs_rights_base, fs_rights_inheriting,
                    fs_flags, fd_app);
            }
        ),
        "(ii*~iIIi*)i",
        nullptr
    });
    wasi->push_back({
        "fd_read",
        (void *) (wasi_errno_t (*)(wasm_exec_env_t, wasi_fd_t, const iovec_app_t *, uint32, uint32 *)) (
            [](wasm_exec_env_t exec_env,
               wasi_fd_t fd, const iovec_app_t *iovec_app, uint32 iovs_len, uint32 *nread_app
            ) -> wasi_errno_t {
                auto m = static_cast<module *>(wasm_runtime_get_user_data(exec_env));
                return m->fd_read(fd, iovec_app, iovs_len, nread_app);
            }
        ),
        "(i*i*)i",
        nullptr
    });
    wasi->push_back({
        "fd_write",
        (void *) (wasi_errno_t (*)(wasm_exec_env_t, wasi_fd_t, const iovec_app_t *, uint32, uint32 *)) (
            [](wasm_exec_env_t exec_env,
               wasi_fd_t fd, const iovec_app_t *iovec_app, uint32 iovs_len, uint32 *nwritten_app
            ) -> wasi_errno_t {
                auto m = static_cast<module *>(wasm_runtime_get_user_data(exec_env));
                return m->fd_write(fd, iovec_app, iovs_len, nwritten_app);
            }
        ),
        "(i*i*)i",
        nullptr
    });
    wasi->push_back({
        "fd_pread",
        (void *) (wasi_errno_t (*)(wasm_exec_env_t, wasi_fd_t, const iovec_app_t *, uint32, wasi_filesize_t,
                                   uint32 *)) (
            [](wasm_exec_env_t exec_env,
               wasi_fd_t fd, const iovec_app_t *iovec_app, uint32 iovs_len, wasi_filesize_t offset, uint32 *nread_app
            ) -> wasi_errno_t {
                auto m = static_cast<module *>(wasm_runtime_get_user_data(exec_env));
                return m->fd_pread(fd, iovec_app, iovs_len, offset, nread_app);
            }
        ),
        "(i*iI*)i",
        nullptr
    });
    wasi->push_back({
        "fd_pwrite",
        (void *) (wasi_errno_t (*)(wasm_exec_env_t, wasi_fd_t, const iovec_app_t *, uint32, wasi_filesize_t,
                                   uint32 *)) (
            [](wasm_exec_env_t exec_env,
               wasi_fd_t fd, const iovec_app_t *iovec_app, uint32 iovs_len, wasi_filesize_t offset, uint32 *nwritten_app
            ) -> wasi_errno_t {
                auto m = static_cast<module *>(wasm_runtime_get_user_data(exec_env));
                return m->fd_pwrite(fd, iovec_app, iovs_len, offset, nwritten_app);
            }
        ),
        "(i*iI*)i",
        nullptr
    });
    wasi->push_back({
        "fd_close",
        (void *) (wasi_errno_t (*)(wasm_exec_env_t exec_env, wasi_fd_t fd)) (
            [](wasm_exec_env_t exec_env, wasi_fd_t fd) -> wasi_errno_t {
                auto m = static_cast<module *>(wasm_runtime_get_user_data(exec_env));
                return m->fd_close(fd);
            }
        ),
        "(i)i",
        nullptr
    });

    if (!wasm_runtime_register_natives(
        "wasi_snapshot_preview1",
        wasi->data(),
        wasi->size()
    )) {
        fprintf(stderr, "wasm_runtime_full_init failed\n");
        return false;
    }

    // TODO: handle leaky `wasi` instance; delete on shutdown

    return true;
}

void module_shutdown(std::shared_ptr<module> &m) {
    // notify module for termination:
    m->notify_events(wasm_event_kind::ev_shutdown);
    // TODO: allow a grace period and then forcefully shut down with m->cancel_thread()
    m.reset();
}

void wasm_host_unload_all_modules() {
    for (auto it = modules.begin(); it != modules.end();) {
        auto &me = *it;

        module_shutdown(me);

        // releasing the shared_ptr should delete the module* and likely crash any running thread
        it = modules.erase(it);
    }
}

bool wasm_host_load_module(const std::string &name, uint8_t *module_binary, uint32_t module_size) {
    char wamrError[1024];

    for (auto it = modules.begin(); it != modules.end();) {
        auto &me = *it;
        if (name != me->name) {
            it++;
            continue;
        }

        module_shutdown(me);

        it = modules.erase(it);
    }

    wasm_module_t mod = wasm_runtime_load(
        module_binary,
        module_size,
        wamrError,
        sizeof(wamrError)
    );
    if (!mod) {
        fprintf(stderr, "wasm_runtime_load: %s\n", wamrError);
        return false;
    }

    wasm_module_inst_t mi = wasm_runtime_instantiate(
        mod,
        32768,
        32768,
        wamrError,
        sizeof(wamrError)
    );
    if (!mi) {
        wasm_runtime_unload(mod);
        fprintf(stderr, "wasm_runtime_instantiate: %s\n", wamrError);
        return false;
    }

    // track the new module:
    auto m = module::create(name, mod, mi);
    modules.emplace_back(m);

    // start the new thread:
    m->start_thread();

    return true;
}

void wasm_host_notify_events(wasm_event_kind events) {
    for_each_module([=](std::shared_ptr<module> m) {
        m->notify_events(events);
    });
}

void wasm_ppux_start_screen() {
    for_each_module([=](std::shared_ptr<module> m) {
        m->notify_events(wasm_event_kind::ev_ppu_frame_start);
        m->ppux.render_cmd();
    });
}

void wasm_ppux_end_screen() {
    for_each_module([=](std::shared_ptr<module> m) {
        m->notify_events(wasm_event_kind::ev_ppu_frame_end);
    });
}
