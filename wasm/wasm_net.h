
#ifndef SNES9X_WASM_NET_H
#define SNES9X_WASM_NET_H

#include <cstdint>
#include <unordered_map>

typedef struct {
    int32_t     slot;
    uint32_t    events;
    uint32_t    revents;
} net_poll_slot;

struct net {
    ~net();

    auto tcp_listen(uint32_t port) -> int32_t;
    auto tcp_accept(int32_t fd) -> int32_t;
    auto poll(net_poll_slot *fds, uint32_t fds_len) -> int32_t;
    auto send(int32_t fd, uint8_t *data, uint32_t len) -> int32_t;
    auto recv(int32_t fd, uint8_t *data, uint32_t len) -> int32_t;
    auto close(int32_t fd) -> int32_t;

    std::unordered_map<int32_t, int> slots;
    int32_t free_slot = 0;
    int32_t max_slot = -1;

    auto allocate_slot(int fd) -> int32_t;
};

#endif //SNES9X_WASM_NET_H
