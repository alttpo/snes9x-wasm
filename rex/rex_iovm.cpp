
#include <cstdint>
#include <algorithm>
#include <utility>

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
            return {(uint8_t *) PPU.CGDATA, sizeof(PPU.CGDATA)};
        case 6: // OAM:
            return {PPU.OAMData, sizeof(PPU.OAMData)};
        default: // memory target not defined:
            return {nullptr, 0};
    }
}

static const int bytes_per_cycle = 4;

extern "C" void iovm1_opcode_cb(struct iovm1_t *vm, struct iovm1_callback_state_t *cbs) {
    auto inst = reinterpret_cast<vm_inst *>(iovm1_get_userdata(vm));
    inst->opcode_cb(cbs);
}

void vm_inst::opcode_cb(struct iovm1_callback_state_t *cbs) {
    auto mt = memory_target(cbs->t);
    auto mem = mt.first;
    auto mem_len = mt.second;

    if (cbs->o == IOVM1_OPCODE_READ) {
        // initialize transfer:
        if (cbs->initial) {
            if (!mem) {
                // memory target not defined:
                cbs->complete = true;
                notifier->vm_notify_read_fail(cbs->tdu, cbs->a, cbs->len);
                return;
            }

            // reserve enough space:
            notifier->vm_notify_read_start(cbs->tdu, cbs->a, cbs->len);
            addr_init = cbs->a;
            len_init = cbs->len;
            if (cbs->d) {
                // reverse direction:
                cbs->a += cbs->len - 1;
            }
        }

        for (int i = 0; (cbs->len > 0) && (i < bytes_per_cycle); i++) {
            // read a byte:
            uint8_t x;
            if (cbs->a < mem_len) {
                x = *(mem + cbs->a);
            } else {
                // out of bounds access yields a 0 byte:
                x = 0;
            }
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
        // write one byte per cycle:
        if (!mem) {
            cbs->complete = true;
            return;
        }
        if (cbs->initial) {
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
            if (cbs->a < mem_len) {
                // write a byte:
                *(mem + cbs->a) = cbs->m[cbs->p];
            }

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

    if (cbs->complete) {
        notifier->vm_notify_wait_complete(cbs->o, cbs->tdu, cbs->a, b);
    }
}

rex_cmd_result vm_inst::vm_init() {
    std::unique_lock<std::mutex> lk(vm_mtx);

    iovm1_init(&vm);
    iovm1_set_userdata(&vm, (void *) this);

    return res_success;
}

rex_cmd_result vm_inst::vm_load(const uint8_t *vmprog, uint32_t vmprog_len) {
    std::unique_lock<std::mutex> lk(vm_mtx);

    auto res = iovm1_load(&vm, vmprog, vmprog_len);
    switch (res) {
        case IOVM1_SUCCESS:
            return res_success;
        case IOVM1_ERROR_VM_INVALID_OPERATION_FOR_STATE:
            return res_cmd_precondition_failed;
        case IOVM1_ERROR_OUT_OF_RANGE:
        default:
            return res_cmd_error;
    }
}

iovm1_state vm_inst::vm_getstate() {
    std::unique_lock<std::mutex> lk(vm_mtx);

    return iovm1_get_exec_state(&vm);
}

rex_cmd_result vm_inst::vm_reset() {
    std::unique_lock<std::mutex> lk(vm_mtx);

    auto res = iovm1_exec_reset(&vm);
    switch (res) {
        case IOVM1_SUCCESS:
            return res_success;
        case IOVM1_ERROR_VM_INVALID_OPERATION_FOR_STATE:
            return res_cmd_precondition_failed;
        default:
            return res_cmd_error;
    }
}

void vm_inst::on_pc(uint32_t pc) {
    // this method is called before every instruction:

    // execute opcodes in the iovm until a blocking operation (read, write, while) occurs:
    std::unique_lock<std::mutex> lk(vm_mtx);
    auto last_state = iovm1_get_exec_state(&vm);
    if (IOVM1_SUCCESS == iovm1_exec(&vm)) {
        auto curr_state = iovm1_get_exec_state(&vm);
        if ((curr_state != last_state) && (curr_state == IOVM1_STATE_ENDED)) {
            notifier->vm_notify_ended();
        }
    }
}
