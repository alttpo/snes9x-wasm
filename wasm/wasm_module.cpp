
#include "wasm_module.h"

#include <utility>
#include "wasm_vfs.h"

module::module(std::string name_p, wasm_module_t mod_p, wasm_module_inst_t mi_p, wasm_exec_env_t exec_env_p)
    : name(std::move(name_p)), mod(mod_p), module_inst(mi_p), exec_env(exec_env_p), ppux() {
    // set user_data to `this`:
    wasm_runtime_set_user_data(exec_env, static_cast<void *>(this));

    // hook up stdout and stderr:
    fds.insert_or_assign(1, std::make_shared<fd_file_out>(1, stdout));
    fds.insert_or_assign(2, std::make_shared<fd_file_out>(2, stderr));
}

module::~module() {
    //printf("wasm [%s] module dtor\n", name.c_str());

    wasm_runtime_destroy_exec_env(exec_env);
    wasm_runtime_deinstantiate(module_inst);
    module_inst = nullptr;
    wasm_runtime_unload(mod);
    mod = nullptr;
}

[[nodiscard]] std::shared_ptr<module>
module::create(std::string name, wasm_module_t mod, wasm_module_inst_t module_inst) {
    auto exec_env = wasm_runtime_create_exec_env(module_inst, 1048576);
    if (!exec_env) {
        wasm_runtime_deinstantiate(module_inst);
        module_inst = nullptr;
        wasm_runtime_unload(mod);
        mod = nullptr;
        return nullptr;
    }

    return std::shared_ptr<module>(new module(std::move(name), mod, module_inst, exec_env));
}

void module::runMain() {
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

void module::start() {
    //pthread_setname_np("wasm");
    printf("wasm [%s] module thread started\n", name.c_str());

    wasm_runtime_init_thread_env();
    runMain();
    wasm_runtime_destroy_thread_env();

    printf("wasm [%s] module thread exited\n", name.c_str());
}

wasi_errno_t module::path_open(
    wasi_fd_t dirfd,
    wasi_lookupflags_t dirflags, const char *path, uint32_t path_len,
    wasi_oflags_t oflags, wasi_rights_t fs_rights_base,
    wasi_rights_t fs_rights_inheriting, wasi_fdflags_t fs_flags,
    wasi_fd_t *fd_app
) {
    std::string lpath(path, path + path_len);

    // special case for absolute paths:
    if (dirfd == (uint32_t) -1) {
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

    fds.insert_or_assign(fd, (it->second)(shared_from_this(), lpath, fd));
    *fd_app = fd;

    // find the next free fd:
    while (fds.find(++fd_free) != fds.end()) {}

    return 0;
}

wasi_errno_t module::fd_close(wasi_fd_t fd) {
    auto it = fds.find(fd);
    if (it == fds.end())
        return WASI_EBADF;

    auto err = it->second->close();

    fds.erase(it);

    // track last freed fd for a quick path_open:
    fd_free = fd;

    return err;
}

wasi_errno_t module::fd_read(wasi_fd_t fd, const iovec_app_t *iovec_app, uint32_t iovs_len, uint32_t *nread_app) {
    // find the fd:
    auto it = fds.find(fd);
    if (it == fds.end())
        return WASI_EBADF;

    // construct a vector of vector<uint8_t>s pointing to wasm memory:
    iovec io = create_iovec(iovec_app, iovs_len);

    // call the fd implementation:
    return it->second->read(io, *nread_app);
}

wasi_errno_t module::fd_write(wasi_fd_t fd, const iovec_app_t *iovec_app, uint32_t iovs_len, uint32_t *nwritten_app) {
    // find the fd:
    auto it = fds.find(fd);
    if (it == fds.end())
        return WASI_EBADF;

    // construct a vector of vector<uint8_t>s pointing to wasm memory:
    iovec io = create_iovec(iovec_app, iovs_len);

    // call the fd implementation:
    return it->second->write(io, *nwritten_app);
}

wasi_errno_t
module::fd_pread(wasi_fd_t fd, const iovec_app_t *iovec_app, uint32_t iovs_len, wasi_filesize_t offset,
                 uint32_t *nread_app) {
    // find the fd:
    auto it = fds.find(fd);
    if (it == fds.end())
        return WASI_EBADF;

    // construct a vector of vector<uint8_t>s pointing to wasm memory:
    iovec io = create_iovec(iovec_app, iovs_len);

    // call the fd implementation:
    return it->second->pread(io, offset, *nread_app);
}

wasi_errno_t module::fd_pwrite(wasi_fd_t fd, const iovec_app_t *iovec_app, uint32_t iovs_len, wasi_filesize_t offset,
                               uint32_t *nwritten_app) {
    // find the fd:
    auto it = fds.find(fd);
    if (it == fds.end())
        return WASI_EBADF;

    // construct a vector of vector<uint8_t>s pointing to wasm memory:
    iovec io = create_iovec(iovec_app, iovs_len);

    // call the fd implementation:
    return it->second->pwrite(io, offset, *nwritten_app);
}

bool module::wait_for_nmi() {
    std::unique_lock<std::mutex> lk(nmi_cv_m);
    //printf("wait_for_nmi()\n");
    nmi_cv.wait_for(lk, std::chrono::microseconds(4000));
    if (nmi_triggered) {
        nmi_triggered = false;
        return true;
    }
    return false;
}

void module::notify_nmi() {
    //printf("notify_nmi()\n");
    {
        std::unique_lock<std::mutex> lk(nmi_cv_m);
        nmi_triggered = true;
    }
    nmi_cv.notify_one();
}

iovec module::create_iovec(const iovec_app_t *iovec_app, uint32_t iovs_len) {
    iovec io;
    io.reserve(iovs_len);
    const iovec_app_t *io_app = iovec_app;
    for (uint32_t i = 0; i < iovs_len; i++, io_app++) {
        auto buf = ((uint8_t *) addr_app_to_native(io_app->buf_offset));
        io.emplace_back(std::make_pair(buf, io_app->buf_len));
    }
    return io;
}