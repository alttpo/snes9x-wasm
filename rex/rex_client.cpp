
#include <utility>

#include "rex.h"
#include "rex_proto.h"

rex_client::rex_client(sock_sp s_p) :
    s(std::move(s_p)),
    vmi(static_cast<vm_notifier *>(this)) {
}

void rex_client::on_pc(uint32_t pc) {
    vmi.on_pc(pc);
}

///////////////////////////////////

void rex_client::vm_notify_ended() {
    // vm_ended message type:
    sbuf[dri++] = rex_notify_iovm_end;

    send_frame(1, 1, dri);
    dri = 0;
}

void rex_client::vm_notify_fail(uint32_t pc, iovm1_opcode o, rex_cmd_result result) {
    // vm_read_complete message type:
    sbuf[dri++] = rex_notify_iovm_opcode_fail;
    // pc:
    sbuf[dri++] = pc & 0xFF;
    sbuf[dri++] = (pc >> 8) & 0xFF;
    sbuf[dri++] = (pc >> 16) & 0xFF;
    sbuf[dri++] = (pc >> 24) & 0xFF;
    // opcode:
    sbuf[dri++] = o;
    // result:
    sbuf[dri++] = result;

    send_frame(1, 1, dri);
    dri = 0;
}

void rex_client::vm_notify_read_start(uint32_t pc, uint8_t tdu, uint32_t addr, uint32_t len) {
    // vm_read_complete message type:
    sbuf[dri++] = rex_notify_iovm_read;
    // pc:
    sbuf[dri++] = pc & 0xFF;
    sbuf[dri++] = (pc >> 8) & 0xFF;
    sbuf[dri++] = (pc >> 16) & 0xFF;
    sbuf[dri++] = (pc >> 24) & 0xFF;
    // memory target:
    sbuf[dri++] = tdu;
    // 24-bit address:
    sbuf[dri++] = (addr & 0xFF);
    sbuf[dri++] = ((addr >> 8) & 0xFF);
    sbuf[dri++] = ((addr >> 16) & 0xFF);
    // 16-bit length (0 -> 65536, else 1..65535):
    uint16_t elen;
    if (len == 65536) {
        elen = 0;
    } else {
        elen = len;
    }
    sbuf[dri++] = (elen & 0xFF);
    sbuf[dri++] = ((elen >> 8) & 0xFF);

    // send start frame instantly:
    send_frame(1, 0, dri);
    dri = 0;
}

void rex_client::vm_notify_read_byte(uint8_t x) {
    sbuf[dri++] = x;
    if (dri >= 63) {
        send_frame(1, 0, dri);
        dri = 0;
    }
}

void rex_client::vm_notify_read_end() {
    // send the final frame:
    send_frame(1, 1, dri);
    dri = 0;
}

void rex_client::vm_notify_write_start(uint32_t pc, uint8_t tdu, uint32_t addr, uint32_t len) {
    // vm_read_complete message type:
    sbuf[dri++] = rex_notify_iovm_write;
    // pc:
    sbuf[dri++] = pc & 0xFF;
    sbuf[dri++] = (pc >> 8) & 0xFF;
    sbuf[dri++] = (pc >> 16) & 0xFF;
    sbuf[dri++] = (pc >> 24) & 0xFF;
    // memory target:
    sbuf[dri++] = tdu;
    // 24-bit address:
    sbuf[dri++] = (addr & 0xFF);
    sbuf[dri++] = ((addr >> 8) & 0xFF);
    sbuf[dri++] = ((addr >> 16) & 0xFF);
    // 16-bit length (0 -> 65536, else 1..65535):
    uint16_t elen;
    if (len == 65536) {
        elen = 0;
    } else {
        elen = len;
    }
    sbuf[dri++] = (elen & 0xFF);
    sbuf[dri++] = ((elen >> 8) & 0xFF);

    // send start frame instantly:
    send_frame(1, 0, dri);
    dri = 0;
}

#ifdef NOTIFY_WRITE_BYTE
void rex_client::vm_notify_write_byte(uint8_t x) {}
#endif

void rex_client::vm_notify_write_end() {
    // send the final frame:
    send_frame(1, 1, dri);
    dri = 0;
}

void rex_client::vm_notify_wait_complete(uint32_t pc, iovm1_opcode o, uint8_t tdu, uint32_t addr, uint8_t x) {
    sbuf[dri++] = rex_notify_iovm_wait;
    // pc:
    sbuf[dri++] = pc & 0xFF;
    sbuf[dri++] = (pc >> 8) & 0xFF;
    sbuf[dri++] = (pc >> 16) & 0xFF;
    sbuf[dri++] = (pc >> 24) & 0xFF;
    // memory target:
    sbuf[dri++] = tdu;
    // 24-bit address:
    sbuf[dri++] = (addr & 0xFF);
    sbuf[dri++] = ((addr >> 8) & 0xFF);
    sbuf[dri++] = ((addr >> 16) & 0xFF);
    // last value read:
    sbuf[dri++] = x;

    // send final frame:
    send_frame(1, 1, dri);
    dri = 0;
}

///////////////////////////////////

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
        assert(len <= frame_size);
        memcpy(sbuf, p, len);
        send_frame(c, 1, len);
    }
}

bool rex_client::send_frame(uint8_t c, uint8_t f, uint8_t l) {
    ssize_t n;
    // relies on sx being immediately prior to sbuf in memory:
    sx = ((f & 1) << 7) | ((c & 1) << 6) | (l & 63);
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
            //  fcll llll   f = final frame of message
            //              c = channel (0..1)
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
        uint8_t f = (rx >> 7) & 1;
        uint8_t c = (rx >> 6) & 1;
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

void rex_client::recv_frame(uint8_t c, uint8_t f, uint8_t l, uint8_t buf[63]) {
    //printf("recv_frame[c=%d,f=%d]: %d bytes\n", c, f, l);

    // append buffer data to message for this channel:
    msgIn[c].insert(msgIn[c].end(), buf, buf + l);
    if (f) {
        // if final frame bit is set, process the entire message:
        recv_message(c, msgIn[c]);
        // clear out message for next:
        msgIn[c].clear();
    }
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
    rex_cmd_result result;
    auto cmd = static_cast<rex_cmd>(m.at(0));
    switch (cmd) {
        case rex_cmd_iovm_exec: // iovm: load & execute program
            if (m.size() <= 1 + 1) {
                send_message(0, {cmd, rex_msg_bad_request});
                fprintf(stderr, "message too short\n");
                return;
            }

            vmi.vm_init();
            result = vmi.vm_load(&m.at(1), m.size() - 1);
            send_message(0, {cmd, result});
            break;
        case rex_cmd_iovm_reset: // iovm: reset
            if (m.size() > 1) {
                send_message(0, {cmd, rex_msg_bad_request});
                fprintf(stderr, "message too long\n");
                return;
            }
            result = vmi.vm_reset();
            send_message(0, {cmd, result});
            break;
        case rex_cmd_iovm_getstate: // iovm: getstate
            if (m.size() > 1) {
                send_message(0, {cmd, rex_msg_bad_request});
                fprintf(stderr, "message too long\n");
                return;
            }
            send_message(0, {cmd, rex_success, static_cast<uint8_t>(vmi.vm_getstate())});
            break;
        case rex_cmd_ppux_exec: // ppux: load & execute program
            if (m.size() <= 1 + 4) {
                send_message(0, {cmd, rex_msg_bad_request});
                fprintf(stderr, "message too short\n");
                return;
            }
            if (((m.size() - 1) & 3) != 0) {
                send_message(0, {cmd, rex_msg_bad_request});
                fprintf(stderr, "message data size must be multiple of 4 bytes\n");
                return;
            }
            result = ppux.cmd_upload(
                (uint32_t *) &m.at(1),
                (m.size() - 1) / sizeof(uint32_t)
            );
            send_message(0, {cmd, result});
            break;
        case rex_cmd_ppux_vram_upload: { // ppux: vram upload
            // TODO: possibly stream in each frame instead of waiting for the complete message
            if (m.size() <= 1 + 4) {
                send_message(0, {cmd, rex_msg_bad_request});
                fprintf(stderr, "message too short\n");
                return;
            }
            auto p = m.cbegin() + 1;
            uint32_t addr = *p++;
            addr |= (uint32_t) *p++ << 8;
            addr |= (uint32_t) *p++ << 16;
            addr |= (uint32_t) *p++ << 24;
            // size is implicit in remaining length of message
            result = ppux.vram_upload(addr, &*p, m.cend() - p);
            send_message(0, {cmd, result});
            break;
        }
        case rex_cmd_ppux_cgram_upload: { // ppux: cgram upload
            // TODO: possibly stream in each frame instead of waiting for the complete message
            if (m.size() <= 1 + 4) {
                send_message(0, {cmd, rex_msg_bad_request});
                fprintf(stderr, "message too short\n");
                return;
            }
            auto p = m.cbegin() + 1;
            uint32_t addr = *p++;
            addr |= (uint32_t) *p++ << 8;
            addr |= (uint32_t) *p++ << 16;
            addr |= (uint32_t) *p++ << 24;
            // size is implicit in remaining length of message
            result = ppux.cgram_upload(addr, &*p, m.cend() - p);
            send_message(0, {cmd, result});
            break;
        }
        default:
            // discard unknown messages
            send_message(0, {cmd, rex_cmd_unknown});
            break;
    }
}
