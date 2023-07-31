
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
    // vm_ended message type:
    send_message(1, {1});
}

void rex_client::vm_read_complete(vm_read_result &&result) {
    v8 msg;
    msg.reserve(7 + result.len);

    // vm_read_complete message type:
    msg.push_back(2);
    // memory target:
    msg.push_back(result.t);
    // 24-bit address:
    msg.push_back((result.a & 0xFF));
    msg.push_back(((result.a >> 8) & 0xFF));
    msg.push_back(((result.a >> 16) & 0xFF));
    // 16-bit length (0 -> 65536, else 1..65535):
    uint16_t elen;
    if (result.len == 65536) {
        elen = 0;
    } else {
        elen = result.len;
    }
    msg.push_back((elen & 0xFF));
    msg.push_back(((elen >> 8) & 0xFF));

    // data follows:
    msg.insert(msg.end(), result.buf.begin(), result.buf.end());

    send_message(1, msg);
}

void rex_client::recv_frame(uint8_t c, uint8_t f, uint8_t l, uint8_t buf[63]) {
    printf("recv_frame[c=%d,f=%d]: %d bytes\n", c, f, l);

    // append buffer data to message for this channel:
    msgIn[c].insert(msgIn[c].end(), buf, buf + l);
    if (f) {
        // if final frame bit is set, process the entire message:
        recv_message(c, msgIn[c]);
        // clear out message for next:
        msgIn[c].clear();
    }
}

void rex_client::send_message(uint8_t c, const v8 &msg) {
    size_t len = msg.size();
    const uint8_t *p = msg.data();

    // send non-final frames:
    const ssize_t frame_size = 63;
    while (len > frame_size) {
        memcpy(sbuf, p, frame_size);
        send_frame(c, 0, frame_size);

        len -= frame_size;
        p += frame_size;
    }

    {
        // send final frame:
        assert(len <= 63);
        memcpy(sbuf, p, len);
        send_frame(c, 1, len);
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

void rex_client::recv_message(uint8_t c, const v8 &m) {
    if (c != 0) {
        // discard messages not on channel 0:
        return;
    }
    if (m.empty()) {
        // discard empty messages:
        return;
    }

    // c=0 is command/response channel:
    bool success;
    switch (m.at(0)) {
        case 1: // iovm: load & execute program
            if (m.size() <= 1+1) {
                fprintf(stderr, "message too short\n");
                return;
            }

            vmi.vm_init();
            vmi.vm_load(&m.at(1), m.size() - 1);
            send_message(0, {1});
            break;
        case 2: // iovm: reset
            if (m.size() > 1) {
                fprintf(stderr, "message too long\n");
                return;
            }
            vmi.vm_reset();
            send_message(0, {2});
            break;
        case 3: // iovm: getstate
            if (m.size() > 1) {
                fprintf(stderr, "message too long\n");
                return;
            }
            send_message(0, {3, static_cast<uint8_t>(vmi.vm_getstate())});
            break;
        case 4: // ppux: load & execute program
            if (m.size() <= 1+4) {
                fprintf(stderr, "message too short\n");
                return;
            }
            if ( ((m.size() - 1) & 3) != 0 ) {
                fprintf(stderr, "message data size must be multiple of 4 bytes\n");
                return;
            }
            success = ppux.cmd_write(
                (uint32_t *)&m.at(1),
                (m.size() - 1) / sizeof(uint32_t)
            );
            send_message(0, {4, static_cast<unsigned char>(success ? 1 : 0)});
            break;
        case 5: { // ppux: vram upload
            // TODO: possibly stream in each frame instead of waiting for the complete message
            if (m.size() <= 1+4) {
                fprintf(stderr, "message too short\n");
                return;
            }
            auto p = m.cbegin() + 1;
            uint32_t addr = *p++;
            addr |= (uint32_t)*p++ << 8;
            addr |= (uint32_t)*p++ << 16;
            addr |= (uint32_t)*p++ << 24;
            // size is implicit in remaining length of message
            success = ppux.vram_upload(addr, &*p, m.cend() - p);
            send_message(0, {5, static_cast<unsigned char>(success ? 1 : 0)});
            break;
        }
        case 6: { // ppux: cgram upload
            // TODO: possibly stream in each frame instead of waiting for the complete message
            if (m.size() <= 1+4) {
                fprintf(stderr, "message too short\n");
                return;
            }
            auto p = m.cbegin() + 1;
            uint32_t addr = *p++;
            addr |= (uint32_t)*p++ << 8;
            addr |= (uint32_t)*p++ << 16;
            addr |= (uint32_t)*p++ << 24;
            // size is implicit in remaining length of message
            success = ppux.cgram_upload(addr, &*p, m.cend() - p);
            send_message(0, {6, static_cast<unsigned char>(success ? 1 : 0)});
            break;
        }
        default:
            // discard unknown messages
            break;
    }
}
