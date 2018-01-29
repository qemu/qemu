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

#ifndef HW_S390_FLIC_H
#define HW_S390_FLIC_H

#include "hw/sysbus.h"
#include "hw/s390x/adapter.h"
#include "hw/virtio/virtio.h"
#include "qemu/queue.h"

/*
 * Reserve enough gsis to accommodate all virtio devices.
 * If any other user of adapter routes needs more of these,
 * we need to bump the value; but virtio looks like the
 * maximum right now.
 */
#define ADAPTER_ROUTES_MAX_GSI VIRTIO_QUEUE_MAX

typedef struct AdapterRoutes {
    AdapterInfo adapter;
    int num_routes;
    int gsi[ADAPTER_ROUTES_MAX_GSI];
} AdapterRoutes;

extern const VMStateDescription vmstate_adapter_routes;

#define VMSTATE_ADAPTER_ROUTES(_f, _s) \
    VMSTATE_STRUCT(_f, _s, 1, vmstate_adapter_routes, AdapterRoutes)

#define TYPE_S390_FLIC_COMMON "s390-flic"
#define S390_FLIC_COMMON(obj) \
    OBJECT_CHECK(S390FLICState, (obj), TYPE_S390_FLIC_COMMON)

typedef struct S390FLICState {
    SysBusDevice parent_obj;
    /* to limit AdapterRoutes.num_routes for compat */
    uint32_t adapter_routes_max_batch;
    bool ais_supported;
} S390FLICState;

#define S390_FLIC_COMMON_CLASS(klass) \
    OBJECT_CLASS_CHECK(S390FLICStateClass, (klass), TYPE_S390_FLIC_COMMON)
#define S390_FLIC_COMMON_GET_CLASS(obj) \
    OBJECT_GET_CLASS(S390FLICStateClass, (obj), TYPE_S390_FLIC_COMMON)

typedef struct S390FLICStateClass {
    DeviceClass parent_class;

    int (*register_io_adapter)(S390FLICState *fs, uint32_t id, uint8_t isc,
                               bool swap, bool maskable, uint8_t flags);
    int (*io_adapter_map)(S390FLICState *fs, uint32_t id, uint64_t map_addr,
                          bool do_map);
    int (*add_adapter_routes)(S390FLICState *fs, AdapterRoutes *routes);
    void (*release_adapter_routes)(S390FLICState *fs, AdapterRoutes *routes);
    int (*clear_io_irq)(S390FLICState *fs, uint16_t subchannel_id,
                        uint16_t subchannel_nr);
    int (*modify_ais_mode)(S390FLICState *fs, uint8_t isc, uint16_t mode);
    int (*inject_airq)(S390FLICState *fs, uint8_t type, uint8_t isc,
                       uint8_t flags);
    void (*inject_service)(S390FLICState *fs, uint32_t parm);
    void (*inject_io)(S390FLICState *fs, uint16_t subchannel_id,
                      uint16_t subchannel_nr, uint32_t io_int_parm,
                      uint32_t io_int_word);
    void (*inject_crw_mchk)(S390FLICState *fs);
} S390FLICStateClass;

#define TYPE_KVM_S390_FLIC "s390-flic-kvm"
#define KVM_S390_FLIC(obj) \
    OBJECT_CHECK(KVMS390FLICState, (obj), TYPE_KVM_S390_FLIC)

#define TYPE_QEMU_S390_FLIC "s390-flic-qemu"
#define QEMU_S390_FLIC(obj) \
    OBJECT_CHECK(QEMUS390FLICState, (obj), TYPE_QEMU_S390_FLIC)

#define SIC_IRQ_MODE_ALL 0
#define SIC_IRQ_MODE_SINGLE 1
#define AIS_MODE_MASK(isc) (0x80 >> isc)

#define ISC_TO_PENDING_IO(_isc) (0x80 >> (_isc))
#define CR6_TO_PENDING_IO(_cr6) (((_cr6) >> 24) & 0xff)

/* organize the ISC bits so that the macros above work */
#define FLIC_PENDING_IO_ISC7            (1 << 0)
#define FLIC_PENDING_IO_ISC6            (1 << 1)
#define FLIC_PENDING_IO_ISC5            (1 << 2)
#define FLIC_PENDING_IO_ISC4            (1 << 3)
#define FLIC_PENDING_IO_ISC3            (1 << 4)
#define FLIC_PENDING_IO_ISC2            (1 << 5)
#define FLIC_PENDING_IO_ISC1            (1 << 6)
#define FLIC_PENDING_IO_ISC0            (1 << 7)
#define FLIC_PENDING_SERVICE            (1 << 8)
#define FLIC_PENDING_MCHK_CR            (1 << 9)

#define FLIC_PENDING_IO (FLIC_PENDING_IO_ISC0 | FLIC_PENDING_IO_ISC1 | \
                         FLIC_PENDING_IO_ISC2 | FLIC_PENDING_IO_ISC3 | \
                         FLIC_PENDING_IO_ISC4 | FLIC_PENDING_IO_ISC5 | \
                         FLIC_PENDING_IO_ISC6 | FLIC_PENDING_IO_ISC7)

typedef struct QEMUS390FlicIO {
    uint16_t id;
    uint16_t nr;
    uint32_t parm;
    uint32_t word;
    QLIST_ENTRY(QEMUS390FlicIO) next;
} QEMUS390FlicIO;

typedef struct QEMUS390FLICState {
    S390FLICState parent_obj;
    uint32_t pending;
    uint32_t service_param;
    uint8_t simm;
    uint8_t nimm;
    QLIST_HEAD(, QEMUS390FlicIO) io[8];
} QEMUS390FLICState;

uint32_t qemu_s390_flic_dequeue_service(QEMUS390FLICState *flic);
QEMUS390FlicIO *qemu_s390_flic_dequeue_io(QEMUS390FLICState *flic,
                                          uint64_t cr6);
void qemu_s390_flic_dequeue_crw_mchk(QEMUS390FLICState *flic);
bool qemu_s390_flic_has_service(QEMUS390FLICState *flic);
bool qemu_s390_flic_has_io(QEMUS390FLICState *fs, uint64_t cr6);
bool qemu_s390_flic_has_crw_mchk(QEMUS390FLICState *flic);
bool qemu_s390_flic_has_any(QEMUS390FLICState *flic);

void s390_flic_init(void);

S390FLICState *s390_get_flic(void);
QEMUS390FLICState *s390_get_qemu_flic(S390FLICState *fs);
S390FLICStateClass *s390_get_flic_class(S390FLICState *fs);
bool ais_needed(void *opaque);

#endif /* HW_S390_FLIC_H */
