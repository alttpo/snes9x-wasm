
#include <utility>

#include "wasm_module.h"
#include "wasm_vfs.h"

// snes9x:
#include "snes9x.h"
#include "memmap.h"

// base interface for handling fd read/write ops:
fd_inst::fd_inst(wasi_fd_t fd_p) : fd(fd_p) {}

wasi_errno_t fd_inst::close() { return 0; }

wasi_errno_t fd_inst::read(const wasi_iovec &iov, uint32 &nread) { return WASI_ENOTSUP; }

wasi_errno_t fd_inst::write(const wasi_iovec &iov, uint32 &nwritten) { return WASI_ENOTSUP; }

wasi_errno_t fd_inst::pread(const wasi_iovec &iov, wasi_filesize_t offset, uint32 &nread) { return WASI_ENOTSUP; }

wasi_errno_t fd_inst::pwrite(const wasi_iovec &iov, wasi_filesize_t offset, uint32 &nwritten) { return WASI_ENOTSUP; }

fd_mem_array::fd_mem_array(wasi_fd_t fd_p, uint8 *mem_p, uint32 size_p)
    : fd_inst(fd_p), mem(mem_p), size(size_p) {}

wasi_errno_t fd_mem_array::pread(const wasi_iovec &iov, wasi_filesize_t offset, uint32 &nread) {
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

wasi_errno_t fd_mem_array::pwrite(const wasi_iovec &iov, wasi_filesize_t offset, uint32 &nwritten) {
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

wasi_errno_t fd_mem_vec::pread(const wasi_iovec &iov, wasi_filesize_t offset, uint32 &nread) {
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

wasi_errno_t fd_mem_vec::pwrite(const wasi_iovec &iov, wasi_filesize_t offset, uint32 &nwritten) {
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

fd_events::fd_events(std::weak_ptr<module> m_p, wasi_fd_t fd_p) : fd_inst(fd_p), m_w(std::move(m_p)) {
}

wasi_errno_t fd_events::read(const wasi_iovec &iov, uint32 &nread) {
    auto m = m_w.lock();
    if (!m) {
        return WASI_EBADF;
    }
    // only accept a single read target:
    if (iov.size() != 1) {
        return WASI_EINVAL;
    }

    // only allow reading a 4-byte uint32:
    auto &io = iov.at(0);
    if (io.second != 4) {
        return WASI_EINVAL;
    }

    // wait for events:
    m->wait_for_events(*((uint32_t *) io.first));
    nread = 4;

    return 0;
}

fd_file_out::fd_file_out(wasi_fd_t fd_p, FILE *fout_p) : fd_inst(fd_p), fout(fout_p) {
}

wasi_errno_t fd_file_out::write(const wasi_iovec &iov, uint32 &nwritten) {
    nwritten = 0;
    for (const auto &item: iov) {
        fprintf(fout, "%.*s", (int) item.second, item.first);
        nwritten += item.second;
    }
    return 0;
}

// map of well-known absolute paths for virtual files:
std::unordered_map<std::string, std::function<std::shared_ptr<fd_inst>(std::shared_ptr<module>, std::string, wasi_fd_t)>>
    file_exact_providers
    {
        // console memory:
        {
            "/tmp/snes/mem/wram",
            [](std::shared_ptr<module> m, std::string path, wasi_fd_t fd) -> std::shared_ptr<fd_inst> {
                return std::make_shared<fd_mem_array>(fd, Memory.RAM, sizeof(Memory.RAM));
            }
        },
        {
            "/tmp/snes/mem/vram",
            [](std::shared_ptr<module> m, std::string path, wasi_fd_t fd) -> std::shared_ptr<fd_inst> {
                return std::make_shared<fd_mem_array>(fd, Memory.VRAM, sizeof(Memory.VRAM));
            }
        },
        // cart:
        {
            "/tmp/snes/mem/rom",
            [](std::shared_ptr<module> m, std::string path, wasi_fd_t fd) -> std::shared_ptr<fd_inst> {
                return std::make_shared<fd_mem_vec>(fd, Memory.ROMStorage);
            }
        },
        {
            "/tmp/snes/mem/sram",
            [](std::shared_ptr<module> m, std::string path, wasi_fd_t fd) -> std::shared_ptr<fd_inst> {
                return std::make_shared<fd_mem_vec>(fd, Memory.SRAMStorage);
            }
        },
        // event subsystem:
        {
            "/tmp/events",
            [](std::shared_ptr<module> m, std::string path, wasi_fd_t fd) -> std::shared_ptr<fd_inst> {
                return std::make_shared<fd_events>(m, fd);
            }
        },
        // network inbox/outbox subsystem:
        {
            "/tmp/net",
            [](std::shared_ptr<module> m, std::string path, wasi_fd_t fd) -> std::shared_ptr<fd_inst> {
                return std::make_shared<fd_net>(m->net, fd);
            }
        },
        // console ppux rendering extensions:
        {
            "/tmp/snes/ppux/cmd",
            [](std::shared_ptr<module> m, std::string path, wasi_fd_t fd) -> std::shared_ptr<fd_inst> {
                return std::make_shared<fd_ppux_cmd>(m, fd);
            }
        },
    };
