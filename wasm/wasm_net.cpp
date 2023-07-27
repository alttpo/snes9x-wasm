
#ifdef __WIN32__
#  include <process.h>
#else
#  include <unistd.h>
#  include <sys/types.h>

#  include <arpa/inet.h>
#  include <sys/fcntl.h>
#  include <netinet/tcp.h>
#  include <sys/poll.h>

#  ifdef __SVR4
#    include <sys/stropts.h>
#  endif
#endif

#include "wasm_net.h"

#include <vector>

net::~net() {
    for (auto &slot: slots) {
#ifdef __WIN32__
        ::closesocket(slot.second);
#else
        ::close(slot.second);
#endif
    }
}

auto net::allocate_slot(native_socket fd) -> int32_t {
    auto slot = free_slot++;
    slots.insert_or_assign(slot, fd);
    return slot;
}

auto net::socket_set_nonblocking(native_socket fd) -> int32_t {
#ifdef __WIN32__
    unsigned long flags = 1;
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

    return 0;
}

auto net::tcp_socket() -> int32_t {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        // TODO: translate to wasi error?
        return -errno;
    }

    int res = socket_set_nonblocking(fd);
    if (res < 0) {
#ifdef __WIN32__
        ::closesocket(fd);
#else
        ::close(fd);
#endif
        return res;
    }

    // set TCP_NODELAY:
    int flags = 1;
#ifdef __WIN32__
    if (setsockopt(fd, SOL_SOCKET, TCP_NODELAY, (const char *)&flags, sizeof(flags)) < 0) {
#else
    if (setsockopt(fd, SOL_SOCKET, TCP_NODELAY, (void *)&flags, sizeof(flags)) < 0) {
#endif
#ifdef __WIN32__
        ::closesocket(fd);
#else
        ::close(fd);
#endif
        // TODO: translate to wasi error?
        return -errno;
    }

    return allocate_slot(fd);
}

auto net::udp_socket() -> int32_t {
    int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        // TODO: translate to wasi error?
        return -errno;
    }

    int res = socket_set_nonblocking(fd);
    if (res < 0) {
#ifdef __WIN32__
        ::closesocket(fd);
#else
        ::close(fd);
#endif
        return res;
    }

    return allocate_slot(fd);
}

auto net::connect(int32_t slot, uint32_t ipv4_addr, uint16_t port) -> int32_t {
    auto it = slots.find(slot);
    if (it == slots.end()) {
        return -EBADF;
    }
    auto fd = it->second;

    struct sockaddr_in address{};
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(ipv4_addr);
    address.sin_port = htons(port);

    if (::connect(fd, (const sockaddr *) &address, sizeof(address)) < 0) {
        // TODO: translate to wasi error?
        return -errno;
    }

    return 0;
}

auto net::bind(int32_t slot, uint32_t ipv4_addr, uint16_t port) -> int32_t {
    auto it = slots.find(slot);
    if (it == slots.end()) {
        return -EBADF;
    }
    auto fd = it->second;

    struct sockaddr_in address{};
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(ipv4_addr);
    address.sin_port = htons(port);

    if (::bind(fd, (const sockaddr *) &address, sizeof(address)) < 0) {
        // TODO: translate to wasi error?
        return -errno;
    }

    return 0;
}

auto net::listen(int32_t slot) -> int32_t {
    auto it = slots.find(slot);
    if (it == slots.end()) {
        return -EBADF;
    }
    auto fd = it->second;

    if (::listen(fd, 1) < 0) {
        // TODO: translate to wasi error?
        return -errno;
    }

    return 0;
}

auto net::accept(int32_t slot, int32_t *o_accepted_slot, uint32_t *o_ipv4_addr, uint16_t *o_port) -> int32_t {
    auto it = slots.find(slot);
    if (it == slots.end()) {
        return -EBADF;
    }
    auto fd = it->second;

    socklen_t address_len = 0;
    struct sockaddr_in address{};
    memset(&address, 0, sizeof(address));

    auto accepted = ::accept(fd, (struct sockaddr *)&address, &address_len);
    if (accepted < 0) {
        return -errno;
    }

    if (o_ipv4_addr) {
        *o_ipv4_addr = ntohl(address.sin_addr.s_addr);
    }
    if (o_port) {
        *o_port = ntohs(address.sin_port);
    }

    *o_accepted_slot = allocate_slot(accepted);
    return 0;
}

auto net::poll(net_poll_slot *poll_slots, uint32_t poll_slots_len) -> int32_t {
#ifdef __WIN32__
    std::vector<WSAPOLLFD> pollfds;
#else
    std::vector<pollfd> pollfds;
#endif
    pollfds.reserve(poll_slots_len);

    // translate slots to real fds:
    for (int i = 0; i < poll_slots_len; i++) {
        auto ps = poll_slots[i];
        auto it = slots.find(ps.slot);
        if (it == slots.end()) {
            return -EBADF;
        }
        pollfds.push_back({it->second, (short)ps.events, (short)ps.revents});
    }

#ifdef __WIN32__
    auto n = ::WSAPoll(pollfds.data(), pollfds.size(), 0);
#else
    auto n = ::poll(pollfds.data(), pollfds.size(), 0);
#endif
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
#ifdef __WIN32__
    auto n = ::send(fd, (const char *)data, len, 0);
#else
    auto n = ::send(fd, data, len, 0);
#endif
    if (n < 0) {
        return -errno;
    }

    return (int32_t)n;
}

auto net::sendto(int32_t slot, uint8_t *data, uint32_t len, uint32_t ipv4_addr, uint16_t port) -> int32_t {
    auto it = slots.find(slot);
    if (it == slots.end()) {
        return -EBADF;
    }

    struct sockaddr_in address{};
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(ipv4_addr);
    address.sin_port = htons(port);

    auto fd = it->second;
#ifdef __WIN32__
    auto n = ::sendto(fd, (const char *)data, len, 0, (struct sockaddr *)&address, sizeof(address));
#else
    auto n = ::sendto(fd, data, len, 0, (struct sockaddr *)&address, sizeof(address));
#endif
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
#ifdef __WIN32__
    auto n = ::recv(fd, (char*)data, len, 0);
#else
    auto n = ::recv(fd, data, len, 0);
#endif
    if (n < 0) {
        return -errno;
    }

    return (int32_t)n;
}

auto net::recvfrom(int32_t slot, uint8_t *data, uint32_t len, uint32_t *o_ipv4_addr, uint16_t *o_port) -> int32_t {
    auto it = slots.find(slot);
    if (it == slots.end()) {
        return -EBADF;
    }

    socklen_t address_len;
    struct sockaddr_in address{};
    memset(&address, 0, sizeof(address));

    auto fd = it->second;
#ifdef __WIN32__
    auto n = ::recv(fd, (char*)data, len, 0);
#else
    auto n = ::recvfrom(fd, data, len, 0, (struct sockaddr *)&address, &address_len);
#endif
    if (n < 0) {
        return -errno;
    }

    *o_ipv4_addr = ntohl(address.sin_addr.s_addr);
    *o_port = ntohs(address.sin_port);

    return (int32_t)n;
}

auto net::close(int32_t slot) -> int32_t {
    auto it = slots.find(slot);
    if (it == slots.end()) {
        return -EBADF;
    }

    auto fd = it->second;
    slots.erase(it);
#ifdef __WIN32__
    auto n = ::closesocket(fd);
#else
    auto n = ::close(fd);
#endif
    if (n < 0) {
        return -errno;
    }

    return 0;
}
