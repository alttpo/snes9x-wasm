
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

    // event subsystem (wait for irq, nmi, ppu frame start/end, shutdown, etc.):
    {
        natives->push_back({
            "wait_for_events",
            (void *) (int32_t (*)(wasm_exec_env_t, uint32_t, uint32_t, uint32_t *)) (
                [](wasm_exec_env_t exec_env,
                   uint32_t mask, uint32_t timeout_usec, uint32_t *o_events
                ) -> int32_t {
                    auto m = reinterpret_cast<module *>(wasm_runtime_get_user_data(exec_env));
                    return m->wait_for_events(mask, timeout_usec, *o_events);
                }
            ),
            "(ii*)i",
            nullptr
        });
    }

    // memory access:
    {
#define FUNC_READ(start, size) \
        (void *) (int32_t (*)(wasm_exec_env_t, uint8_t *, uint32_t, uint32_t)) ( \
            [](wasm_exec_env_t exec_env, uint8_t *dest, uint32_t dest_len, uint32_t offset) -> int32_t { \
                if (offset >= size) return false; \
                if (offset + dest_len > size) return false; \
                std::unique_lock<std::mutex> lk(Memory.lock); \
                memcpy(dest, start + offset, dest_len); \
                return true; \
            } \
        )
#define FUNC_WRITE(start, size) \
        (void *) (int32_t (*)(wasm_exec_env_t, uint8_t *, uint32_t, uint32_t)) ( \
            [](wasm_exec_env_t exec_env, uint8_t *dest, uint32_t dest_len, uint32_t offset) -> int32_t { \
                if (offset >= size) return false; \
                if (offset + dest_len > size) return false; \
                std::unique_lock<std::mutex> lk(Memory.lock); \
                memcpy(start + offset, dest, dest_len); \
                return true; \
            } \
        )

        natives->push_back({
            "rom_read",
            FUNC_READ(Memory.ROMStorage.data(), Memory.ROMStorage.size()),
            "(*~i)i",
            nullptr
        });
        natives->push_back({
            "sram_read",
            FUNC_READ(Memory.SRAMStorage.data(), Memory.SRAMStorage.size()),
            "(*~i)i",
            nullptr
        });
        natives->push_back({
            "sram_write",
            FUNC_WRITE(Memory.SRAMStorage.data(), Memory.SRAMStorage.size()),
            "(*~i)i",
            nullptr
        });
        natives->push_back({
            "wram_read",
            FUNC_READ(Memory.RAM, sizeof(Memory.RAM)),
            "(*~i)i",
            nullptr
        });
        natives->push_back({
            "wram_write",
            FUNC_WRITE(Memory.RAM, sizeof(Memory.RAM)),
            "(*~i)i",
            nullptr
        });
        natives->push_back({
            "vram_read",
            FUNC_READ(Memory.VRAM, sizeof(Memory.VRAM)),
            "(*~i)i",
            nullptr
        });
        natives->push_back({
            "oam_read",
            FUNC_READ(PPU.OAMData, sizeof(PPU.OAMData)),
            "(*~i)i",
            nullptr
        });
#undef FUNC_WRITE
#undef FUNC_READ
    }

    // ppux command queue:
    {
        natives->push_back({
            "ppux_write",
            (void *) (int32_t (*)(wasm_exec_env_t, uint32_t *, uint32_t)) (
                [](wasm_exec_env_t exec_env,
                   uint32_t *data, uint32_t size
                ) -> int32_t {
                    auto m = reinterpret_cast<module *>(wasm_runtime_get_user_data(exec_env));
                    return m->ppux.write_cmd(data, size);
                }
            ),
            "(*~)i",
            nullptr
        });
    }

    // network interface:
    {
        // net_tcp_listen(uint32_t port) -> int32_t
        natives->push_back({
            "net_tcp_listen",
            (void *) (int32_t (*)(wasm_exec_env_t, uint32_t)) (
                [](wasm_exec_env_t exec_env,
                   uint32_t port
                ) -> int32_t {
                    auto m = reinterpret_cast<module *>(wasm_runtime_get_user_data(exec_env));
                    return m->net.tcp_listen(port);
                }
            ),
            "(i)i",
            nullptr
        });
        // net_tcp_accept(int32_t fd) -> int32_t
        natives->push_back({
            "net_tcp_accept",
            (void *) (int32_t (*)(wasm_exec_env_t, int32_t)) (
                [](wasm_exec_env_t exec_env,
                   int32_t fd
                ) -> int32_t {
                    auto m = reinterpret_cast<module *>(wasm_runtime_get_user_data(exec_env));
                    return m->net.tcp_accept(fd);
                }
            ),
            "(i)i",
            nullptr
        });
        // net_poll(net_poll_slot *poll_slots, uint32_t poll_slots_len) -> int32_t
        natives->push_back({
            "net_poll",
            (void *) (int32_t (*)(wasm_exec_env_t, net_poll_slot *fds, uint32_t fds_len)) (
                [](wasm_exec_env_t exec_env,
                   net_poll_slot *poll_slots, uint32_t poll_slots_len
                ) -> int32_t {
                    auto m = reinterpret_cast<module *>(wasm_runtime_get_user_data(exec_env));
                    return m->net.poll(poll_slots, poll_slots_len);
                }
            ),
            "(*~)i",
            nullptr
        });
        // net_send(int32_t fd, uint8_t *data, uint32_t data_len) -> int32_t
        natives->push_back({
            "net_send",
            (void *) (int32_t (*)(wasm_exec_env_t, int32_t, uint8_t *data, uint32_t len)) (
                [](wasm_exec_env_t exec_env,
                   int32_t fd, uint8_t *data, uint32_t len
                ) -> int32_t {
                    auto m = reinterpret_cast<module *>(wasm_runtime_get_user_data(exec_env));
                    return m->net.send(fd, data, len);
                }
            ),
            "(i*~)i",
            nullptr
        });
        // net_recv(int32_t fd, uint8_t *data, uint32_t data_len) -> int32_t
        natives->push_back({
            "net_recv",
            (void *) (int32_t (*)(wasm_exec_env_t, int32_t, uint8_t *data, uint32_t len)) (
                [](wasm_exec_env_t exec_env,
                   int32_t fd, uint8_t *data, uint32_t len
                ) -> int32_t {
                    auto m = reinterpret_cast<module *>(wasm_runtime_get_user_data(exec_env));
                    return m->net.recv(fd, data, len);
                }
            ),
            "(i*~)i",
            nullptr
        });
        // net_close(int32_t fd) -> int32_t
        natives->push_back({
            "net_close",
            (void *) (int32_t (*)(wasm_exec_env_t, int32_t)) (
                [](wasm_exec_env_t exec_env,
                   int32_t fd
                ) -> int32_t {
                    auto m = reinterpret_cast<module *>(wasm_runtime_get_user_data(exec_env));
                    return m->net.close(fd);
                }
            ),
            "(i)i",
            nullptr
        });
    }

    init.n_native_symbols = natives->size();
    init.native_symbols = natives->data();
    init.native_module_name = "rex";

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
        1024 * 1024 * 4,
        1024 * 1024 * 64,
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
