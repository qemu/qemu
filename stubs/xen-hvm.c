/*
 * Copyright (C) 2010       Citrix Ltd.
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "hw/xen/xen.h"
#include "exec/memory.h"
#include "qapi/qapi-commands-misc.h"

int xen_pci_slot_get_pirq(PCIDevice *pci_dev, int irq_num)
{
    return -1;
}

void xen_piix3_set_irq(void *opaque, int irq_num, int level)
{
}

void xen_piix_pci_write_config_client(uint32_t address, uint32_t val, int len)
{
}

void xen_hvm_inject_msi(uint64_t addr, uint32_t data)
{
}

int xen_is_pirq_msi(uint32_t msi_data)
{
    return 0;
}

void xen_ram_alloc(ram_addr_t ram_addr, ram_addr_t size, MemoryRegion *mr,
                   Error **errp)
{
}

qemu_irq *xen_interrupt_controller_init(void)
{
    return NULL;
}

void xen_register_framebuffer(MemoryRegion *mr)
{
}

void xen_hvm_modified_memory(ram_addr_t start, ram_addr_t length)
{
}

void xen_hvm_init(PCMachineState *pcms, MemoryRegion **ram_memory)
{
}

void qmp_xen_set_global_dirty_log(bool enable, Error **errp)
{
}
