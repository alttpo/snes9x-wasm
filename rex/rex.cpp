
#include "rex.h"

struct rex rex;

void rex_host_init() {
}

void rex_rom_loaded() {
    rex.start();
}

void rex_rom_unloaded() {
    rex.shutdown();
}

void rex_on_pc(uint32_t pc) {
    rex.on_pc(pc);
}

void rex_host_frame_start() {
    rex.handle_net();
    rex.ppux.render_cmd();
}

void rex_ppux_render_obj_lines(bool sub, uint8_t zstart) {
    ppux &ppux = rex.ppux;
    ppux.priority_depth_map[0] = zstart;
    ppux.priority_depth_map[1] = zstart + 4;
    ppux.priority_depth_map[2] = zstart + 8;
    ppux.priority_depth_map[3] = zstart + 12;

    if (sub) {
        ppux.render_line_sub(ppux::layer::OBJ);
    } else {
        ppux.render_line_main(ppux::layer::OBJ);
    }
}

void rex_ppux_render_bg_lines(int layer, bool sub, uint8_t zh, uint8_t zl) {
    ppux &ppux = rex.ppux;
    ppux.priority_depth_map[0] = zl;
    ppux.priority_depth_map[1] = zh;
    ppux.priority_depth_map[2] = zl;
    ppux.priority_depth_map[3] = zh;

    if (sub) {
        ppux.render_line_sub((ppux::layer) layer);
    } else {
        ppux.render_line_main((ppux::layer) layer);
    }
}

void rex_host_frame_end() {
}

void rex_host_frame_skip() {
}


void rex::start() {
    listener = sock::make_tcp();
    if (!(bool)*listener) {
        fprintf(stderr, "failed to allocate socket: %s\n", listener->error_text().c_str());
        return;
    }

    listener->bind(0x7F000001UL, 0x2C00); // 11264
    if (!(bool)*listener) {
        fprintf(stderr, "failed to bind socket: %s\n", listener->error_text().c_str());
        return;
    }
    listener->listen();
    if (!(bool)*listener) {
        fprintf(stderr, "failed to listen on socket: %s\n", listener->error_text().c_str());
        return;
    }

    all_socks.push_back(listener);
}

void rex::shutdown() {
    for (auto &item: clients) {
        item.reset();
    }
    listener.reset();
}

void rex::handle_net() {
    int n;
    int err;
    if (!sock::poll(all_socks, n, err)) {
        // TODO
        fprintf(stderr, "poll failed: %s\n", strerror(err));
        return;
    }

    for (auto it = clients.begin(); it != clients.end();) {
        auto &cl = *it;

        // close errored-out clients and remove them:
        if (cl->isErrored() || cl->isClosed()) {
            fprintf(stderr, "client closed\n");
            cl.reset();
            all_socks.erase(all_socks.begin() + 1 + (it - clients.begin()));
            it = clients.erase(it);
            assert(all_socks.size() >= 1);
            continue;
        }
        if (!cl->isReadAvailable()) {
            it++;
            continue;
        }

        // read available data:
        ssize_t n;
        uint8_t data[8192];
        cl->recv(data, sizeof(data), n);
        // TODO
        fprintf(stderr, "received data from client\n");
        it++;
    }

    if (listener->isReadAvailable()) {
        uint32_t ip;
        uint16_t port;

        auto accepted = listener->accept(ip, port);
        if (!(bool)*accepted) {
            fprintf(stderr, "failed to accept socket: %s\n", listener->error_text().c_str());
            return;
        }

        fprintf(stderr, "accepted connection from %08x:%04x\n", ip, port);
        all_socks.push_back(accepted);
        clients.push_back(accepted);
    }
}