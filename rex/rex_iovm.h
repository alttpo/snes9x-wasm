
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
#include "rex.h"

#define IOVM1_USE_USERDATA

#include "iovm.h"

class vm_notifier {
public:
    virtual void vm_notify_ended(uint32_t pc, iovm1_error result) = 0;

    virtual void vm_notify_read(uint32_t pc, uint8_t c, uint24_t a, uint8_t l, uint8_t *d) = 0;
};

class vm_inst {
    vm_notifier *notifier;

    std::recursive_mutex vm_mtx;
    struct iovm1_t vm{};

    std::vector<uint8_t> prog{};

    // memory controller:
    uint8_t  c{};
    uint24_t a{};
    uint24_t a_init{};
    rex_memory_chip_desc mc{};
    uint8_t *r{};
    uint8_t rdbuf[6 + 256];
    const uint8_t *w{};

    uint64_t timeout_cycles{};

public:
    explicit vm_inst(vm_notifier *notifier);

    iovm1_error vm_init();

    iovm1_error vm_load(const uint8_t *vmprog, uint32_t vmprog_len);

    iovm1_state vm_getstate();

    iovm1_error vm_reset();

    void inc_cycles(int32_t delta);

    void on_pc(uint32_t pc);

private:

    // advance memory-read state machine, use `vm->rd` for tracking state
    friend enum iovm1_error host_memory_read_state_machine(struct iovm1_t *vm);
    // advance memory-write state machine, use `vm->wr` for tracking state
    friend enum iovm1_error host_memory_write_state_machine(struct iovm1_t *vm);
    // advance memory-wait state machine, use `vm->wa` for tracking state, use `iovm1_memory_wait_test_byte` for comparison func
    friend enum iovm1_error host_memory_wait_state_machine(struct iovm1_t *vm);

    // try to read a byte from a memory chip, return byte in `*b` if successful
    friend enum iovm1_error host_memory_try_read_byte(struct iovm1_t *vm, enum iovm1_memory_chip c, uint24_t a, uint8_t *b);

    friend void host_send_end(struct iovm1_t *vm);
};

#endif //SNES9X_REX_IOVM_H
