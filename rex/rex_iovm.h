
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
#include <mutex>

#include "rex_proto.h"

#define IOVM1_USE_USERDATA

#include "iovm.h"

class vm_notifier {
public:
    virtual void vm_notify_ended(uint32_t pc, iovm1_opcode o, iovm1_error result, iovm1_state state) = 0;

    virtual void vm_notify_read_start(uint32_t pc, uint8_t tdu, uint32_t addr, uint32_t len) = 0;

    virtual void vm_notify_read_byte(uint8_t x) = 0;

    virtual void vm_notify_read_end() = 0;

    virtual void vm_notify_write_start(uint32_t pc, uint8_t tdu, uint32_t addr, uint32_t len) = 0;

#ifdef NOTIFY_WRITE_BYTE
    virtual void vm_notify_write_byte(uint8_t x) = 0;
#endif

    virtual void vm_notify_write_end() = 0;

    virtual void vm_notify_wait_complete(uint32_t pc, iovm1_opcode o, uint8_t tdu, uint32_t addr, uint8_t x) = 0;
};

class vm_inst {
    vm_notifier *notifier;

    uint32_t addr_init{};

    uint32_t p_init{};
    uint32_t len_init{};

    std::recursive_mutex vm_mtx;
    struct iovm1_t vm{};

    std::vector<uint8_t> prog{};

    uint64_t cycles{};

    uint64_t timeout_cycles{};

    int64_t delay_cycles{};

public:
    explicit vm_inst(vm_notifier *notifier);

    iovm1_error vm_init();

    iovm1_error vm_load(const uint8_t *vmprog, uint32_t vmprog_len);

    iovm1_state vm_getstate();

    iovm1_error vm_reset();

    void inc_cycles(int32_t delta);

    void on_pc(uint32_t pc);

private:
    friend void iovm1_opcode_cb(struct iovm1_t *vm, struct iovm1_callback_state_t *cbs);

    void opcode_cb(struct iovm1_callback_state_t *cbs);
};

#endif //SNES9X_REX_IOVM_H
