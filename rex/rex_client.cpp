
#include <utility>

#include "rex.h"

rex_client::rex_client(sock_sp s_p) :
    s(std::move(s_p)),
    vmi(static_cast<vm_notifier *>(this)) {
}

void rex_client::on_pc(uint32_t pc) {
    vmi.on_pc(pc);
}

bool rex_client::handle_net() {
    // close errored-out clients and remove them:
    if (s->isErrored()) {
        fprintf(stderr, "client errored: %s\n", s->error_text().c_str());
        return false;
    }
    if (s->isClosed()) {
        fprintf(stderr, "client closed\n");
        return false;
    }
    if (!s->isReadAvailable()) {
        return true;
    }

    // read available bytes:
    ssize_t n;
    if (!s->recv(rbuf + rt, 64 - rt, n)) {
        fprintf(stderr, "unable to read\n");
        return false;
    }
    if (n == 0) {
        // EOF?
        fprintf(stderr, "eof?\n");
        return false;
    }
    rt += n;

    while (rh < rt) {
        if (!rf) {
            // [7654 3210]
            //  cfll llll   c = channel (0..1)
            //              f = final frame of message
            //              l = length of frame (0..63)
            rx = rbuf[rh++];
            // determine length of frame:
            rl = rx & 63;
            // read the frame header byte:
            rf = true;
        }

        if (rh + rl > rt) {
            // not enough data for frame:
            return true;
        }

        // handle this current frame:
        uint8_t c = (rx >> 7) & 1;
        uint8_t f = (rx >> 6) & 1;
        recv_frame(c, f, rl, rbuf + rh);

        rf = false;
        rh += rl;

        if (rh >= rt) {
            // buffer is empty:
            rh = 0;
            rt = 0;
            return true;
        }

        // remaining bytes begin the next frame:
        memmove(rbuf, rbuf + rh, rt - rh);
        rt -= rh;
        rh = 0;
    }

    return true;
}

void rex_client::vm_ended() {
    // vn_ended message type:
    sbuf[0] = 1;
    send_frame(1, 1, 1);
}

void rex_client::vm_read_complete(vm_read_result &&result) {
    // vm_read_complete message type:
    sbuf[0] = 2;
    // memory target:
    sbuf[1] = result.t;
    // 24-bit address:
    sbuf[2] = (result.a & 0xFF);
    sbuf[3] = ((result.a >> 8) & 0xFF);
    sbuf[4] = ((result.a >> 16) & 0xFF);
    // 16-bit length (0 -> 65536, else 1..65535):
    uint16_t elen;
    if (result.len == 65536) {
        elen = 0;
    } else {
        elen = result.len;
    }
    sbuf[5] = (elen & 0xFF);
    sbuf[6] = ((elen >> 8) & 0xFF);
    int r = 7;

    // data follows:
    uint8_t *p = result.buf.data();
    while (result.len > (63 - r)) {
        ssize_t frame_size = 63 - r;
        memcpy(sbuf + r, p, frame_size);
        send_frame(1, 0, frame_size);

        result.len -= frame_size;
        p += frame_size;

        // don't need the leading bytes in the following frame(s):
        r = 0;
    }

    {
        assert(result.len <= 63 - r);
        memcpy(sbuf + r, p, result.len);
        send_frame(1, 1, r + result.len);
    }
}

void rex_client::recv_frame(uint8_t c, uint8_t f, uint8_t l, uint8_t buf[63]) {
    printf("recv_frame[c=%d,f=%d]: %d bytes\n", c, f, l);
    switch (c) {
        case 0: // command channel:

            break;
        case 1: // notification channel:
            break;
        default: // no other channels possible
            assert(false);
            break;
    }
}

bool rex_client::send_frame(uint8_t c, uint8_t f, uint8_t l) {
    ssize_t n;
    // relies on sx being immediately prior to sbuf in memory:
    sx = ((c & 1) << 7) | ((f & 1) << 6) | (l & 63);
    if (!s->send(&sx, l + 1, n)) {
        fprintf(stderr, "error sending\n");
        return false;
    }
    if (n != l + 1) {
        fprintf(stderr, "incomplete send\n");
        return false;
    }
    return true;
}
