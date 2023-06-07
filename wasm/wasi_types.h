
#ifndef SNES9X_WASI_TYPES_H
#define SNES9X_WASI_TYPES_H

#include <cstdint>

#define WASI_ESUCCESS        (0)
#define WASI_E2BIG           (1)
#define WASI_EACCES          (2)
#define WASI_EADDRINUSE      (3)
#define WASI_EADDRNOTAVAIL   (4)
#define WASI_EAFNOSUPPORT    (5)
#define WASI_EAGAIN          (6)
#define WASI_EALREADY        (7)
#define WASI_EBADF           (8)
#define WASI_EBADMSG         (9)
#define WASI_EBUSY           (10)
#define WASI_ECANCELED       (11)
#define WASI_ECHILD          (12)
#define WASI_ECONNABORTED    (13)
#define WASI_ECONNREFUSED    (14)
#define WASI_ECONNRESET      (15)
#define WASI_EDEADLK         (16)
#define WASI_EDESTADDRREQ    (17)
#define WASI_EDOM            (18)
#define WASI_EDQUOT          (19)
#define WASI_EEXIST          (20)
#define WASI_EFAULT          (21)
#define WASI_EFBIG           (22)
#define WASI_EHOSTUNREACH    (23)
#define WASI_EIDRM           (24)
#define WASI_EILSEQ          (25)
#define WASI_EINPROGRESS     (26)
#define WASI_EINTR           (27)
#define WASI_EINVAL          (28)
#define WASI_EIO             (29)
#define WASI_EISCONN         (30)
#define WASI_EISDIR          (31)
#define WASI_ELOOP           (32)
#define WASI_EMFILE          (33)
#define WASI_EMLINK          (34)
#define WASI_EMSGSIZE        (35)
#define WASI_EMULTIHOP       (36)
#define WASI_ENAMETOOLONG    (37)
#define WASI_ENETDOWN        (38)
#define WASI_ENETRESET       (39)
#define WASI_ENETUNREACH     (40)
#define WASI_ENFILE          (41)
#define WASI_ENOBUFS         (42)
#define WASI_ENODEV          (43)
#define WASI_ENOENT          (44)
#define WASI_ENOEXEC         (45)
#define WASI_ENOLCK          (46)
#define WASI_ENOLINK         (47)
#define WASI_ENOMEM          (48)
#define WASI_ENOMSG          (49)
#define WASI_ENOPROTOOPT     (50)
#define WASI_ENOSPC          (51)
#define WASI_ENOSYS          (52)
#define WASI_ENOTCONN        (53)
#define WASI_ENOTDIR         (54)
#define WASI_ENOTEMPTY       (55)
#define WASI_ENOTRECOVERABLE (56)
#define WASI_ENOTSOCK        (57)
#define WASI_ENOTSUP         (58)
#define WASI_ENOTTY          (59)
#define WASI_ENXIO           (60)
#define WASI_EOVERFLOW       (61)
#define WASI_EOWNERDEAD      (62)
#define WASI_EPERM           (63)
#define WASI_EPIPE           (64)
#define WASI_EPROTO          (65)
#define WASI_EPROTONOSUPPORT (66)
#define WASI_EPROTOTYPE      (67)
#define WASI_ERANGE          (68)
#define WASI_EROFS           (69)
#define WASI_ESPIPE          (70)
#define WASI_ESRCH           (71)
#define WASI_ESTALE          (72)
#define WASI_ETIMEDOUT       (73)
#define WASI_ETXTBSY         (74)
#define WASI_EXDEV           (75)
#define WASI_ENOTCAPABLE     (76)


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

typedef std::vector<std::pair<uint8_t *, uint32_t>> wasi_iovec;

typedef uint8_t wasi_preopentype_t;
typedef struct wasi_prestat_app {
    wasi_preopentype_t pr_type;
    uint32_t pr_name_len;
} wasi_prestat_app_t;

typedef uint32_t wasi_clockid_t;
#define WASI_CLOCK_REALTIME           (0)
#define WASI_CLOCK_MONOTONIC          (1)
#define WASI_CLOCK_PROCESS_CPUTIME_ID (2)
#define WASI_CLOCK_THREAD_CPUTIME_ID  (3)

typedef uint64_t wasi_timestamp_t;

typedef uint32_t wasi_exitcode_t;

#endif //SNES9X_WASI_TYPES_H
