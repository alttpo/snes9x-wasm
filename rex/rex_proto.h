
#ifndef SNES9X_REX_PROTO_H
#define SNES9X_REX_PROTO_H

#include <cstdint>

enum rex_cmd : uint8_t {
    // command request type on channel 0 (incoming):
    rex_cmd_iovm_exec,
    rex_cmd_iovm_stop,

    rex_cmd_ppux_cmd_upload = 16,
    rex_cmd_ppux_vram_upload,
    rex_cmd_ppux_cgram_upload
};

enum rex_rsp : uint8_t {
    // command response type on channel 0 (outgoing):
    rex_rsp_iovm_exec,
    rex_rsp_iovm_stop,

    rex_rsp_ppux_cmd_upload = 16,
    rex_rsp_ppux_vram_upload,
    rex_rsp_ppux_cgram_upload,

    // response notification type on channel 1 (outgoing):
    rex_notify_iovm_end = 0x80,
    rex_notify_iovm_read,
};

enum rex_cmd_result : uint8_t {
    rex_success,
    rex_msg_too_short,
    rex_cmd_unknown,
    rex_cmd_error,
};

#endif //SNES9X_REX_PROTO_H
