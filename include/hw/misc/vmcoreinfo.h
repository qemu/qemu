/*
 * Virtual Machine coreinfo device
 *
 * Copyright (C) 2017 Red Hat, Inc.
 *
 * Authors: Marc-André Lureau <marcandre.lureau@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */
#ifndef VMCOREINFO_H
#define VMCOREINFO_H

#include "hw/qdev.h"

#define VMCOREINFO_DEVICE "vmcoreinfo"
#define VMCOREINFO(obj) OBJECT_CHECK(VMCoreInfoState, (obj), VMCOREINFO_DEVICE)

#define VMCOREINFO_FORMAT_NONE 0x0
#define VMCOREINFO_FORMAT_ELF 0x1

/* all fields are little-endian */
typedef struct FWCfgVMCoreInfo {
    uint16_t host_format; /* set on reset */
    uint16_t guest_format;
    uint32_t size;
    uint64_t paddr;
} QEMU_PACKED FWCfgVMCoreInfo;

typedef struct VMCoreInfoState {
    DeviceClass parent_obj;

    bool has_vmcoreinfo;
    FWCfgVMCoreInfo vmcoreinfo;
} VMCoreInfoState;

/* returns NULL unless there is exactly one device */
static inline VMCoreInfoState *vmcoreinfo_find(void)
{
    Object *o = object_resolve_path_type("", VMCOREINFO_DEVICE, NULL);

    return o ? VMCOREINFO(o) : NULL;
}

#endif
