
#ifndef SNES9X_REX_IOVM_H
#define SNES9X_REX_IOVM_H

#include <cstdint>
#include <memory>
#include <array>
#include <utility>
#include <thread>
#include <vector>
#include <queue>
#include <string>

#define IOVM1_USE_USERDATA
#include "iovm.h"

struct vm_read_result {
    uint32_t len;
    uint32_t a;
    uint8_t t;
    std::vector<uint8_t> buf;

    vm_read_result();

    vm_read_result(
        const std::vector<uint8_t> &buf,
        uint32_t len,
        uint32_t a,
        uint8_t t
    );
};

class vm_notifier {
public:
    virtual void vm_read_complete(vm_read_result &&result) = 0;

    virtual void vm_ended() = 0;
};

class vm_inst {
    vm_notifier *notifier;

    uint32_t addr_init{};

    uint32_t p_init{};
    uint32_t len_init{};

    vm_read_result read_result;

    std::mutex vm_mtx;
    struct iovm1_t vm{};

public:
    explicit vm_inst(vm_notifier *notifier);

    int32_t vm_init();

    int32_t vm_load(const uint8_t *vmprog, uint32_t vmprog_len);

    iovm1_state vm_getstate();

    int32_t vm_reset();

    void on_pc(uint32_t pc);

private:
    friend void iovm1_opcode_cb(struct iovm1_t *vm, struct iovm1_callback_state_t *cbs);

    void opcode_cb(struct iovm1_callback_state_t *cbs);
};

#endif //SNES9X_REX_IOVM_H
