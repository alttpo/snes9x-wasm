
#include "sock.h"

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

sock::sock(native_socket_t fd_p) : fd(fd_p)
{
    fprintf(stderr, "sock::ctor -> %p\n", this);
}

sock::sock(sock &&o) noexcept :
    fd(o.fd), err(o.err), errfn(std::move(o.errfn)), events(o.events), revents(o.revents)
{
    o.fd = -1;
    o.err = 0;
    o.errfn.clear();
    o.events = 0;
    o.revents = 0;
    fprintf(stderr, "sock::move %p -> %p\n", &o, this);
}

sock::~sock() {
    fprintf(stderr, "sock::dtor -> %p\n", this);
    socket_close();
}

auto sock::get_last_error() -> int {
    int err;
#ifdef __WIN32__
    err = WSAGetLastError();
#else
    err = errno;
#endif
    return err;
}

auto sock::capture_error(const std::string &fn) {
    err = get_last_error();
    errfn = fn;
}

auto sock::error_text() -> std::string {
    if (err == 0) {
        return "no error";
    }

    std::string s;
    s.append(errfn);
    s.append(": ");
#ifdef __WIN32__
    LPVOID lpMsgBuf;
    // TODO: error-handling here
    FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER |
        FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,
        err,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPSTR) &lpMsgBuf,
        0,
        NULL
    );

    assert(lpMsgBuf);
    s.append((const char *)lpMsgBuf);
#else
    s.append(strerror(err));
#endif

    return s;
}

auto sock::socket_close() -> bool {
    if (fd < 0) {
        return true;
    }

    // TODO: handle close() errors to not override `err` field
#ifdef __WIN32__
    ::closesocket(fd);
#else
    ::close(fd);
#endif

    fd = -1;

    return true;
}

auto sock::socket_set_nonblocking() -> bool {
#ifdef __WIN32__
    unsigned long flags = 1;
    int res = ::ioctlsocket(fd, FIONBIO, &flags);
    if (res != NO_ERROR) {
        capture_error("ioctlsocket(FIONBIO)");
        return false;
    }
#else
    int flags = ::fcntl(fd, F_GETFL);
    if (flags < 0) {
        capture_error("fcntl(F_GETFL)");
        return false;
    }

    int res = ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    if (res < 0) {
        capture_error("fcntl(F_SETFL)");
        return false;
    }
#endif

    return true;
}

auto sock::socket_set_tcpnodelay() -> bool {
    int flags = 1;
#ifdef __WIN32__
    if (::setsockopt(fd, SOL_SOCKET, TCP_NODELAY, (const char *)&flags, sizeof(flags)) < 0) {
#else
    if (::setsockopt(fd, SOL_SOCKET, TCP_NODELAY, (void *) &flags, sizeof(flags)) < 0) {
#endif
        capture_error("setsockopt");
        return false;
    }

    return true;
}

sock::operator bool() const {
    return (fd >= 0) && (err == 0);
}

auto sock::make_tcp() -> sock_sp {
    native_socket_t fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        auto sk = std::make_shared<sock>(fd);
        sk->capture_error("socket");
        return sk;
    }

    auto sk = std::make_shared<sock>(fd);

    if (!sk->socket_set_nonblocking()) {
        sk->socket_close();
        return sk;
    }

    if (!sk->socket_set_tcpnodelay()) {
        sk->socket_close();
        return sk;
    }

    return sk;
}

auto sock::make_udp() -> sock_sp {
    native_socket_t fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        auto sk = std::make_shared<sock>(fd);
        sk->capture_error("socket");
        return sk;
    }

    auto sk = std::make_shared<sock>(fd);

    if (!sk->socket_set_nonblocking()) {
        sk->socket_close();
        return sk;
    }

    return sk;
}

auto sock::connect(uint32_t ipv4_addr, uint16_t port) -> bool {
    struct sockaddr_in address{};
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(ipv4_addr);
    address.sin_port = htons(port);

    if (::connect(fd, (const sockaddr *) &address, sizeof(address)) < 0) {
        capture_error("connect");
        return false;
    }

    return true;
}

auto sock::bind(uint32_t ipv4_addr, uint16_t port) -> bool {
    struct sockaddr_in address{};
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(ipv4_addr);
    address.sin_port = htons(port);

    if (::bind(fd, (const sockaddr *) &address, sizeof(address)) < 0) {
        capture_error("bind");
        return false;
    }

    return true;
}

auto sock::listen() -> bool {
    if (::listen(fd, 1) < 0) {
        capture_error("listen");
        return false;
    }

    return true;
}

auto sock::accept(uint32_t &o_ipv4_addr, uint16_t &o_port) -> sock_sp {
    socklen_t address_len = 0;
    struct sockaddr_in address{};
    memset(&address, 0, sizeof(address));

    auto accepted = ::accept(fd, (struct sockaddr *) &address, &address_len);
    if (accepted < 0) {
        auto sk = std::make_shared<sock>(accepted);
        sk->capture_error("accept");
        return sk;
    }

    o_ipv4_addr = ntohl(address.sin_addr.s_addr);
    o_port = ntohs(address.sin_port);

    return std::make_shared<sock>(accepted);
}

auto sock::poll(const std::vector<sock_wp> &socks, int &n, int &err) -> bool {
#ifdef __WIN32__
    std::vector<WSAPOLLFD> pollfds;
#else
    std::vector<pollfd> pollfds;
#endif
    pollfds.reserve(socks.size());

    std::vector<sock_sp> locked;
    locked.reserve(socks.size());

    for (const auto &ws: socks) {
        auto ps = ws.lock();
        if (!ps) {
            continue;
        }
        locked.emplace_back(ps);

        ps->events |= (short)POLLIN;
        pollfds.push_back({ps->fd, ps->events, ps->revents});
    }

#ifdef __WIN32__
    n = ::WSAPoll(pollfds.data(), pollfds.size(), 0);
#else
    n = ::poll(pollfds.data(), pollfds.size(), 0);
#endif
    if (n < 0) {
        err = get_last_error();
        return false;
    }

    for (int i = 0; i < pollfds.size(); i++) {
        locked[i]->revents = pollfds[i].revents;
    }

    return true;
}

auto sock::send(uint8_t *data, uint32_t len, ssize_t &n) -> bool {
#ifdef __WIN32__
    n = ::send(fd, (const char *)data, len, 0);
#else
    n = ::send(fd, data, len, 0);
#endif
    if (n < 0) {
        capture_error("send");
        return false;
    }

    return true;
}

auto sock::sendto(uint8_t *data, uint32_t len, uint32_t ipv4_addr, uint16_t port, ssize_t &n) -> bool {
    struct sockaddr_in address{};
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(ipv4_addr);
    address.sin_port = htons(port);

#ifdef __WIN32__
    n = ::sendto(fd, (const char *)data, len, 0, (struct sockaddr *)&address, sizeof(address));
#else
    n = ::sendto(fd, data, len, 0, (struct sockaddr *) &address, sizeof(address));
#endif
    if (n < 0) {
        capture_error("sendto");
        return false;
    }

    return true;
}

auto sock::recv(uint8_t *data, uint32_t len, ssize_t &n) -> bool {
#ifdef __WIN32__
    n = ::recv(fd, (char*)data, len, 0);
#else
    n = ::recv(fd, data, len, 0);
#endif
    if (n < 0) {
        capture_error("recv");
        return false;
    }

    return true;
}

auto sock::recvfrom(uint8_t *data, uint32_t len, uint32_t &o_ipv4_addr, uint16_t &o_port, ssize_t &n) -> bool {
    socklen_t address_len;
    struct sockaddr_in address{};
    memset(&address, 0, sizeof(address));

#ifdef __WIN32__
    n = ::recv(fd, (char*)data, len, 0);
#else
    n = ::recvfrom(fd, data, len, 0, (struct sockaddr *) &address, &address_len);
#endif
    if (n < 0) {
        capture_error("recvfrom");
        return false;
    }

    o_ipv4_addr = ntohl(address.sin_addr.s_addr);
    o_port = ntohs(address.sin_port);

    return true;
}

auto sock::close() -> bool {
    return socket_close();
}
