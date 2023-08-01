
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

#include "rex_proto.h"

#define IOVM1_USE_USERDATA
#include "iovm.h"

class vm_notifier {
public:
    virtual void vm_notify_read_fail(uint8_t tdu, uint32_t addr, uint32_t len) = 0;

    virtual void vm_notify_read_start(uint8_t tdu, uint32_t addr, uint32_t len) = 0;

    virtual void vm_notify_read_byte(uint8_t x) = 0;

    virtual void vm_notify_read_end() = 0;

    virtual void vm_notify_wait_complete(iovm1_opcode o, uint8_t tdu, uint32_t addr, uint8_t x) = 0;

    virtual void vm_notify_ended() = 0;
};

class vm_inst {
    vm_notifier *notifier;

    uint32_t addr_init{};

    uint32_t p_init{};
    uint32_t len_init{};

    std::mutex vm_mtx;
    struct iovm1_t vm{};

public:
    explicit vm_inst(vm_notifier *notifier);

    rex_cmd_result vm_init();

    rex_cmd_result vm_load(const uint8_t *vmprog, uint32_t vmprog_len);

    iovm1_state vm_getstate();

    rex_cmd_result vm_reset();

    void on_pc(uint32_t pc);

private:
    friend void iovm1_opcode_cb(struct iovm1_t *vm, struct iovm1_callback_state_t *cbs);

    void opcode_cb(struct iovm1_callback_state_t *cbs);
};

#endif //SNES9X_REX_IOVM_H
