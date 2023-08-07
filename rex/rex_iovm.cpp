
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

struct mem_target_desc {
    uint8_t *p;
    size_t size;
    bool readable;
    bool writable;
};

static mem_target_desc memory_target(iovm1_target target) {
    switch (target) {
        case 0: // WRAM:
            return {Memory.RAM, sizeof(Memory.RAM), true, false};
        case 1: // SRAM:
            return {Memory.SRAM, Memory.SRAMStorage.size(), true, true};
        case 2: // ROM:
            return {Memory.ROM, Memory.ROMStorage.size(), true, true};
#ifdef EMULATE_FXPAKPRO
        case 3: // 2C00:
            return {Memory.Extra2C00, sizeof(Memory.Extra2C00), true, true};
#endif
        case 4: // VRAM:
            return {Memory.VRAM, sizeof(Memory.VRAM), true, false};
        case 5: // CGRAM:
            return {(uint8_t *) PPU.CGDATA, sizeof(PPU.CGDATA), true, false};
        case 6: // OAM:
            return {PPU.OAMData, sizeof(PPU.OAMData), true, false};
        default: // memory target not defined:
            return {nullptr, 0, false, false};
    }
}

static const int bytes_per_cycle = 4;

extern "C" void iovm1_opcode_cb(struct iovm1_t *vm, struct iovm1_callback_state_t *cbs) {
    auto inst = reinterpret_cast<vm_inst *>(iovm1_get_userdata(vm));
    inst->opcode_cb(cbs);
}

void vm_inst::opcode_cb(struct iovm1_callback_state_t *cbs) {
    auto mt = memory_target(cbs->t);
    auto mem = mt.p;
    auto mem_len = mt.size;
    auto readable = mt.readable;
    auto writable = mt.writable;

    if (cbs->o == IOVM1_OPCODE_READ) {
        // initialize transfer:
        if (cbs->initial) {
            // validate:
            if (!mem) {
                // memory target not defined:
                cbs->complete = true;
                cbs->result = IOVM1_ERROR_MEMORY_TARGET_UNDEFINED;
                return;
            }
            if (!readable) {
                // memory target not readable:
                cbs->complete = true;
                cbs->result = IOVM1_ERROR_MEMORY_TARGET_NOT_READABLE;
                return;
            }
            if (cbs->a + cbs->len > mem_len) {
                // memory target address out of range:
                cbs->complete = true;
                cbs->result = IOVM1_ERROR_MEMORY_TARGET_ADDRESS_OUT_OF_RANGE;
                return;
            }

            // notify host the read operation started:
            notifier->vm_notify_read_start(cbs->p, cbs->tdu, cbs->a, cbs->len);
            addr_init = cbs->a;
            len_init = cbs->len;
            if (cbs->d) {
                // reverse direction:
                cbs->a += cbs->len - 1;
            }
        }

        for (int i = 0; (cbs->len > 0) && (i < bytes_per_cycle); i++) {
            // read a byte:
            uint8_t x = *(mem + cbs->a);
            notifier->vm_notify_read_byte(x);
            if (cbs->d) {
                cbs->a--;
            } else {
                cbs->a++;
            }
            cbs->len--;
        }

        // finished with transfer?
        if (cbs->len == 0) {
            // push out the current read buffer:
            cbs->complete = true;
            notifier->vm_notify_read_end();
        }

        return;
    }

    if (cbs->o == IOVM1_OPCODE_WRITE) {
        if (cbs->initial) {
            // validate:
            if (!mem) {
                // memory target not defined:
                cbs->complete = true;
                cbs->result = IOVM1_ERROR_MEMORY_TARGET_UNDEFINED;
                return;
            }
            if (!writable) {
                // memory target not writable:
                cbs->complete = true;
                cbs->result = IOVM1_ERROR_MEMORY_TARGET_NOT_WRITABLE;
                return;
            }
            if (cbs->a + cbs->len > mem_len) {
                // memory target address out of range:
                cbs->complete = true;
                cbs->result = IOVM1_ERROR_MEMORY_TARGET_ADDRESS_OUT_OF_RANGE;
                return;
            }

            notifier->vm_notify_write_start(cbs->p, cbs->tdu, cbs->a, cbs->len);
            addr_init = cbs->a;
            p_init = cbs->p;
            len_init = cbs->len;
            if (cbs->d) {
                // reverse direction:
                cbs->a += cbs->len - 1;
                cbs->p += cbs->len - 1;
            }
        }

        for (int i = 0; (cbs->len > 0) && (i < bytes_per_cycle); i++) {
            // write a byte:
            uint8_t x = cbs->m[cbs->p];
            *(mem + cbs->a) = x;
#ifdef NOTIFY_WRITE_BYTE
            notifier->vm_notify_write_byte(x);
#endif

            if (cbs->d) {
                cbs->a--;
                cbs->p--;
            } else {
                cbs->a++;
                cbs->p++;
            }
            cbs->len--;
        }

        // finished with transfer?
        if (cbs->len == 0) {
            cbs->a = addr_init + len_init;
            cbs->p = p_init + len_init;
            cbs->complete = true;
            notifier->vm_notify_write_end();
        }

        return;
    }

    // remaining opcodes are wait-while:

    if (cbs->initial) {
        // validate:
        if (!mem) {
            // memory target not defined:
            cbs->complete = true;
            cbs->result = IOVM1_ERROR_MEMORY_TARGET_UNDEFINED;
            return;
        }
        if (!readable) {
            // memory target not readable:
            cbs->complete = true;
            cbs->result = IOVM1_ERROR_MEMORY_TARGET_NOT_READABLE;
            return;
        }
        if (cbs->a >= mem_len) {
            // out of range:
            cbs->complete = true;
            cbs->result = IOVM1_ERROR_MEMORY_TARGET_ADDRESS_OUT_OF_RANGE;
            return;
        }

        // default the timeout to 1 frame if not set:
        if (cbs->tim == 0) {
            // 1364 master cycles * 262 scanlines = 357368 cycles / frame
            cbs->tim = 357368;
        }
        // offset the timeout with the current cycle count:
        cbs->tim += cycles;
    }

    // check timeout:
    if (cycles >= cbs->tim) {
        cbs->complete = true;
        cbs->result = IOVM1_ERROR_TIMED_OUT;
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

    if (cbs->complete) {
        notifier->vm_notify_wait_complete(cbs->p, cbs->o, cbs->tdu, cbs->a, b);
    }
}

iovm1_error vm_inst::vm_init() {
    std::lock_guard lk(vm_mtx);

    iovm1_init(&vm);
    iovm1_set_userdata(&vm, (void *) this);

    return IOVM1_SUCCESS;
}

iovm1_error vm_inst::vm_load(const uint8_t *vmprog, uint32_t vmprog_len) {
    std::lock_guard lk(vm_mtx);

    return iovm1_load(&vm, vmprog, vmprog_len);
}

iovm1_state vm_inst::vm_getstate() {
    std::lock_guard lk(vm_mtx);

    return iovm1_get_exec_state(&vm);
}

iovm1_error vm_inst::vm_reset() {
    std::lock_guard lk(vm_mtx);

    return iovm1_exec_reset(&vm);
}

void vm_inst::on_pc(uint32_t pc) {
    // this method is called before every instruction:

    // execute opcodes in the iovm:
    std::lock_guard lk(vm_mtx);

    // capture master cycle count from snes9x global:
    cycles = (uint32_t) CPU.Cycles;

    auto last_state = iovm1_get_exec_state(&vm);
    auto result = iovm1_exec(&vm);
    auto curr_state = iovm1_get_exec_state(&vm);

    if ((curr_state != last_state) && (curr_state >= IOVM1_STATE_ENDED)) {
        notifier->vm_notify_ended(vm.m.off, vm.cbs.o, result, curr_state);
    }
}
