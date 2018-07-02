/*
 * TOD (Time Of Day) clock - KVM implementation
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
#include "kvm_s390x.h"

static void kvm_s390_tod_get(const S390TODState *td, S390TOD *tod, Error **errp)
{
    int r;

    r = kvm_s390_get_clock_ext(&tod->high, &tod->low);
    if (r == -ENXIO) {
        r = kvm_s390_get_clock(&tod->high, &tod->low);
    }
    if (r) {
        error_setg(errp, "Unable to get KVM guest TOD clock: %s",
                   strerror(-r));
    }
}

static void kvm_s390_tod_set(S390TODState *td, const S390TOD *tod, Error **errp)
{
    int r;

    r = kvm_s390_set_clock_ext(tod->high, tod->low);
    if (r == -ENXIO) {
        r = kvm_s390_set_clock(tod->high, tod->low);
    }
    if (r) {
        error_setg(errp, "Unable to set KVM guest TOD clock: %s",
                   strerror(-r));
    }
}

static void kvm_s390_tod_class_init(ObjectClass *oc, void *data)
{
    S390TODClass *tdc = S390_TOD_CLASS(oc);

    tdc->get = kvm_s390_tod_get;
    tdc->set = kvm_s390_tod_set;
}

static TypeInfo kvm_s390_tod_info = {
    .name = TYPE_KVM_S390_TOD,
    .parent = TYPE_S390_TOD,
    .instance_size = sizeof(S390TODState),
    .class_init = kvm_s390_tod_class_init,
    .class_size = sizeof(S390TODClass),
};

static void register_types(void)
{
    type_register_static(&kvm_s390_tod_info);
}
type_init(register_types);
