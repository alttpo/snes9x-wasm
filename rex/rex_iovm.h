
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

struct rex;

struct vm_read_result {
    uint16_t                len;
    uint32_t                a;
    uint8_t                 t;
    std::vector<uint8_t>    buf;

    vm_read_result();
    vm_read_result(
        const std::vector<uint8_t> &buf,
        uint16_t                    len,
        uint32_t                    a,
        uint8_t                     t
    );
};

struct vm_inst {
    struct rex *m;
    unsigned n;

    std::mutex vm_mtx;
    struct iovm1_t vm{};

    vm_read_result read_result;
    std::queue<vm_read_result> read_queue{};

    uint32_t addr_init;
    uint32_t p_init;
    uint32_t len_init;

    vm_inst();

    void trim_read_queue();
};

#endif //SNES9X_REX_IOVM_H
