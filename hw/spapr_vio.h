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

#define SPAPR_VIO_TCE_PAGE_SHIFT   12
#define SPAPR_VIO_TCE_PAGE_SIZE    (1ULL << SPAPR_VIO_TCE_PAGE_SHIFT)
#define SPAPR_VIO_TCE_PAGE_MASK    (SPAPR_VIO_TCE_PAGE_SIZE - 1)

enum VIOsPAPR_TCEAccess {
    SPAPR_TCE_FAULT = 0,
    SPAPR_TCE_RO = 1,
    SPAPR_TCE_WO = 2,
    SPAPR_TCE_RW = 3,
};

struct VIOsPAPRDevice;

typedef struct VIOsPAPR_RTCE {
    uint64_t tce;
} VIOsPAPR_RTCE;

typedef struct VIOsPAPR_CRQ {
    uint64_t qladdr;
    uint32_t qsize;
    uint32_t qnext;
    int(*SendFunc)(struct VIOsPAPRDevice *vdev, uint8_t *crq);
} VIOsPAPR_CRQ;

typedef struct VIOsPAPRDevice {
    DeviceState qdev;
    uint32_t reg;
    qemu_irq qirq;
    uint32_t vio_irq_num;
    target_ulong signal_state;
    uint32_t rtce_window_size;
    VIOsPAPR_RTCE *rtce_table;
    VIOsPAPR_CRQ crq;
} VIOsPAPRDevice;

typedef struct VIOsPAPRBus {
    BusState bus;
} VIOsPAPRBus;

typedef struct {
    DeviceInfo qdev;
    const char *dt_name, *dt_type, *dt_compatible;
    target_ulong signal_mask;
    int (*init)(VIOsPAPRDevice *dev);
    void (*hcalls)(VIOsPAPRBus *bus);
    int (*devnode)(VIOsPAPRDevice *dev, void *fdt, int node_off);
} VIOsPAPRDeviceInfo;

extern VIOsPAPRBus *spapr_vio_bus_init(void);
extern VIOsPAPRDevice *spapr_vio_find_by_reg(VIOsPAPRBus *bus, uint32_t reg);
extern void spapr_vio_bus_register_withprop(VIOsPAPRDeviceInfo *info);
extern int spapr_populate_vdevice(VIOsPAPRBus *bus, void *fdt);

extern int spapr_vio_signal(VIOsPAPRDevice *dev, target_ulong mode);

int spapr_vio_check_tces(VIOsPAPRDevice *dev, target_ulong ioba,
                         target_ulong len,
                         enum VIOsPAPR_TCEAccess access);

int spapr_tce_dma_read(VIOsPAPRDevice *dev, uint64_t taddr,
                       void *buf, uint32_t size);
int spapr_tce_dma_write(VIOsPAPRDevice *dev, uint64_t taddr,
                        const void *buf, uint32_t size);
int spapr_tce_dma_zero(VIOsPAPRDevice *dev, uint64_t taddr, uint32_t size);
void stb_tce(VIOsPAPRDevice *dev, uint64_t taddr, uint8_t val);
void sth_tce(VIOsPAPRDevice *dev, uint64_t taddr, uint16_t val);
void stw_tce(VIOsPAPRDevice *dev, uint64_t taddr, uint32_t val);
void stq_tce(VIOsPAPRDevice *dev, uint64_t taddr, uint64_t val);
uint64_t ldq_tce(VIOsPAPRDevice *dev, uint64_t taddr);

int spapr_vio_send_crq(VIOsPAPRDevice *dev, uint8_t *crq);

void vty_putchars(VIOsPAPRDevice *sdev, uint8_t *buf, int len);
void spapr_vty_create(VIOsPAPRBus *bus,
                      uint32_t reg, CharDriverState *chardev,
                      qemu_irq qirq, uint32_t vio_irq_num);

void spapr_vlan_create(VIOsPAPRBus *bus, uint32_t reg, NICInfo *nd,
                       qemu_irq qirq, uint32_t vio_irq_num);

void spapr_vscsi_create(VIOsPAPRBus *bus, uint32_t reg,
                        qemu_irq qirq, uint32_t vio_irq_num);

#endif /* _HW_SPAPR_VIO_H */
