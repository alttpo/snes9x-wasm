
#ifndef SNES9X_REX_PROTO_H
#define SNES9X_REX_PROTO_H

#include <cstdint>

enum rex_cmd : uint8_t {
    rex_cmd_iovm_exec,
    rex_cmd_iovm_reset,
    rex_cmd_iovm_getstate,
    rex_cmd_ppux_exec,
    rex_cmd_ppux_vram_upload,
    rex_cmd_ppux_cgram_upload
};

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

#endif //SNES9X_REX_PROTO_H
