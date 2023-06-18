# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

set (PLATFORM_SHARED_DIR ${CMAKE_CURRENT_LIST_DIR})

add_definitions(-DBH_PLATFORM_DARWIN)

include_directories(${PLATFORM_SHARED_DIR})
include_directories(${SHARED_DIR}/platform/include)
include_directories(${PLATFORM_SHARED_DIR}/../../)

include (${SHARED_DIR}/platform/common/posix/platform_api_posix.cmake)

file (GLOB_RECURSE source_all ${PLATFORM_SHARED_DIR}/*.cpp)

set (PLATFORM_SHARED_SOURCE ${source_all} ${PLATFORM_COMMON_POSIX_SOURCE})

file (GLOB header ${SHARED_DIR}/platform/include/*.h)
LIST (APPEND RUNTIME_LIB_HEADER_LIST ${header})
