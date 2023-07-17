
#ifndef SNES9X_WASM_MODULE_H
#define SNES9X_WASM_MODULE_H

#include <cstdint>
#include <memory>
#include <utility>
#include <thread>
#include <vector>
#include <queue>
#include <string>

#ifdef __WIN32__
#include <winsock2.h>
#endif

#include "snes9x.h"

// WAMR:
#include "wasm_export.h"

#include "wasi_types.h"
#include "wasm_ppux.h"
#include "wasm_host.h"
#include "wasm_net.h"

#define IOVM1_USE_USERDATA
#include "iovm.h"

struct pc_event {
    std::chrono::nanoseconds timeout;
    uint32_t pc;
};

class module : public std::enable_shared_from_this<module> {
public:
    module(
        std::string name_p, wasm_module_t mod_p, wasm_module_inst_t mi_p, wasm_exec_env_t exec_env_p,
        uint8_t *module_binary_p, uint32_t module_size_p
    );

    ~module();

    [[nodiscard]] static std::shared_ptr<module>
    create(
        std::string name_p, wasm_module_t mod_p, wasm_module_inst_t mi_p, uint8_t *module_binary_p,
        uint32_t module_size_p
    );

    void start_thread();

    void cancel_thread();

    void thread_main();

    std::string name;

public:
    bool wait_for_event(uint32_t timeout_nsec, uint32_t &o_event);

    void ack_last_event();

    void wait_for_ack_last_event(std::chrono::nanoseconds timeout);

    void notify_event(uint32_t event_p);

    void debugger_enable(bool enabled);

    void notify_pc(uint32_t pc);

    uint32_t register_pc_event(uint32_t pc, uint32_t timeout_nsec);

    void unregister_pc_event(uint32_t pc);

public:
    int32_t vm_init();

    int32_t vm_load(const uint8_t *vmprog, uint32_t vmprog_len);

    iovm1_state vm_getstate();

    int32_t vm_reset();

    int32_t vm_read_data(uint8_t *dst, uint32_t dst_len, uint32_t *o_read);

    void vm_ended();

private:
    wasm_module_t mod;
    wasm_module_inst_t module_inst;
    wasm_exec_env_t exec_env;
    uint8_t *module_binary;
    uint32_t module_size;

    std::mutex event_mtx;
    std::condition_variable event_notify_cv;
    std::condition_variable event_ack_cv;

    uint32_t event = wasm_event_kind::ev_none;
    bool event_triggered = false;

    std::array<pc_event, 8> pc_events;

    std::mutex vm_mtx;
    struct iovm1_t vm{};
    std::queue<std::vector<uint8_t>> vm_read_buf{};

    friend void iovm1_read_cb(struct iovm1_state_t *s);
    friend void iovm1_write_cb(struct iovm1_state_t *s);
    friend void iovm1_while_neq_cb(struct iovm1_state_t *s);
    friend void iovm1_while_eq_cb(struct iovm1_state_t *s);

public:
    ppux ppux;
    net net;

    uint32_t trace_mask = (1 << 0);

    template<typename ... Args>
    void trace_printf(uint32_t flags, const std::string& format, Args ... args) {
        if ((flags & trace_mask) == 0) {
            return;
        }

        wasm_host_stdout_printf(format, args ...);
    }
};

extern std::vector<std::shared_ptr<module>> modules;

template<typename ITER>
static void for_each_module(ITER iter) {
    for (auto &m: modules) {
        if (!m) {
            continue;
        }

        iter(m);
    }
}

#endif //SNES9X_WASM_MODULE_H
