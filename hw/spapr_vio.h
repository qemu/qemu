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

typedef struct VIOsPAPRDevice {
    DeviceState qdev;
    uint32_t reg;
} VIOsPAPRDevice;

typedef struct VIOsPAPRBus {
    BusState bus;
} VIOsPAPRBus;

typedef struct {
    DeviceInfo qdev;
    const char *dt_name, *dt_type, *dt_compatible;
    int (*init)(VIOsPAPRDevice *dev);
    void (*hcalls)(VIOsPAPRBus *bus);
    int (*devnode)(VIOsPAPRDevice *dev, void *fdt, int node_off);
} VIOsPAPRDeviceInfo;

extern VIOsPAPRBus *spapr_vio_bus_init(void);
extern VIOsPAPRDevice *spapr_vio_find_by_reg(VIOsPAPRBus *bus, uint32_t reg);
extern void spapr_vio_bus_register_withprop(VIOsPAPRDeviceInfo *info);
extern int spapr_populate_vdevice(VIOsPAPRBus *bus, void *fdt);

void vty_putchars(VIOsPAPRDevice *sdev, uint8_t *buf, int len);
void spapr_vty_create(VIOsPAPRBus *bus,
                      uint32_t reg, CharDriverState *chardev);

#endif /* _HW_SPAPR_VIO_H */
