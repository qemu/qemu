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
#include "qemu/osdep.h"
#include "hw/i386/pc.h"
#include "hw/i386/sgx-epc.h"
#include "hw/mem/memory-device.h"
#include "hw/qdev-properties.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "target/i386/cpu.h"
#include "system/address-spaces.h"

static const Property sgx_epc_properties[] = {
    DEFINE_PROP_UINT64(SGX_EPC_ADDR_PROP, SGXEPCDevice, addr, 0),
    DEFINE_PROP_UINT32(SGX_EPC_NUMA_NODE_PROP, SGXEPCDevice, node, 0),
    DEFINE_PROP_LINK(SGX_EPC_MEMDEV_PROP, SGXEPCDevice, hostmem,
                     TYPE_MEMORY_BACKEND_EPC, HostMemoryBackendEpc *),
};

static void sgx_epc_get_size(Object *obj, Visitor *v, const char *name,
                             void *opaque, Error **errp)
{
    Error *local_err = NULL;
    uint64_t value;

    value = memory_device_get_region_size(MEMORY_DEVICE(obj), &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    visit_type_uint64(v, name, &value, errp);
}

static void sgx_epc_init(Object *obj)
{
    object_property_add(obj, SGX_EPC_SIZE_PROP, "uint64", sgx_epc_get_size,
                        NULL, NULL, NULL);
}

static void sgx_epc_realize(DeviceState *dev, Error **errp)
{
    PCMachineState *pcms = PC_MACHINE(qdev_get_machine());
    X86MachineState *x86ms = X86_MACHINE(pcms);
    MemoryDeviceState *md = MEMORY_DEVICE(dev);
    SGXEPCState *sgx_epc = &pcms->sgx_epc;
    SGXEPCDevice *epc = SGX_EPC(dev);
    HostMemoryBackend *hostmem;
    const char *path;

    if (x86ms->boot_cpus != 0) {
        error_setg(errp, "'" TYPE_SGX_EPC "' can't be created after vCPUs,"
                         "e.g. via -device");
        return;
    }

    if (!epc->hostmem) {
        error_setg(errp, "'" SGX_EPC_MEMDEV_PROP "' property is not set");
        return;
    }
    hostmem = MEMORY_BACKEND(epc->hostmem);
    if (host_memory_backend_is_mapped(hostmem)) {
        path = object_get_canonical_path_component(OBJECT(hostmem));
        error_setg(errp, "can't use already busy memdev: %s", path);
        return;
    }

    epc->addr = sgx_epc->base + sgx_epc->size;

    memory_region_add_subregion(&sgx_epc->mr, epc->addr - sgx_epc->base,
                                host_memory_backend_get_memory(hostmem));

    host_memory_backend_set_mapped(hostmem, true);

    sgx_epc->sections = g_renew(SGXEPCDevice *, sgx_epc->sections,
                                sgx_epc->nr_sections + 1);
    sgx_epc->sections[sgx_epc->nr_sections++] = epc;

    sgx_epc->size += memory_device_get_region_size(md, errp);
}

static void sgx_epc_unrealize(DeviceState *dev)
{
    SGXEPCDevice *epc = SGX_EPC(dev);
    HostMemoryBackend *hostmem = MEMORY_BACKEND(epc->hostmem);

    host_memory_backend_set_mapped(hostmem, false);
}

static uint64_t sgx_epc_md_get_addr(const MemoryDeviceState *md)
{
    const SGXEPCDevice *epc = SGX_EPC(md);

    return epc->addr;
}

static void sgx_epc_md_set_addr(MemoryDeviceState *md, uint64_t addr,
                                Error **errp)
{
    object_property_set_uint(OBJECT(md), SGX_EPC_ADDR_PROP, addr, errp);
}

static uint64_t sgx_epc_md_get_plugged_size(const MemoryDeviceState *md,
                                            Error **errp)
{
    return 0;
}

static MemoryRegion *sgx_epc_md_get_memory_region(MemoryDeviceState *md,
                                                  Error **errp)
{
    SGXEPCDevice *epc = SGX_EPC(md);
    HostMemoryBackend *hostmem;

    if (!epc->hostmem) {
        error_setg(errp, "'" SGX_EPC_MEMDEV_PROP "' property must be set");
        return NULL;
    }

    hostmem = MEMORY_BACKEND(epc->hostmem);
    return host_memory_backend_get_memory(hostmem);
}

static void sgx_epc_md_fill_device_info(const MemoryDeviceState *md,
                                        MemoryDeviceInfo *info)
{
    SgxEPCDeviceInfo *se = g_new0(SgxEPCDeviceInfo, 1);
    SGXEPCDevice *epc = SGX_EPC(md);

    se->memaddr = epc->addr;
    se->size = object_property_get_uint(OBJECT(epc), SGX_EPC_SIZE_PROP,
                                        NULL);
    se->node = object_property_get_uint(OBJECT(epc), SGX_EPC_NUMA_NODE_PROP,
                                        NULL);
    se->memdev = object_get_canonical_path(OBJECT(epc->hostmem));

    info->u.sgx_epc.data = se;
    info->type = MEMORY_DEVICE_INFO_KIND_SGX_EPC;
}

static void sgx_epc_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    MemoryDeviceClass *mdc = MEMORY_DEVICE_CLASS(oc);

    dc->hotpluggable = false;
    dc->realize = sgx_epc_realize;
    dc->unrealize = sgx_epc_unrealize;
    dc->desc = "SGX EPC section";
    dc->user_creatable = false;
    device_class_set_props(dc, sgx_epc_properties);

    mdc->get_addr = sgx_epc_md_get_addr;
    mdc->set_addr = sgx_epc_md_set_addr;
    mdc->get_plugged_size = sgx_epc_md_get_plugged_size;
    mdc->get_memory_region = sgx_epc_md_get_memory_region;
    mdc->fill_device_info = sgx_epc_md_fill_device_info;
}

static const TypeInfo sgx_epc_info = {
    .name          = TYPE_SGX_EPC,
    .parent        = TYPE_DEVICE,
    .instance_size = sizeof(SGXEPCDevice),
    .instance_init = sgx_epc_init,
    .class_init    = sgx_epc_class_init,
    .class_size    = sizeof(DeviceClass),
    .interfaces = (const InterfaceInfo[]) {
        { TYPE_MEMORY_DEVICE },
        { }
    },
};

static void sgx_epc_register_types(void)
{
    type_register_static(&sgx_epc_info);
}

type_init(sgx_epc_register_types)
