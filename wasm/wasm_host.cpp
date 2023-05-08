
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

// WAMR:
#include "wasm_export.h"

#include "wasm_host.h"
#include "snes9x.h"

#include "display.h"

class module {
public:
    module(wasm_module_t mod_p, wasm_module_inst_t mi_p)
        : mod(mod_p), mi(mi_p)
    {
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
        printf("module thread started\n");
        wasm_runtime_init_thread_env();
        runMain();
        wasm_runtime_destroy_thread_env();
        printf("module thread exited\n");
    }

private:
    wasm_module_t mod;
    wasm_module_inst_t mi;
    wasm_exec_env_t env;
};

std::vector< std::shared_ptr< module > > modules;

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
    typedef uint16_t wasi_fdflags_t;
    typedef uint32_t wasi_lookupflags_t;
    typedef uint16_t wasi_oflags_t;
    typedef uint64_t wasi_rights_t;

    auto wasi_overrides = new NativeSymbol[1];
    new (&wasi_overrides[0]) NativeSymbol
        {
            "path_open",
            (void *) (wasi_errno_t (*)(wasm_exec_env_t, wasi_fd_t,
            wasi_lookupflags_t, const char *, uint32, wasi_oflags_t, wasi_rights_t,
            wasi_rights_t, wasi_fdflags_t, wasi_fd_t *)) (
                [](wasm_exec_env_t exec_env,
                    wasi_fd_t dirfd, wasi_lookupflags_t dirflags,
                    const char *path, uint32 path_len,
                    wasi_oflags_t oflags,
                    wasi_rights_t fs_rights_base,
                    wasi_rights_t fs_rights_inheriting,
                    wasi_fdflags_t fs_flags,
                    wasi_fd_t *fd_app
                ) -> wasi_errno_t {
                    printf("path_open(\"%.*s\")\n", path_len, path);
                    return 0;
                }
            ),
            "(ii*~iIIi*)i",
            nullptr
        };
    if (!wasm_runtime_register_natives(
        "wasi_snapshot_preview1",
        wasi_overrides,
        1
    )) {
        printf("wasm_runtime_full_init failed\n");
        return false;
    }

    return true;
}

bool wasm_host_load_module(uint8_t *module_binary, uint32_t module_size) {
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
        8192,
        8192,
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
    auto &m = modules.emplace_back(std::make_shared<module>(mod, mi));

    std::thread(&module::start, m).detach();

    return true;
}
