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

#ifndef __HW_S390_FLIC_H
#define __HW_S390_FLIC_H

#include "hw/sysbus.h"
#include "hw/s390x/adapter.h"
#include "hw/virtio/virtio.h"

typedef struct AdapterRoutes {
    AdapterInfo adapter;
    int num_routes;
    int gsi[VIRTIO_PCI_QUEUE_MAX];
} AdapterRoutes;

#define TYPE_S390_FLIC_COMMON "s390-flic"
#define S390_FLIC_COMMON(obj) \
    OBJECT_CHECK(S390FLICState, (obj), TYPE_S390_FLIC_COMMON)

typedef struct S390FLICState {
    SysBusDevice parent_obj;

} S390FLICState;

#define S390_FLIC_COMMON_CLASS(klass) \
    OBJECT_CLASS_CHECK(S390FLICStateClass, (klass), TYPE_S390_FLIC_COMMON)
#define S390_FLIC_COMMON_GET_CLASS(obj) \
    OBJECT_GET_CLASS(S390FLICStateClass, (obj), TYPE_S390_FLIC_COMMON)

typedef struct S390FLICStateClass {
    DeviceClass parent_class;

    int (*register_io_adapter)(S390FLICState *fs, uint32_t id, uint8_t isc,
                               bool swap, bool maskable);
    int (*io_adapter_map)(S390FLICState *fs, uint32_t id, uint64_t map_addr,
                          bool do_map);
    int (*add_adapter_routes)(S390FLICState *fs, AdapterRoutes *routes);
    void (*release_adapter_routes)(S390FLICState *fs, AdapterRoutes *routes);
} S390FLICStateClass;

#define TYPE_KVM_S390_FLIC "s390-flic-kvm"
#define KVM_S390_FLIC(obj) \
    OBJECT_CHECK(KVMS390FLICState, (obj), TYPE_KVM_S390_FLIC)

#define TYPE_QEMU_S390_FLIC "s390-flic-qemu"
#define QEMU_S390_FLIC(obj) \
    OBJECT_CHECK(QEMUS390FLICState, (obj), TYPE_QEMU_S390_FLIC)

typedef struct QEMUS390FLICState {
    S390FLICState parent_obj;
} QEMUS390FLICState;

void s390_flic_init(void);

S390FLICState *s390_get_flic(void);

#ifdef CONFIG_KVM
DeviceState *s390_flic_kvm_create(void);
#else
static inline DeviceState *s390_flic_kvm_create(void)
{
    return NULL;
}
#endif

#endif /* __HW_S390_FLIC_H */
