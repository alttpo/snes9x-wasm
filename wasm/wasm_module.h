
#ifndef SNES9X_WASM_MODULE_H
#define SNES9X_WASM_MODULE_H

#include <cstdint>
#include <memory>
#include <utility>
#include <thread>
#include <vector>
#include <string>

#include "snes9x.h"

// WAMR:
#include "wasm_export.h"

#include "wasi_types.h"
#include "wasm_vfs.h"
#include "wasm_ppux.h"
#include "wasm_host.h"

class module : public std::enable_shared_from_this<module> {
public:
    module(std::string name_p, wasm_module_t mod_p, wasm_module_inst_t mi_p, wasm_exec_env_t exec_env_p);

    ~module();

    [[nodiscard]] static std::shared_ptr<module>
    create(std::string name_p, wasm_module_t mod_p, wasm_module_inst_t mi_p);

    void start_thread();

    void cancel_thread();

    void thread_main();

    std::string name;

public:

public:
    bool wait_for_events(uint32_t &events_p);

    void notify_events(uint32_t events_p);

private:
    wasi_iovec create_iovec(const iovec_app_t *iovec_app, uint32_t iovs_len);

private:
    wasm_module_t mod;
    wasm_module_inst_t module_inst;

    wasm_exec_env_t exec_env;
    std::unordered_map<wasi_fd_t, std::shared_ptr<fd_inst>> fds;

    wasi_fd_t fd_free = 3;
    std::mutex events_cv_mtx;
    std::condition_variable events_cv;

    std::atomic<uint32_t> events = wasm_event_kind::ev_none;

public:
    ppux ppux;
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
