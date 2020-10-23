#ifndef HW_SPAPR_VIO_H
#define HW_SPAPR_VIO_H

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
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "hw/ppc/spapr.h"
#include "sysemu/dma.h"
#include "hw/irq.h"
#include "qom/object.h"

#define TYPE_VIO_SPAPR_DEVICE "vio-spapr-device"
OBJECT_DECLARE_TYPE(SpaprVioDevice, SpaprVioDeviceClass,
                    VIO_SPAPR_DEVICE)

#define TYPE_SPAPR_VIO_BUS "spapr-vio-bus"
OBJECT_DECLARE_SIMPLE_TYPE(SpaprVioBus, SPAPR_VIO_BUS)

#define TYPE_SPAPR_VIO_BRIDGE "spapr-vio-bridge"

typedef struct SpaprVioCrq {
    uint64_t qladdr;
    uint32_t qsize;
    uint32_t qnext;
    int(*SendFunc)(struct SpaprVioDevice *vdev, uint8_t *crq);
} SpaprVioCrq;


struct SpaprVioDeviceClass {
    DeviceClass parent_class;

    const char *dt_name, *dt_type, *dt_compatible;
    target_ulong signal_mask;
    uint32_t rtce_window_size;
    void (*realize)(SpaprVioDevice *dev, Error **errp);
    void (*reset)(SpaprVioDevice *dev);
    int (*devnode)(SpaprVioDevice *dev, void *fdt, int node_off);
    const char *(*get_dt_compatible)(SpaprVioDevice *dev);
};

struct SpaprVioDevice {
    DeviceState qdev;
    uint32_t reg;
    uint32_t irq;
    uint64_t signal_state;
    SpaprVioCrq crq;
    AddressSpace as;
    MemoryRegion mrroot;
    MemoryRegion mrbypass;
    SpaprTceTable *tcet;
};

#define DEFINE_SPAPR_PROPERTIES(type, field)           \
        DEFINE_PROP_UINT32("reg", type, field.reg, -1)

struct SpaprVioBus {
    BusState bus;
    uint32_t next_reg;
};

SpaprVioBus *spapr_vio_bus_init(void);
SpaprVioDevice *spapr_vio_find_by_reg(SpaprVioBus *bus, uint32_t reg);
void spapr_dt_vdevice(SpaprVioBus *bus, void *fdt);
gchar *spapr_vio_stdout_path(SpaprVioBus *bus);

static inline void spapr_vio_irq_pulse(SpaprVioDevice *dev)
{
    SpaprMachineState *spapr = SPAPR_MACHINE(qdev_get_machine());

    qemu_irq_pulse(spapr_qirq(spapr, dev->irq));
}

static inline bool spapr_vio_dma_valid(SpaprVioDevice *dev, uint64_t taddr,
                                       uint32_t size, DMADirection dir)
{
    return dma_memory_valid(&dev->as, taddr, size, dir);
}

static inline int spapr_vio_dma_read(SpaprVioDevice *dev, uint64_t taddr,
                                     void *buf, uint32_t size)
{
    return (dma_memory_read(&dev->as, taddr, buf, size) != 0) ?
        H_DEST_PARM : H_SUCCESS;
}

static inline int spapr_vio_dma_write(SpaprVioDevice *dev, uint64_t taddr,
                                      const void *buf, uint32_t size)
{
    return (dma_memory_write(&dev->as, taddr, buf, size) != 0) ?
        H_DEST_PARM : H_SUCCESS;
}

static inline int spapr_vio_dma_set(SpaprVioDevice *dev, uint64_t taddr,
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

int spapr_vio_send_crq(SpaprVioDevice *dev, uint8_t *crq);

SpaprVioDevice *vty_lookup(SpaprMachineState *spapr, target_ulong reg);
void vty_putchars(SpaprVioDevice *sdev, uint8_t *buf, int len);
void spapr_vty_create(SpaprVioBus *bus, Chardev *chardev);
void spapr_vlan_create(SpaprVioBus *bus, NICInfo *nd);
void spapr_vscsi_create(SpaprVioBus *bus);

SpaprVioDevice *spapr_vty_get_default(SpaprVioBus *bus);

extern const VMStateDescription vmstate_spapr_vio;

#define VMSTATE_SPAPR_VIO(_f, _s) \
    VMSTATE_STRUCT(_f, _s, 0, vmstate_spapr_vio, SpaprVioDevice)

void spapr_vio_set_bypass(SpaprVioDevice *dev, bool bypass);

#endif /* HW_SPAPR_VIO_H */
