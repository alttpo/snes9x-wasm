
#include <cstdint>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

// WAMR:
#include "wasm_export.h"

#include "wasm_host.h"
#include "snes9x.h"

#include "display.h"

typedef uint16_t wasi_errno_t;
typedef uint32_t wasi_fd_t;

typedef struct iovec_app {
    uint32 buf_offset;
    uint32 buf_len;
} iovec_app_t;

class module {
public:
    module(std::string name_p, wasm_module_t mod_p, wasm_module_inst_t mi_p)
        : name(std::move(name_p)), mod(mod_p), module_inst(mi_p) {
        exec_env = wasm_runtime_create_exec_env(module_inst, 1048576);
        if (!exec_env) {
            wasm_runtime_deinstantiate(module_inst);
            module_inst = nullptr;
            wasm_runtime_unload(mod);
            mod = nullptr;
            return;
        }

        // set user_data to `this`:
        wasm_runtime_set_user_data(exec_env, static_cast<void *>(this));
    }

    ~module() {
        printf("wasm [%s] module dtor\n", name.c_str());

        wasm_runtime_destroy_exec_env(exec_env);
        wasm_runtime_deinstantiate(module_inst);
        module_inst = nullptr;
        wasm_runtime_unload(mod);
        mod = nullptr;
    }

    void runMain() {
        WASMFunctionInstanceCommon *func;

#if WASM_ENABLE_LIBC_WASI != 0
        /* In wasi mode, we should call the function named "_start"
           which initializes the wasi envrionment and then calls
           the actual main function. Directly calling main function
           may cause exception thrown. */
        if ((func = wasm_runtime_lookup_wasi_start_function(module_inst))) {
            if (!wasm_runtime_call_wasm(exec_env, func, 0, nullptr)) {
                printf("wasm_runtime_call_wasm('_start'): %s\n", wasm_runtime_get_exception(module_inst));
            }
            return;
        }
#endif /* end of WASM_ENABLE_LIBC_WASI */

        if (!(func = wasm_runtime_lookup_function(module_inst, "main", nullptr))
            && !(func = wasm_runtime_lookup_function(module_inst, "__main_argc_argv", nullptr))
            && !(func = wasm_runtime_lookup_function(module_inst, "_main", nullptr))) {
#if WASM_ENABLE_LIBC_WASI != 0
            wasm_runtime_set_exception(
                module_inst,
                "lookup the entry point symbol (like _start, main, "
                "_main, __main_argc_argv) failed"
            );
#else
            wasm_runtime_set_exception(
                module_inst,
                "lookup the entry point symbol (like main, "
                "_main, __main_argc_argv) failed"
            );
#endif
        }
    }

    void start() {
        //pthread_setname_np("wasm");
        printf("wasm [%s] module thread started\n", name.c_str());

        wasm_runtime_init_thread_env();
        runMain();
        wasm_runtime_destroy_thread_env();

        printf("wasm [%s] module thread exited\n", name.c_str());
    }

    wasi_errno_t fd_read(wasi_fd_t fd, const iovec_app_t *iovec_app, uint32 iovs_len, uint32 *nread_app) {
        //printf("fd_read: fd=%d, iovs=%d\n", fd, iovs_len);
        auto buf = ((uint8_t *) addr_app_to_native(iovec_app->buf_offset));
        if (fd == 0) {
            // stdin:
            buf[0] = 1;
        }

        *nread_app = iovec_app->buf_len;
        return 0;
    }

    wasi_errno_t fd_write(wasi_fd_t fd, const iovec_app_t *iovec_app, uint32 iovs_len, uint32 *nwritten_app) {
        auto len = iovec_app->buf_len;
        *nwritten_app = len;

        if (fd == 1) {
            // don't echo wasm stdout to native stdout:
            return 0;
        } else if (fd == 2) {
            // wasm stderr goes to native stderr:
            const char *str = ((const char *) addr_app_to_native(iovec_app->buf_offset));
            fprintf(stderr, "%.*s", len, str);
        } else {
            // TODO: handle other fds
            return -1;
        }

        return 0;
    }

private:
    std::string name;
    wasm_module_t mod;
    wasm_module_inst_t module_inst;
    wasm_exec_env_t exec_env;
};

std::vector<std::weak_ptr<module> > modules;

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
        printf("wasm_runtime_full_init failed\n");
        return false;
    }

    auto *wasi_overrides = new std::vector<NativeSymbol>();
    wasi_overrides->push_back({
        "fd_read",
        (void *) (wasi_errno_t (*)(wasm_exec_env_t, wasi_fd_t, const iovec_app_t *, uint32,
                                   uint32 *)) (
            [](wasm_exec_env_t exec_env,
               wasi_fd_t fd, const iovec_app_t *iovec_app, uint32 iovs_len, uint32 *nread_app
            ) -> wasi_errno_t {
                auto m = static_cast<module *>( wasm_runtime_get_user_data(exec_env));
                return m->fd_read(fd, iovec_app, iovs_len, nread_app);
            }
        ),
        "(i*i*)i",
        nullptr
    });
    wasi_overrides->push_back({
        "fd_write",
        (void *) (wasi_errno_t (*)(wasm_exec_env_t, wasi_fd_t, const iovec_app_t *, uint32, uint32 *)) (
            [](wasm_exec_env_t exec_env,
               wasi_fd_t fd, const iovec_app_t *iovec_app, uint32 iovs_len, uint32 *nwritten_app
            ) -> wasi_errno_t {
                auto m = static_cast<module *>( wasm_runtime_get_user_data(exec_env));
                return m->fd_write(fd, iovec_app, iovs_len, nwritten_app);
            }
        ),
        "(i*i*)i",
        nullptr
    });

    if (!wasm_runtime_register_natives(
        "wasi_snapshot_preview1",
        wasi_overrides->data(),
        wasi_overrides->size()
    )) {
        printf("wasm_runtime_full_init failed\n");
        return false;
    }

    // TODO: handle leaky `wasi_overrides`

    return true;
}

bool wasm_host_load_module(const std::string &name, uint8_t *module_binary, uint32_t module_size) {
    char wamrError[1024];

    wasm_module_t mod = wasm_runtime_load(
        module_binary,
        module_size,
        wamrError,
        sizeof(wamrError)
    );
    if (!mod) {
        printf(
            "wasm_runtime_load: %s\n",
            wamrError
        );
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
        printf(
            "wasm_runtime_instantiate: %s\n",
            wamrError
        );
        return false;
    }

    // track the new module:
    const std::shared_ptr<module> &m = std::make_shared<module>(name, mod, mi);
    modules.emplace_back(m);

    // create a dedicated thread to run the wasm main:
    auto t = std::thread(
        [](std::shared_ptr<module> *sm) {
            // transfer shared_ptr to a local:
            std::shared_ptr<module> m(*sm);
            delete sm;

            // start wasm:
            m->start();

            // clean up:
            m.reset();
        },
        new std::shared_ptr<module>(m)
    );
    t.detach();

    return true;
}
