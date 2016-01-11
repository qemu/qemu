#ifndef _HW_SPAPR_VIO_H
#define _HW_SPAPR_VIO_H
/*
 * QEMU sPAPR VIO bus definitions
 *
 * Copyright (c) 2010 David Gibson, IBM Corporation <david@gibson.dropbear.id.au>
 * Based on the s390 virtio bus definitions:
 * Copyright (c) 2009 Alexander Graf <agraf@suse.de>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "sysemu/dma.h"

#define TYPE_VIO_SPAPR_DEVICE "vio-spapr-device"
#define VIO_SPAPR_DEVICE(obj) \
     OBJECT_CHECK(VIOsPAPRDevice, (obj), TYPE_VIO_SPAPR_DEVICE)
#define VIO_SPAPR_DEVICE_CLASS(klass) \
     OBJECT_CLASS_CHECK(VIOsPAPRDeviceClass, (klass), TYPE_VIO_SPAPR_DEVICE)
#define VIO_SPAPR_DEVICE_GET_CLASS(obj) \
     OBJECT_GET_CLASS(VIOsPAPRDeviceClass, (obj), TYPE_VIO_SPAPR_DEVICE)

#define TYPE_SPAPR_VIO_BUS "spapr-vio-bus"
#define SPAPR_VIO_BUS(obj) OBJECT_CHECK(VIOsPAPRBus, (obj), TYPE_SPAPR_VIO_BUS)

#define TYPE_SPAPR_VIO_BRIDGE "spapr-vio-bridge"

typedef struct VIOsPAPR_CRQ {
    uint64_t qladdr;
    uint32_t qsize;
    uint32_t qnext;
    int(*SendFunc)(struct VIOsPAPRDevice *vdev, uint8_t *crq);
} VIOsPAPR_CRQ;

typedef struct VIOsPAPRDevice VIOsPAPRDevice;
typedef struct VIOsPAPRBus VIOsPAPRBus;

typedef struct VIOsPAPRDeviceClass {
    DeviceClass parent_class;

    const char *dt_name, *dt_type, *dt_compatible;
    target_ulong signal_mask;
    uint32_t rtce_window_size;
    void (*realize)(VIOsPAPRDevice *dev, Error **errp);
    void (*reset)(VIOsPAPRDevice *dev);
    int (*devnode)(VIOsPAPRDevice *dev, void *fdt, int node_off);
} VIOsPAPRDeviceClass;

struct VIOsPAPRDevice {
    DeviceState qdev;
    uint32_t reg;
    uint32_t irq;
    target_ulong signal_state;
    VIOsPAPR_CRQ crq;
    AddressSpace as;
    MemoryRegion mrroot;
    MemoryRegion mrbypass;
    sPAPRTCETable *tcet;
};

#define DEFINE_SPAPR_PROPERTIES(type, field)           \
        DEFINE_PROP_UINT32("reg", type, field.reg, -1)

struct VIOsPAPRBus {
    BusState bus;
    uint32_t next_reg;
    int (*init)(VIOsPAPRDevice *dev);
    int (*devnode)(VIOsPAPRDevice *dev, void *fdt, int node_off);
};

extern VIOsPAPRBus *spapr_vio_bus_init(void);
extern VIOsPAPRDevice *spapr_vio_find_by_reg(VIOsPAPRBus *bus, uint32_t reg);
extern int spapr_populate_vdevice(VIOsPAPRBus *bus, void *fdt);
extern int spapr_populate_chosen_stdout(void *fdt, VIOsPAPRBus *bus);

extern int spapr_vio_signal(VIOsPAPRDevice *dev, target_ulong mode);

static inline qemu_irq spapr_vio_qirq(VIOsPAPRDevice *dev)
{
    sPAPRMachineState *spapr = SPAPR_MACHINE(qdev_get_machine());

    return xics_get_qirq(spapr->icp, dev->irq);
}

static inline bool spapr_vio_dma_valid(VIOsPAPRDevice *dev, uint64_t taddr,
                                       uint32_t size, DMADirection dir)
{
    return dma_memory_valid(&dev->as, taddr, size, dir);
}

static inline int spapr_vio_dma_read(VIOsPAPRDevice *dev, uint64_t taddr,
                                     void *buf, uint32_t size)
{
    return (dma_memory_read(&dev->as, taddr, buf, size) != 0) ?
        H_DEST_PARM : H_SUCCESS;
}

static inline int spapr_vio_dma_write(VIOsPAPRDevice *dev, uint64_t taddr,
                                      const void *buf, uint32_t size)
{
    return (dma_memory_write(&dev->as, taddr, buf, size) != 0) ?
        H_DEST_PARM : H_SUCCESS;
}

static inline int spapr_vio_dma_set(VIOsPAPRDevice *dev, uint64_t taddr,
                                    uint8_t c, uint32_t size)
{
    return (dma_memory_set(&dev->as, taddr, c, size) != 0) ?
        H_DEST_PARM : H_SUCCESS;
}

#define vio_stb(_dev, _addr, _val) (stb_dma(&(_dev)->as, (_addr), (_val)))
#define vio_sth(_dev, _addr, _val) (stw_be_dma(&(_dev)->as, (_addr), (_val)))
#define vio_stl(_dev, _addr, _val) (stl_be_dma(&(_dev)->as, (_addr), (_val)))
#define vio_stq(_dev, _addr, _val) (stq_be_dma(&(_dev)->as, (_addr), (_val)))
#define vio_ldq(_dev, _addr) (ldq_be_dma(&(_dev)->as, (_addr)))

int spapr_vio_send_crq(VIOsPAPRDevice *dev, uint8_t *crq);

VIOsPAPRDevice *vty_lookup(sPAPRMachineState *spapr, target_ulong reg);
void vty_putchars(VIOsPAPRDevice *sdev, uint8_t *buf, int len);
void spapr_vty_create(VIOsPAPRBus *bus, CharDriverState *chardev);
void spapr_vlan_create(VIOsPAPRBus *bus, NICInfo *nd);
void spapr_vscsi_create(VIOsPAPRBus *bus);

VIOsPAPRDevice *spapr_vty_get_default(VIOsPAPRBus *bus);

void spapr_vio_quiesce(void);

extern const VMStateDescription vmstate_spapr_vio;

#define VMSTATE_SPAPR_VIO(_f, _s) \
    VMSTATE_STRUCT(_f, _s, 0, vmstate_spapr_vio, VIOsPAPRDevice)

void spapr_vio_set_bypass(VIOsPAPRDevice *dev, bool bypass);

#endif /* _HW_SPAPR_VIO_H */
