
#ifndef SNES9X_WASM_VFS_H
#define SNES9X_WASM_VFS_H

#include "wasi_types.h"

#include <mutex>

class module;

// base interface for handling fd read/write ops:
class fd_inst {
public:
    explicit fd_inst(wasi_fd_t fd_p);

    virtual ~fd_inst() = default;

    virtual wasi_errno_t close();

    virtual wasi_errno_t read(const iovec &iov, uint32_t &nread);

    virtual wasi_errno_t write(const iovec &iov, uint32_t &nwritten);

    virtual wasi_errno_t pread(const iovec &iov, wasi_filesize_t offset, uint32_t &nread);

    virtual wasi_errno_t pwrite(const iovec &iov, wasi_filesize_t offset, uint32_t &nwritten);

protected:
    wasi_fd_t fd;
};

class fd_mem_array : public fd_inst {
public:
    explicit fd_mem_array(wasi_fd_t fd_p, uint8_t *mem_p, uint32_t size_p);

    wasi_errno_t pread(const iovec &iov, wasi_filesize_t offset, uint32_t &nread) override;

    wasi_errno_t pwrite(const iovec &iov, wasi_filesize_t offset, uint32_t &nwritten) override;

    uint8_t *mem;
    uint32_t size;
};

class fd_mem_vec : public fd_inst {
public:
    explicit fd_mem_vec(wasi_fd_t fd_p, std::vector<uint8_t> &mem_p);

    wasi_errno_t pread(const iovec &iov, wasi_filesize_t offset, uint32_t &nread) override;

    wasi_errno_t pwrite(const iovec &iov, wasi_filesize_t offset, uint32_t &nwritten) override;

    std::vector<uint8_t> &mem;
};

class fd_nmi_blocking : public fd_inst {
public:
    explicit fd_nmi_blocking(std::weak_ptr<module> m_p, wasi_fd_t fd_p);

    wasi_errno_t read(const iovec &iov, uint32_t &nread) override;

    std::weak_ptr<module> m_w;
};

class fd_file_out : public fd_inst {
public:
    explicit fd_file_out(wasi_fd_t fd_p, FILE *fout_p);

    wasi_errno_t write(const iovec &iov, uint32_t &nwritten) override;

    FILE *fout;
};

class fd_ppux : public fd_inst {
public:
    explicit fd_ppux(std::weak_ptr<module> m_p, int layer_p, wasi_fd_t fd_p);

    wasi_errno_t pwrite(const iovec &iov, wasi_filesize_t offset, uint32_t &nwritten) override;

    std::weak_ptr<module> m_w;
    int layer;
};

// map of well-known absolute paths for virtual files:
extern std::unordered_map<
    std::string,
    std::function<std::shared_ptr<fd_inst>(std::weak_ptr<module>, std::string, wasi_fd_t)>
> file_exact_providers;

#endif //SNES9X_WASM_VFS_H
