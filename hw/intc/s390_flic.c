/*
 * QEMU S390x floating interrupt controller (flic)
 *
 * Copyright 2014 IBM Corp.
 * Author(s): Jens Freimann <jfrei@linux.vnet.ibm.com>
 *            Cornelia Huck <cornelia.huck@de.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qemu/main-loop.h"
#include "qemu/module.h"
#include "hw/sysbus.h"
#include "hw/s390x/ioinst.h"
#include "hw/s390x/s390_flic.h"
#include "hw/qdev-properties.h"
#include "hw/s390x/css.h"
#include "trace.h"
#include "cpu.h"
#include "qapi/error.h"
#include "hw/s390x/s390-virtio-ccw.h"

S390FLICStateClass *s390_get_flic_class(S390FLICState *fs)
{
    static S390FLICStateClass *class;

    if (!class) {
        /* we only have one flic device, so this is fine to cache */
        class = S390_FLIC_COMMON_GET_CLASS(fs);
    }
    return class;
}

QEMUS390FLICState *s390_get_qemu_flic(S390FLICState *fs)
{
    static QEMUS390FLICState *flic;

    if (!flic) {
        /* we only have one flic device, so this is fine to cache */
        flic = QEMU_S390_FLIC(fs);
    }
    return flic;
}

S390FLICState *s390_get_flic(void)
{
    static S390FLICState *fs;

    if (!fs) {
        fs = S390_FLIC_COMMON(object_resolve_path_type("",
                                                       TYPE_S390_FLIC_COMMON,
                                                       NULL));
    }
    return fs;
}

void s390_flic_init(void)
{
    DeviceState *dev;

    if (kvm_enabled()) {
        dev = qdev_create(NULL, TYPE_KVM_S390_FLIC);
        object_property_add_child(qdev_get_machine(), TYPE_KVM_S390_FLIC,
                                  OBJECT(dev), NULL);
    } else {
        dev = qdev_create(NULL, TYPE_QEMU_S390_FLIC);
        object_property_add_child(qdev_get_machine(), TYPE_QEMU_S390_FLIC,
                                  OBJECT(dev), NULL);
    }
    qdev_init_nofail(dev);
}

static int qemu_s390_register_io_adapter(S390FLICState *fs, uint32_t id,
                                         uint8_t isc, bool swap,
                                         bool is_maskable, uint8_t flags)
{
    /* nothing to do */
    return 0;
}

static int qemu_s390_io_adapter_map(S390FLICState *fs, uint32_t id,
                                    uint64_t map_addr, bool do_map)
{
    /* nothing to do */
    return 0;
}

static int qemu_s390_add_adapter_routes(S390FLICState *fs,
                                        AdapterRoutes *routes)
{
    return -ENOSYS;
}

static void qemu_s390_release_adapter_routes(S390FLICState *fs,
                                             AdapterRoutes *routes)
{
}

static int qemu_s390_clear_io_flic(S390FLICState *fs, uint16_t subchannel_id,
                           uint16_t subchannel_nr)
{
    QEMUS390FLICState *flic  = s390_get_qemu_flic(fs);
    QEMUS390FlicIO *cur, *next;
    uint8_t isc;

    g_assert(qemu_mutex_iothread_locked());
    if (!(flic->pending & FLIC_PENDING_IO)) {
        return 0;
    }

    /* check all iscs */
    for (isc = 0; isc < 8; isc++) {
        if (QLIST_EMPTY(&flic->io[isc])) {
            continue;
        }

        /* search and delete any matching one */
        QLIST_FOREACH_SAFE(cur, &flic->io[isc], next, next) {
            if (cur->id == subchannel_id && cur->nr == subchannel_nr) {
                QLIST_REMOVE(cur, next);
                g_free(cur);
            }
        }

        /* update our indicator bit */
        if (QLIST_EMPTY(&flic->io[isc])) {
            flic->pending &= ~ISC_TO_PENDING_IO(isc);
        }
    }
    return 0;
}

static int qemu_s390_modify_ais_mode(S390FLICState *fs, uint8_t isc,
                                     uint16_t mode)
{
    QEMUS390FLICState *flic  = s390_get_qemu_flic(fs);

    switch (mode) {
    case SIC_IRQ_MODE_ALL:
        flic->simm &= ~AIS_MODE_MASK(isc);
        flic->nimm &= ~AIS_MODE_MASK(isc);
        break;
    case SIC_IRQ_MODE_SINGLE:
        flic->simm |= AIS_MODE_MASK(isc);
        flic->nimm &= ~AIS_MODE_MASK(isc);
        break;
    default:
        return -EINVAL;
    }

    return 0;
}

static int qemu_s390_inject_airq(S390FLICState *fs, uint8_t type,
                                 uint8_t isc, uint8_t flags)
{
    QEMUS390FLICState *flic = s390_get_qemu_flic(fs);
    S390FLICStateClass *fsc = s390_get_flic_class(fs);
    bool flag = flags & S390_ADAPTER_SUPPRESSIBLE;
    uint32_t io_int_word = (isc << 27) | IO_INT_WORD_AI;

    if (flag && (flic->nimm & AIS_MODE_MASK(isc))) {
        trace_qemu_s390_airq_suppressed(type, isc);
        return 0;
    }

    fsc->inject_io(fs, 0, 0, 0, io_int_word);

    if (flag && (flic->simm & AIS_MODE_MASK(isc))) {
        flic->nimm |= AIS_MODE_MASK(isc);
        trace_qemu_s390_suppress_airq(isc, "Single-Interruption Mode",
                                      "NO-Interruptions Mode");
    }

    return 0;
}

static void qemu_s390_flic_notify(uint32_t type)
{
    CPUState *cs;

    /*
     * We have to make all CPUs see CPU_INTERRUPT_HARD, so they might
     * consider it. We will kick all running CPUs and only relevant
     * sleeping ones.
     */
    CPU_FOREACH(cs) {
        S390CPU *cpu = S390_CPU(cs);

        cs->interrupt_request |= CPU_INTERRUPT_HARD;

        /* ignore CPUs that are not sleeping */
        if (s390_cpu_get_state(cpu) != S390_CPU_STATE_OPERATING &&
            s390_cpu_get_state(cpu) != S390_CPU_STATE_LOAD) {
            continue;
        }

        /* we always kick running CPUs for now, this is tricky */
        if (cs->halted) {
            /* don't check for subclasses, CPUs double check when waking up */
            if (type & FLIC_PENDING_SERVICE) {
                if (!(cpu->env.psw.mask & PSW_MASK_EXT)) {
                    continue;
                }
            } else if (type & FLIC_PENDING_IO) {
                if (!(cpu->env.psw.mask & PSW_MASK_IO)) {
                    continue;
                }
            } else if (type & FLIC_PENDING_MCHK_CR) {
                if (!(cpu->env.psw.mask & PSW_MASK_MCHECK)) {
                    continue;
                }
            }
        }
        cpu_interrupt(cs, CPU_INTERRUPT_HARD);
    }
}

uint32_t qemu_s390_flic_dequeue_service(QEMUS390FLICState *flic)
{
    uint32_t tmp;

    g_assert(qemu_mutex_iothread_locked());
    g_assert(flic->pending & FLIC_PENDING_SERVICE);
    tmp = flic->service_param;
    flic->service_param = 0;
    flic->pending &= ~FLIC_PENDING_SERVICE;

    return tmp;
}

/* caller has to free the returned object */
QEMUS390FlicIO *qemu_s390_flic_dequeue_io(QEMUS390FLICState *flic, uint64_t cr6)
{
    QEMUS390FlicIO *io;
    uint8_t isc;

    g_assert(qemu_mutex_iothread_locked());
    if (!(flic->pending & CR6_TO_PENDING_IO(cr6))) {
        return NULL;
    }

    for (isc = 0; isc < 8; isc++) {
        if (QLIST_EMPTY(&flic->io[isc]) || !(cr6 & ISC_TO_ISC_BITS(isc))) {
            continue;
        }
        io = QLIST_FIRST(&flic->io[isc]);
        QLIST_REMOVE(io, next);

        /* update our indicator bit */
        if (QLIST_EMPTY(&flic->io[isc])) {
            flic->pending &= ~ISC_TO_PENDING_IO(isc);
        }
        return io;
    }

    return NULL;
}

void qemu_s390_flic_dequeue_crw_mchk(QEMUS390FLICState *flic)
{
    g_assert(qemu_mutex_iothread_locked());
    g_assert(flic->pending & FLIC_PENDING_MCHK_CR);
    flic->pending &= ~FLIC_PENDING_MCHK_CR;
}

static void qemu_s390_inject_service(S390FLICState *fs, uint32_t parm)
{
    QEMUS390FLICState *flic = s390_get_qemu_flic(fs);

    g_assert(qemu_mutex_iothread_locked());
    /* multiplexing is good enough for sclp - kvm does it internally as well */
    flic->service_param |= parm;
    flic->pending |= FLIC_PENDING_SERVICE;

    qemu_s390_flic_notify(FLIC_PENDING_SERVICE);
}

static void qemu_s390_inject_io(S390FLICState *fs, uint16_t subchannel_id,
                                uint16_t subchannel_nr, uint32_t io_int_parm,
                                uint32_t io_int_word)
{
    const uint8_t isc = IO_INT_WORD_ISC(io_int_word);
    QEMUS390FLICState *flic = s390_get_qemu_flic(fs);
    QEMUS390FlicIO *io;

    g_assert(qemu_mutex_iothread_locked());
    io = g_new0(QEMUS390FlicIO, 1);
    io->id = subchannel_id;
    io->nr = subchannel_nr;
    io->parm = io_int_parm;
    io->word = io_int_word;

    QLIST_INSERT_HEAD(&flic->io[isc], io, next);
    flic->pending |= ISC_TO_PENDING_IO(isc);

    qemu_s390_flic_notify(ISC_TO_PENDING_IO(isc));
}

static void qemu_s390_inject_crw_mchk(S390FLICState *fs)
{
    QEMUS390FLICState *flic = s390_get_qemu_flic(fs);

    g_assert(qemu_mutex_iothread_locked());
    flic->pending |= FLIC_PENDING_MCHK_CR;

    qemu_s390_flic_notify(FLIC_PENDING_MCHK_CR);
}

bool qemu_s390_flic_has_service(QEMUS390FLICState *flic)
{
    /* called without lock via cc->has_work, will be validated under lock */
    return !!(flic->pending & FLIC_PENDING_SERVICE);
}

bool qemu_s390_flic_has_io(QEMUS390FLICState *flic, uint64_t cr6)
{
    /* called without lock via cc->has_work, will be validated under lock */
    return !!(flic->pending & CR6_TO_PENDING_IO(cr6));
}

bool qemu_s390_flic_has_crw_mchk(QEMUS390FLICState *flic)
{
    /* called without lock via cc->has_work, will be validated under lock */
    return !!(flic->pending & FLIC_PENDING_MCHK_CR);
}

bool qemu_s390_flic_has_any(QEMUS390FLICState *flic)
{
    g_assert(qemu_mutex_iothread_locked());
    return !!flic->pending;
}

static void qemu_s390_flic_reset(DeviceState *dev)
{
    QEMUS390FLICState *flic = QEMU_S390_FLIC(dev);
    QEMUS390FlicIO *cur, *next;
    int isc;

    g_assert(qemu_mutex_iothread_locked());
    flic->simm = 0;
    flic->nimm = 0;
    flic->pending = 0;

    /* remove all pending io interrupts */
    for (isc = 0; isc < 8; isc++) {
        QLIST_FOREACH_SAFE(cur, &flic->io[isc], next, next) {
            QLIST_REMOVE(cur, next);
            g_free(cur);
        }
    }
}

bool ais_needed(void *opaque)
{
    S390FLICState *s = opaque;

    return s->ais_supported;
}

static const VMStateDescription qemu_s390_flic_vmstate = {
    .name = "qemu-s390-flic",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = ais_needed,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8(simm, QEMUS390FLICState),
        VMSTATE_UINT8(nimm, QEMUS390FLICState),
        VMSTATE_END_OF_LIST()
    }
};

static void qemu_s390_flic_instance_init(Object *obj)
{
    QEMUS390FLICState *flic = QEMU_S390_FLIC(obj);
    int isc;

    for (isc = 0; isc < 8; isc++) {
        QLIST_INIT(&flic->io[isc]);
    }
}

static void qemu_s390_flic_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    S390FLICStateClass *fsc = S390_FLIC_COMMON_CLASS(oc);

    dc->reset = qemu_s390_flic_reset;
    dc->vmsd = &qemu_s390_flic_vmstate;
    fsc->register_io_adapter = qemu_s390_register_io_adapter;
    fsc->io_adapter_map = qemu_s390_io_adapter_map;
    fsc->add_adapter_routes = qemu_s390_add_adapter_routes;
    fsc->release_adapter_routes = qemu_s390_release_adapter_routes;
    fsc->clear_io_irq = qemu_s390_clear_io_flic;
    fsc->modify_ais_mode = qemu_s390_modify_ais_mode;
    fsc->inject_airq = qemu_s390_inject_airq;
    fsc->inject_service = qemu_s390_inject_service;
    fsc->inject_io = qemu_s390_inject_io;
    fsc->inject_crw_mchk = qemu_s390_inject_crw_mchk;
}

static Property s390_flic_common_properties[] = {
    DEFINE_PROP_UINT32("adapter_routes_max_batch", S390FLICState,
                       adapter_routes_max_batch, ADAPTER_ROUTES_MAX_GSI),
    DEFINE_PROP_END_OF_LIST(),
};

static void s390_flic_common_realize(DeviceState *dev, Error **errp)
{
    S390FLICState *fs = S390_FLIC_COMMON(dev);
    uint32_t max_batch = fs->adapter_routes_max_batch;

    if (max_batch > ADAPTER_ROUTES_MAX_GSI) {
        error_setg(errp, "flic property adapter_routes_max_batch too big"
                   " (%d > %d)", max_batch, ADAPTER_ROUTES_MAX_GSI);
        return;
    }

    fs->ais_supported = s390_has_feat(S390_FEAT_ADAPTER_INT_SUPPRESSION);
}

static void s390_flic_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->props = s390_flic_common_properties;
    dc->realize = s390_flic_common_realize;
}

static const TypeInfo qemu_s390_flic_info = {
    .name          = TYPE_QEMU_S390_FLIC,
    .parent        = TYPE_S390_FLIC_COMMON,
    .instance_size = sizeof(QEMUS390FLICState),
    .instance_init = qemu_s390_flic_instance_init,
    .class_init    = qemu_s390_flic_class_init,
};


static const TypeInfo s390_flic_common_info = {
    .name          = TYPE_S390_FLIC_COMMON,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(S390FLICState),
    .class_init    = s390_flic_class_init,
    .class_size    = sizeof(S390FLICStateClass),
};

static void qemu_s390_flic_register_types(void)
{
    type_register_static(&s390_flic_common_info);
    type_register_static(&qemu_s390_flic_info);
}

type_init(qemu_s390_flic_register_types)

static bool adapter_info_so_needed(void *opaque)
{
    return css_migration_enabled();
}

const VMStateDescription vmstate_adapter_info_so = {
    .name = "s390_adapter_info/summary_offset",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = adapter_info_so_needed,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(summary_offset, AdapterInfo),
        VMSTATE_END_OF_LIST()
    }
};

const VMStateDescription vmstate_adapter_info = {
    .name = "s390_adapter_info",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT64(ind_offset, AdapterInfo),
        /*
         * We do not have to migrate neither the id nor the addresses.
         * The id is set by css_register_io_adapter and the addresses
         * are set based on the IndAddr objects after those get mapped.
         */
        VMSTATE_END_OF_LIST()
    },
    .subsections = (const VMStateDescription * []) {
        &vmstate_adapter_info_so,
        NULL
    }
};

const VMStateDescription vmstate_adapter_routes = {

    .name = "s390_adapter_routes",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_STRUCT(adapter, AdapterRoutes, 1, vmstate_adapter_info,
                       AdapterInfo),
        VMSTATE_END_OF_LIST()
    }
};
