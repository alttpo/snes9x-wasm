
#include "wasm_module.h"
#include "wasm_net.h"

#include <utility>

fd_net::fd_net(std::shared_ptr<module> m_p, wasi_fd_t fd_p) : fd_inst(fd_p), m_w(m_p) {
    m_p->net.late_init(m_p);
}

wasi_errno_t fd_net::read(const wasi_iovec &iov, uint32_t &nread) {
    auto m = m_w.lock();
    if (!m) {
        return WASI_EBADF;
    }
    auto &net = m->net;

    nread = 0;
    if (iov.empty()) {
        return WASI_EINVAL;
    }

    std::unique_lock<std::mutex> lk(net.inbox_mtx);
    if (net.inbox.empty()) {
        // EOF
        return 0;
    }

    auto &msg = net.inbox.front();

    // determine total size available to read into:
    size_t available = 0;
    for (auto &io: iov) {
        available += io.second;
    }
    if (msg.bytes.size() > available) {
        // not enough space given to read full message. we don't want to track partial read state.
        return WASI_ERANGE;
    }

    // fill in buffers with the message:
    uint8_t *p = msg.bytes.data();
    for (auto &io: iov) {
        memcpy(io.first, p, io.second);
        p += io.second;
        nread += io.second;
    }

    // pop message off the queue:
    net.inbox.pop();

    return 0;
}

wasi_errno_t fd_net::write(const wasi_iovec &iov, uint32_t &nwritten) {
    auto m = m_w.lock();
    if (!m) {
        return WASI_EBADF;
    }
    auto &net = m->net;

    nwritten = 0;
    if (iov.empty()) {
        return WASI_EINVAL;
    }

    // construct a message from all the io buffers concatenated together:
    net_msg msg;
    for (const auto &io: iov) {
        msg.bytes.insert(msg.bytes.end(), io.first, io.first + io.second);
        nwritten += io.second;
    }

    // push the message to the outbox:
    std::unique_lock<std::mutex> lk(net.outbox_mtx);
    net.outbox.push(msg);

    return 0;
}

void net_queues_io_thread(std::shared_ptr<module> *m_p) {
    printf("net_sock: thread\n");

    // downgrade to a weak_ptr so this thread isn't an owner:
    std::weak_ptr<module> m_w(*m_p);
    delete m_p;

    while (!m_w.expired()) {
        // take a temporary shared_ptr during each loop iteration:
        auto m = m_w.lock();
        auto &net = m->net;
        std::unique_lock<std::mutex> lk(net.sock_mtx);

        int max_fd = 0;
        fd_set readfds;
        fd_set writefds;
        FD_ZERO(&readfds);
        FD_ZERO(&writefds);

        if (net.sock[1] > 0) {
            // only listen to single connection:
            FD_SET(net.sock[1], &readfds);
            FD_SET(net.sock[1], &writefds);
            max_fd = net.sock[1] + 1;
        } else {
            // accept incoming connection:
            FD_SET(net.sock[0], &readfds);
            max_fd = net.sock[0] + 1;
        }

        // select on either socket for read/write:
        struct timeval timeout{};
        timeout.tv_sec = 0;
        timeout.tv_usec = 500000;
        printf("net_sock: select(%d)\n", max_fd);
        int res = select(max_fd, &readfds, nullptr, nullptr, &timeout);
        if (res < 0) {
            fprintf(stderr, "net_sock: select error %d\n", errno);
            break;
        }

        // accept a new connection:
        if ((net.sock[1] < 0) && FD_ISSET(net.sock[0], &readfds)) {
            printf("net_sock: accept\n");
            int sock = accept(net.sock[0], nullptr, nullptr);
            if (sock < 0) {
                fprintf(stderr, "net_sock: accept error %d\n", errno);
                break;
            }

            net.sock[1] = sock;
            continue;
        }

        // read incoming data:
        if ((net.sock[1] > 0) && FD_ISSET(net.sock[1], &readfds)) {
            // TODO: framing protocol at this layer?
            uint8_t buf[65536];
            printf("net_sock: read\n");
            auto n = read(net.sock[1], buf, 65536);
            if (n < 0) {
                fprintf(stderr, "net_sock: read error %d\n", errno);
                break;
            }
            if (n == 0) {
                // EOF
                printf("net_sock: close(%d)\n", net.sock[1]);
                if (close(net.sock[1]) < 0) {
                    fprintf(stderr, "net_sock: close error %d\n", errno);
                    break;
                }
                net.sock[1] = -1;
                continue;
            }

            // construct message:
            net_msg msg;
            msg.bytes.insert(msg.bytes.end(), buf, buf + n);

            // push message to inbox:
            std::unique_lock<std::mutex> inbox_lk(net.inbox_mtx);
            net.inbox.push(msg);

            // notify wasm module of message received:
            m->notify_events(wasm_event_kind::ev_msg_received);
        }

        // write outgoing data:
        if ((net.sock[1] > 0) && FD_ISSET(net.sock[1], &writefds)) {
            std::unique_lock<std::mutex> outbox_lk(net.outbox_mtx);

            if (!net.outbox.empty()) {
                auto &msg = net.outbox.front();
                printf("net_sock: write\n");
                auto n = write(net.sock[1], msg.bytes.data(), msg.bytes.size());
                if (n < 0) {
                    fprintf(stderr, "net_sock: write error %d\n", errno);
                    break;
                }
                // TODO: confirm n written bytes
                net.outbox.pop();
            }
        }
    }

    printf("net_sock: ~thread\n");
}

net_sock::~net_sock() {
    printf("~net_sock\n");

    std::unique_lock<std::mutex> lk(sock_mtx);
    if (sock[0] > 0) {
        close(sock[0]);
        sock[0] = 0;
    }
    if (sock[1] > 0) {
        close(sock[1]);
        sock[1] = 0;
    }
}

void net_sock::late_init(std::shared_ptr<module> m_p) {
    std::call_once(late_init_flag, [this, &m_p]() {
        printf("net_sock: socket\n");
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
        address.sin_port = htons(25600);

        printf("net_sock: bind\n");
        if (bind(s, (const sockaddr *) &address, sizeof(address)) < 0) {
            fprintf(stderr, "net_sock: unable to bind socket; error %d\n", errno);
            return;
        }

        printf("net_sock: listen\n");
        if (listen(s, 1) < 0) {
            fprintf(stderr, "net_sock: unable to listen on socket; error %d\n", errno);
            return;
        }

        // assign listen socket to instance:
        sock[0] = s;
        sock[1] = -1;

        // spawn a network i/o thread:
        std::thread(
            net_queues_io_thread,
            new std::shared_ptr<module>(m_p)
        ).detach();
    });
}
