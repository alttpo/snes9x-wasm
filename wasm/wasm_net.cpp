
#include <vector>
#include "wasm_net.h"

#ifdef __WIN32__
#  include <winsock.h>
#  include <process.h>

#  define ioctl ioctlsocket
#  define close(h) if(h){closesocket(h);}
#  define read(a,b,c) recv(a, b, c, 0)
#  define write(a,b,c) send(a, b, c, 0)
#else
#  include <unistd.h>
#  include <sys/types.h>

#  include <arpa/inet.h>
#include <sys/fcntl.h>
#include <netinet/tcp.h>
#include <sys/poll.h>

#  ifdef __SVR4
#    include <sys/stropts.h>
#  endif
#endif

net::~net() {
    for (auto &slot: slots) {
        ::close(slot.second);
    }
}

auto net::allocate_slot(int fd) -> int32_t {
    auto slot = free_slot++;
    slots.insert_or_assign(slot, fd);
    return slot;
}

auto net::tcp_listen(uint32_t port) -> int32_t {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        // TODO: translate to wasi error?
        return -errno;
    }

#ifdef __WIN32__
    int flags = 1;
    int res = ioctlsocket(fd, FIONBIO, &flags);
    if (res != NO_ERROR) {
        return -res;
    }
#else
    int flags = fcntl(fd, F_GETFL);
    int res = fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    if (res < 0) {
        // TODO: translate to wasi error?
        return -errno;
    }
#endif

    // set TCP_NODELAY:
    flags = 1;
    if (setsockopt(fd, SOL_SOCKET, TCP_NODELAY, (void *)&flags, sizeof(flags)) < 0) {
        // TODO: translate to wasi error?
        return -errno;
    }

    struct sockaddr_in address{};
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(port);

    if (::bind(fd, (const sockaddr *) &address, sizeof(address)) < 0) {
        fprintf(stderr, "net_tcp_listen: unable to bind socket; error %d\n", errno);
        // TODO: translate to wasi error?
        return -errno;
    }

    if (::listen(fd, 1) < 0) {
        fprintf(stderr, "net_tcp_listen: unable to listen on socket; error %d\n", errno);
        // TODO: translate to wasi error?
        return -errno;
    }

    return allocate_slot(fd);
}

auto net::tcp_accept(int32_t slot) -> int32_t {
    auto it = slots.find(slot);
    if (it == slots.end()) {
        return -EBADF;
    }

    auto fd = it->second;
    auto accepted = ::accept(fd, nullptr, nullptr);
    if (accepted < 0) {
        return -errno;
    }

    return allocate_slot(accepted);
}

auto net::poll(net_poll_slot *poll_slots, uint32_t poll_slots_len) -> int32_t {
    // translate slots to real fds:
    std::vector<pollfd> pollfds(poll_slots_len);
    for (int i = 0; i < poll_slots_len; i++) {
        auto ps = poll_slots[i];
        auto it = slots.find(ps.slot);
        if (it == slots.end()) {
            return -EBADF;
        }
        pollfds.push_back({it->second, (short)ps.events, 0});
    }

    auto n = ::poll(pollfds.data(), pollfds.size(), 0);
    if (n < 0) {
        return -errno;
    }

    for (int i = 0; i < pollfds.size(); i++) {
        poll_slots[i].revents = pollfds[i].revents;
    }

    return n;
}

auto net::send(int32_t slot, uint8_t *data, uint32_t len) -> int32_t {
    auto it = slots.find(slot);
    if (it == slots.end()) {
        return -EBADF;
    }

    auto fd = it->second;
    auto n = ::send(fd, data, len, 0);
    if (n < 0) {
        return -errno;
    }

    return (int32_t)n;
}

auto net::recv(int32_t slot, uint8_t *data, uint32_t len) -> int32_t {
    auto it = slots.find(slot);
    if (it == slots.end()) {
        return -EBADF;
    }

    auto fd = it->second;
    auto n = ::recv(fd, data, len, 0);
    if (n < 0) {
        return -errno;
    }

    return (int32_t)n;
}

auto net::close(int32_t slot) -> int32_t {
    auto it = slots.find(slot);
    if (it == slots.end()) {
        return -EBADF;
    }

    auto fd = it->second;
    slots.erase(it);
    if (::close(fd) < 0) {
        return -errno;
    }

    return 0;
}
