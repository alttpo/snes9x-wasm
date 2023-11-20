
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

    friend void host_send_abort(struct iovm1_t *vm);
    friend void host_send_read(struct iovm1_t *vm, uint8_t l, uint8_t *d);
    friend void host_send_end(struct iovm1_t *vm);

    friend void host_timer_reset(struct iovm1_t *vm);
    friend bool host_timer_elapsed(struct iovm1_t *vm);

    friend enum iovm1_error host_memory_init(struct iovm1_t *vm, iovm1_memory_chip_t c, uint24_t a);
    friend enum iovm1_error host_memory_read_validate(struct iovm1_t *vm, int l);
    friend enum iovm1_error host_memory_write_validate(struct iovm1_t *vm, int l);

    friend uint8_t host_memory_read_auto_advance(struct iovm1_t *vm);
    friend uint8_t host_memory_read_no_advance(struct iovm1_t *vm);
    friend void host_memory_write_auto_advance(struct iovm1_t *vm, uint8_t b);
};

#endif //SNES9X_REX_IOVM_H
