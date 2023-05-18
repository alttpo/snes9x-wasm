
#ifndef SNES9X_WASM_NET_H
#define SNES9X_WASM_NET_H

#include <cstdint>
#include <memory>
#include <utility>
#include <queue>
#include <thread>
#include <vector>
#include <string>

#ifdef __WIN32__
#  include <winsock.h>
#  include <process.h>

#  define ioctl ioctlsocket
#  define close(h) if(h){closesocket(h);}
#  define read(a,b,c) recv(a, b, c, 0)
#  define write(a,b,c) send(a, b, c, 0)
#else

#  include <unistd.h>
#  include <sys/time.h>
#  include <sys/types.h>
#  include <sys/stat.h>

#  include <netdb.h>
#  include <sys/ioctl.h>
#  include <sys/socket.h>
#  include <sys/param.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>

#  ifdef __SVR4
#    include <sys/stropts.h>
#  endif
#endif

#include "snes9x.h"

// WAMR:
#include "wasm_export.h"

#include "wasi_types.h"

struct net_msg {
    std::vector<uint8_t> bytes;
};

struct net_sock {
    ~net_sock();

    void late_init(std::shared_ptr<module> m_p);

    void send(net_msg &&msg);

public:
    std::mutex sock_mtx;
    int sock[2];

    std::once_flag late_init_flag;

    std::mutex inbox_mtx;
    std::queue<net_msg> inbox;

    std::mutex outbox_mtx;
    std::queue<net_msg> outbox;
};

class fd_net : public fd_inst {
public:
    explicit fd_net(std::shared_ptr<module> m_p, wasi_fd_t fd_p);

    wasi_errno_t read(const wasi_iovec &iov, uint32_t &nread) override;

    wasi_errno_t write(const wasi_iovec &iov, uint32_t &nwritten) override;

    std::weak_ptr<module> m_w;
};

#endif //SNES9X_WASM_NET_H
