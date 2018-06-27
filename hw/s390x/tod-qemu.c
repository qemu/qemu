/*
 * TOD (Time Of Day) clock - QEMU implementation
 *
 * Copyright 2018 Red Hat, Inc.
 * Author(s): David Hildenbrand <david@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/s390x/tod.h"

static void qemu_s390_tod_get(const S390TODState *td, S390TOD *tod,
                              Error **errp)
{
    /* FIXME */
    tod->high = 0;
    tod->low = 0;
}

static void qemu_s390_tod_set(S390TODState *td, const S390TOD *tod,
                              Error **errp)
{
    /* FIXME */
}

static void qemu_s390_tod_class_init(ObjectClass *oc, void *data)
{
    S390TODClass *tdc = S390_TOD_CLASS(oc);

    tdc->get = qemu_s390_tod_get;
    tdc->set = qemu_s390_tod_set;
}

static TypeInfo qemu_s390_tod_info = {
    .name = TYPE_QEMU_S390_TOD,
    .parent = TYPE_S390_TOD,
    .instance_size = sizeof(S390TODState),
    .class_init = qemu_s390_tod_class_init,
    .class_size = sizeof(S390TODClass),
};

static void register_types(void)
{
    type_register_static(&qemu_s390_tod_info);
}
type_init(register_types);
