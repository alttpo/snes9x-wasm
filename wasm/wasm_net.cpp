
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
    uint32_t remaining = msg.bytes.size();
    for (auto &io: iov) {
        const uint32_t &n = std::min(remaining, io.second);
        if (n == 0) {
            break;
        }

        memcpy(io.first, p, n);

        p += n;
        nread += n;
        remaining -= n;
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
    {
        std::unique_lock<std::mutex> lk(net.outbox_mtx);
        net.outbox.push(msg);
    }

    // notify write thread:
    printf("fd_net: write: notify\n");
    net.outbox_cv.notify_one();

    return 0;
}

void net_queues_read_thread(std::shared_ptr<module> *m_p) {
    printf("net_sock_reader\n");

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
        int sock_data;
        {
            std::unique_lock<std::mutex> lk(net.sock_mtx);
            sock_accept = net.sock[0];
            sock_data = net.sock[1];
        }

        if (sock_data > 0) {
            // only listen to data connection:
            FD_SET(sock_data, &readfds);
            max_fd = sock_data + 1;
        } else {
            // accept incoming connection:
            FD_SET(sock_accept, &readfds);
            max_fd = sock_accept + 1;
        }

        // select on either socket for read/write:
        struct timeval timeout{};
        timeout.tv_sec = 0;
        timeout.tv_usec = 500000;
        printf("net_sock_reader: select(%d)\n", max_fd);
        int res = select(max_fd, &readfds, nullptr, nullptr, &timeout);
        if (res < 0) {
            fprintf(stderr, "net_sock_reader: select error %d\n", errno);
            break;
        }

        // accept only a single connection:
        if ((sock_data <= 0) && FD_ISSET(sock_accept, &readfds)) {
            printf("net_sock_reader: accept\n");
            int sock = accept(sock_accept, nullptr, nullptr);
            if (sock < 0) {
                fprintf(stderr, "net_sock_reader: accept error %d\n", errno);
                break;
            }

            std::unique_lock<std::mutex> lk(net.sock_mtx);
            net.sock[1] = sock;
            continue;
        }

        // read incoming data:
        if ((sock_data > 0) && FD_ISSET(sock_data, &readfds)) {
            // TODO: framing protocol at this layer?
            uint8_t buf[65536];
            printf("net_sock_reader: read\n");
            auto n = read(sock_data, buf, 65536);
            if (n < 0) {
                fprintf(stderr, "net_sock_reader: read error %d\n", errno);
                break;
            }
            if (n == 0) {
                // EOF
                printf("net_sock_reader: close(%d)\n", sock_data);
                if (close(sock_data) < 0) {
                    fprintf(stderr, "net_sock_reader: close error %d\n", errno);
                    break;
                }

                std::unique_lock<std::mutex> lk(net.sock_mtx);
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
    }

    printf("!net_sock_reader\n");
}

void net_queues_write_thread(std::shared_ptr<module> *m_p) {
    printf("net_sock_writer\n");

    // downgrade to a weak_ptr so this thread isn't an owner:
    std::weak_ptr<module> m_w(*m_p);
    delete m_p;

    while (!m_w.expired()) {
        // take a temporary shared_ptr during each loop iteration:
        auto m = m_w.lock();
        auto &net = m->net;

        // wait for a condition that an outbox message is available:
        {
            std::unique_lock<std::mutex> outbox_lk(net.outbox_cv_mtx);
            if (!net.outbox_cv.wait_for(
                outbox_lk,
                std::chrono::microseconds(1000),
                [&net]() { return !net.outbox.empty(); }
            )) {
                continue;
            }
        }

        printf("net_sock_writer: notified\n");

        fd_set writefds;
        FD_ZERO(&writefds);

        int sock_data;
        {
            std::unique_lock<std::mutex> lk(net.sock_mtx);
            sock_data = net.sock[1];
        }

        int max_fd = 0;
        if (sock_data > 0) {
            // only listen to data connection:
            FD_SET(sock_data, &writefds);
            max_fd = sock_data + 1;
        }

        // select on either socket for read/write:
        struct timeval timeout{};
        timeout.tv_sec = 0;
        timeout.tv_usec = 500000;
        printf("net_sock_writer: select(%d)\n", max_fd);
        int res = select(max_fd, nullptr, &writefds, nullptr, &timeout);
        if (res < 0) {
            fprintf(stderr, "net_sock_writer: select error %d\n", errno);
            break;
        }

        // write outgoing data:
        if ((sock_data > 0) && FD_ISSET(sock_data, &writefds)) {
            std::unique_lock<std::mutex> outbox_lk(net.outbox_mtx);

            if (!net.outbox.empty()) {
                auto &msg = net.outbox.front();

                printf("net_sock_writer: write\n");
                auto n = write(sock_data, msg.bytes.data(), msg.bytes.size());
                if (n < 0) {
                    fprintf(stderr, "net_sock_writer: write error %d\n", errno);
                    break;
                }
                if (n == 0) {
                    // EOF:
                    printf("net_sock_writer: close(%d)\n", sock_data);
                    if (close(sock_data) < 0) {
                        fprintf(stderr, "net_sock_writer: close error %d\n", errno);
                        break;
                    }

                    std::unique_lock<std::mutex> lk(net.sock_mtx);
                    net.sock[1] = -1;
                    continue;
                }

                // TODO: confirm n written bytes
                net.outbox.pop();
            }
        }
    }

    printf("~net_sock_writer\n");
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

        // spawn a network i/o thread for reading:
        std::thread(
            net_queues_read_thread,
            new std::shared_ptr<module>(m_p)
        ).detach();

        // spawn a network i/o thread for writing:
        std::thread(
            net_queues_write_thread,
            new std::shared_ptr<module>(m_p)
        ).detach();
    });
}
