
#ifndef SNES9X_REX_PROTO_H
#define SNES9X_REX_PROTO_H

#include <cstdint>

enum rex_cmd : uint8_t {
    cmd_iovm_exec,
    cmd_iovm_reset,
    cmd_iovm_getstate,
    cmd_ppux_exec,
    cmd_ppux_vram_upload,
    cmd_ppux_cgram_upload
};

enum rex_cmd_result : uint8_t {
    res_success,
    res_msg_bad_request,
    res_cmd_bad_request,
    res_cmd_precondition_failed,
    res_cmd_unknown,
    res_cmd_error
};

#endif //SNES9X_REX_PROTO_H
