
#include <utility>

#include "wasm_module.h"
#include "thread_manager.h"
#include "memmap.h"

module::module(std::string name_p, wasm_module_t mod_p, wasm_module_inst_t mi_p, wasm_exec_env_t exec_env_p)
    : name(std::move(name_p)), mod(mod_p), module_inst(mi_p), exec_env(exec_env_p), ppux() {
    // set user_data to `this`:
    wasm_runtime_set_user_data(exec_env, static_cast<void *>(this));
}

module::~module() {
    wasm_runtime_destroy_exec_env(exec_env);
    wasm_runtime_deinstantiate(module_inst);
    module_inst = nullptr;
    wasm_runtime_unload(mod);
    mod = nullptr;
}

[[nodiscard]] std::shared_ptr<module>
module::create(std::string name, wasm_module_t mod, wasm_module_inst_t module_inst) {
    auto exec_env = wasm_runtime_create_exec_env(module_inst, 1048576);
    if (!exec_env) {
        wasm_runtime_deinstantiate(module_inst);
        module_inst = nullptr;
        wasm_runtime_unload(mod);
        mod = nullptr;
        return nullptr;
    }

    return std::shared_ptr<module>(new module(std::move(name), mod, module_inst, exec_env));
}

void module::start_thread() {
    // spawn thread which can be later canceled:
    std::thread(
        [](std::shared_ptr<module> *m_p) {
            std::shared_ptr<module> m = *m_p;
            delete m_p;

            //pthread_setname_np("wasm");

            wasm_runtime_init_thread_env();
            m->thread_main();
            wasm_runtime_destroy_thread_env();
        },
        new std::shared_ptr<module>(shared_from_this())
    ).detach();
}

void module::cancel_thread() {
    wasm_cluster_cancel_thread(exec_env);
}

void module::thread_main() {
    WASMFunctionInstanceCommon *func;

#if WASM_ENABLE_LIBC_WASI != 0
    /* In wasi mode, we should call the function named "_start"
       which initializes the wasi envrionment and then calls
       the actual main function. Directly calling main function
       may cause exception thrown. */
    if ((func = wasm_runtime_lookup_wasi_start_function(module_inst))) {
        goto exec_main;
    }
#else
    if ((func = wasm_runtime_lookup_function(module_inst, "_start", nullptr))) {
        goto exec_main;
    }
#endif /* end of WASM_ENABLE_LIBC_WASI */

    if ((func = wasm_runtime_lookup_function(module_inst, "main", nullptr))) {
        goto exec_main;
    }
    if ((func = wasm_runtime_lookup_function(module_inst, "__main_argc_argv", nullptr))) {
        goto exec_main;
    }
    if ((func = wasm_runtime_lookup_function(module_inst, "_main", nullptr))) {
        goto exec_main;
    }

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
fail:
    fprintf(stderr, "wasm exception: %s\n", wasm_runtime_get_exception(module_inst));
    return;

exec_main:
    if (!wasm_runtime_call_wasm(exec_env, func, 0, nullptr)) {
        goto fail;
    }
}

bool module::wait_for_events(uint32_t mask, uint32_t timeout_usec, uint32_t &o_events) {
    std::unique_lock<std::mutex> lk(events_cv_mtx);
    if (events_cv.wait_for(
        lk,
        std::chrono::microseconds(timeout_usec),
        [this]() { return events_changed; }
    )) {
        events_changed = false;
        o_events = events & mask;
        // clear event signals according to the mask:
        events &= ~mask;
        return true;
    }

    return false;
}

void module::notify_events(uint32_t events_p) {
    {
        std::unique_lock<std::mutex> lk(events_cv_mtx);
        events |= events_p;
        events_changed = true;
    }
    events_cv.notify_one();
}
