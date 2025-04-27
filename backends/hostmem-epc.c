/*
 * QEMU host SGX EPC memory backend
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
#include <sys/ioctl.h>
#include "qom/object_interfaces.h"
#include "qapi/error.h"
#include "system/hostmem.h"
#include "hw/i386/hostmem-epc.h"

static bool
sgx_epc_backend_memory_alloc(HostMemoryBackend *backend, Error **errp)
{
    g_autofree char *name = NULL;
    uint32_t ram_flags;
    int fd;

    if (!backend->size) {
        error_setg(errp, "can't create backend with size 0");
        return false;
    }

    fd = qemu_open("/dev/sgx_vepc", O_RDWR, errp);
    if (fd < 0) {
        return false;
    }

    backend->aligned = true;
    name = object_get_canonical_path(OBJECT(backend));
    ram_flags = (backend->share ? RAM_SHARED : RAM_PRIVATE) | RAM_PROTECTED;
    return memory_region_init_ram_from_fd(&backend->mr, OBJECT(backend), name,
                                          backend->size, ram_flags, fd, 0, errp);
}

static void sgx_epc_backend_instance_init(Object *obj)
{
    HostMemoryBackend *m = MEMORY_BACKEND(obj);

    m->share = true;
    m->merge = false;
    m->dump = false;
}

static void sgx_epc_backend_class_init(ObjectClass *oc, const void *data)
{
    HostMemoryBackendClass *bc = MEMORY_BACKEND_CLASS(oc);

    bc->alloc = sgx_epc_backend_memory_alloc;
}

static const TypeInfo sgx_epc_backed_info = {
    .name = TYPE_MEMORY_BACKEND_EPC,
    .parent = TYPE_MEMORY_BACKEND,
    .instance_init = sgx_epc_backend_instance_init,
    .class_init = sgx_epc_backend_class_init,
    .instance_size = sizeof(HostMemoryBackendEpc),
};

static void register_types(void)
{
    int fd = qemu_open_old("/dev/sgx_vepc", O_RDWR);
    if (fd >= 0) {
        close(fd);

        type_register_static(&sgx_epc_backed_info);
    }
}

type_init(register_types);
