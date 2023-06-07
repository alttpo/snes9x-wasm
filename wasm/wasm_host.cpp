
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
#include "memmap.h"

std::vector<std::shared_ptr<module>> modules;

#ifdef MEASURE_TIMING
#  define MEASURE_TIMING_RETURN(name, expr) { \
        auto t_start = std::chrono::steady_clock::now(); \
        auto retval = (expr); \
        auto t_end = std::chrono::steady_clock::now(); \
        printf(name ": %lld us\n", std::chrono::duration_cast<std::chrono::microseconds>(t_end - t_start).count()); \
        return retval; \
    }
#  define MEASURE_TIMING_DO(name, stmt) { \
        auto t_start = std::chrono::steady_clock::now(); \
        stmt; \
        auto t_end = std::chrono::steady_clock::now(); \
        printf(name ": %lld us\n", std::chrono::duration_cast<std::chrono::microseconds>(t_end - t_start).count()); \
    }
#else
#  define MEASURE_TIMING_RETURN(name, expr) return (expr)
#  define MEASURE_TIMING_DO(name, stmt) stmt
#endif

bool wasm_host_init() {
    // initialize wasm runtime
    RuntimeInitArgs init;

    init.mem_alloc_type = Alloc_With_Pool;
    init.mem_alloc_option.pool.heap_size = 1048576 * 64;
    init.mem_alloc_option.pool.heap_buf = new uint8_t[init.mem_alloc_option.pool.heap_size];

    init.running_mode = Mode_Interp;

    {
        auto *natives = new std::vector<NativeSymbol>();

        // event subsystem (wait for irq, nmi, ppu frame start/end, shutdown, etc.):
        {
            natives->push_back({
                "wait_for_event",
                (void *) (+[](wasm_exec_env_t exec_env, uint32_t timeout_usec, uint32_t *o_events) -> int32_t {
                    auto m = reinterpret_cast<module *>(wasm_runtime_get_user_data(exec_env));
                    MEASURE_TIMING_RETURN("wait_for_event", m->wait_for_event(timeout_usec, *o_events));
                }),
                "(i*)i",
                nullptr
            });
            natives->push_back({
                "ack_last_event",
                (void *) (+[](wasm_exec_env_t exec_env) -> void {
                    auto m = reinterpret_cast<module *>(wasm_runtime_get_user_data(exec_env));
                    MEASURE_TIMING_DO("ack_last_event", m->ack_last_event());
                }),
                "()",
                nullptr
            });
        }

        // memory access:
        {
#define FUNC_READ(start, size) \
        ((void *)(+[](wasm_exec_env_t exec_env, uint8_t *dest, uint32_t dest_len, uint32_t offset) -> int32_t { \
            if (offset >= size) return false; \
            if (offset + dest_len > size) return false; \
            MEASURE_TIMING_DO("mem_read", { std::unique_lock<std::mutex> lk(Memory.lock); memcpy(dest, start + offset, dest_len); }); \
            return true; \
        }))
#define FUNC_WRITE(start, size) \
        ((void *)(+[](wasm_exec_env_t exec_env, uint8_t *dest, uint32_t dest_len, uint32_t offset) -> int32_t { \
            if (offset >= size) return false; \
            if (offset + dest_len > size) return false; \
            MEASURE_TIMING_DO("mem_write", { std::unique_lock<std::mutex> lk(Memory.lock); memcpy(start + offset, dest, dest_len); }); \
            return true; \
        }))

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
                (void *) (+[](wasm_exec_env_t exec_env, uint32_t *data, uint32_t size) -> int32_t {
                    auto m = reinterpret_cast<module *>(wasm_runtime_get_user_data(exec_env));
                    MEASURE_TIMING_RETURN("ppux_write", m->ppux.write_cmd(data, size));
                }),
                "(*~)i",
                nullptr
            });
        }

        // network interface:
        {
            // net_tcp_listen(uint32_t port) -> int32_t
            natives->push_back({
                "net_tcp_listen",
                (void *) (+[](wasm_exec_env_t exec_env, uint32_t port) -> int32_t {
                    auto m = reinterpret_cast<module *>(wasm_runtime_get_user_data(exec_env));
                    MEASURE_TIMING_RETURN("net_tcp_listen", m->net.tcp_listen(port));
                }),
                "(i)i",
                nullptr
            });
            // net_tcp_accept(int32_t fd) -> int32_t
            natives->push_back({
                "net_tcp_accept",
                (void *) (+[](wasm_exec_env_t exec_env, int32_t fd) -> int32_t {
                    auto m = reinterpret_cast<module *>(wasm_runtime_get_user_data(exec_env));
                    MEASURE_TIMING_RETURN("net_tcp_accept", m->net.tcp_accept(fd));
                }),
                "(i)i",
                nullptr
            });
            // net_poll(net_poll_slot *poll_slots, uint32_t poll_slots_len) -> int32_t
            natives->push_back({
                "net_poll",
                (void *) (+[](wasm_exec_env_t exec_env, net_poll_slot *poll_slots, uint32_t poll_slots_len) -> int32_t {
                    auto m = reinterpret_cast<module *>(wasm_runtime_get_user_data(exec_env));
                    MEASURE_TIMING_RETURN("net_poll", m->net.poll(poll_slots, poll_slots_len));
                }),
                "(*~)i",
                nullptr
            });
            // net_send(int32_t fd, uint8_t *data, uint32_t data_len) -> int32_t
            natives->push_back({
                "net_send",
                (void *) (+[](wasm_exec_env_t exec_env, int32_t fd, uint8_t *data, uint32_t len) -> int32_t {
                    auto m = reinterpret_cast<module *>(wasm_runtime_get_user_data(exec_env));
                    MEASURE_TIMING_RETURN("net_send", m->net.send(fd, data, len));
                }),
                "(i*~)i",
                nullptr
            });
            // net_recv(int32_t fd, uint8_t *data, uint32_t data_len) -> int32_t
            natives->push_back({
                "net_recv",
                (void *) (+[](wasm_exec_env_t exec_env, int32_t fd, uint8_t *data, uint32_t len) -> int32_t {
                    auto m = reinterpret_cast<module *>(wasm_runtime_get_user_data(exec_env));
                    MEASURE_TIMING_RETURN("net_recv", m->net.recv(fd, data, len));
                }),
                "(i*~)i",
                nullptr
            });
            // net_close(int32_t fd) -> int32_t
            natives->push_back({
                "net_close",
                (void *) (+[](wasm_exec_env_t exec_env, int32_t fd) -> int32_t {
                    auto m = reinterpret_cast<module *>(wasm_runtime_get_user_data(exec_env));
                    MEASURE_TIMING_RETURN("net_close", m->net.close(fd));
                }),
                "(i)i",
                nullptr
            });
        }

        init.n_native_symbols = natives->size();
        init.native_symbols = natives->data();
        init.native_module_name = "rex";
    }

    if (!wasm_runtime_full_init(&init)) {
        fprintf(stderr, "wasm_runtime_full_init failed\n");
        return false;
    }

    {
        // register minimal WASI implementation:
        auto *natives = new std::vector<NativeSymbol>();

        // tinygo wasi target imports:
        // clock_time_get
        // args_sizes_get
        // args_get
        // environ_get
        // environ_sizes_get
        // fd_close
        // fd_fdstat_get
        // fd_prestat_get
        // fd_prestat_dir_name
        // fd_read
        // fd_seek
        // fd_write
        // path_open
        // proc_exit

        // wasi basic signature strings:
        // (args_get, "(**)i"),
        // (args_sizes_get, "(**)i"),
        // (clock_res_get, "(i*)i"),
        // (clock_time_get, "(iI*)i"),
        // (environ_get, "(**)i"),
        // (environ_sizes_get, "(**)i"),
        // (fd_prestat_get, "(i*)i"),
        // (fd_prestat_dir_name, "(i*~)i"),
        // (fd_close, "(i)i"),
        // (fd_datasync, "(i)i"),
        // (fd_pread, "(i*iI*)i"),
        // (fd_pwrite, "(i*iI*)i"),
        // (fd_read, "(i*i*)i"),
        // (fd_renumber, "(ii)i"),
        // (fd_seek, "(iIi*)i"),
        // (fd_tell, "(i*)i"),
        // (fd_fdstat_get, "(i*)i"),
        // (fd_fdstat_set_flags, "(ii)i"),
        // (fd_fdstat_set_rights, "(iII)i"),
        // (fd_sync, "(i)i"),
        // (fd_write, "(i*i*)i"),
        // (fd_advise, "(iIIi)i"),
        // (fd_allocate, "(iII)i"),
        // (path_create_directory, "(i*~)i"),
        // (path_link, "(ii*~i*~)i"),
        // (path_open, "(ii*~iIIi*)i"),
        // (fd_readdir, "(i*~I*)i"),
        // (path_readlink, "(i*~*~*)i"),
        // (path_rename, "(i*~i*~)i"),
        // (fd_filestat_get, "(i*)i"),
        // (fd_filestat_set_times, "(iIIi)i"),
        // (fd_filestat_set_size, "(iI)i"),
        // (path_filestat_get, "(ii*~*)i"),
        // (path_filestat_set_times, "(ii*~IIi)i"),
        // (path_symlink, "(*~i*~)i"),
        // (path_unlink_file, "(i*~)i"),
        // (path_remove_directory, "(i*~)i"),
        // (poll_oneoff, "(**i*)i"),
        // (proc_exit, "(i)"),
        // (proc_raise, "(i)i"),
        // (random_get, "(*~)i"),
        // (sock_accept, "(ii*)i"),
        // (sock_addr_local, "(i*)i"),
        // (sock_addr_remote, "(i*)i"),
        // (sock_addr_resolve, "($$**i*)i"),
        // (sock_bind, "(i*)i"),
        // (sock_close, "(i)i"),
        // (sock_connect, "(i*)i"),
        // (sock_get_broadcast, "(i*)i"),
        // (sock_get_keep_alive, "(i*)i"),
        // (sock_get_linger, "(i**)i"),
        // (sock_get_recv_buf_size, "(i*)i"),
        // (sock_get_recv_timeout, "(i*)i"),
        // (sock_get_reuse_addr, "(i*)i"),
        // (sock_get_reuse_port, "(i*)i"),
        // (sock_get_send_buf_size, "(i*)i"),
        // (sock_get_send_timeout, "(i*)i"),
        // (sock_get_tcp_fastopen_connect, "(i*)i"),
        // (sock_get_tcp_keep_idle, "(i*)i"),
        // (sock_get_tcp_keep_intvl, "(i*)i"),
        // (sock_get_tcp_no_delay, "(i*)i"),
        // (sock_get_tcp_quick_ack, "(i*)i"),
        // (sock_get_ip_multicast_loop, "(ii*)i"),
        // (sock_get_ip_multicast_ttl, "(i*)i"),
        // (sock_get_ip_ttl, "(i*)i"),
        // (sock_get_ipv6_only, "(i*)i"),
        // (sock_listen, "(ii)i"),
        // (sock_open, "(iii*)i"),
        // (sock_recv, "(i*ii**)i"),
        // (sock_recv_from, "(i*ii**)i"),
        // (sock_send, "(i*ii*)i"),
        // (sock_send_to, "(i*ii**)i"),
        // (sock_set_broadcast, "(ii)i"),
        // (sock_set_keep_alive, "(ii)i"),
        // (sock_set_linger, "(iii)i"),
        // (sock_set_recv_buf_size, "(ii)i"),
        // (sock_set_recv_timeout, "(iI)i"),
        // (sock_set_reuse_addr, "(ii)i"),
        // (sock_set_reuse_port, "(ii)i"),
        // (sock_set_send_buf_size, "(ii)i"),
        // (sock_set_send_timeout, "(iI)i"),
        // (sock_set_tcp_fastopen_connect, "(ii)i"),
        // (sock_set_tcp_keep_idle, "(ii)i"),
        // (sock_set_tcp_keep_intvl, "(ii)i"),
        // (sock_set_tcp_no_delay, "(ii)i"),
        // (sock_set_tcp_quick_ack, "(ii)i"),
        // (sock_set_ip_multicast_loop, "(iii)i"),
        // (sock_set_ip_multicast_ttl, "(ii)i"),
        // (sock_set_ip_add_membership, "(i*i)i"),
        // (sock_set_ip_drop_membership, "(i*i)i"),
        // (sock_set_ip_ttl, "(ii)i"),
        // (sock_set_ipv6_only, "(ii)i"),
        // (sock_shutdown, "(ii)i"),
        // (sched_yield, "()i"),

        typedef uint8_t wasi_preopentype_t;
        typedef struct wasi_prestat_app {
            wasi_preopentype_t pr_type;
            uint32 pr_name_len;
        } wasi_prestat_app_t;

        natives->push_back({
            "fd_prestat_get",
            (void *) (+[](wasm_exec_env_t exec_env, wasi_fd_t fd, wasi_prestat_app_t *prestat_app) -> int32_t {
                auto m = reinterpret_cast<module *>(wasm_runtime_get_user_data(exec_env));
                MEASURE_TIMING_RETURN("fd_prestat_get", -1);
            }),
            "(i*)i",
            nullptr
        });

        wasm_runtime_register_natives(
            "wasi_snapshot_preview1",
            natives->data(),
            natives->size()
        );
    }

    return true;
}

void module_shutdown(std::shared_ptr<module> &m) {
    // notify module for termination:
    m->notify_event(wasm_event_kind::ev_shutdown);
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
        m->notify_event(events);
    });
}
