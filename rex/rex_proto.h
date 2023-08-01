
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
    rex_notify_iovm_opcode_fail,
    rex_notify_iovm_read,
    rex_notify_iovm_write,
    rex_notify_iovm_wait,
};

enum rex_cmd_result : uint8_t {
    rex_success,
    rex_msg_bad_request,
    rex_cmd_unknown,
    rex_iovm_internal_error,
    rex_iovm_memory_target_undefined,
    rex_iovm_memory_target_not_readable,
    rex_iovm_memory_target_not_writable,
    rex_iovm_memory_target_address_out_of_range,
    rex_ppux_invalid_opcode,
    rex_ppux_missing_end,
    rex_ppux_address_out_of_range,
};

#endif //SNES9X_REX_PROTO_H
