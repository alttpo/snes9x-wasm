
#ifndef SNES9X_REX_SOCK_H
#define SNES9X_REX_SOCK_H

#include <memory>
#include <string>

#ifdef __WIN32__
#include <ws2tcpip.h>
typedef SOCKET native_socket_t;
#else
typedef int native_socket_t;
#endif

class sock {
    using sock_sp = std::shared_ptr<sock>;
    using sock_wp = std::weak_ptr<sock>;

    native_socket_t fd;

    int         err{};
    std::string errfn{};

    short events{};
    short revents{};

    auto socket_close() -> bool;
    auto socket_set_nonblocking() -> bool;
    auto socket_set_tcpnodelay() -> bool;
    auto capture_error(const std::string &fn);

    static auto get_last_error() -> int;

public:
    explicit sock(native_socket_t fd);
    sock(sock &&o) noexcept;
    ~sock();

    static auto make_tcp() -> sock_sp;

    static auto make_udp() -> sock_sp;

    static auto poll(const std::vector<sock_wp> &socks, int &n, int &err) -> bool;

    auto connect(uint32_t ipv4_addr, uint16_t port) -> bool;
    auto bind(uint32_t ipv4_addr, uint16_t port) -> bool;
    auto listen() -> bool;
    auto accept(uint32_t &o_ipv4_addr, uint16_t &o_port) -> sock_sp;

    auto send(uint8_t *data, size_t len, long &n) -> bool;
    auto sendto(uint8_t *data, size_t data_len, uint32_t ipv4_addr, uint16_t port, long &n) -> bool;

    auto recv(uint8_t *data, size_t len, long &n) -> bool;
    auto recvfrom(uint8_t *data, size_t data_len, uint32_t &o_ipv4_addr, uint16_t &o_port, long &n) -> bool;

    auto close() -> bool;

    explicit operator bool() const;
    auto error_text() -> std::string;

    inline auto isReadAvailable() const -> bool { return (revents&0x0001) != 0; }
    inline auto isWritable() const -> bool      { return (revents&0x0004) != 0; }
    inline auto isErrored() const -> bool       { return (revents&0x0008) != 0; }
    inline auto isClosed() const -> bool        { return (revents&0x0010) != 0; }
    inline auto isEventsInvalid() const -> bool { return (revents&0x0020) != 0; }
};

using sock_sp = std::shared_ptr<sock>;
using sock_wp = std::weak_ptr<sock>;

#endif //SNES9X_REX_SOCK_H
