/*
 * SGX EPC device
 *
 * Copyright (C) 2019 Intel Corporation
 *
 * Authors:
 *   Sean Christopherson <sean.j.christopherson@intel.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#ifndef QEMU_SGX_EPC_H
#define QEMU_SGX_EPC_H

#include "hw/i386/hostmem-epc.h"

#define TYPE_SGX_EPC "sgx-epc"
#define SGX_EPC(obj) \
    OBJECT_CHECK(SGXEPCDevice, (obj), TYPE_SGX_EPC)
#define SGX_EPC_CLASS(oc) \
    OBJECT_CLASS_CHECK(SGXEPCDeviceClass, (oc), TYPE_SGX_EPC)
#define SGX_EPC_GET_CLASS(obj) \
    OBJECT_GET_CLASS(SGXEPCDeviceClass, (obj), TYPE_SGX_EPC)

#define SGX_EPC_ADDR_PROP "addr"
#define SGX_EPC_SIZE_PROP "size"
#define SGX_EPC_MEMDEV_PROP "memdev"
#define SGX_EPC_NUMA_NODE_PROP "node"

/**
 * SGXEPCDevice:
 * @addr: starting guest physical address, where @SGXEPCDevice is mapped.
 *         Default value: 0, means that address is auto-allocated.
 * @hostmem: host memory backend providing memory for @SGXEPCDevice
 */
typedef struct SGXEPCDevice {
    /* private */
    DeviceState parent_obj;

    /* public */
    uint64_t addr;
    uint32_t node;
    HostMemoryBackendEpc *hostmem;
} SGXEPCDevice;

/*
 * @base: address in guest physical address space where EPC regions start
 * @mr: address space container for memory devices
 */
typedef struct SGXEPCState {
    uint64_t base;
    uint64_t size;

    MemoryRegion mr;

    struct SGXEPCDevice **sections;
    int nr_sections;
} SGXEPCState;

bool sgx_epc_get_section(int section_nr, uint64_t *addr, uint64_t *size);
void sgx_epc_build_srat(GArray *table_data);

static inline uint64_t sgx_epc_above_4g_end(SGXEPCState *sgx_epc)
{
    assert(sgx_epc != NULL && sgx_epc->base >= 0x100000000ULL);

    return sgx_epc->base + sgx_epc->size;
}

#endif
