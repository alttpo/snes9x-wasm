
#include <utility>

#include "rex_client.h"
#include "rex_proto.h"

rex_client::rex_client(sock_sp s_p) :
    s(std::move(s_p)),
    vmi(static_cast<vm_notifier *>(this))
{
    frame64rd_init(
        &fi,
        (void *)this,
        +[](struct frame64rd *fr, uint8_t *buf, uint8_t len, uint8_t chn, bool fin) {
            auto self = static_cast<rex_client *>(fr->opaque);
            return self->recv_frame(buf, len, chn, fin);
        }
    );

    frame64wr_init(
        &fo[0],
        (void *) this,
        +[](struct frame64wr *fr, uint8_t *buf, size_t size, long *n) {
            auto self = static_cast<rex_client *>(fr->opaque);
            return self->s->send(buf, size, *n);
        }
    );
    frame64wr_init(
        &fo[1],
        (void *) this,
        +[](struct frame64wr *fr, uint8_t *buf, size_t size, long *n) {
            auto self = static_cast<rex_client *>(fr->opaque);
            return self->s->send(buf, size, *n);
        }
    );
}

void rex_client::inc_cycles(int32_t delta) {
    vmi.inc_cycles(delta);
}

void rex_client::on_pc(uint32_t pc) {
    if (!vm_running) {
        return;
    }

    vmi.on_pc(pc);
}

///////////////////////////////////

void rex_client::send_frame(uint8_t c, bool fin) {
    if (!frame64wr_send(&fo[c], c, fin)) {
        fprintf(stderr, "failed to send frame (err=%d): %s\n", s->error_num(), s->error_text().c_str());
        frame64wr_reset(&fo[c]);
    }
}

void rex_client::reply_byte(uint8_t c, bool fin, uint8_t b) {
    *fo[c].p++ = b;
    if (fin || (frame64wr_len(&fo[c]) >= 63)) {
        send_frame(c, fin);
    }
}

void rex_client::vm_notify_ended(uint32_t pc, iovm1_error result) {
    vm_running = false;

    // end message:
    reply_byte(0, false, 0xFF);
    // error code: (finalizes message)
    reply_byte(0, true, result);

    (void)pc;
}

void rex_client::vm_notify_read(uint32_t pc, uint8_t c, uint24_t a, uint8_t l_raw, uint8_t *d) {
    // read data message:
    reply_byte(0, false, 0xFE);
    // length:
    reply_byte(0, false, l_raw);

    (void)pc;
    (void)c;
    (void)a;

    int l = l_raw;
    if (l == 0) { l = 256; }
    while (l-- > 0) {
        reply_byte(0, false, *d++);
    }

    // always send the frame at the end of read-data:
    send_frame(0, false);
}

///////////////////////////////////

void rex_client::send_message(uint8_t c, const v8 &msg) {
    size_t len = msg.size();
    const uint8_t *p = msg.data();

    // send non-final frames:
    const size_t frame_size = 63;
    while (len > frame_size) {
        frame64wr_append_bytes(&fo[c], p, frame_size);
        send_frame(c, false);

        len -= frame_size;
        p += frame_size;
    }

    {
        // send final frame:
        assert(len <= frame_size);
        frame64wr_append_bytes(&fo[c], p, len);
        send_frame(c, true);
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

    // read more data into the frame's buffer:
    long n;
    if (!s->recv(frame64rd_read_dest(&fi), frame64rd_read_size(&fi), n)) {
        return false;
    }
    if (n <= 0) {
        return false;
    }
    frame64rd_read_complete(&fi, n);

    // parse what was just read in:
    return frame64rd_parse(&fi);
}

bool rex_client::recv_frame(uint8_t buf[63], uint8_t len, uint8_t chn, uint8_t fin) {
    //printf("recv_frame[c=%d,f=%d]: %d bytes\n", c, f, l);

    // append buffer data to message for this channel:
    msgIn[chn].insert(msgIn[chn].end(), buf, buf + len);
    if (fin) {
        // if final frame bit is set, process the entire message:
        recv_message(chn, msgIn[chn]);
        // clear out message for next:
        msgIn[chn].clear();
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
    rex_cmd_result result = rex_success;
    iovm1_error vmerr;
    ppux_error ppuxerr;
    auto cmd = static_cast<rex_cmd>(m.at(0));
    auto p = m.cbegin() + 1;
    unsigned long size = m.size() - 1;
    switch (cmd) {
        // TODO: command to fetch ROM details: filename, hash, etc.

        case rex_cmd_iovm_exec: { // iovm: load and execute program
            if (size < 1) {
                send_message(0, {cmd, rex_msg_too_short});
                fprintf(stderr, "rex: incoming message too short\n");
                return;
            }

            vmi.vm_init();

            vmerr = vmi.vm_load(&*p, m.cend() - p);
            if (vmerr != IOVM1_SUCCESS) {
                result = rex_cmd_error;
            } else {
                vmerr = vmi.vm_reset();
                if (vmerr != IOVM1_SUCCESS) {
                    result = rex_cmd_error;
                }
            }

            // start the reply:
            reply_byte(0, false, cmd);
            reply_byte(0, false, result);

            vm_running = (vmerr == IOVM1_SUCCESS);
            if (!vm_running) {
                vm_notify_ended(0, vmerr);
            } else {
                // send the first frame of response then wait for read or end:
                send_frame(0, false);
            }
            break;
        }

        case rex_cmd_ppux_cmd_upload: // ppux: load & execute program
            if (size <= 4) {
                send_message(0, {cmd, rex_msg_too_short});
                fprintf(stderr, "message too short\n");
                return;
            }
            if ((size & 3) != 0) {
                send_message(0, {cmd, rex_msg_too_short});
                fprintf(stderr, "message data size must be multiple of 4 bytes\n");
                return;
            }
            ppuxerr = ppux.cmd_upload(
                (uint32_t *) &*p,
                size / sizeof(uint32_t)
            );
            if (ppuxerr != PPUX_SUCCESS) {
                result = rex_cmd_error;
            }
            send_message(0, {cmd, result, static_cast<uint8_t>(ppuxerr)});
            break;
        case rex_cmd_ppux_vram_upload: { // ppux: vram upload
            // TODO: possibly stream in each frame instead of waiting for the complete message
            if (size <= 4) {
                send_message(0, {cmd, rex_msg_too_short});
                fprintf(stderr, "message too short\n");
                return;
            }
            uint32_t addr = *p++;
            addr |= (uint32_t) *p++ << 8;
            addr |= (uint32_t) *p++ << 16;
            addr |= (uint32_t) *p++ << 24;
            // size is implicit in remaining length of message
            ppuxerr = ppux.vram_upload(addr, &*p, m.cend() - p);
            if (ppuxerr != PPUX_SUCCESS) {
                result = rex_cmd_error;
            }
            send_message(0, {cmd, result, static_cast<uint8_t>(ppuxerr)});
            break;
        }
        case rex_cmd_ppux_cgram_upload: { // ppux: cgram upload
            // TODO: possibly stream in each frame instead of waiting for the complete message
            if (size <= 4) {
                send_message(0, {cmd, rex_msg_too_short});
                fprintf(stderr, "message too short\n");
                return;
            }
            uint32_t addr = *p++;
            addr |= (uint32_t) *p++ << 8;
            addr |= (uint32_t) *p++ << 16;
            addr |= (uint32_t) *p++ << 24;
            // size is implicit in remaining length of message
            ppuxerr = ppux.cgram_upload(addr, &*p, m.cend() - p);
            if (ppuxerr != PPUX_SUCCESS) {
                result = rex_cmd_error;
            }
            send_message(0, {cmd, result, static_cast<uint8_t>(ppuxerr)});
            break;
        }

        case rex_cmd_rom_info: // get rom info
            // TODO
            //Memory.ROMName;
            send_message(0, {cmd, result, });
            break;

        default:
            // discard unknown messages
            send_message(0, {cmd, rex_cmd_unknown});
            break;
    }
}
