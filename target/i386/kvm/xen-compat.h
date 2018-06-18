/*
 * Xen HVM emulation support in KVM
 *
 * Copyright © 2022 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef QEMU_I386_KVM_XEN_COMPAT_H
#define QEMU_I386_KVM_XEN_COMPAT_H

#include "hw/xen/interface/memory.h"

typedef uint32_t compat_pfn_t;
typedef uint32_t compat_ulong_t;

struct compat_xen_add_to_physmap {
    domid_t domid;
    uint16_t size;
    unsigned int space;
    compat_ulong_t idx;
    compat_pfn_t gpfn;
};

#endif /* QEMU_I386_XEN_COMPAT_H */
