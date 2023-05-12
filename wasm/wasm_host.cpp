
#include <cstdint>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

// WAMR:
#include "wasm_export.h"
#include "wasm_host.h"

// snes9x:
#include "snes9x.h"
#include "memmap.h"

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
    uint32 buf_offset;
    uint32 buf_len;
} iovec_app_t;

typedef std::vector<std::pair<uint8 *, uint32>> iovec;

// base interface for handling fd read/write ops:
class fd_inst {
public:
    explicit fd_inst(wasi_fd_t fd_p) : fd(fd_p) {}

    virtual ~fd_inst() = default;

    virtual wasi_errno_t read(const iovec &iov, uint32 &nread) { return WASI_ENOTSUP; }

    virtual wasi_errno_t write(const iovec &iov, uint32 &nwritten) { return WASI_ENOTSUP; }

    virtual wasi_errno_t pread(const iovec &iov, wasi_filesize_t offset, uint32 &nread) { return WASI_ENOTSUP; }

    virtual wasi_errno_t pwrite(const iovec &iov, wasi_filesize_t offset, uint32 &nwritten) { return WASI_ENOTSUP; }

protected:
    wasi_fd_t fd;
};

class fd_mem_array : public fd_inst {
public:
    explicit fd_mem_array(wasi_fd_t fd_p, uint8 *mem_p, uint32 size_p)
        : fd_inst(fd_p), mem(mem_p), size(size_p) {}

    wasi_errno_t pread(const iovec &iov, wasi_filesize_t offset, uint32 &nread) override {
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

    wasi_errno_t pwrite(const iovec &iov, wasi_filesize_t offset, uint32 &nwritten) override {
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

    uint8 *mem;
    uint32 size;
};

class fd_mem_vec : public fd_inst {
public:
    explicit fd_mem_vec(wasi_fd_t fd_p, std::vector<uint8> &mem_p)
        : fd_inst(fd_p), mem(mem_p) {}

    wasi_errno_t pread(const iovec &iov, wasi_filesize_t offset, uint32 &nread) override {
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

    wasi_errno_t pwrite(const iovec &iov, wasi_filesize_t offset, uint32 &nwritten) override {
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

    std::vector<uint8> &mem;
    uint32 size;
};

class fd_stdout : public fd_inst {
public:
    explicit fd_stdout(wasi_fd_t fd_p) : fd_inst(fd_p) {
    }

    wasi_errno_t write(const iovec &iov, uint32 &nwritten) override {
        nwritten = 0;
        for (const auto &item: iov) {
            fprintf(stdout, "%.*s", (int) item.second, item.first);
            nwritten += item.second;
        }
        return 0;
    }
};

// map of well-known absolute paths for virtual files:
std::unordered_map<std::string, std::function<std::shared_ptr<fd_inst>(wasi_fd_t fd)>>
    file_exact_providers
    {
        // console:
        {
            "/tmp/snes/mem/wram",
            [](wasi_fd_t fd) -> std::shared_ptr<fd_inst> {
                return std::make_shared<fd_mem_array>(fd, Memory.RAM, sizeof(Memory.RAM));
            }
        },
        {
            "/tmp/snes/mem/vram",
            [](wasi_fd_t fd) -> std::shared_ptr<fd_inst> {
                return std::make_shared<fd_mem_array>(fd, Memory.VRAM, sizeof(Memory.VRAM));
            }
        },
        // cart:
        {
            "/tmp/snes/mem/rom",
            [](wasi_fd_t fd) -> std::shared_ptr<fd_inst> {
                return std::make_shared<fd_mem_vec>(fd, Memory.ROMStorage);
            }
        },
        {
            "/tmp/snes/mem/sram",
            [](wasi_fd_t fd) -> std::shared_ptr<fd_inst> {
                return std::make_shared<fd_mem_vec>(fd, Memory.SRAMStorage);
            }
        }
    };

class module {
public:
    module(std::string name_p, wasm_module_t mod_p, wasm_module_inst_t mi_p)
        : name(std::move(name_p)), mod(mod_p), module_inst(mi_p) {
        // TODO: move this to a static construct function returning optional<T>:
        exec_env = wasm_runtime_create_exec_env(module_inst, 1048576);
        if (!exec_env) {
            wasm_runtime_deinstantiate(module_inst);
            module_inst = nullptr;
            wasm_runtime_unload(mod);
            mod = nullptr;
            return;
        }

        // set user_data to `this`:
        wasm_runtime_set_user_data(exec_env, static_cast<void *>(this));

        // hook up stdout:
        fds.insert_or_assign(1, std::make_shared<fd_stdout>(1));
    }

    ~module() {
        printf("wasm [%s] module dtor\n", name.c_str());

        wasm_runtime_destroy_exec_env(exec_env);
        wasm_runtime_deinstantiate(module_inst);
        module_inst = nullptr;
        wasm_runtime_unload(mod);
        mod = nullptr;
    }

    void runMain() {
        WASMFunctionInstanceCommon *func;

#if WASM_ENABLE_LIBC_WASI != 0
        /* In wasi mode, we should call the function named "_start"
           which initializes the wasi envrionment and then calls
           the actual main function. Directly calling main function
           may cause exception thrown. */
        if ((func = wasm_runtime_lookup_wasi_start_function(module_inst))) {
            if (!wasm_runtime_call_wasm(exec_env, func, 0, nullptr)) {
                printf("wasm_runtime_call_wasm('_start'): %s\n", wasm_runtime_get_exception(module_inst));
            }
            return;
        }
#endif /* end of WASM_ENABLE_LIBC_WASI */

        if (!(func = wasm_runtime_lookup_function(module_inst, "main", nullptr))
            && !(func = wasm_runtime_lookup_function(module_inst, "__main_argc_argv", nullptr))
            && !(func = wasm_runtime_lookup_function(module_inst, "_main", nullptr))) {
#if WASM_ENABLE_LIBC_WASI != 0
            wasm_runtime_set_exception(
                module_inst,
                "lookup the entry point symbol (like _start, main, "
                "_main, __main_argc_argv) failed"
            );
#else
            wasm_runtime_set_exception(
                module_inst,
                "lookup the entry point symbol (like main, "
                "_main, __main_argc_argv) failed"
            );
#endif
        }
    }

    void start() {
        //pthread_setname_np("wasm");
        printf("wasm [%s] module thread started\n", name.c_str());

        wasm_runtime_init_thread_env();
        runMain();
        wasm_runtime_destroy_thread_env();

        printf("wasm [%s] module thread exited\n", name.c_str());
    }

public:
    wasi_errno_t path_open(
        wasi_fd_t dirfd,
        wasi_lookupflags_t dirflags, const char *path, uint32 path_len,
        wasi_oflags_t oflags, wasi_rights_t fs_rights_base,
        wasi_rights_t fs_rights_inheriting, wasi_fdflags_t fs_flags,
        wasi_fd_t *fd_app
    ) {
        std::string lpath(path, path + path_len);

        // special case for absolute paths:
        if (dirfd == (uint32) -1) {
            lpath.insert(0, "/");
        }

        // we only know how to open a limited set of virtual files identified by absolute paths,
        // similar to the linux procfs:
        auto it = file_exact_providers.find(lpath);
        if (it == file_exact_providers.end()) {
            return WASI_ENOENT;
        }

        // always succeed and create a new fd:
        auto fd = fd_free;

        fds.insert_or_assign(fd, (it->second)(fd));
        *fd_app = fd;

        // find the next free fd:
        while (fds.find(++fd_free) != fds.end()) {}

        return 0;
    }

    wasi_errno_t fd_close(wasi_fd_t fd) {
        auto it = fds.find(fd);
        if (it == fds.end())
            return WASI_EBADF;

        fds.erase(it);

        // track last freed fd for a quick path_open:
        fd_free = fd;

        return 0;
    }

    wasi_errno_t fd_read(wasi_fd_t fd, const iovec_app_t *iovec_app, uint32 iovs_len, uint32 *nread_app) {
        // find the fd:
        auto it = fds.find(fd);
        if (it == fds.end())
            return WASI_EBADF;

        // construct a vector of vector<uint8>s pointing to wasm memory:
        iovec io = create_iovec(iovec_app, iovs_len);

        // call the fd implementation:
        return it->second->read(io, *nread_app);
    }

    wasi_errno_t fd_write(wasi_fd_t fd, const iovec_app_t *iovec_app, uint32 iovs_len, uint32 *nwritten_app) {
        // find the fd:
        auto it = fds.find(fd);
        if (it == fds.end())
            return WASI_EBADF;

        // construct a vector of vector<uint8>s pointing to wasm memory:
        iovec io = create_iovec(iovec_app, iovs_len);

        // call the fd implementation:
        return it->second->write(io, *nwritten_app);
    }

    wasi_errno_t
    fd_pread(wasi_fd_t fd, const iovec_app_t *iovec_app, uint32 iovs_len, wasi_filesize_t offset, uint32 *nread_app) {
        // find the fd:
        auto it = fds.find(fd);
        if (it == fds.end())
            return WASI_EBADF;

        // construct a vector of vector<uint8>s pointing to wasm memory:
        iovec io = create_iovec(iovec_app, iovs_len);

        // call the fd implementation:
        return it->second->pread(io, offset, *nread_app);
    }

    wasi_errno_t fd_pwrite(wasi_fd_t fd, const iovec_app_t *iovec_app, uint32 iovs_len, wasi_filesize_t offset,
                           uint32 *nwritten_app) {
        // find the fd:
        auto it = fds.find(fd);
        if (it == fds.end())
            return WASI_EBADF;

        // construct a vector of vector<uint8>s pointing to wasm memory:
        iovec io = create_iovec(iovec_app, iovs_len);

        // call the fd implementation:
        return it->second->pwrite(io, offset, *nwritten_app);
    }

private:
    iovec create_iovec(const iovec_app_t *iovec_app, uint32 iovs_len) {
        iovec io;
        io.reserve(iovs_len);
        const iovec_app_t *io_app = iovec_app;
        for (uint32 i = 0; i < iovs_len; i++, io_app++) {
            auto buf = ((uint8 *) addr_app_to_native(io_app->buf_offset));
            io.emplace_back(std::make_pair(buf, io_app->buf_len));
        }
        return io;
    }

private:
    std::string name;
    wasm_module_t mod;
    wasm_module_inst_t module_inst;
    wasm_exec_env_t exec_env;

    std::unordered_map<wasi_fd_t, std::shared_ptr<fd_inst>> fds;
    wasi_fd_t fd_free = 3;
};

std::vector<std::weak_ptr<module> > modules;

bool wasm_host_init() {
    // initialize wasm runtime
    RuntimeInitArgs init;

    init.mem_alloc_type = Alloc_With_Pool;
    init.mem_alloc_option.pool.heap_size = 1048576 * 64;
    init.mem_alloc_option.pool.heap_buf = new uint8_t[init.mem_alloc_option.pool.heap_size];

    init.running_mode = Mode_Interp;

    init.native_module_name = "snes";
    init.n_native_symbols = 0;
    init.native_symbols = {};

    if (!wasm_runtime_full_init(&init)) {
        printf("wasm_runtime_full_init failed\n");
        return false;
    }

    auto *wasi = new std::vector<NativeSymbol>();
    wasi->push_back({
        "path_open",
        (void *) (wasi_errno_t (*)(wasm_exec_env_t, wasi_fd_t dirfd,
                                   wasi_lookupflags_t dirflags, const char *path, uint32 path_len,
                                   wasi_oflags_t oflags, wasi_rights_t fs_rights_base,
                                   wasi_rights_t fs_rights_inheriting, wasi_fdflags_t fs_flags,
                                   wasi_fd_t *fd_app)) (
            [](wasm_exec_env_t exec_env,
               wasi_fd_t dirfd,
               wasi_lookupflags_t dirflags, const char *path, uint32 path_len,
               wasi_oflags_t oflags, wasi_rights_t fs_rights_base,
               wasi_rights_t fs_rights_inheriting, wasi_fdflags_t fs_flags,
               wasi_fd_t *fd_app
            ) -> wasi_errno_t {
                auto m = static_cast<module *>(wasm_runtime_get_user_data(exec_env));
                return m->path_open(dirfd, dirflags, path, path_len, oflags, fs_rights_base, fs_rights_inheriting,
                    fs_flags, fd_app);
            }
        ),
        "(ii*~iIIi*)i",
        nullptr
    });
    wasi->push_back({
        "fd_read",
        (void *) (wasi_errno_t (*)(wasm_exec_env_t, wasi_fd_t, const iovec_app_t *, uint32, uint32 *)) (
            [](wasm_exec_env_t exec_env,
               wasi_fd_t fd, const iovec_app_t *iovec_app, uint32 iovs_len, uint32 *nread_app
            ) -> wasi_errno_t {
                auto m = static_cast<module *>(wasm_runtime_get_user_data(exec_env));
                return m->fd_read(fd, iovec_app, iovs_len, nread_app);
            }
        ),
        "(i*i*)i",
        nullptr
    });
    wasi->push_back({
        "fd_write",
        (void *) (wasi_errno_t (*)(wasm_exec_env_t, wasi_fd_t, const iovec_app_t *, uint32, uint32 *)) (
            [](wasm_exec_env_t exec_env,
               wasi_fd_t fd, const iovec_app_t *iovec_app, uint32 iovs_len, uint32 *nwritten_app
            ) -> wasi_errno_t {
                auto m = static_cast<module *>(wasm_runtime_get_user_data(exec_env));
                return m->fd_write(fd, iovec_app, iovs_len, nwritten_app);
            }
        ),
        "(i*i*)i",
        nullptr
    });
    wasi->push_back({
        "fd_pread",
        (void *) (wasi_errno_t (*)(wasm_exec_env_t, wasi_fd_t, const iovec_app_t *, uint32, wasi_filesize_t,
                                   uint32 *)) (
            [](wasm_exec_env_t exec_env,
               wasi_fd_t fd, const iovec_app_t *iovec_app, uint32 iovs_len, wasi_filesize_t offset, uint32 *nread_app
            ) -> wasi_errno_t {
                auto m = static_cast<module *>(wasm_runtime_get_user_data(exec_env));
                return m->fd_pread(fd, iovec_app, iovs_len, offset, nread_app);
            }
        ),
        "(i*iI*)i",
        nullptr
    });
    wasi->push_back({
        "fd_pwrite",
        (void *) (wasi_errno_t (*)(wasm_exec_env_t, wasi_fd_t, const iovec_app_t *, uint32, wasi_filesize_t,
                                   uint32 *)) (
            [](wasm_exec_env_t exec_env,
               wasi_fd_t fd, const iovec_app_t *iovec_app, uint32 iovs_len, wasi_filesize_t offset, uint32 *nwritten_app
            ) -> wasi_errno_t {
                auto m = static_cast<module *>(wasm_runtime_get_user_data(exec_env));
                return m->fd_pwrite(fd, iovec_app, iovs_len, offset, nwritten_app);
            }
        ),
        "(i*iI*)i",
        nullptr
    });
    wasi->push_back({
        "fd_close",
        (void *) (wasi_errno_t (*)(wasm_exec_env_t exec_env, wasi_fd_t fd)) (
            [](wasm_exec_env_t exec_env, wasi_fd_t fd) -> wasi_errno_t {
                auto m = static_cast<module *>(wasm_runtime_get_user_data(exec_env));
                return m->fd_close(fd);
            }
        ),
        "(i)i",
        nullptr
    });

    if (!wasm_runtime_register_natives(
        "wasi_snapshot_preview1",
        wasi->data(),
        wasi->size()
    )) {
        printf("wasm_runtime_full_init failed\n");
        return false;
    }

    // TODO: handle leaky `wasi` instance; delete on shutdown

    return true;
}

bool wasm_host_load_module(const std::string &name, uint8_t *module_binary, uint32_t module_size) {
    char wamrError[1024];

    wasm_module_t mod = wasm_runtime_load(
        module_binary,
        module_size,
        wamrError,
        sizeof(wamrError)
    );
    if (!mod) {
        printf(
            "wasm_runtime_load: %s\n",
            wamrError
        );
        return false;
    }

    wasm_module_inst_t mi = wasm_runtime_instantiate(
        mod,
        32768,
        32768,
        wamrError,
        sizeof(wamrError)
    );
    if (!mi) {
        wasm_runtime_unload(mod);
        printf(
            "wasm_runtime_instantiate: %s\n",
            wamrError
        );
        return false;
    }

    // track the new module:
    const std::shared_ptr<module> &m = std::make_shared<module>(name, mod, mi);
    modules.emplace_back(m);

    // create a dedicated thread to run the wasm main:
    auto t = std::thread(
        [](std::shared_ptr<module> *sm) {
            // transfer shared_ptr to a local:
            std::shared_ptr<module> m(*sm);
            delete sm;

            // start wasm:
            m->start();

            // clean up:
            m.reset();
        },
        new std::shared_ptr<module>(m)
    );
    t.detach();

    return true;
}

void wasm_host_notify_nmi() {
    // TODO
    return;
}
