
#ifndef SNES9X_REX_CLIENT_H
#define SNES9X_REX_CLIENT_H

#include "sock.h"

class rex_client : public vm_notifier {
    sock_sp s;

public:
    struct ppux ppux;
    vm_inst vmi;

public:
    explicit rex_client(sock_sp s);

    void on_pc(uint32_t pc);

    bool handle_net();

public: // vm_notifier
    void vm_ended() override;
    void vm_read_complete(vm_read_result &&result) override;
};

using rex_client_sp = std::shared_ptr<rex_client>;

#endif //SNES9X_REX_CLIENT_H
