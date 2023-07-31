
#ifndef SNES9X_REX_CLIENT_H
#define SNES9X_REX_CLIENT_H

#include "sock.h"

class rex_client : public vm_notifier {
    sock_sp s;

    uint8_t rbuf[32]{};
    uint8_t rh{};   // head
    uint8_t rt{};   // tail
    bool    rf{};   // read frame header?
    uint8_t rx{};   // frame header byte
    uint8_t rl{};   // frame length

    uint8_t sbuf[32]{};

public:
    struct ppux ppux{};
    vm_inst vmi;

public:
    explicit rex_client(sock_sp s);
    virtual ~rex_client() = default;

    void on_pc(uint32_t pc);

    bool handle_net();

public: // vm_notifier
    void vm_ended() override;
    void vm_read_complete(vm_read_result &&result) override;

    void recv_frame(uint8_t c, uint8_t m, uint8_t l, uint8_t buf[32]);
    bool send_frame(uint8_t c, uint8_t m, uint8_t l);
};

using rex_client_sp = std::shared_ptr<rex_client>;

#endif //SNES9X_REX_CLIENT_H
