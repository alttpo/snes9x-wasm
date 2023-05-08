
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

class module {
public:
    module(std::string name_p, wasm_module_t mod_p, wasm_module_inst_t mi_p)
        : name(std::move(name_p)), mod(mod_p), mi(mi_p) {
        env = wasm_runtime_create_exec_env(mi, 1048576);
        if (!env) {
            wasm_runtime_deinstantiate(mi);
            mi = nullptr;
            wasm_runtime_unload(mod);
            mod = nullptr;
        }
    }

    ~module() {
        wasm_runtime_destroy_exec_env(env);
        wasm_runtime_deinstantiate(mi);
        mi = nullptr;
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
        if ((func = wasm_runtime_lookup_wasi_start_function(mi))) {
            if (!wasm_runtime_call_wasm(env, func, 0, nullptr)) {
                printf("wasm_runtime_call_wasm('_start'): %s\n", wasm_runtime_get_exception(mi));
            }
            return;
        }
#endif /* end of WASM_ENABLE_LIBC_WASI */

        if (!(func = wasm_runtime_lookup_function(mi, "main", nullptr))
            && !(func = wasm_runtime_lookup_function(mi, "__main_argc_argv", nullptr))
            && !(func = wasm_runtime_lookup_function(mi, "_main", nullptr))) {
#if WASM_ENABLE_LIBC_WASI != 0
            wasm_runtime_set_exception(
                mi,
                "lookup the entry point symbol (like _start, main, "
                "_main, __main_argc_argv) failed"
            );
#else
            wasm_runtime_set_exception(
                mi,
                "lookup the entry point symbol (like main, "
                "_main, __main_argc_argv) failed"
            );
#endif
        }
    }

    void start() {
        //pthread_setname_np("wasm");
        //printf("module thread started\n");

        wasm_runtime_init_thread_env();
        runMain();
        wasm_runtime_destroy_thread_env();

        auto m = static_cast<std::shared_ptr<module>*>(wasm_runtime_get_custom_data(mi));
        delete m;
        //printf("module thread exited\n");
    }



private:
    std::string name;
    wasm_module_t mod;
    wasm_module_inst_t mi;
    wasm_exec_env_t env;
};

std::vector<std::shared_ptr<module> > modules;

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

    typedef uint16_t wasi_errno_t;
    typedef uint32_t wasi_fd_t;

    typedef struct iovec_app {
        uint32 buf_offset;
        uint32 buf_len;
    } iovec_app_t;

    auto *wasi_overrides = new std::vector<NativeSymbol>();
    wasi_overrides->push_back({
        "fd_read",
        (void *) (wasi_errno_t (*)(wasm_exec_env_t, wasi_fd_t, const iovec_app_t *, uint32, uint32 *)) (
            [](wasm_exec_env_t exec_env,
               wasi_fd_t fd, const iovec_app_t *iovec_app, uint32 iovs_len, uint32 *nread_app
            ) -> wasi_errno_t {
                //printf("fd_read: fd=%d, iovs=%d\n", fd, iovs_len);

                auto module_inst = wasm_runtime_get_module_inst(exec_env);
                auto buf = ((uint8_t *)addr_app_to_native(iovec_app->buf_offset));
                buf[0] = 1;

                *nread_app = iovec_app->buf_len;
                return 0;
            }
        ),
        "(i*i*)i",
        nullptr
    });
    wasi_overrides->push_back({
        "fd_write",
        (void *) (wasi_errno_t (*)(wasm_exec_env_t, wasi_fd_t, const iovec_app_t *, uint32, uint32 *)) (
            [](wasm_exec_env_t exec_env, wasi_fd_t fd,
               const iovec_app_t *iovec_app, uint32 iovs_len,
               uint32 *nwritten_app
            ) -> wasi_errno_t {
                //printf("fd_write: fd=%d, iovs=%d\n", fd, iovs_len);

                auto module_inst = wasm_runtime_get_module_inst(exec_env);
                auto m = *static_cast<std::shared_ptr<module>*>(wasm_runtime_get_custom_data(module_inst));

                auto len = iovec_app->buf_len;
                *nwritten_app = len;

                if (fd == 1) {
                    // don't echo wasm stdout to native stdout:
                    return 0;
                } else if (fd == 2) {
                    // wasm stderr goes to native stderr:
                    const char *str = ((const char *)addr_app_to_native(iovec_app->buf_offset));
                    fprintf(stderr, "%.*s", len, str);
                } else {
                    // TODO: handle other fds
                    return -1;
                }

                return 0;
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

bool wasm_host_load_module(const std::string& name, uint8_t *module_binary, uint32_t module_size) {
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
    auto &m = modules.emplace_back(std::make_shared<module>(name, mod, mi));
    // set custom_data to a new shared_ptr to `module`:
    wasm_runtime_set_custom_data(mi, static_cast<void*>(new std::shared_ptr<module>(m)));

    // create a dedicated thread to run the wasm main:
    auto t = std::thread(&module::start, m);
    t.detach();

    return true;
}
