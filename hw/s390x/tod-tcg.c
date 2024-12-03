/*
 * TOD (Time Of Day) clock - TCG implementation
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
#include "qemu/timer.h"
#include "qemu/cutils.h"
#include "qemu/module.h"
#include "cpu.h"
#include "tcg/tcg_s390x.h"
#include "system/rtc.h"

static void qemu_s390_tod_get(const S390TODState *td, S390TOD *tod,
                              Error **errp)
{
    *tod = td->base;

    tod->low += time2tod(qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL));
    if (tod->low < td->base.low) {
        tod->high++;
    }
}

static void qemu_s390_tod_set(S390TODState *td, const S390TOD *tod,
                              Error **errp)
{
    CPUState *cpu;

    td->base = *tod;

    td->base.low -= time2tod(qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL));
    if (td->base.low > tod->low) {
        td->base.high--;
    }

    /*
     * The TOD has been changed and we have to recalculate the CKC values
     * for all CPUs. We do this asynchronously, as "SET CLOCK should be
     * issued only while all other activity on all CPUs .. has been
     * suspended".
     */
    CPU_FOREACH(cpu) {
        async_run_on_cpu(cpu, tcg_s390_tod_updated, RUN_ON_CPU_NULL);
    }
}

static void qemu_s390_tod_class_init(ObjectClass *oc, void *data)
{
    S390TODClass *tdc = S390_TOD_CLASS(oc);

    tdc->get = qemu_s390_tod_get;
    tdc->set = qemu_s390_tod_set;
}

static void qemu_s390_tod_init(Object *obj)
{
    S390TODState *td = S390_TOD(obj);
    struct tm tm;

    qemu_get_timedate(&tm, 0);
    td->base.high = 0;
    td->base.low = TOD_UNIX_EPOCH + (time2tod(mktimegm(&tm)) * 1000000000ULL);
    if (td->base.low < TOD_UNIX_EPOCH) {
        td->base.high += 1;
    }
}

static const TypeInfo qemu_s390_tod_info = {
    .name = TYPE_QEMU_S390_TOD,
    .parent = TYPE_S390_TOD,
    .instance_size = sizeof(S390TODState),
    .instance_init = qemu_s390_tod_init,
    .class_init = qemu_s390_tod_class_init,
    .class_size = sizeof(S390TODClass),
};

static void register_types(void)
{
    type_register_static(&qemu_s390_tod_info);
}
type_init(register_types);
