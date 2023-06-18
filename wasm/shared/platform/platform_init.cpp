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
    char str[4096];

    if (!stdout_write_cb) {
        return 0;
    }

    va_start(ap, format);
    ret = vsnprintf(str, 4095, format, ap);
    va_end(ap);
    stdout_write_cb(str, str + ret);

    return ret;
}

int
os_vprintf(const char *format, va_list ap) {
    int ret;
    char str[4096];

    if (!stdout_write_cb) {
        return 0;
    }

    ret = vsnprintf(str, 4095, format, ap);
    stdout_write_cb(str, str + ret);

    return ret;
}

}
