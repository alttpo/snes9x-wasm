
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

struct vm_read_result {
    uint16_t                len;
    uint32_t                a;
    uint8_t                 t;
    std::vector<uint8_t>    buf;

    vm_read_result();
    vm_read_result(
        const std::vector<uint8_t> &buf,
        uint16_t                    len,
        uint32_t                    a,
        uint8_t                     t
    );
};

class module;

struct vm_inst {
    module *m;
    unsigned n;

    std::mutex vm_mtx;
    struct iovm1_t vm{};

    vm_read_result read_result;
    std::queue<vm_read_result> read_queue{};

    uint32_t addr_init;
    uint32_t p_init;
    uint32_t len_init;

    vm_inst();

    void trim_read_queue();
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

    void notify_event(uint32_t event_p);

    void debugger_enable(bool enabled);

    void notify_pc(uint32_t pc);

    bool wait_for_exit();

    void notify_exit();

public:
    int32_t vm_init(unsigned n);

    int32_t vm_load(unsigned n, const uint8_t *vmprog, uint32_t vmprog_len);

    iovm1_state vm_getstate(unsigned n);

    int32_t vm_reset(unsigned n);

    int32_t vm_read_data(unsigned n, uint8_t *dst, uint32_t dst_len, uint32_t *o_read, uint32_t *o_addr, uint8_t *o_target);

private:
    wasm_module_t mod;
    wasm_module_inst_t module_inst;
    wasm_exec_env_t exec_env;
    uint8_t *module_binary;
    uint32_t module_size;

    std::mutex event_mtx;
    std::condition_variable event_notify_cv;

    std::queue<uint32_t> events{};

    std::mutex exit_mtx;
    std::condition_variable exit_cv;
    bool exited = false;

    std::array<struct vm_inst, 2> vms{};

    friend void iovm1_opcode_cb(struct iovm1_t *vm, struct iovm1_callback_state_t *cbs);

public:
    ppux ppux;
    net net;

    uint32_t trace_mask = (1UL << 0);
    // for debugging:
    //uint32_t trace_mask = (1UL << 0) | (1UL << 31);

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
