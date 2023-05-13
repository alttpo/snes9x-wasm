
#include "wasm_module.h"
#include "wasm_vfs.h"

#include <utility>

// snes9x:
#include "snes9x.h"
#include "memmap.h"

extern std::mutex nmi_cv_m;
extern std::condition_variable nmi_cv;

// base interface for handling fd read/write ops:
fd_inst::fd_inst(wasi_fd_t fd_p) : fd(fd_p) {}

wasi_errno_t fd_inst::read(const iovec &iov, uint32 &nread) { return WASI_ENOTSUP; }

wasi_errno_t fd_inst::write(const iovec &iov, uint32 &nwritten) { return WASI_ENOTSUP; }

wasi_errno_t fd_inst::pread(const iovec &iov, wasi_filesize_t offset, uint32 &nread) { return WASI_ENOTSUP; }

wasi_errno_t fd_inst::pwrite(const iovec &iov, wasi_filesize_t offset, uint32 &nwritten) { return WASI_ENOTSUP; }

fd_mem_array::fd_mem_array(wasi_fd_t fd_p, uint8 *mem_p, uint32 size_p)
    : fd_inst(fd_p), mem(mem_p), size(size_p) {}

wasi_errno_t fd_mem_array::pread(const iovec &iov, wasi_filesize_t offset, uint32 &nread) {
    nread = 0;
    for (auto &item: iov) {
        if (offset + item.second > size) {
            return WASI_EINVAL;
        }
        memcpy((void *) item.first, &(mem)[offset], item.second);
        offset += item.second;
        nread += item.second;
    }
    return 0;
}

wasi_errno_t fd_mem_array::pwrite(const iovec &iov, wasi_filesize_t offset, uint32 &nwritten) {
    nwritten = 0;
    for (auto &item: iov) {
        if (offset + item.second > size) {
            return WASI_EINVAL;
        }
        memcpy(&(mem)[offset], (void *) item.first, item.second);
        offset += item.second;
        nwritten += item.second;
    }
    return 0;
}

fd_mem_vec::fd_mem_vec(wasi_fd_t fd_p, std::vector<uint8> &mem_p)
    : fd_inst(fd_p), mem(mem_p) {}

wasi_errno_t fd_mem_vec::pread(const iovec &iov, wasi_filesize_t offset, uint32 &nread) {
    nread = 0;
    for (auto &item: iov) {
        if (offset + item.second > mem.size()) {
            return WASI_EINVAL;
        }
        memcpy((void *) item.first, &mem.at(offset), item.second);
        offset += item.second;
        nread += item.second;
    }
    return 0;
}

wasi_errno_t fd_mem_vec::pwrite(const iovec &iov, wasi_filesize_t offset, uint32 &nwritten) {
    nwritten = 0;
    for (auto &item: iov) {
        if (offset + item.second > mem.size()) {
            return WASI_EINVAL;
        }
        memcpy(&mem.at(offset), (void *) item.first, item.second);
        offset += item.second;
        nwritten += item.second;
    }
    return 0;
}

fd_nmi_blocking::fd_nmi_blocking(std::weak_ptr<module> m_p, wasi_fd_t fd_p) : fd_inst(fd_p), m_w(std::move(m_p)) {
}

wasi_errno_t fd_nmi_blocking::read(const iovec &iov, uint32 &nread) {
    auto m = m_w.lock();
    if (!m) {
        return EBADF;
    }

    // wait for NMI:
    auto status = m->wait_for_nmi();

    // translate the no_timeout vs timeout status to a 1/0 value for the wasm module to consume:
    uint8 report;
    if (status == std::cv_status::no_timeout) {
        report = 1;
    } else {
        report = 0;
    }

    nread = 0;
    for (const auto &item: iov) {
        item.first[0] = report;
        nread++;
    }

    return 0;
}


fd_file_out::fd_file_out(wasi_fd_t fd_p, FILE *fout_p) : fd_inst(fd_p), fout(fout_p) {
}

wasi_errno_t fd_file_out::write(const iovec &iov, uint32 &nwritten) {
    nwritten = 0;
    for (const auto &item: iov) {
        fprintf(fout, "%.*s", (int) item.second, item.first);
        nwritten += item.second;
    }
    return 0;
}

// map of well-known absolute paths for virtual files:
std::unordered_map<std::string, std::function<std::shared_ptr<fd_inst>(std::weak_ptr<module>, std::string, wasi_fd_t)>>
    file_exact_providers
    {
        // console memory:
        {
            "/tmp/snes/mem/wram",
            [](std::weak_ptr<module> m, std::string path, wasi_fd_t fd) -> std::shared_ptr<fd_inst> {
                return std::make_shared<fd_mem_array>(fd, Memory.RAM, sizeof(Memory.RAM));
            }
        },
        {
            "/tmp/snes/mem/vram",
            [](std::weak_ptr<module> m, std::string path, wasi_fd_t fd) -> std::shared_ptr<fd_inst> {
                return std::make_shared<fd_mem_array>(fd, Memory.VRAM, sizeof(Memory.VRAM));
            }
        },
        // console signals:
        {
            "/tmp/snes/sig/blocking/nmi",
            [](std::weak_ptr<module> m, std::string path, wasi_fd_t fd) -> std::shared_ptr<fd_inst> {
                return std::make_shared<fd_nmi_blocking>(m, fd);
            }
        },
        // cart:
        {
            "/tmp/snes/mem/rom",
            [](std::weak_ptr<module> m, std::string path, wasi_fd_t fd) -> std::shared_ptr<fd_inst> {
                return std::make_shared<fd_mem_vec>(fd, Memory.ROMStorage);
            }
        },
        {
            "/tmp/snes/mem/sram",
            [](std::weak_ptr<module> m, std::string path, wasi_fd_t fd) -> std::shared_ptr<fd_inst> {
                return std::make_shared<fd_mem_vec>(fd, Memory.SRAMStorage);
            }
        }
    };
