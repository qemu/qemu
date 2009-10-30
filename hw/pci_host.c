/*
 * pci_host.c
 *
 * Copyright (c) 2009 Isaku Yamahata <yamahata at valinux co jp>
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
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "pci.h"
#include "pci_host.h"

/* debug PCI */
//#define DEBUG_PCI

#ifdef DEBUG_PCI
#define PCI_DPRINTF(fmt, ...) \
do { printf("pci_host_data: " fmt , ## __VA_ARGS__); } while (0)
#else
#define PCI_DPRINTF(fmt, ...)
#endif

#define PCI_ADDR_T      target_phys_addr_t
#define PCI_HOST_SUFFIX _mmio

#include "pci_host_template.h"

static CPUWriteMemoryFunc * const pci_host_data_write_mmio[] = {
    pci_host_data_writeb_mmio,
    pci_host_data_writew_mmio,
    pci_host_data_writel_mmio,
};

static CPUReadMemoryFunc * const pci_host_data_read_mmio[] = {
    pci_host_data_readb_mmio,
    pci_host_data_readw_mmio,
    pci_host_data_readl_mmio,
};

int pci_host_data_register_io_memory(PCIHostState *s)
{
    return cpu_register_io_memory(pci_host_data_read_mmio,
                                  pci_host_data_write_mmio,
                                  s);
}

#undef PCI_ADDR_T
#undef PCI_HOST_SUFFIX

#define PCI_ADDR_T      uint32_t
#define PCI_HOST_SUFFIX _ioport

#include "pci_host_template.h"

void pci_host_data_register_ioport(pio_addr_t ioport, PCIHostState *s)
{
    register_ioport_write(ioport, 4, 1, pci_host_data_writeb_ioport, s);
    register_ioport_write(ioport, 4, 2, pci_host_data_writew_ioport, s);
    register_ioport_write(ioport, 4, 4, pci_host_data_writel_ioport, s);
    register_ioport_read(ioport, 4, 1, pci_host_data_readb_ioport, s);
    register_ioport_read(ioport, 4, 2, pci_host_data_readw_ioport, s);
    register_ioport_read(ioport, 4, 4, pci_host_data_readl_ioport, s);
}
