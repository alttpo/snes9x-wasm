
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

std::vector<std::shared_ptr<module>> modules;

bool wasm_host_init() {
    // initialize wasm runtime
    RuntimeInitArgs init;

    init.mem_alloc_type = Alloc_With_Pool;
    init.mem_alloc_option.pool.heap_size = 1048576 * 64;
    init.mem_alloc_option.pool.heap_buf = new uint8_t[init.mem_alloc_option.pool.heap_size];

    init.running_mode = Mode_Interp;

    auto *natives = new std::vector<NativeSymbol>();
    natives->push_back({
        "rom_read",
        (void *) (int32_t (*)(wasm_exec_env_t, uint8_t *, uint32_t, uint32_t)) (
            [](wasm_exec_env_t exec_env,
               uint8_t *dest, uint32_t dest_len, uint32_t offset
            ) -> int32_t {
                auto &vec = Memory.ROMStorage;
                if (offset >= vec.size()) {
                    return false;
                }
                if (offset + dest_len > vec.size()) {
                    return false;
                }

                memcpy(dest, vec.data() + offset, dest_len);

                return true;
            }
        ),
        "(*~i)i",
        nullptr
    });

    init.n_native_symbols = natives->size();
    init.native_symbols = natives->data();
    init.native_module_name = "snes";

    if (!wasm_runtime_full_init(&init)) {
        fprintf(stderr, "wasm_runtime_full_init failed\n");
        return false;
    }

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
