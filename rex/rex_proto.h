
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
    rex_cmd_ppux_exec = 16,
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
    rex_iovm_flag_notify_write_start = 1 << 0,
    rex_iovm_flag_notify_write_byte = 1 << 1,
    rex_iovm_flag_notify_write_end = 1 << 2,
    rex_iovm_flag_notify_wait_complete = 1 << 3,
    rex_iovm_flag_notify_error = 1 << 4,
    rex_iovm_flag_notify_end = 1 << 5,
    rex_iovm_flag_auto_restart_on_error = 1 << 6,
    rex_iovm_flag_auto_restart_on_end = 1 << 7
};

#endif //SNES9X_REX_PROTO_H
