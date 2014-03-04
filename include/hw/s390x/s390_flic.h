/*
 * QEMU S390x KVM floating interrupt controller (flic)
 *
 * Copyright 2014 IBM Corp.
 * Author(s): Jens Freimann <jfrei@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */

#ifndef __KVM_S390_FLIC_H
#define __KVM_S390_FLIC_H

#include "hw/sysbus.h"

#define TYPE_KVM_S390_FLIC "s390-flic"
#define KVM_S390_FLIC(obj) \
    OBJECT_CHECK(KVMS390FLICState, (obj), TYPE_KVM_S390_FLIC)

typedef struct KVMS390FLICState {
    SysBusDevice parent_obj;

    uint32_t fd;
} KVMS390FLICState;

#ifdef CONFIG_KVM
void s390_flic_init(void);
#else
static inline void s390_flic_init(void) { }
#endif

#endif /* __KVM_S390_FLIC_H */
