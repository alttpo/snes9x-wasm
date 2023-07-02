
#include <utility>

#include "wasm_module.h"
extern "C" {
#include "debug_engine.h"
}
//#include "thread_manager.h"
#include "memmap.h"

module::module(std::string name_p, wasm_module_t mod_p, wasm_module_inst_t mi_p, wasm_exec_env_t exec_env_p, uint8_t* module_binary_p, uint32_t module_size_p)
    : name(std::move(name_p)), mod(mod_p), module_inst(mi_p), exec_env(exec_env_p), module_binary(module_binary_p), module_size(module_size_p), ppux() {
    // set user_data to `this`:
    wasm_runtime_set_user_data(exec_env, static_cast<void *>(this));

    trace_printf(1UL << 0, "%s wasm module created\n", name.c_str());
}

module::~module() {
    wasm_runtime_destroy_exec_env(exec_env);
    wasm_runtime_deinstantiate(module_inst);
    module_inst = nullptr;
    wasm_runtime_unload(mod);
    mod = nullptr;
    delete[] module_binary;
    module_binary = nullptr;
    module_size = 0;
    trace_printf(1UL << 0, "%s wasm module destroyed\n", name.c_str());
}

[[nodiscard]] std::shared_ptr<module>
module::create(std::string name, wasm_module_t mod, wasm_module_inst_t module_inst, uint8_t* module_binary, uint32_t module_size) {
    auto exec_env = wasm_runtime_create_exec_env(module_inst, 1048576);
    if (!exec_env) {
        wasm_runtime_deinstantiate(module_inst);
        module_inst = nullptr;
        wasm_runtime_unload(mod);
        mod = nullptr;
        return nullptr;
    }

    return std::shared_ptr<module>(new module(std::move(name), mod, module_inst, exec_env, module_binary, module_size));
}

void module::start_thread() {
    // spawn thread which can be later canceled:
    std::thread(
        [](std::shared_ptr<module> *m_p) {
            std::shared_ptr<module> m = *m_p;
            delete m_p;

#ifdef _POSIX_THREADS
            auto buf_size = m->name.length() + 6 + 1;
            std::unique_ptr<char[]> buf(new char[buf_size]);
            std::snprintf(buf.get(), buf_size, "%s(wasm)", m->name.c_str());
            pthread_setname_np(buf.get());
#endif

            wasm_runtime_init_thread_env();

            // create a debug instance:
            auto cluster = wasm_exec_env_get_cluster(m->exec_env);
            wasm_debug_instance_create(cluster, -1);
            wasm_cluster_thread_continue(m->exec_env);

            m->thread_main();
            wasm_runtime_destroy_thread_env();
        },
        new std::shared_ptr<module>(shared_from_this())
    ).detach();
}

void module::cancel_thread() {
    //wasm_cluster_cancel_thread(exec_env);
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
    const char *ex;
    ex = wasm_runtime_get_exception(module_inst);
    trace_printf(1UL << 0, "%s wasm module encountered exception: %s\n", name.c_str(), ex);
    return;

exec_main:
    if (!wasm_runtime_call_wasm(exec_env, func, 0, nullptr)) {
        goto fail;
    }
    trace_printf(1UL << 0, "%s wasm module exited normally\n", name.c_str());
}

bool module::wait_for_event(uint32_t timeout_usec, uint32_t &o_event) {
    trace_printf(1UL << 31, "{ wait_for_event(%llu)\n", timeout_usec);
    std::unique_lock<std::mutex> lk(event_mtx);
    if (event_notify_cv.wait_for(
        lk,
        std::chrono::microseconds(timeout_usec),
        [this]() { return event_triggered; }
    )) {
        event_triggered = false;
        o_event = event;
        trace_printf(1UL << 31, "} wait_for_event(%llu) -> %lu\n", timeout_usec, o_event);
        return true;
    }

    return false;
}

void module::ack_last_event() {
    {
        std::unique_lock<std::mutex> lk(event_mtx);
        event = 0;
        event_triggered = false;
    }
    event_ack_cv.notify_one();
    trace_printf(1UL << 31, "ack_last_event()\n");
}

void module::notify_event(uint32_t event_p) {
    {
        std::unique_lock<std::mutex> lk(event_mtx);
        event = event_p;
        event_triggered = true;
    }
    event_notify_cv.notify_one();
    trace_printf(1UL << 31, "notify_event(%lu)\n", event_p);
}

void module::wait_for_ack_last_event(std::chrono::nanoseconds timeout) {
    if (timeout == std::chrono::nanoseconds::zero()) {
        return;
    }

    // wait for ack_last_event call:
    trace_printf(1UL << 31, "{ wait_for_ack_last_event()\n");
    std::unique_lock<std::mutex> lk(event_mtx);
    event_ack_cv.wait_for(
        lk,
        timeout,
        [this]() { return !event_triggered; }
    );
    trace_printf(1UL << 31, "} wait_for_ack_last_event()\n");
}

void module::debugger_enable(bool enabled) {
    auto debug_instance = wasm_exec_env_get_instance(exec_env);
    if (enabled) {
        // set module to single-step mode so remote debugger can attach:
        wasm_cluster_send_signal_all(debug_instance->cluster, WAMR_SIG_SINGSTEP);
    } else {
        // continue wasm execution:
        wasm_cluster_thread_continue(exec_env);
        //wasm_debug_instance_continue(debug_instance);
    }
}

void module::notify_pc(uint32_t pc) {
    for (const auto &it: pc_events) {
        if (it.pc == pc) {
            // notify wasm of the PC-hit event:
            notify_event(ev_user0 + (pc & 0x00ffffff));

            // wait for wasm to complete its work:
            wait_for_ack_last_event(it.timeout);
        }
    }
}

uint32_t module::register_pc_event(uint32_t pc, uint32_t timeout_nsec) {
    // put a 14 millisecond cap on the timeout so it doesn't take an entire 60fps frame (16.67 milliseconds):
    if (timeout_nsec > 14000000UL) {
        timeout_nsec = 14000000UL;
    }

    uint32_t event_id = ev_user0 + (pc & 0x00ffffffUL);

    auto it = std::find_if(
        pc_events.begin(),
        pc_events.end(),
        [&](const auto &item) {
            return item.pc == pc;
        }
    );
    if (it != pc_events.end()) {
        it->timeout = std::chrono::nanoseconds(timeout_nsec);
        return event_id;
    }

    pc_events.emplace_back(
        pc_event{
            .timeout = std::chrono::nanoseconds(timeout_nsec),
            .pc = pc
        }
    );

    // return the event id:
    return event_id;
}

void module::unregister_pc_event(uint32_t pc) {
    auto it = std::find_if(
        pc_events.begin(),
        pc_events.end(),
        [&](const auto &item) {
            return item.pc == pc;
        }
    );
    if (!(it != pc_events.end())) return;
    pc_events.erase(it);
}
