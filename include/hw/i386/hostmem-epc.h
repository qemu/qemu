/*
 * SGX EPC backend
 *
 * Copyright (C) 2019 Intel Corporation
 *
 * Authors:
 *   Sean Christopherson <sean.j.christopherson@intel.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#ifndef QEMU_HOSTMEM_EPC_H
#define QEMU_HOSTMEM_EPC_H

#include "sysemu/hostmem.h"

#define TYPE_MEMORY_BACKEND_EPC "memory-backend-epc"

#define MEMORY_BACKEND_EPC(obj)                                        \
    OBJECT_CHECK(HostMemoryBackendEpc, (obj), TYPE_MEMORY_BACKEND_EPC)

typedef struct HostMemoryBackendEpc HostMemoryBackendEpc;

struct HostMemoryBackendEpc {
    HostMemoryBackend parent_obj;
};

#endif
