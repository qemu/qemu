/*
 * TOD (Time Of Day) clock
 *
 * Copyright 2018 Red Hat, Inc.
 * Author(s): David Hildenbrand <david@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef HW_S390_TOD_H
#define HW_S390_TOD_H

#include "hw/qdev.h"

typedef struct S390TOD {
    uint8_t high;
    uint64_t low;
} S390TOD;

#define TYPE_S390_TOD "s390-tod"
#define S390_TOD(obj) OBJECT_CHECK(S390TODState, (obj), TYPE_S390_TOD)
#define S390_TOD_CLASS(oc) OBJECT_CLASS_CHECK(S390TODClass, (oc), \
                                              TYPE_S390_TOD)
#define S390_TOD_GET_CLASS(obj) OBJECT_GET_CLASS(S390TODClass, (obj), \
                                                 TYPE_S390_TOD)
#define TYPE_KVM_S390_TOD TYPE_S390_TOD "-kvm"
#define TYPE_QEMU_S390_TOD TYPE_S390_TOD "-qemu"

typedef struct S390TODState {
    /* private */
    DeviceState parent_obj;

    /* unused by KVM implementation */
    S390TOD base;
} S390TODState;

typedef struct S390TODClass {
    /* private */
    DeviceClass parent_class;

    /* public */
    void (*get)(const S390TODState *td, S390TOD *tod, Error **errp);
    void (*set)(S390TODState *td, const S390TOD *tod, Error **errp);
} S390TODClass;

/* The value of the TOD clock for 1.1.1970. */
#define TOD_UNIX_EPOCH 0x7d91048bca000000ULL

/* Converts ns to s390's clock format */
static inline uint64_t time2tod(uint64_t ns)
{
    return (ns << 9) / 125 + (((ns & 0xff80000000000000ull) / 125) << 9);
}

/* Converts s390's clock format to ns */
static inline uint64_t tod2time(uint64_t t)
{
    return ((t >> 9) * 125) + (((t & 0x1ff) * 125) >> 9);
}

void s390_init_tod(void);
S390TODState *s390_get_todstate(void);

#endif
