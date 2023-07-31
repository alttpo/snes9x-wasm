
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
    if (!s->recv(rbuf + rt, 32 - rt, n)) {
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
            //  ccml llll   c = channel (0..3)
            //              m = more data follows (frame unfinished)
            //              l = length of frame (0..31)
            rx = rbuf[rh++];
            // determine length of frame:
            rl = rx & 31;
            // read the frame header byte:
            rf = true;
        }

        if (rh + rl > rt) {
            // not enough data for frame:
            return true;
        }

        // handle this current frame:
        uint8_t c = rx >> 6;
        uint8_t m = (rx >> 5) & 1;
        recv_frame(c, m, rl, rbuf + rh);

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
    sbuf[1] = 1;
    send_frame(1, 0, 1);
}

void rex_client::vm_read_complete(vm_read_result &&result) {
    // vm_read_complete message type:
    sbuf[1] = 2;
    sbuf[2] = result.t;
    sbuf[3] = (result.a & 0xFF);
    sbuf[4] = ((result.a >> 8) & 0xFF);
    sbuf[5] = ((result.a >> 16) & 0xFF);
    int r = 5;

    uint8_t *p = result.buf.data();
    while (result.len > (31 - r)) {
        ssize_t frame_size = 31 - r;
        memcpy(sbuf + 1 + r, p, frame_size);
        send_frame(1, 1, frame_size);

        result.len -= frame_size;
        p += frame_size;

        // don't need the leading 5 bytes in the following frames:
        r = 0;
    }

    {
        assert(result.len <= 31 - r);
        memcpy(sbuf + 1 + r, p, result.len);
        send_frame(1, 0, r + result.len);
    }
}

void rex_client::recv_frame(uint8_t c, uint8_t m, uint8_t l, uint8_t buf[32]) {
    printf("recv_frame[c=%d,m=%d]: %d bytes\n", c, m, l);
}

bool rex_client::send_frame(uint8_t c, uint8_t m, uint8_t l) {
    ssize_t n;
    sbuf[0] = ((c & 3) << 6) | ((m & 1) << 5) | (l & 31);
    if (!s->send(sbuf, l + 1, n)) {
        fprintf(stderr, "error sending\n");
        return false;
    }
    if (n != l + 1) {
        fprintf(stderr, "incomplete send\n");
        return false;
    }
    return true;
}
