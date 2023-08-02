
#include <utility>

#include "rex.h"
#include "rex_proto.h"

rex_client::rex_client(sock_sp s_p) :
    s(std::move(s_p)),
    vmi(static_cast<vm_notifier *>(this))
{
    frame_incoming_init(
        &fi,
        (void *)this,
        +[](struct frame_incoming *fr, uint8_t *buf, long size, long *n) {
            auto self = static_cast<rex_client *>(fr->opaque);
            return self->s->recv(buf, size, *n);
        },
        +[](struct frame_incoming *fr, uint8_t *buf, uint8_t len, uint8_t chn, bool fin) {
            auto self = static_cast<rex_client *>(fr->opaque);
            self->recv_frame(buf, len, chn, fin);
        }
    );

    frame_outgoing_init(
        &fo[0],
        (void *)this,
        +[](struct frame_outgoing *fr, uint8_t *buf, size_t size, long *n) {
            auto self = static_cast<rex_client *>(fr->opaque);
            return self->s->send(buf, size, *n);
        }
    );
    frame_outgoing_init(
        &fo[1],
        (void *)this,
        +[](struct frame_outgoing *fr, uint8_t *buf, size_t size, long *n) {
            auto self = static_cast<rex_client *>(fr->opaque);
            return self->s->send(buf, size, *n);
        }
    );
}

void rex_client::on_pc(uint32_t pc) {
    vmi.on_pc(pc);
}

///////////////////////////////////

void rex_client::vm_notify_ended() {
    // vm_ended message type:
    frame_outgoing_append(&fo[1], rex_notify_iovm_end);

    frame_outgoing_send(&fo[1], 1, true);
}

void rex_client::vm_notify_fail(uint32_t pc, iovm1_opcode o, rex_cmd_result result) {
    // vm_read_complete message type:
    *fo[1].p++ = rex_notify_iovm_opcode_fail;
    *fo[1].p++ = rex_notify_iovm_opcode_fail;
    // pc:
    *fo[1].p++ = pc & 0xFF;
    *fo[1].p++ = (pc >> 8) & 0xFF;
    *fo[1].p++ = (pc >> 16) & 0xFF;
    *fo[1].p++ = (pc >> 24) & 0xFF;
    // opcode:
    *fo[1].p++ = o;
    // result:
    *fo[1].p++ = result;

    frame_outgoing_send(&fo[1], 1, true);
}

void rex_client::vm_notify_read_start(uint32_t pc, uint8_t tdu, uint32_t addr, uint32_t len) {
    // vm_read_complete message type:
    *fo[1].p++ = rex_notify_iovm_read;
    // pc:
    *fo[1].p++ = pc & 0xFF;
    *fo[1].p++ = (pc >> 8) & 0xFF;
    *fo[1].p++ = (pc >> 16) & 0xFF;
    *fo[1].p++ = (pc >> 24) & 0xFF;
    // memory target:
    *fo[1].p++ = tdu;
    // 24-bit address:
    *fo[1].p++ = (addr & 0xFF);
    *fo[1].p++ = ((addr >> 8) & 0xFF);
    *fo[1].p++ = ((addr >> 16) & 0xFF);
    // 16-bit length (0 -> 65536, else 1..65535):
    uint16_t elen;
    if (len == 65536) {
        elen = 0;
    } else {
        elen = len;
    }
    *fo[1].p++ = (elen & 0xFF);
    *fo[1].p++ = ((elen >> 8) & 0xFF);

    // send start frame instantly:
    frame_outgoing_send(&fo[1], 1, false);
}

void rex_client::vm_notify_read_byte(uint8_t x) {
    *fo[1].p++ = x;
    if (frame_outgoing_len(&fo[1]) >= 63) {
        frame_outgoing_send(&fo[1], 1, false);
    }
}

void rex_client::vm_notify_read_end() {
    // send the final frame:
    frame_outgoing_send(&fo[1], 1, true);
}

void rex_client::vm_notify_write_start(uint32_t pc, uint8_t tdu, uint32_t addr, uint32_t len) {
    // vm_read_complete message type:
    *fo[1].p++ = rex_notify_iovm_write;
    // pc:
    *fo[1].p++ = pc & 0xFF;
    *fo[1].p++ = (pc >> 8) & 0xFF;
    *fo[1].p++ = (pc >> 16) & 0xFF;
    *fo[1].p++ = (pc >> 24) & 0xFF;
    // memory target:
    *fo[1].p++ = tdu;
    // 24-bit address:
    *fo[1].p++ = (addr & 0xFF);
    *fo[1].p++ = ((addr >> 8) & 0xFF);
    *fo[1].p++ = ((addr >> 16) & 0xFF);
    // 16-bit length (0 -> 65536, else 1..65535):
    uint16_t elen;
    if (len == 65536) {
        elen = 0;
    } else {
        elen = len;
    }
    *fo[1].p++ = (elen & 0xFF);
    *fo[1].p++ = ((elen >> 8) & 0xFF);

    // send start frame instantly:
    frame_outgoing_send(&fo[1], 1, false);
}

#ifdef NOTIFY_WRITE_BYTE
void rex_client::vm_notify_write_byte(uint8_t x) {}
#endif

void rex_client::vm_notify_write_end() {
    // send the final frame:
    frame_outgoing_send(&fo[1], 1, true);
}

void rex_client::vm_notify_wait_complete(uint32_t pc, iovm1_opcode o, uint8_t tdu, uint32_t addr, uint8_t x) {
    *fo[1].p++ = rex_notify_iovm_wait;
    // pc:
    *fo[1].p++ = pc & 0xFF;
    *fo[1].p++ = (pc >> 8) & 0xFF;
    *fo[1].p++ = (pc >> 16) & 0xFF;
    *fo[1].p++ = (pc >> 24) & 0xFF;
    // memory target:
    *fo[1].p++ = tdu;
    // 24-bit address:
    *fo[1].p++ = (addr & 0xFF);
    *fo[1].p++ = ((addr >> 8) & 0xFF);
    *fo[1].p++ = ((addr >> 16) & 0xFF);
    // last value read:
    *fo[1].p++ = x;

    // send final frame:
    frame_outgoing_send(&fo[1], 1, true);
}

///////////////////////////////////

void rex_client::send_message(uint8_t c, const v8 &msg) {
    size_t len = msg.size();
    const uint8_t *p = msg.data();

    // send non-final frames:
    const ssize_t frame_size = 63;
    while (len > frame_size) {
        frame_outgoing_append_bytes(&fo[c], p, frame_size);
        frame_outgoing_send(&fo[c], c, false);

        len -= frame_size;
        p += frame_size;
    }

    {
        // send final frame:
        assert(len <= frame_size);
        frame_outgoing_append_bytes(&fo[c], p, len);
        frame_outgoing_send(&fo[c], c, true);
    }
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

    return frame_incoming_read(&fi);
}

void rex_client::recv_frame(uint8_t buf[63], uint8_t len, uint8_t chn, uint8_t fin) {
    //printf("recv_frame[c=%d,f=%d]: %d bytes\n", c, f, l);

    // append buffer data to message for this channel:
    msgIn[chn].insert(msgIn[chn].end(), buf, buf + len);
    if (fin) {
        // if final frame bit is set, process the entire message:
        recv_message(chn, msgIn[chn]);
        // clear out message for next:
        msgIn[chn].clear();
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
