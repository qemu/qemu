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
#include "hw/sysbus.h"
#include "hw/s390x/ioinst.h"
#include "hw/s390x/s390_flic.h"
#include "hw/s390x/css.h"
#include "trace.h"
#include "cpu.h"
#include "hw/qdev.h"
#include "qapi/error.h"
#include "hw/s390x/s390-virtio-ccw.h"

S390FLICState *s390_get_flic(void)
{
    static S390FLICState *fs;

    if (!fs) {
        fs = S390_FLIC_COMMON(object_resolve_path(TYPE_KVM_S390_FLIC, NULL));
        if (!fs) {
            fs = S390_FLIC_COMMON(object_resolve_path(TYPE_QEMU_S390_FLIC,
                                                      NULL));
        }
    }
    return fs;
}

void s390_flic_init(void)
{
    DeviceState *dev;

    dev = s390_flic_kvm_create();
    if (!dev) {
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
    /* Fixme TCG */
    return -ENOSYS;
}

static int qemu_s390_modify_ais_mode(S390FLICState *fs, uint8_t isc,
                                     uint16_t mode)
{
    QEMUS390FLICState *flic  = QEMU_S390_FLIC(fs);

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
    QEMUS390FLICState *flic = QEMU_S390_FLIC(fs);
    bool flag = flags & S390_ADAPTER_SUPPRESSIBLE;
    uint32_t io_int_word = (isc << 27) | IO_INT_WORD_AI;

    if (flag && (flic->nimm & AIS_MODE_MASK(isc))) {
        trace_qemu_s390_airq_suppressed(type, isc);
        return 0;
    }

    s390_io_interrupt(0, 0, 0, io_int_word);

    if (flag && (flic->simm & AIS_MODE_MASK(isc))) {
        flic->nimm |= AIS_MODE_MASK(isc);
        trace_qemu_s390_suppress_airq(isc, "Single-Interruption Mode",
                                      "NO-Interruptions Mode");
    }

    return 0;
}

static void qemu_s390_flic_reset(DeviceState *dev)
{
    QEMUS390FLICState *flic = QEMU_S390_FLIC(dev);

    flic->simm = 0;
    flic->nimm = 0;
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
