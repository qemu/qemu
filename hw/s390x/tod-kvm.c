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
#include "qemu/module.h"
#include "sysemu/runstate.h"
#include "hw/s390x/tod.h"
#include "kvm/kvm_s390x.h"

static void kvm_s390_get_tod_raw(S390TOD *tod, Error **errp)
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

static void kvm_s390_tod_get(const S390TODState *td, S390TOD *tod, Error **errp)
{
    if (td->stopped) {
        *tod = td->base;
        return;
    }

    kvm_s390_get_tod_raw(tod, errp);
}

static void kvm_s390_set_tod_raw(const S390TOD *tod, Error **errp)
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

static void kvm_s390_tod_set(S390TODState *td, const S390TOD *tod, Error **errp)
{
    Error *local_err = NULL;

    /*
     * Somebody (e.g. migration) set the TOD. We'll store it into KVM to
     * properly detect errors now but take a look at the runstate to decide
     * whether really to keep the tod running. E.g. during migration, this
     * is the point where we want to stop the initially running TOD to fire
     * it back up when actually starting the migrated guest.
     */
    kvm_s390_set_tod_raw(tod, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    if (runstate_is_running()) {
        td->stopped = false;
    } else {
        td->stopped = true;
        td->base = *tod;
    }
}

static void kvm_s390_tod_vm_state_change(void *opaque, bool running,
                                         RunState state)
{
    S390TODState *td = opaque;
    Error *local_err = NULL;

    if (running && td->stopped) {
        /* Set the old TOD when running the VM - start the TOD clock. */
        kvm_s390_set_tod_raw(&td->base, &local_err);
        if (local_err) {
            warn_report_err(local_err);
        }
        /* Treat errors like the TOD was running all the time. */
        td->stopped = false;
    } else if (!running && !td->stopped) {
        /* Store the TOD when stopping the VM - stop the TOD clock. */
        kvm_s390_get_tod_raw(&td->base, &local_err);
        if (local_err) {
            /* Keep the TOD running in case we could not back it up. */
            warn_report_err(local_err);
        } else {
            td->stopped = true;
        }
    }
}

static void kvm_s390_tod_realize(DeviceState *dev, Error **errp)
{
    S390TODState *td = S390_TOD(dev);
    S390TODClass *tdc = S390_TOD_GET_CLASS(td);
    Error *local_err = NULL;

    tdc->parent_realize(dev, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    /*
     * We need to know when the VM gets started/stopped to start/stop the TOD.
     * As we can never have more than one TOD instance (and that will never be
     * removed), registering here and never unregistering is good enough.
     */
    qemu_add_vm_change_state_handler(kvm_s390_tod_vm_state_change, td);
}

static void kvm_s390_tod_class_init(ObjectClass *oc, void *data)
{
    S390TODClass *tdc = S390_TOD_CLASS(oc);

    device_class_set_parent_realize(DEVICE_CLASS(oc), kvm_s390_tod_realize,
                                    &tdc->parent_realize);
    tdc->get = kvm_s390_tod_get;
    tdc->set = kvm_s390_tod_set;
}

static void kvm_s390_tod_init(Object *obj)
{
    S390TODState *td = S390_TOD(obj);

    /*
     * The TOD is initially running (value stored in KVM). Avoid needless
     * loading/storing of the TOD when starting a simple VM, so let it
     * run although the (never started) VM is stopped. For migration, we
     * will properly set the TOD later.
     */
    td->stopped = false;
}

static const TypeInfo kvm_s390_tod_info = {
    .name = TYPE_KVM_S390_TOD,
    .parent = TYPE_S390_TOD,
    .instance_size = sizeof(S390TODState),
    .instance_init = kvm_s390_tod_init,
    .class_init = kvm_s390_tod_class_init,
    .class_size = sizeof(S390TODClass),
};

static void register_types(void)
{
    type_register_static(&kvm_s390_tod_info);
}
type_init(register_types);
