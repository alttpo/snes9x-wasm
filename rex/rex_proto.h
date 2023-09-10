
#ifndef SNES9X_REX_PROTO_H
#define SNES9X_REX_PROTO_H

#include <cstdint>

// command request type on channel 0 (incoming):
enum rex_cmd : uint8_t {
    rex_cmd_iovm_load,
    rex_cmd_iovm_start,
    rex_cmd_iovm_stop,
    rex_cmd_iovm_reset,
    rex_cmd_iovm_flags,
    rex_cmd_iovm_getstate,
    rex_cmd_ppux_cmd_upload = 16,
    rex_cmd_ppux_vram_upload,
    rex_cmd_ppux_cgram_upload
};

// response notification type on channel 1 (outgoing):
enum rex_notification : uint8_t {
    rex_notify_iovm_end,
    rex_notify_iovm_read,
    rex_notify_iovm_write,
    rex_notify_iovm_wait,
};

enum rex_cmd_result : uint8_t {
    rex_success,
    rex_msg_too_short,
    rex_cmd_unknown,
    rex_cmd_error,
};

enum rex_iovm_flags : uint8_t {
    rex_iovm_flag_notify_write = 1 << 0,
    rex_iovm_flag_notify_wait = 1 << 1,
};

#endif //SNES9X_REX_PROTO_H
