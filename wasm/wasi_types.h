
#ifndef SNES9X_WASI_TYPES_H
#define SNES9X_WASI_TYPES_H

#include <cstdint>

#define WASI_EBADF           (8)
#define WASI_EINVAL          (28)
#define WASI_ENOENT          (44)
#define WASI_ENOTSUP         (58)

typedef uint16_t wasi_errno_t;
typedef uint32_t wasi_fd_t;
typedef uint64_t wasi_filesize_t;
typedef uint32_t wasi_lookupflags_t;
typedef uint16_t wasi_oflags_t;
typedef uint64_t wasi_rights_t;
typedef uint16_t wasi_fdflags_t;

typedef struct iovec_app {
    uint32_t buf_offset;
    uint32_t buf_len;
} iovec_app_t;

typedef std::vector<std::pair<uint8_t *, uint32_t>> iovec;

#endif //SNES9X_WASI_TYPES_H
