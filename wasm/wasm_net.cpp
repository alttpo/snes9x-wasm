
#include "wasm_module.h"
#include "wasm_net.h"

fd_net::fd_net(std::shared_ptr<net_sock> net_p, wasi_fd_t fd_p) : fd_inst(fd_p), net_w(net_p) {
    net_p->late_init();
}

wasi_errno_t fd_net::read(const wasi_iovec &iov, uint32_t &nread) {
    auto net = net_w.lock();
    if (!net) {
        return WASI_EBADF;
    }

    nread = 0;
    if (iov.empty()) {
        return WASI_EINVAL;
    }

    std::unique_lock<std::mutex> lk(net->inbox_mtx);
    if (net->inbox.empty()) {
        // EOF
        return 0;
    }

    auto &msg = net->inbox.front();

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
    net->inbox.pop();

    return 0;
}

wasi_errno_t fd_net::write(const wasi_iovec &iov, uint32_t &nwritten) {
    auto net = net_w.lock();
    if (!net) {
        return WASI_EBADF;
    }

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
    std::unique_lock<std::mutex> lk(net->outbox_mtx);
    net->outbox.push(msg);

    return 0;
}

void net_queues_io_thread(std::shared_ptr<net_sock> *net_p) {
    // downgrade to a weak_ptr so this thread isn't an owner:
    std::weak_ptr<net_sock> net_w(*net_p);
    delete net_p;

    while (!net_w.expired()) {
        // take a temporary shared_ptr during each loop iteration:
        auto net = net_w.lock();
        std::unique_lock<std::mutex> lk(net->sock_mtx);

        int max_fd = 0;
        fd_set readfds;
        fd_set writefds;
        FD_ZERO(&readfds);
        FD_ZERO(&writefds);

        if (net->sock[1] != 0) {
            // only listen to single connection:
            FD_SET(net->sock[1], &readfds);
            FD_SET(net->sock[1], &writefds);
            max_fd = net->sock[1] + 1;
        } else {
            // accept incoming connection:
            FD_SET(net->sock[0], &readfds);
            max_fd = net->sock[0] + 1;
        }

        // select on either socket for read/write:
        struct timeval timeout{};
        timeout.tv_sec = 0;
        timeout.tv_usec = 1000;
        int res = select(max_fd, &readfds, &writefds, nullptr, &timeout);
        if (res <= 0) {
            break;
        }

        // accept a new connection:
        if ((net->sock[1] == 0) && FD_ISSET(net->sock[0], &readfds)) {
            net->sock[1] = accept(net->sock[0], nullptr, nullptr);
            continue;
        }

        // read incoming data:
        if ((net->sock[1] > 0) && FD_ISSET(net->sock[1], &readfds)) {
            // TODO: framing protocol at this layer?
            uint8_t buf[65536];
            auto n = read(net->sock[1], buf, 65536);

            // construct message:
            net_msg msg;
            msg.bytes.insert(msg.bytes.end(), buf, buf + n);

            // push message to inbox:
            std::unique_lock<std::mutex> inbox_lk(net->inbox_mtx);
            net->inbox.push(msg);
        }

        // write outgoing data:
        if ((net->sock[1] > 0) && FD_ISSET(net->sock[1], &writefds)) {
            std::unique_lock<std::mutex> outbox_lk(net->outbox_mtx);

            if (!net->outbox.empty()) {
                auto &msg = net->outbox.front();
                write(net->sock[1], msg.bytes.data(), msg.bytes.size());
                // TODO: confirm n written bytes
                net->outbox.pop();
            }
        }
    }
}

net_sock::~net_sock() {
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

void net_sock::late_init() {
    std::call_once(late_init_flag, [this]() {
        auto s = socket(AF_INET, SOCK_STREAM, 0);
        if (s < 0) {
            fprintf(stderr, "wasm: unable to create network socket\n");
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

        if (bind(s, (const sockaddr *) &address, sizeof(address)) < 0) {
            fprintf(stderr, "wasm: unable to bind socket\n");
            return;
        }

        if (listen(s, 1) < 0) {
            fprintf(stderr, "wasm: unable to listen on socket\n");
            return;
        }

        // assign listen socket to instance:
        sock[0] = s;
        sock[1] = 0;

        // spawn a network i/o thread:
        std::thread(
            net_queues_io_thread,
            new std::shared_ptr<net_sock>(this)
        ).detach();
    });
}