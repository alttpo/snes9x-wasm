/*
 * Copyright (C) 2019 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include "platform_api_vmcore.h"

#include "wasm_host.h"

extern wasi_write_cb stdout_write_cb;
extern wasi_write_cb stderr_write_cb;

extern "C" {

int
bh_platform_init() {
    return 0;
}

void
bh_platform_destroy() {}

int
os_printf(const char *format, ...) {
    int ret;
    va_list ap;

    if (!stdout_write_cb) {
        return 0;
    }

    // size up the buffer appropriately:
    va_start(ap, format);
    os_vprintf(format, ap);
    va_end(ap);

    return ret;
}

int
os_vprintf(const char *format, va_list ap) {
    int ret;

    if (!stdout_write_cb) {
        return 0;
    }

    // size up the buffer appropriately:
    auto size_s = vsnprintf(nullptr, 0, format, ap) + 1;

    // allocate a new buffer:
    std::unique_ptr<char[]> buf(new char[size_s]);

    // format into the buffer:
    ret = vsnprintf(buf.get(), size_s, format, ap);

    stdout_write_cb(buf.get(), buf.get() + ret);

    return ret;
}

}
