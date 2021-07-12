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

#include "hw/qdev-core.h"
#include "tcg/s390-tod.h"
#include "qom/object.h"

typedef struct S390TOD {
    uint8_t high;
    uint64_t low;
} S390TOD;

#define TYPE_S390_TOD "s390-tod"
OBJECT_DECLARE_TYPE(S390TODState, S390TODClass, S390_TOD)
#define TYPE_KVM_S390_TOD TYPE_S390_TOD "-kvm"
#define TYPE_QEMU_S390_TOD TYPE_S390_TOD "-qemu"

struct S390TODState {
    /* private */
    DeviceState parent_obj;

    /*
     * Used by TCG to remember the time base. Used by KVM to backup the TOD
     * while the TOD is stopped.
     */
    S390TOD base;
    /* Used by KVM to remember if the TOD is stopped and base is valid. */
    bool stopped;
};

struct S390TODClass {
    /* private */
    DeviceClass parent_class;
    void (*parent_realize)(DeviceState *dev, Error **errp);

    /* public */
    void (*get)(const S390TODState *td, S390TOD *tod, Error **errp);
    void (*set)(S390TODState *td, const S390TOD *tod, Error **errp);
};

void s390_init_tod(void);
S390TODState *s390_get_todstate(void);

#endif
