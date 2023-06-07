
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

#include "wasi_impl.h"

void wasm_host_register_wasi() {
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

    natives->push_back({
        "fd_prestat_get",
        (void *) (+[](wasm_exec_env_t exec_env, wasi_fd_t fd, wasi_prestat_app_t *prestat_app) -> int32_t {
            auto m = reinterpret_cast<module *>(wasm_runtime_get_user_data(exec_env));
            if (fd >= 3) {
                return WASI_EBADF;
            }
            return 0;
        }),
        "(i*)i",
        nullptr
    });

    natives->push_back({
        "fd_prestat_dir_name",
        (void *) (+[](wasm_exec_env_t exec_env, wasi_fd_t fd, char *path, size_t path_len) -> int32_t {
            auto m = reinterpret_cast<module *>(wasm_runtime_get_user_data(exec_env));
            return 0;
        }),
        "(i*~)i",
        nullptr
    });

    natives->push_back({
        "args_sizes_get",
        (void *) (+[](wasm_exec_env_t exec_env, uint32_t *argc_app, uint32_t *argv_buf_size_app) -> int32_t {
            auto m = reinterpret_cast<module *>(wasm_runtime_get_user_data(exec_env));
            *argc_app = 0;
            *argv_buf_size_app = 0;
            return 0;
        }),
        "(**)i",
        nullptr
    });

    natives->push_back({
        "clock_time_get",
        (void *) (+[](wasm_exec_env_t exec_env, wasi_clockid_t clock_id, wasi_timestamp_t precision, wasi_timestamp_t *time) -> int32_t {
            auto m = reinterpret_cast<module *>(wasm_runtime_get_user_data(exec_env));
            switch (clock_id) {
                case WASI_CLOCK_REALTIME:
                    *time = std::chrono::system_clock::now().time_since_epoch().count();
                    return 0;
                case WASI_CLOCK_MONOTONIC:
                    *time = std::chrono::steady_clock::now().time_since_epoch().count();
                    return 0;
                default:
                    return 0;
            }
        }),
        "(iI*)i",
        nullptr
    });

    natives->push_back({
        "proc_exit",
        (void *) (+[](wasm_exec_env_t exec_env, wasi_exitcode_t exitcode) -> void {
            auto m = reinterpret_cast<module *>(wasm_runtime_get_user_data(exec_env));
            wasm_runtime_set_exception(wasm_runtime_get_module_inst(exec_env), "wasi proc exit");
        }),
        "(i)",
        nullptr
    });

    wasm_runtime_register_natives(
        "wasi_snapshot_preview1",
        natives->data(),
        natives->size()
    );
}
