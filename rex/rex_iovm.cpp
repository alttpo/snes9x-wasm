
#ifdef __WIN32__
#define WIN32_LEAN_AND_MEAN
#endif

#include "snes9x.h"
#include "memmap.h"

#include "rex.h"

#define IOVM1_USE_USERDATA

#include "iovm.c"
#include "rex_iovm.h"
#include "rex_proto.h"


////////////////////////////////////////////////////////////////////////

vm_inst::vm_inst(vm_notifier *notifier_p) :
    notifier(notifier_p)
{}

void vm_inst::inc_cycles(int32_t delta) {
    if (timeout_cycles > 0) {
        timeout_cycles -= delta;
    }
}

void vm_inst::on_pc(uint32_t pc) {
    // this method is called before every instruction:

    (void)pc;

    // execute opcodes in the iovm:
    std::lock_guard lk(vm_mtx);

    iovm1_exec(&vm);
}

static const int bytes_per_cycle = 4;

extern "C" void host_send_abort(struct iovm1_t *vm) {
    auto inst = reinterpret_cast<vm_inst *>(iovm1_get_userdata(vm));

    inst->notifier->vm_notify_ended(vm->p, vm->e);
}

extern "C" void host_send_read(struct iovm1_t *vm, uint8_t l_raw, uint8_t *d) {
    auto inst = reinterpret_cast<vm_inst *>(iovm1_get_userdata(vm));

    inst->notifier->vm_notify_read(vm->p, inst->c, inst->a_init, l_raw, d);
}

extern "C" void host_send_end(struct iovm1_t *vm) {
    auto inst = reinterpret_cast<vm_inst *>(iovm1_get_userdata(vm));

    inst->notifier->vm_notify_ended(vm->p, vm->e);
}

extern "C" void host_timer_reset(struct iovm1_t *vm) {
    auto inst = reinterpret_cast<vm_inst *>(iovm1_get_userdata(vm));
    // 1364 master cycles * 262 scanlines = 357368 cycles / frame
    inst->timeout_cycles = 357368;
}

extern "C" bool host_timer_elapsed(struct iovm1_t *vm) {
    auto inst = reinterpret_cast<vm_inst *>(iovm1_get_userdata(vm));
    return inst->timeout_cycles <= 0;
}

extern "C" enum iovm1_error host_memory_init(struct iovm1_t *vm, iovm1_memory_chip_t c, uint24_t a) {
    auto inst = reinterpret_cast<vm_inst *>(iovm1_get_userdata(vm));

    inst->c = c;
    inst->a = a;
    inst->a_init = a;

    auto mt = rex_memory_chip(c);
    if (!mt.p) {
        // memory target not defined:
        return IOVM1_ERROR_MEMORY_CHIP_UNDEFINED;
    }

    auto mem_len = mt.size;
    if (a > mem_len) {
        // memory target address out of range:
        return IOVM1_ERROR_MEMORY_CHIP_ADDRESS_OUT_OF_RANGE;
    }

    return IOVM1_SUCCESS;
}

extern "C" enum iovm1_error host_memory_read_validate(struct iovm1_t *vm, int l) {
    auto inst = reinterpret_cast<vm_inst *>(iovm1_get_userdata(vm));

    auto mt = rex_memory_chip(inst->c);

    // validate:
    if (!mt.p) {
        // memory chip not defined:
        return IOVM1_ERROR_MEMORY_CHIP_UNDEFINED;
    }
    if (!mt.readable) {
        // memory chip not readable:
        return IOVM1_ERROR_MEMORY_CHIP_NOT_READABLE;
    }
    if (inst->a + l > mt.size) {
        // memory chip address out of range:
        return IOVM1_ERROR_MEMORY_CHIP_ADDRESS_OUT_OF_RANGE;
    }

    return IOVM1_SUCCESS;
}

extern "C" enum iovm1_error host_memory_write_validate(struct iovm1_t *vm, int l) {
    auto inst = reinterpret_cast<vm_inst *>(iovm1_get_userdata(vm));

    auto mt = rex_memory_chip(inst->c);

    // validate:
    if (!mt.p) {
        // memory chip not defined:
        return IOVM1_ERROR_MEMORY_CHIP_UNDEFINED;
    }
    if (!mt.writable) {
        // memory chip not writable:
        return IOVM1_ERROR_MEMORY_CHIP_NOT_WRITABLE;
    }
    if (inst->a + l > mt.size) {
        // memory chip address out of range:
        return IOVM1_ERROR_MEMORY_CHIP_ADDRESS_OUT_OF_RANGE;
    }

    return IOVM1_SUCCESS;
}

extern "C" uint8_t host_memory_read_auto_advance(struct iovm1_t *vm) {
    auto inst = reinterpret_cast<vm_inst *>(iovm1_get_userdata(vm));

    auto mt = rex_memory_chip(inst->c);
    return mt.p[inst->a++];
}

extern "C" uint8_t host_memory_read_no_advance(struct iovm1_t *vm) {
    auto inst = reinterpret_cast<vm_inst *>(iovm1_get_userdata(vm));

    auto mt = rex_memory_chip(inst->c);
    return mt.p[inst->a];
}

extern "C" void host_memory_write_auto_advance(struct iovm1_t *vm, uint8_t b) {
    auto inst = reinterpret_cast<vm_inst *>(iovm1_get_userdata(vm));

    auto mt = rex_memory_chip(inst->c);
    mt.p[inst->a++] = b;
}

iovm1_error vm_inst::vm_init() {
    std::lock_guard lk(vm_mtx);

    iovm1_init(&vm);
    iovm1_set_userdata(&vm, (void *) this);

    return IOVM1_SUCCESS;
}

iovm1_error vm_inst::vm_load(const uint8_t *vmprog, uint32_t vmprog_len) {
    std::lock_guard lk(vm_mtx);

    // make a copy of the program data:
    prog.clear();
    prog.resize(vmprog_len);
    std::copy(vmprog, vmprog + vmprog_len, prog.begin());

    return iovm1_load(&vm, prog.data(), prog.size());
}

iovm1_state vm_inst::vm_getstate() {
    std::lock_guard lk(vm_mtx);

    return iovm1_get_exec_state(&vm);
}

iovm1_error vm_inst::vm_reset() {
    std::lock_guard lk(vm_mtx);

    return iovm1_exec_reset(&vm);
}
