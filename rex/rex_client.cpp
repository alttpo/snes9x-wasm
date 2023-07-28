
#include <utility>

#include "rex.h"

rex_client::rex_client(sock_sp s_p) :
    s(std::move(s_p)),
    vmi(static_cast<vm_notifier *>(this))
{
}

void rex_client::on_pc(uint32_t pc) {
    vmi.on_pc(pc);
}

bool rex_client::handle_net() {
    // close errored-out clients and remove them:
    if (s->isErrored() || s->isClosed()) {
        fprintf(stderr, "client closed\n");
        return false;
    }
    if (!s->isReadAvailable()) {
        return true;
    }

    // read available data:
    ssize_t n;
    uint8_t data[8192];
    s->recv(data, sizeof(data), n);

    // TODO
    fprintf(stderr, "received data from client\n");

    return true;
}

void rex_client::vm_ended() {}
void rex_client::vm_read_complete(vm_read_result &&result) {}