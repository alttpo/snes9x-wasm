
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

// advance memory-read state machine, use `vm->rd` for tracking state
extern "C" enum iovm1_error host_memory_read_state_machine(struct iovm1_t *vm) {
    auto inst = reinterpret_cast<vm_inst *>(iovm1_get_userdata(vm));

    if (vm->rd.os == IOVM1_OPSTATE_INIT) {
        inst->mc = rex_memory_chip(vm->rd.c);
        if (!inst->mc.p) {
            return IOVM1_ERROR_MEMORY_CHIP_UNDEFINED;
        }
        if (!inst->mc.readable) {
            return IOVM1_ERROR_MEMORY_CHIP_NOT_READABLE;
        }
        if (vm->rd.a >= inst->mc.size) {
            return IOVM1_ERROR_MEMORY_CHIP_ADDRESS_OUT_OF_RANGE;
        }
        if (vm->rd.a + vm->rd.l > inst->mc.size) {
            return IOVM1_ERROR_MEMORY_CHIP_ADDRESS_OUT_OF_RANGE;
        }

        inst->c = vm->rd.c;
        inst->a = vm->rd.a;
        inst->a_init = vm->rd.a;
        inst->r = inst->rdbuf;
        vm->rd.os = IOVM1_OPSTATE_CONTINUE;
    }

    vm->rd.l--;
    *inst->r++ = inst->mc.p[inst->a++];
    if (vm->rd.l <= 0) {
        vm->rd.os = IOVM1_OPSTATE_COMPLETED;
        inst->notifier->vm_notify_read(vm->p, inst->c, inst->a_init, vm->rd.l_raw, inst->rdbuf);
    }

    return IOVM1_SUCCESS;
}

// advance memory-write state machine, use `vm->wr` for tracking state
extern "C" enum iovm1_error host_memory_write_state_machine(struct iovm1_t *vm) {
    auto inst = reinterpret_cast<vm_inst *>(iovm1_get_userdata(vm));

    if (vm->wr.os == IOVM1_OPSTATE_INIT) {
        inst->mc = rex_memory_chip(vm->wr.c);
        if (!inst->mc.p) {
            return IOVM1_ERROR_MEMORY_CHIP_UNDEFINED;
        }
        if (!inst->mc.writable) {
            return IOVM1_ERROR_MEMORY_CHIP_NOT_WRITABLE;
        }
        if (vm->wr.a >= inst->mc.size) {
            return IOVM1_ERROR_MEMORY_CHIP_ADDRESS_OUT_OF_RANGE;
        }
        if (vm->wr.a + vm->wr.l > inst->mc.size) {
            return IOVM1_ERROR_MEMORY_CHIP_ADDRESS_OUT_OF_RANGE;
        }

        inst->c = vm->wr.c;
        inst->a = vm->wr.a;
        inst->a_init = vm->wr.a;
        inst->w = vm->m.ptr + vm->m.off;
        vm->wr.os = IOVM1_OPSTATE_CONTINUE;
    }

    vm->wr.l--;
    inst->mc.p[inst->a++] = *inst->w++;

    if (vm->wr.l <= 0) {
        vm->wr.os = IOVM1_OPSTATE_COMPLETED;
    }

    return IOVM1_SUCCESS;
}

// advance memory-wait state machine, use `vm->wa` for tracking state, use `iovm1_memory_wait_test_byte` for comparison func
extern "C" enum iovm1_error host_memory_wait_state_machine(struct iovm1_t *vm) {
    auto inst = reinterpret_cast<vm_inst *>(iovm1_get_userdata(vm));

    // init:
    if (vm->wa.os == IOVM1_OPSTATE_INIT) {
        // 1364 master cycles * 262 scanlines = 357368 cycles / frame
        inst->timeout_cycles = 357368;
        vm->wa.os = IOVM1_OPSTATE_CONTINUE;
    }

    // check timer:
    if (inst->timeout_cycles <= 0) {
        return IOVM1_ERROR_TIMED_OUT;
    }

    // read the byte:
    uint8_t b;
    enum iovm1_error err = host_memory_try_read_byte(vm, vm->wa.c, vm->wa.a, &b);
    if (err != IOVM1_SUCCESS) {
        return err;
    }

    // test the byte:
    if (iovm1_memory_wait_test_byte(vm, b)) {
        vm->wa.os = IOVM1_OPSTATE_COMPLETED;
    }

    return IOVM1_SUCCESS;
}

// try to read a byte from a memory chip, return byte in `*b` if successful
extern "C" enum iovm1_error host_memory_try_read_byte(struct iovm1_t *vm, enum iovm1_memory_chip c, uint24_t a, uint8_t *b) {
    auto mc = rex_memory_chip(vm->rd.c);
    if (!mc.p) {
        return IOVM1_ERROR_MEMORY_CHIP_UNDEFINED;
    }
    if (!mc.readable) {
        return IOVM1_ERROR_MEMORY_CHIP_NOT_READABLE;
    }
    if (a >= mc.size) {
        return IOVM1_ERROR_MEMORY_CHIP_ADDRESS_OUT_OF_RANGE;
    }

    // read the byte:
    *b = mc.p[a];

    return IOVM1_SUCCESS;
}

extern "C" void host_send_end(struct iovm1_t *vm) {
    auto inst = reinterpret_cast<vm_inst *>(iovm1_get_userdata(vm));

    inst->notifier->vm_notify_ended(vm->p, vm->e);
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
