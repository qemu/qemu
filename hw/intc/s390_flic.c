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
#include "migration/qemu-file.h"
#include "hw/s390x/s390_flic.h"
#include "trace.h"

S390FLICState *s390_get_flic(void)
{
    S390FLICState *fs;

    fs = S390_FLIC_COMMON(object_resolve_path(TYPE_KVM_S390_FLIC, NULL));
    if (!fs) {
        fs = S390_FLIC_COMMON(object_resolve_path(TYPE_QEMU_S390_FLIC, NULL));
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
                                         bool is_maskable)
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

static void qemu_s390_flic_class_init(ObjectClass *oc, void *data)
{
    S390FLICStateClass *fsc = S390_FLIC_COMMON_CLASS(oc);

    fsc->register_io_adapter = qemu_s390_register_io_adapter;
    fsc->io_adapter_map = qemu_s390_io_adapter_map;
    fsc->add_adapter_routes = qemu_s390_add_adapter_routes;
    fsc->release_adapter_routes = qemu_s390_release_adapter_routes;
    fsc->clear_io_irq = qemu_s390_clear_io_flic;
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
    .class_size    = sizeof(S390FLICStateClass),
};

static void qemu_s390_flic_register_types(void)
{
    type_register_static(&s390_flic_common_info);
    type_register_static(&qemu_s390_flic_info);
}

type_init(qemu_s390_flic_register_types)
