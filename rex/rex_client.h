
#ifndef SNES9X_REX_CLIENT_H
#define SNES9X_REX_CLIENT_H

#include "rex_iovm.h"
#include "rex_ppux.h"

#include "sock.h"

extern "C" {
#include "incoming.h"
#include "outgoing.h"
}

using v8 = std::vector<uint8_t>;

class rex_client : public vm_notifier {
    sock_sp s;

    struct frame64rd fi{};
    struct frame64wr fo[2]{};

    // received messages per channel:
    v8    msgIn[2]{};

    void send_frame(uint8_t c, bool fin);

public:
    struct ppux ppux{};

    vm_inst vmi;
    bool vm_running{};

public:
    explicit rex_client(sock_sp s);
    virtual ~rex_client() = default;

    void inc_cycles(int32_t delta);
    void on_pc(uint32_t pc);

    bool handle_net();

public: // vm_notifier
    void vm_notify_ended(uint32_t pc, iovm1_error result) override;

    void vm_notify_read(uint32_t pc, uint8_t c, uint24_t a, uint8_t l, uint8_t *d) override;

public:
    bool recv_frame(uint8_t buf[63], uint8_t len, uint8_t chn, uint8_t fin);

    void send_message(uint8_t c, const v8 &msg);
    void recv_message(uint8_t c, const v8 &msg);
};

using rex_client_sp = std::shared_ptr<rex_client>;

#endif //SNES9X_REX_CLIENT_H
