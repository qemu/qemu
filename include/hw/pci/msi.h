/*
 * msi.h
 *
 * Copyright (c) 2010 Isaku Yamahata <yamahata at valinux co jp>
 *                    VA Linux Systems Japan K.K.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef QEMU_MSI_H
#define QEMU_MSI_H

#include "hw/pci/pci.h"

struct MSIMessage {
    uint64_t address;
    uint32_t data;
};

extern bool msi_nonbroken;

void msi_set_message(PCIDevice *dev, MSIMessage msg);
MSIMessage msi_get_message(PCIDevice *dev, unsigned int vector);
bool msi_enabled(const PCIDevice *dev);
int msi_init(struct PCIDevice *dev, uint8_t offset,
             unsigned int nr_vectors, bool msi64bit,
             bool msi_per_vector_mask, Error **errp);
void msi_uninit(struct PCIDevice *dev);
void msi_reset(PCIDevice *dev);
bool msi_is_masked(const PCIDevice *dev, unsigned int vector);
void msi_notify(PCIDevice *dev, unsigned int vector);
void msi_send_message(PCIDevice *dev, MSIMessage msg);
void msi_write_config(PCIDevice *dev, uint32_t addr, uint32_t val, int len);
unsigned int msi_nr_vectors_allocated(const PCIDevice *dev);
void msi_set_mask(PCIDevice *dev, int vector, bool mask, Error **errp);

static inline bool msi_present(const PCIDevice *dev)
{
    return dev->cap_present & QEMU_PCI_CAP_MSI;
}

#endif /* QEMU_MSI_H */
