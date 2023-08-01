
#ifndef SNES9X_REX_CLIENT_H
#define SNES9X_REX_CLIENT_H

#include "sock.h"

using v8 = std::vector<uint8_t>;

class rex_client : public vm_notifier {
    sock_sp s;

    uint8_t rbuf[64]{}; // recv frame data
    uint8_t rh{};   // head
    uint8_t rt{};   // tail
    bool    rf{};   // read frame header?
    uint8_t rx{};   // frame header byte
    uint8_t rl{};   // frame length

    // sx must be immediately prior to sbuf in memory:
    uint8_t sx{};   // send header byte
    uint8_t sbuf[63]{}; // send frame data

    // data read frame:
    uint8_t dri{};

    // received messages per channel:
    v8    msgIn[2]{};

public:
    struct ppux ppux{};
    vm_inst vmi;

public:
    explicit rex_client(sock_sp s);
    virtual ~rex_client() = default;

    void on_pc(uint32_t pc);

    bool handle_net();

public: // vm_notifier
    void vm_notify_read_fail(uint8_t tdu, uint32_t addr, uint32_t len) override;
    void vm_notify_read_start(uint8_t tdu, uint32_t addr, uint32_t len) override;
    void vm_notify_read_byte(uint8_t x) override;
    void vm_notify_read_end() override;
    void vm_notify_wait_complete(iovm1_opcode o, uint8_t tdu, uint32_t addr, uint8_t x) override;
    void vm_notify_ended() override;

public:
    void recv_frame(uint8_t c, uint8_t f, uint8_t l, uint8_t buf[63]);
    bool send_frame(uint8_t c, uint8_t f, uint8_t l);

    void send_message(uint8_t c, const v8 &msg);
    void recv_message(uint8_t c, const v8 &msg);
};

using rex_client_sp = std::shared_ptr<rex_client>;

#endif //SNES9X_REX_CLIENT_H
