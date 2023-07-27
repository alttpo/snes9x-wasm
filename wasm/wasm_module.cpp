
#include <utility>

#include "wasm_module.h"

extern "C" {
#include "debug_engine.h"
}
//#include "thread_manager.h"
#include "memmap.h"

#define IOVM1_USE_USERDATA

#include "iovm.c"

vm_read_result::vm_read_result() : len(0), a(0), t(0), buf() {}

vm_read_result::vm_read_result(
    const std::vector<uint8_t> &buf_p,
    uint16_t len_p,
    uint32_t a_p,
    uint8_t t_p
) : len(len_p), a(a_p), t(t_p), buf(buf_p) {}

vm_inst::vm_inst() : m(nullptr), n(-1) {}

module::module(
    std::string name_p, wasm_module_t mod_p, wasm_module_inst_t mi_p, wasm_exec_env_t exec_env_p,
    uint8_t *module_binary_p, uint32_t module_size_p
)
    : name(std::move(name_p)), mod(mod_p), module_inst(mi_p), exec_env(exec_env_p), module_binary(module_binary_p),
    module_size(module_size_p), ppux() {
    // set user_data to `this`:
    wasm_runtime_set_user_data(exec_env, static_cast<void *>(this));

    // initialize iovms:
    for (unsigned n = 0; n < 2; n++) {
        vms[n].m = this;
        vms[n].n = n;
        vm_init(n);
    }

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
module::create(
    std::string name, wasm_module_t mod, wasm_module_inst_t module_inst, uint8_t *module_binary, uint32_t module_size
) {
    auto exec_env = wasm_runtime_create_exec_env(module_inst, 1048576);
    if (!exec_env) {
        wasm_runtime_deinstantiate(module_inst);
        wasm_runtime_unload(mod);
        return nullptr;
    }

    return std::shared_ptr<module>(
        new module(
            std::move(name),
            mod,
            module_inst,
            exec_env,
            module_binary,
            module_size
        ));
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

            m->notify_exit();
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

bool module::wait_for_event(uint32_t timeout_nsec, uint32_t &o_event) {
    auto timeout_usec = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::nanoseconds(timeout_nsec)).count();
    trace_printf(1UL << 31, "{ wait_for_event(%llu us)\n", timeout_usec);
    std::unique_lock<std::mutex> lk(event_mtx);
    if (event_notify_cv.wait_for(
        lk,
        std::chrono::nanoseconds(timeout_nsec),
        [this]() { return !events.empty(); }
    )) {
        o_event = events.front();
        events.pop();
        if (!events.empty()) {
            event_notify_cv.notify_one();
        }
        trace_printf(1UL << 31, "} wait_for_event(%llu us) -> %lu\n", timeout_usec, o_event);
        return true;
    }

    trace_printf(1UL << 31, "} wait_for_event(%llu us) -> fail\n", timeout_usec);
    return false;
}

void module::notify_event(uint32_t event_p) {
    {
        std::unique_lock<std::mutex> lk(event_mtx);
        events.push(event_p);
    }
    event_notify_cv.notify_one();
    trace_printf(1UL << 31, "notify_event(%lu)\n", event_p);
}

bool module::wait_for_exit() {
    std::unique_lock<std::mutex> lk(exit_mtx);
    return exit_cv.wait_for(
        lk,
        std::chrono::microseconds(16700),
        [this] { return exited; }
    );
}

void module::notify_exit() {
    {
        std::unique_lock<std::mutex> lk(exit_mtx);
        exited = true;
    }
    exit_cv.notify_all();
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

////////////////////////////////////////////////////////////////////////

static std::pair<uint8_t *, uint32_t> memory_target(iovm1_target target) {
    switch (target) {
        case 0: // WRAM:
            return {Memory.RAM, sizeof(Memory.RAM)};
        case 1: // SRAM:
            return {Memory.SRAM, Memory.SRAMStorage.size()};
        case 2: // ROM:
            return {Memory.ROM, Memory.ROMStorage.size()};
#ifdef EMULATE_FXPAKPRO
        case 3: // 2C00:
            return {Memory.Extra2C00, sizeof(Memory.Extra2C00)};
#endif
        case 4: // VRAM:
            return {Memory.VRAM, sizeof(Memory.VRAM)};
        case 5: // CGRAM:
            return {(uint8_t*)PPU.CGDATA, sizeof(PPU.CGDATA)};
        case 6: // OAM:
            return {PPU.OAMData, sizeof(PPU.OAMData)};
        default: // memory target not defined:
            return {nullptr, 0};
    }
}

void vm_inst::trim_read_queue() {
    // remove oldest unread buffers to prevent infinite growth:
    while (read_queue.size() > 1024) {
        read_queue.pop();
    }
}

static const int bytes_per_cycle = 4;

extern "C" void iovm1_opcode_cb(struct iovm1_t *vm, struct iovm1_callback_state_t *cbs) {
    auto inst = reinterpret_cast<vm_inst *>(iovm1_get_userdata(vm));
    auto m = inst->m;

    auto mt = memory_target(cbs->t);
    auto mem = mt.first;
    auto mem_len = mt.second;

    if (cbs->o == IOVM1_OPCODE_READ) {
        // initialize transfer:
        if (cbs->initial) {
            if (!mem) {
                // memory target not defined; fill read buffer with 0s:
                inst->read_queue.emplace(
                    std::vector<uint8_t>(cbs->len, (uint8_t) 0),
                    cbs->len,
                    cbs->a,
                    cbs->t
                );
                inst->trim_read_queue();
                cbs->complete = true;
                m->notify_event(ev_iovm0_read_complete + inst->n);
                return;
            }

            // reserve enough space:
            inst->read_result = vm_read_result(
                std::vector<uint8_t>(),
                cbs->len,
                cbs->a,
                cbs->t
            );
            inst->read_result.buf.reserve(cbs->len);
        }

        for (int i = 0; (cbs->len > 0) && (i < bytes_per_cycle); i++) {
            // read a byte:
            uint8_t x;
            if (cbs->a < mem_len) {
                x = *(mem + cbs->a++);
            } else {
                // out of bounds access yields a 0 byte:
                x = 0;
                cbs->a++;
            }
            inst->read_result.buf.push_back(x);
            cbs->len--;
        }

        // finished with transfer?
        if (cbs->len == 0) {
            // push out the current read buffer:
            inst->read_queue.push(std::move(inst->read_result));
            inst->trim_read_queue();
            cbs->complete = true;
            m->notify_event(ev_iovm0_read_complete + inst->n);
        }

        return;
    }

    if (cbs->o == IOVM1_OPCODE_WRITE) {
        // write one byte per cycle:
        if (!mem) {
            cbs->complete = true;
            return;
        }

        for (int i = 0; (cbs->len > 0) && (i < bytes_per_cycle); i++) {
            // write a byte:
            if (cbs->a < mem_len) {
                *(mem + cbs->a++) = cbs->m[cbs->p++];
            } else {
                // out of bounds access:
                cbs->a++;
                cbs->p++;
            }
            cbs->len--;
        }

        // finished with transfer?
        if (cbs->len == 0) {
            cbs->complete = true;
        }

        return;
    }

    // remaining opcodes are wait-while:

    if (!mem) {
        // memory target not defined:
        cbs->complete = true;
        return;
    }

    if (cbs->a >= mem_len) {
        // out of range:
        cbs->complete = true;
        return;
    }

    // read byte and apply mask:
    uint8_t b = *(mem + cbs->a) & cbs->msk;
    bool cond;
    switch (cbs->o) {
        case IOVM1_OPCODE_WAIT_WHILE_NEQ:
            cond = (b != cbs->cmp);
            break;
        case IOVM1_OPCODE_WAIT_WHILE_EQ:
            cond = (b == cbs->cmp);
            break;
        case IOVM1_OPCODE_WAIT_WHILE_LT:
            cond = (b < cbs->cmp);
            break;
        case IOVM1_OPCODE_WAIT_WHILE_GT:
            cond = (b > cbs->cmp);
            break;
        case IOVM1_OPCODE_WAIT_WHILE_LTE:
            cond = (b <= cbs->cmp);
            break;
        case IOVM1_OPCODE_WAIT_WHILE_GTE:
            cond = (b >= cbs->cmp);
            break;
        default:
            cond = false;
            break;
    }
    cbs->complete = !cond;
}

int32_t module::vm_init(unsigned n) {
    if (n >= vms.size()) {
        return IOVM1_ERROR_OUT_OF_RANGE;
    }

    std::unique_lock<std::mutex> lk(vms[n].vm_mtx);

    iovm1_init(&vms[n].vm);
    iovm1_set_userdata(&vms[n].vm, (void *) &vms[n]);

    return IOVM1_SUCCESS;
}

int32_t module::vm_load(unsigned n, const uint8_t *vmprog, uint32_t vmprog_len) {
    if (n >= vms.size()) {
        return IOVM1_ERROR_OUT_OF_RANGE;
    }

    std::unique_lock<std::mutex> lk(vms[n].vm_mtx);

    return iovm1_load(&vms[n].vm, vmprog, vmprog_len);
}

iovm1_state module::vm_getstate(unsigned n) {
    if (n >= vms.size()) {
        return static_cast<iovm1_state>(-1);
    }

    std::unique_lock<std::mutex> lk(vms[n].vm_mtx);

    return iovm1_get_exec_state(&vms[n].vm);
}

int32_t module::vm_reset(unsigned n) {
    if (n >= vms.size()) {
        return IOVM1_ERROR_OUT_OF_RANGE;
    }

    std::unique_lock<std::mutex> lk(vms[n].vm_mtx);

    return iovm1_exec_reset(&vms[n].vm);
}

int32_t module::vm_read_data(unsigned n, uint8_t *dst, uint32_t dst_len, uint32_t *o_read, uint32_t *o_addr, uint8_t *o_target) {
    if (n >= vms.size()) {
        return IOVM1_ERROR_OUT_OF_RANGE;
    }

    std::unique_lock<std::mutex> lk(vms[n].vm_mtx);

    *o_read = 0;
    *o_addr = -1;
    *o_target = -1;

    std::queue<vm_read_result> &rq = vms[n].read_queue;

    if (rq.empty()) {
        return IOVM1_ERROR_NO_DATA;
    }

    auto &v = rq.front();
    *o_addr = v.a;
    *o_target = v.t;

    if (v.buf.size() > dst_len) {
        // not enough space to read into:
        return IOVM1_ERROR_BUFFER_TOO_SMALL;
    }

    // fill in the buffer:
    uint32_t i;
    for (i = 0; i < v.buf.size(); i++) {
        dst[i] = v.buf[i];
    }
    *o_read = v.buf.size();

    rq.pop();

    return IOVM1_SUCCESS;
}

////////////////////////////////////////////////////////////////////////


void module::notify_pc(uint32_t pc) {
    // this method is called before every instruction:

    for (unsigned n = 0; n < 2; n++) {
        // execute opcodes in the iovm until a blocking operation (read, write, while) occurs:
        std::unique_lock<std::mutex> lk(vms[n].vm_mtx);
        auto last_state = iovm1_get_exec_state(&vms[n].vm);
        if (IOVM1_SUCCESS == iovm1_exec(&vms[n].vm)) {
            auto curr_state = iovm1_get_exec_state(&vms[n].vm);
            if ((curr_state != last_state) && (curr_state == IOVM1_STATE_ENDED)) {
                // fire ev_iovm_end event:
                notify_event(ev_iovm0_end + n);
            }
        }
    }
}
