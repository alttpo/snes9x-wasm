
#include <memory>
#include <thread>
#include <utility>
#include <vector>

// WAMR:
#include "wasm_export.h"
extern "C" {
#include "debug_engine.h"
}

#include "wasi_types.h"
#include "wasm_host.h"
#include "wasm_module.h"
#include "wasi_impl.h"

// snes9x:
#include "memmap.h"

std::vector<std::shared_ptr<module>> modules;

#ifdef MEASURE_TIMING
#  define MEASURE_TIMING_RETURN(name, expr) { \
        auto t_start = std::chrono::steady_clock::now(); \
        auto retval = (expr); \
        auto t_end = std::chrono::steady_clock::now(); \
        wasm_host_stdout_printf(name ": %lld us\n", std::chrono::duration_cast<std::chrono::microseconds>(t_end - t_start).count()); \
        return retval; \
    }
#  define MEASURE_TIMING_DO(name, stmt) { \
        auto t_start = std::chrono::steady_clock::now(); \
        stmt; \
        auto t_end = std::chrono::steady_clock::now(); \
        wasm_host_stdout_printf(name ": %lld us\n", std::chrono::duration_cast<std::chrono::microseconds>(t_end - t_start).count()); \
    }
#else
#  define MEASURE_TIMING_RETURN(name, expr) return (expr)
#  define MEASURE_TIMING_DO(name, stmt) stmt
#endif

bool wasm_host_init() {
    // initialize wasm runtime
    RuntimeInitArgs init;

    init.mem_alloc_type = Alloc_With_System_Allocator;
    //init.mem_alloc_type = Alloc_With_Pool;
    //init.mem_alloc_option.pool.heap_size = 1048576 * 64;
    //init.mem_alloc_option.pool.heap_buf = new uint8_t[init.mem_alloc_option.pool.heap_size];

    init.running_mode = Mode_Interp;
    strcpy(init.ip_addr, "127.0.0.1");
    init.instance_port = 0x0ACE; // 2766

    {
        auto *natives = new std::vector<NativeSymbol>();

        // output logging and trace subsystem:
        {
            natives->push_back({
                "log_stdout",
                (void *) (+[](wasm_exec_env_t exec_env, const char *text, uint32_t len) -> void {
                    wasm_host_stdout_write(text, text + len);
                }),
                "(*~)",
                nullptr
            });
            natives->push_back({
                "log_stderr",
                (void *) (+[](wasm_exec_env_t exec_env, const char *text, uint32_t len) -> void {
                    wasm_host_stderr_write(text, text + len);
                }),
                "(*~)",
                nullptr
            });
            natives->push_back({
                "log_trace",
                (void *) (+[](wasm_exec_env_t exec_env, uint32_t flags, const char *text, uint32_t len) -> void {
                    auto m = reinterpret_cast<module *>(wasm_runtime_get_user_data(exec_env));
                    m->trace_printf(flags, "%.*s", len, text);
                }),
                "(i*~)",
                nullptr
            });
        }

        // event subsystem (wait for irq, nmi, ppu frame start/end, shutdown, etc.):
        {
            natives->push_back({
                "event_wait_for",
                (void *) (+[](wasm_exec_env_t exec_env, uint32_t timeout_nsec, uint32_t *o_events) -> int32_t {
                    auto m = reinterpret_cast<module *>(wasm_runtime_get_user_data(exec_env));
                    MEASURE_TIMING_RETURN("event_wait_for", m->wait_for_event(timeout_nsec, *o_events));
                }),
                "(i*)i",
                nullptr
            });
        }

#ifdef REX_ALLOW_DIRECT_MEM_ACCESS
        // memory access:
        {
#define FUNC_READ(name, start, size) { \
                name, \
                ((void *)(+[](wasm_exec_env_t exec_env, uint8_t *dest, uint32_t dest_len, uint32_t offset) -> int32_t { \
                    if (offset >= size) return false; \
                    if (offset + dest_len > size) return false; \
                    MEASURE_TIMING_DO(name, { std::unique_lock<std::mutex> lk(Memory.lock); memcpy(dest, start + offset, dest_len); }); \
                    return true; \
                })), \
                "(*~i)i", \
                nullptr \
            }

#define FUNC_WRITE(name, start, size) { \
                name, \
                ((void *)(+[](wasm_exec_env_t exec_env, uint8_t *src, uint32_t src_len, uint32_t offset) -> int32_t { \
                    if (offset >= size) return false; \
                    if (offset + src_len > size) return false; \
                    MEASURE_TIMING_DO(name, { std::unique_lock<std::mutex> lk(Memory.lock); memcpy(start + offset, src, src_len); }); \
                    return true; \
                })), \
                "(*~i)i", \
                nullptr \
            }

            // mem_read_rom(uint8_t *dest, uint32_t dest_len, uint32_t offset) -> int32_t;
            // mem_read_sram(uint8_t *dest, uint32_t dest_len, uint32_t offset) -> int32_t;
            // mem_write_sram(uint8_t *src, uint32_t src_len, uint32_t offset) -> int32_t;
            // mem_read_wram(uint8_t *dest, uint32_t dest_len, uint32_t offset) -> int32_t;
            // mem_write_wram(uint8_t *src, uint32_t src_len, uint32_t offset) -> int32_t;
            // mem_read_vram(uint8_t *dest, uint32_t dest_len, uint32_t offset) -> int32_t;
            // mem_read_oam(uint8_t *dest, uint32_t dest_len, uint32_t offset) -> int32_t;
            natives->push_back(FUNC_READ("mem_read_rom", Memory.ROM, Memory.MAX_ROM_SIZE));
            natives->push_back(FUNC_READ("mem_read_sram", Memory.SRAM, Memory.SRAMStorage.size()));
            natives->push_back(FUNC_WRITE("mem_write_sram", Memory.SRAM, Memory.SRAM_SIZE));
            natives->push_back(FUNC_READ("mem_read_wram", Memory.RAM, sizeof(Memory.RAM)));
            natives->push_back(FUNC_WRITE("mem_write_wram", Memory.RAM, sizeof(Memory.RAM)));
            natives->push_back(FUNC_READ("mem_read_vram", Memory.VRAM, sizeof(Memory.VRAM)));
            natives->push_back(FUNC_READ("mem_read_oam", PPU.OAMData, sizeof(PPU.OAMData)));
#undef FUNC_WRITE
#undef FUNC_READ
        }
#endif

        // iovm I/O virtual machine subsystem used to perform low-latency memory access similar to how
        // console flash carts operate:
        {
            natives->push_back({
                "iovm1_init",
                ((void*)+[](wasm_exec_env_t exec_env) -> int32_t {
                    auto m = reinterpret_cast<module *>(wasm_runtime_get_user_data(exec_env));
                    return m->vm_init();
                }),
                "()i",
                nullptr
            });
            natives->push_back({
                "iovm1_load",
                ((void*)+[](wasm_exec_env_t exec_env, const uint8_t *vmprog, uint32_t vmprog_len) -> int32_t {
                    auto m = reinterpret_cast<module *>(wasm_runtime_get_user_data(exec_env));
                    return m->vm_load(vmprog, vmprog_len);
                }),
                "(*~)i",
                nullptr
            });
            natives->push_back({
                "iovm1_get_exec_state",
                ((void*)+[](wasm_exec_env_t exec_env) -> int32_t {
                    auto m = reinterpret_cast<module *>(wasm_runtime_get_user_data(exec_env));
                    return m->vm_getstate();
                }),
                "()i",
                nullptr
            });
            natives->push_back({
                "iovm1_exec_reset",
                ((void*)+[](wasm_exec_env_t exec_env) -> int32_t {
                    auto m = reinterpret_cast<module *>(wasm_runtime_get_user_data(exec_env));
                    return m->vm_reset();
                }),
                "()i",
                nullptr
            });
            natives->push_back({
                "iovm1_read_data",
                ((void*)+[](wasm_exec_env_t exec_env, uint8_t *dst, uint32_t dst_len,
                                     uint32_t *o_read, uint32_t *o_addr, uint8_t *o_target) -> int32_t {
                    auto m = reinterpret_cast<module *>(wasm_runtime_get_user_data(exec_env));
                    return m->vm_read_data(dst, dst_len, o_read, o_addr, o_target);
                }),
                "(*~***)i",
                nullptr
            });
        }

#ifdef EMULATE_FXPAKPRO
        // FX Pak Pro NMI override feature ($2C00) emulation:
        {
            natives->push_back({
                "upload_nmi_routine",
                ((void*)+[](wasm_exec_env_t exec_env, uint8_t *routine, uint32_t routine_len) -> int32_t {
                    //auto m = reinterpret_cast<module *>(wasm_runtime_get_user_data(exec_env));
                    // uploads a 65816 routine to $00:2C00..2DFF
                    memcpy(Memory.Extra2C00, routine, std::min(routine_len, 0x200U));
                    return (routine_len > 0x200U);
                }),
                "(*~)i",
                nullptr
            });
        }
#endif

        // ppux (PPU integrated extensions):
        {
            natives->push_back({
                "ppux_write_cmd",
                (void *) (+[](wasm_exec_env_t exec_env, uint32_t *data, uint32_t size) -> int32_t {
                    auto m = reinterpret_cast<module *>(wasm_runtime_get_user_data(exec_env));
                    MEASURE_TIMING_RETURN("ppux_write_cmd", m->ppux.cmd_write(data, size));
                }),
                "(*~)i",
                nullptr
            });
            natives->push_back({
                "ppux_write_vram",
                (void *) (+[](wasm_exec_env_t exec_env, uint32_t addr, uint8_t *data, uint32_t size) -> int32_t {
                    auto m = reinterpret_cast<module *>(wasm_runtime_get_user_data(exec_env));
                    MEASURE_TIMING_RETURN("ppux_write_vram", m->ppux.vram_upload(addr, data, size));
                }),
                "(i*~)i",
                nullptr
            });
            natives->push_back({
                "ppux_write_cgram",
                (void *) (+[](wasm_exec_env_t exec_env, uint32_t addr, uint8_t *data, uint32_t size) -> int32_t {
                    auto m = reinterpret_cast<module *>(wasm_runtime_get_user_data(exec_env));
                    MEASURE_TIMING_RETURN("ppux_write_cgram", m->ppux.cgram_upload(addr, data, size));
                }),
                "(i*~)i",
                nullptr
            });
        }

        // network interface:
        {
            // net_tcp_socket() -> int32_t
            natives->push_back({
                "net_tcp_socket",
                (void *) (+[](wasm_exec_env_t exec_env) -> int32_t {
                    auto m = reinterpret_cast<module *>(wasm_runtime_get_user_data(exec_env));
                    MEASURE_TIMING_RETURN("net_tcp_socket", m->net.tcp_socket());
                }),
                "()i",
                nullptr
            });
            // net_udp_socket() -> int32_t
            natives->push_back({
                "net_udp_socket",
                (void *) (+[](wasm_exec_env_t exec_env) -> int32_t {
                    auto m = reinterpret_cast<module *>(wasm_runtime_get_user_data(exec_env));
                    MEASURE_TIMING_RETURN("net_udp_socket", m->net.udp_socket());
                }),
                "()i",
                nullptr
            });
            // net_connect(int32_t slot, uint32_t ipv4_addr, uint16_t port) -> int32_t
            natives->push_back({
                "net_connect",
                (void *) (+[](wasm_exec_env_t exec_env, int32_t slot, uint32_t ipv4_addr, uint16_t port) -> int32_t {
                    auto m = reinterpret_cast<module *>(wasm_runtime_get_user_data(exec_env));
                    MEASURE_TIMING_RETURN("net_connect", m->net.connect(slot, ipv4_addr, port));
                }),
                "(iii)i",
                nullptr
            });
            // net_bind(int32_t slot, uint32_t ipv4_addr, uint16_t port) -> int32_t
            natives->push_back({
                "net_bind",
                (void *) (+[](wasm_exec_env_t exec_env, int32_t slot, uint32_t ipv4_addr, uint16_t port) -> int32_t {
                    auto m = reinterpret_cast<module *>(wasm_runtime_get_user_data(exec_env));
                    MEASURE_TIMING_RETURN("net_bind", m->net.bind(slot, ipv4_addr, port));
                }),
                "(iii)i",
                nullptr
            });
            // net_listen(int32_t slot) -> int32_t
            natives->push_back({
                "net_listen",
                (void *) (+[](wasm_exec_env_t exec_env, int32_t slot) -> int32_t {
                    auto m = reinterpret_cast<module *>(wasm_runtime_get_user_data(exec_env));
                    MEASURE_TIMING_RETURN("net_listen", m->net.listen(slot));
                }),
                "(i)i",
                nullptr
            });
            // net_accept(int32_t slot, uint32_t *o_ipv4_addr, uint16_t *o_port) -> int32_t
            natives->push_back({
                "net_accept",
                (void *) (+[](wasm_exec_env_t exec_env, int32_t slot, int32_t *o_accepted_slot, uint32_t *o_ipv4_addr, uint16_t *o_port) -> int32_t {
                    auto m = reinterpret_cast<module *>(wasm_runtime_get_user_data(exec_env));
                    MEASURE_TIMING_RETURN("net_accept", m->net.accept(slot, o_accepted_slot, o_ipv4_addr, o_port));
                }),
                "(i***)i",
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
            // net_send(int32_t slot, uint8_t *data, uint32_t data_len) -> int32_t
            natives->push_back({
                "net_send",
                (void *) (+[](wasm_exec_env_t exec_env, int32_t slot, uint8_t *data, uint32_t len) -> int32_t {
                    auto m = reinterpret_cast<module *>(wasm_runtime_get_user_data(exec_env));
                    MEASURE_TIMING_RETURN("net_send", m->net.send(slot, data, len));
                }),
                "(i*~)i",
                nullptr
            });
            // net_sendto(int32_t slot, uint8_t *data, uint32_t data_len, uint32_t ipv4_addr, uint16_t port) -> int32_t
            natives->push_back({
                "net_sendto",
                (void *) (+[](wasm_exec_env_t exec_env, int32_t slot, uint8_t *data, uint32_t len, uint32_t ipv4_addr, uint16_t port) -> int32_t {
                    auto m = reinterpret_cast<module *>(wasm_runtime_get_user_data(exec_env));
                    MEASURE_TIMING_RETURN("net_sendto", m->net.sendto(slot, data, len, ipv4_addr, port));
                }),
                "(i*~ii)i",
                nullptr
            });
            // net_recv(int32_t slot, uint8_t *data, uint32_t data_len) -> int32_t
            natives->push_back({
                "net_recv",
                (void *) (+[](wasm_exec_env_t exec_env, int32_t slot, uint8_t *data, uint32_t len) -> int32_t {
                    auto m = reinterpret_cast<module *>(wasm_runtime_get_user_data(exec_env));
                    MEASURE_TIMING_RETURN("net_recv", m->net.recv(slot, data, len));
                }),
                "(i*~)i",
                nullptr
            });
            // net_recvfrom(int32_t slot, uint8_t *data, uint32_t data_len, uint32_t *o_ipv4_addr, uint16_t *o_port) -> int32_t
            natives->push_back({
                "net_recvfrom",
                (void *) (+[](wasm_exec_env_t exec_env, int32_t slot, uint8_t *data, uint32_t len, uint32_t *o_ipv4_addr, uint16_t *o_port) -> int32_t {
                    auto m = reinterpret_cast<module *>(wasm_runtime_get_user_data(exec_env));
                    MEASURE_TIMING_RETURN("net_recvfrom", m->net.recvfrom(slot, data, len, o_ipv4_addr, o_port));
                }),
                "(i*~**)i",
                nullptr
            });
            // net_close(int32_t slot) -> int32_t
            natives->push_back({
                "net_close",
                (void *) (+[](wasm_exec_env_t exec_env, int32_t slot) -> int32_t {
                    auto m = reinterpret_cast<module *>(wasm_runtime_get_user_data(exec_env));
                    MEASURE_TIMING_RETURN("net_close", m->net.close(slot));
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
        wasm_host_stderr_printf("wasm_runtime_full_init failed\n");
        return false;
    }

    wasm_host_register_wasi();

    // each module listens for remote debuggers on port 2766+n where n >= 0:
    wasm_debug_engine_init((char *)"127.0.0.1", 2766);

    return true;
}

void module_shutdown(std::shared_ptr<module> &m) {
    // notify module for termination:
    m->notify_event(wasm_event_kind::ev_shutdown);

    // TODO: allow a grace period and then forcefully shut down with m->cancel_thread()
    m->wait_for_exit();

    m.reset();
}

void wasm_host_unload_all_modules() {
    for (auto it = modules.begin(); it != modules.end();) {
        auto &m = *it;

        module_shutdown(m);

        // releasing the shared_ptr should delete the module* and likely crash any running thread
        it = modules.erase(it);
    }
}

int wasm_host_load_module(const std::string &name, uint8_t *module_binary, uint32_t module_size) {
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
        wasm_host_stderr_printf("wasm_runtime_load: %s\n", wamrError);
        delete [] module_binary;
        return -1;
    }

    wasm_module_inst_t mi = wasm_runtime_instantiate(
        mod,
        1024 * 1024 * 4,
        1024 * 1024 * 128,
        wamrError,
        sizeof(wamrError)
    );
    if (!mi) {
        wasm_runtime_unload(mod);
        wasm_host_stderr_printf("wasm_runtime_instantiate: %s\n", wamrError);
        delete [] module_binary;
        return -1;
    }

    // track the new module:
    auto m = module::create(name, mod, mi, module_binary, module_size);
    modules.emplace_back(m);
    auto index = (int)modules.size() - 1;

    // start the new thread:
    m->start_thread();

    return index;
}

void wasm_host_notify_events(wasm_event_kind events) {
    for_each_module([=](const std::shared_ptr<module>& m) {
        m->notify_event(events);
    });
}

void wasm_host_debugger_enable(bool enabled) {
    for_each_module([=](const std::shared_ptr<module>& m) {
        m->debugger_enable(enabled);
    });
}

void wasm_host_notify_pc(uint32_t pc) {
    for_each_module([=](const std::shared_ptr<module>& m) {
        m->notify_pc(pc);
    });
}
