/*
 * Copyright (C) 2010       Citrix Ltd.
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#include "qemu-common.h"
#include "hw/xen.h"

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

int xen_init(void)
{
    return -ENOSYS;
}
