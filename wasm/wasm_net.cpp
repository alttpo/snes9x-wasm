
#include "wasm_module.h"
#include "wasm_net.h"

#include <utility>
#include <sys/uio.h>

fd_net_sock::fd_net_sock(std::shared_ptr<module> m_p, wasi_fd_t fd_p, int sock_index_p) : fd_inst(fd_p), m_w(m_p),
    sock_index(sock_index_p) {
}

wasi_errno_t fd_net_sock::close() {
    auto m = m_w.lock();
    if (!m) {
        return WASI_EBADF;
    }
    auto &net = m->net;

    wasi_errno_t retval = 0;

    int sockfd;
    {
        std::unique_lock<std::mutex> lk(net.sock_mtx);
        sockfd = net.sock[sock_index];
    }

    // close socket:
    if (::close(sockfd) < 0) {
        retval = posix_to_wasi_error(errno);
    }

    // clear out slot after closing:
    {
        std::unique_lock<std::mutex> lk(net.sock_mtx);
        net.sock[sock_index] = -1;
    }

    return retval;
}

wasi_errno_t fd_net_sock::read(const wasi_iovec &iov, uint32_t &nread) {
    auto m = m_w.lock();
    if (!m) {
        return WASI_EBADF;
    }
    auto &net = m->net;

    nread = 0;
    if (iov.empty()) {
        return WASI_EINVAL;
    }

    int sockfd;
    {
        std::unique_lock<std::mutex> lk(net.sock_mtx);
        sockfd = net.sock[sock_index];
    }

    printf("fd_net_sock(%d): read\n", sock_index);
    auto n = ::readv(sockfd, reinterpret_cast<const iovec *>(iov.data()), (int)iov.size());
    printf("fd_net_sock(%d): ~read\n", sock_index);
    if (n < 0) {
        return posix_to_wasi_error(errno);
    }
    nread = n;

    return 0;
}

wasi_errno_t fd_net_sock::write(const wasi_iovec &iov, uint32_t &nwritten) {
    auto m = m_w.lock();
    if (!m) {
        return WASI_EBADF;
    }
    auto &net = m->net;

    nwritten = 0;
    if (iov.empty()) {
        return WASI_EINVAL;
    }

    int sockfd;
    {
        std::unique_lock<std::mutex> lk(net.sock_mtx);
        sockfd = net.sock[sock_index];
    }

    printf("fd_net_sock(%d): write\n", sock_index);
    auto n = ::writev(sockfd, reinterpret_cast<const iovec *>(iov.data()), (int)iov.size());
    printf("fd_net_sock(%d): ~write\n", sock_index);
    if (n < 0) {
        return posix_to_wasi_error(errno);
    }

    nwritten = n;

    return 0;
}

void net_listener_thread(std::shared_ptr<module> *m_p) {
    //printf("net_listener_thread\n");

    // downgrade to a weak_ptr so this thread isn't an owner:
    std::weak_ptr<module> m_w(*m_p);
    delete m_p;

    while (!m_w.expired()) {
        // take a temporary shared_ptr during each loop iteration:
        auto m = m_w.lock();
        auto &net = m->net;

        fd_set readfds;
        FD_ZERO(&readfds);

        int max_fd = 0;
        int sock_accept;

        {
            std::unique_lock<std::mutex> lk(net.sock_mtx);

            sock_accept = net.sock_accept;
            FD_SET(sock_accept, &readfds);
            max_fd = sock_accept + 1;

            // check each available connection:
            for (const auto &slot: net.sock) {
                if (slot <= 0) {
                    continue;
                }

                FD_SET(slot, &readfds);
                if (slot + 1 > max_fd) {
                    max_fd = slot + 1;
                }
            }
        }

        // select on either socket for read/write:
        struct timeval timeout{};
        timeout.tv_sec = 0;
        timeout.tv_usec = 500000;
        printf("net_listener_thread: select(%d)\n", max_fd);
        int res = ::select(max_fd, &readfds, nullptr, nullptr, &timeout);
        if (res < 0) {
            fprintf(stderr, "net_listener_thread: select error %d\n", errno);
            break;
        }

        // determine which socket slots have reads available:
        uint32_t events = 0;
        for (int i = 0; i < 8; i++) {
            if (net.sock[i] <= 0) {
                continue;
            }

            if (FD_ISSET(net.sock[i], &readfds)) {
                events |= (1 << i);
            }
        }

        // notify wasm module of sockets available to read:
        m->notify_events(events);

        // accept up to 8 connections:
        if (FD_ISSET(sock_accept, &readfds)) {
            //printf("net_listener_thread: accept\n");
            int accepted = ::accept(sock_accept, nullptr, nullptr);
            if (accepted < 0) {
                fprintf(stderr, "net_listener_thread: accept error %d\n", errno);
            } else {
                printf("net_listener_thread: accepted tcp connection %d\n", accepted);

                std::unique_lock<std::mutex> lk(net.sock_mtx);
                bool set = false;
                for (auto &slot: net.sock) {
                    if (slot <= 0) {
                        slot = accepted;
                        set = true;
                        break;
                    }
                }

                // no room left for connection; close it:
                if (!set) {
                    fprintf(stderr,
                        "net_listener_thread: no slot available for accepted tcp connection %d; closing\n",
                        accepted
                    );
                    if (::close(sock_accept) < 0) {
                        fprintf(stderr, "net_listener_thread: close error %d\n", errno);
                    }
                }
            }
        }
    }

    //printf("~net_listener_thread\n");
}

net_listener::~net_listener() {
    //printf("net_listener\n");

    std::unique_lock<std::mutex> lk(sock_mtx);
    if (sock_accept > 0) {
        close(sock_accept);
        sock_accept = 0;
    }

    for (auto &s: sock) {
        if (s > 0) {
            close(s);
            s = 0;
        }
    }
}

fd_net_listener::fd_net_listener(std::shared_ptr<module> m_p, wasi_fd_t fd_p, uint16_t port) : fd_inst(fd_p), m_w(m_p) {
    // NOTE: this allows only one listener to be open at a time per module
    m_p->net.late_init(m_p, port);
}

void net_listener::late_init(std::shared_ptr<module> m_p, uint16_t port) {
    std::call_once(late_init_flag, [this, &m_p, port]() {
        //printf("net_listener: socket\n");
        auto s = socket(AF_INET, SOCK_STREAM, 0);
        if (s < 0) {
            fprintf(stderr, "wasm: unable to create network socket; error %d\n", errno);
            return;
        }

        //int val = 1;
        //setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (char *)&val, sizeof (val));

        struct sockaddr_in address{};
        memset(&address, 0, sizeof(address));
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_ANY);
        // TODO: configurable port per module?
        address.sin_port = htons(port);

        //printf("net_listener: bind\n");
        if (bind(s, (const sockaddr *) &address, sizeof(address)) < 0) {
            fprintf(stderr, "net_listener: unable to bind socket; error %d\n", errno);
            return;
        }

        //printf("net_listener: listen\n");
        if (listen(s, 0) < 0) {
            fprintf(stderr, "net_listener: unable to listen on socket; error %d\n", errno);
            return;
        }

        printf("net_listener: listening on tcp port %d\n", ntohs(address.sin_port));

        // assign listen socket to instance:
        sock_accept = s;

        // spawn a network i/o thread for reading:
        std::thread(
            net_listener_thread,
            new std::shared_ptr<module>(m_p)
        ).detach();
    });
}
