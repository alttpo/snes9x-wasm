
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

////////////////////////////////////////////////////////////////////////

vm_read_result::vm_read_result() : len(0), a(0), t(0), buf() {}

vm_read_result::vm_read_result(
    const std::vector<uint8_t> &buf_p,
    uint16_t len_p,
    uint32_t a_p,
    uint8_t t_p
) : len(len_p), a(a_p), t(t_p), buf(buf_p) {}

vm_inst::vm_inst() : m(nullptr), n(-1) {}

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
                //TODO: m->notify_event(ev_iovm0_read_complete + inst->n);
                return;
            }

            // reserve enough space:
            inst->read_result = vm_read_result(
                std::vector<uint8_t>(),
                cbs->len,
                cbs->a,
                cbs->t
            );
            inst->read_result.buf.resize(cbs->len);
            inst->addr_init = cbs->a;
            inst->len_init = cbs->len;
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
            inst->read_result.buf[cbs->a - inst->addr_init] = x;
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
            inst->read_queue.push(std::move(inst->read_result));
            inst->trim_read_queue();
            cbs->complete = true;
            //TODO: m->notify_event(ev_iovm0_read_complete + inst->n);
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
            inst->addr_init = cbs->a;
            inst->p_init = cbs->p;
            inst->len_init = cbs->len;
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
            cbs->a = inst->addr_init + inst->len_init;
            cbs->p = inst->p_init + inst->len_init;
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

int32_t rex::vm_init(unsigned n) {
    if (n >= vms.size()) {
        return IOVM1_ERROR_OUT_OF_RANGE;
    }

    std::unique_lock<std::mutex> lk(vms[n].vm_mtx);

    iovm1_init(&vms[n].vm);
    iovm1_set_userdata(&vms[n].vm, (void *) &vms[n]);

    return IOVM1_SUCCESS;
}

int32_t rex::vm_load(unsigned n, const uint8_t *vmprog, uint32_t vmprog_len) {
    if (n >= vms.size()) {
        return IOVM1_ERROR_OUT_OF_RANGE;
    }

    std::unique_lock<std::mutex> lk(vms[n].vm_mtx);

    return iovm1_load(&vms[n].vm, vmprog, vmprog_len);
}

iovm1_state rex::vm_getstate(unsigned n) {
    if (n >= vms.size()) {
        return static_cast<iovm1_state>(-1);
    }

    std::unique_lock<std::mutex> lk(vms[n].vm_mtx);

    return iovm1_get_exec_state(&vms[n].vm);
}

int32_t rex::vm_reset(unsigned n) {
    if (n >= vms.size()) {
        return IOVM1_ERROR_OUT_OF_RANGE;
    }

    std::unique_lock<std::mutex> lk(vms[n].vm_mtx);

    return iovm1_exec_reset(&vms[n].vm);
}

int32_t rex::vm_read_data(unsigned n, uint8_t *dst, uint32_t dst_len, uint32_t *o_read, uint32_t *o_addr, uint8_t *o_target) {
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

void rex::on_pc(uint32_t pc) {
    // this method is called before every instruction:

    for (unsigned n = 0; n < 2; n++) {
        // execute opcodes in the iovm until a blocking operation (read, write, while) occurs:
        std::unique_lock<std::mutex> lk(vms[n].vm_mtx);
        auto last_state = iovm1_get_exec_state(&vms[n].vm);
        if (IOVM1_SUCCESS == iovm1_exec(&vms[n].vm)) {
            auto curr_state = iovm1_get_exec_state(&vms[n].vm);
            if ((curr_state != last_state) && (curr_state == IOVM1_STATE_ENDED)) {
                // fire ev_iovm_end event:
                //TODO: notify_event(ev_iovm0_end + n);
            }
        }
    }
}
