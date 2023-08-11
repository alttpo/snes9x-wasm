
#include <algorithm>
#include <utility>

#include "rex.h"

#include "snes9x.h"
#include "memmap.h"

#include "rex_ppux.h"
#include "rex_iovm.h"
#include "rex_client.h"

struct rex {
    void inc_cycles(int32_t delta);

    void on_pc(uint32_t pc);

    void start();

    void shutdown();

    void handle_net();

    void frame_start();

    void ppux_render_obj_lines(bool sub, uint8_t zstart);

    void ppux_render_bg_lines(int layer, bool sub, uint8_t zh, uint8_t zl);

private:
    std::vector<rex_client_sp> clients;

    sock_sp listener;
    std::vector<sock_wp> all_socks;
};

struct rex rex;

int32_t last_cycles;

void rex_host_init() {
    sock::startup();
}

void rex_rom_loaded() {
    rex.start();
}

void rex_rom_unloaded() {
    rex.shutdown();
}

void rex_set_last_cycles(int32_t last) {
    last_cycles = last;
}

void rex_set_curr_cycles(int32_t curr) {
    rex.inc_cycles(curr - last_cycles);
    last_cycles = curr;
}

void rex_on_pc(uint32_t pc) {
    rex.on_pc(pc);
}

void rex_host_frame_start() {
    rex.frame_start();
}

void rex_ppux_render_obj_lines(bool sub, uint8_t zstart) {
    rex.ppux_render_obj_lines(sub, zstart);
}

void rex_ppux_render_bg_lines(int layer, bool sub, uint8_t zh, uint8_t zl) {
    rex.ppux_render_bg_lines(layer, sub, zh, zl);
}

void rex_host_frame_end() {
}

void rex_host_frame_skip() {
}

////////////////////////////////////////////////////////////////////////////////////////////////

void rex::start() {
    listener = sock::make_tcp();
    if (!(bool) *listener) {
        fprintf(stderr, "failed to allocate socket: %s\n", listener->error_text().c_str());
        return;
    }

    listener->bind(0x7F000001UL, 0x2C00); // 11264
    if (!(bool) *listener) {
        fprintf(stderr, "failed to bind socket: %s\n", listener->error_text().c_str());
        return;
    }
    listener->listen();
    if (!(bool) *listener) {
        fprintf(stderr, "failed to listen on socket: %s\n", listener->error_text().c_str());
        return;
    }

    all_socks.push_back(listener);
}

void rex::shutdown() {
    clients.clear();
    all_socks.clear();
    listener.reset();
}

void rex::frame_start() {
    handle_net();
    for (auto &cl: clients) {
        cl->ppux.render_cmd();
    }
}

void rex::handle_net() {
    int npolled = -1;
    int err;

    // continually poll and handle network events until the queue dries up:
    while (npolled != 0) {
        if (!sock::poll(all_socks, npolled, err)) {
            fprintf(stderr, "poll failed: %s\n", sock::error_text(err).c_str());
            break;
        }
        if (npolled <= 0) {
            break;
        }

        for (auto it = clients.begin(); it != clients.end();) {
            auto &cl = *it;
            assert((bool) cl);

            if (!cl->handle_net()) {
                all_socks.erase(all_socks.begin() + 1 + (it - clients.begin()));
                it = clients.erase(it);
                assert(all_socks.size() >= 1);
                continue;
            }
            it++;
        }

        if (listener->isReadAvailable()) {
            uint32_t ip;
            uint16_t port;

            auto accepted = listener->accept(ip, port);
            if (!(bool) *accepted) {
                fprintf(stderr, "failed to accept socket: %s\n", listener->error_text().c_str());
                return;
            }

            fprintf(stderr, "accepted connection from %08x:%04x\n", ip, port);
            all_socks.push_back(accepted);
            clients.push_back(std::make_shared<rex_client>(accepted));
        }
    }
}

void rex::inc_cycles(int32_t delta) {
    for (auto &cl: clients) {
        cl->inc_cycles(delta);
    }
}

void rex::on_pc(uint32_t pc) {
    for (auto &cl: clients) {
        cl->on_pc(pc);
    }
}

void rex::ppux_render_obj_lines(bool sub, uint8_t zstart) {
    for (auto &cl: clients) {
        ppux &ppux = cl->ppux;
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
}

void rex::ppux_render_bg_lines(int layer, bool sub, uint8_t zh, uint8_t zl) {
    for (auto &cl: clients) {
        ppux &ppux = cl->ppux;
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
}
