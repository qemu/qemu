/*
 * Copyright (C) 2010       Citrix Ltd.
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#include "hw/pci.h"
#include "hw/xen_common.h"
#include "hw/xen_backend.h"

#include "xen-mapcache.h"
#include "trace.h"

/* Xen specific function for piix pci */

int xen_pci_slot_get_pirq(PCIDevice *pci_dev, int irq_num)
{
    return irq_num + ((pci_dev->devfn >> 3) << 2);
}

void xen_piix3_set_irq(void *opaque, int irq_num, int level)
{
    xc_hvm_set_pci_intx_level(xen_xc, xen_domid, 0, 0, irq_num >> 2,
                              irq_num & 3, level);
}

void xen_piix_pci_write_config_client(uint32_t address, uint32_t val, int len)
{
    int i;

    /* Scan for updates to PCI link routes (0x60-0x63). */
    for (i = 0; i < len; i++) {
        uint8_t v = (val >> (8 * i)) & 0xff;
        if (v & 0x80) {
            v = 0;
        }
        v &= 0xf;
        if (((address + i) >= 0x60) && ((address + i) <= 0x63)) {
            xc_hvm_set_pci_link_route(xen_xc, xen_domid, address + i - 0x60, v);
        }
    }
}

/* Xen Interrupt Controller */

static void xen_set_irq(void *opaque, int irq, int level)
{
    xc_hvm_set_isa_irq_level(xen_xc, xen_domid, irq, level);
}

qemu_irq *xen_interrupt_controller_init(void)
{
    return qemu_allocate_irqs(xen_set_irq, NULL, 16);
}

/* Memory Ops */

static void xen_ram_init(ram_addr_t ram_size)
{
    RAMBlock *new_block;
    ram_addr_t below_4g_mem_size, above_4g_mem_size = 0;

    new_block = qemu_mallocz(sizeof (*new_block));
    pstrcpy(new_block->idstr, sizeof (new_block->idstr), "xen.ram");
    new_block->host = NULL;
    new_block->offset = 0;
    new_block->length = ram_size;

    QLIST_INSERT_HEAD(&ram_list.blocks, new_block, next);

    ram_list.phys_dirty = qemu_realloc(ram_list.phys_dirty,
                                       new_block->length >> TARGET_PAGE_BITS);
    memset(ram_list.phys_dirty + (new_block->offset >> TARGET_PAGE_BITS),
           0xff, new_block->length >> TARGET_PAGE_BITS);

    if (ram_size >= 0xe0000000 ) {
        above_4g_mem_size = ram_size - 0xe0000000;
        below_4g_mem_size = 0xe0000000;
    } else {
        below_4g_mem_size = ram_size;
    }

    cpu_register_physical_memory(0, below_4g_mem_size, new_block->offset);
#if TARGET_PHYS_ADDR_BITS > 32
    if (above_4g_mem_size > 0) {
        cpu_register_physical_memory(0x100000000ULL, above_4g_mem_size,
                                     new_block->offset + below_4g_mem_size);
    }
#endif
}

void xen_ram_alloc(ram_addr_t ram_addr, ram_addr_t size)
{
    unsigned long nr_pfn;
    xen_pfn_t *pfn_list;
    int i;

    trace_xen_ram_alloc(ram_addr, size);

    nr_pfn = size >> TARGET_PAGE_BITS;
    pfn_list = qemu_malloc(sizeof (*pfn_list) * nr_pfn);

    for (i = 0; i < nr_pfn; i++) {
        pfn_list[i] = (ram_addr >> TARGET_PAGE_BITS) + i;
    }

    if (xc_domain_populate_physmap_exact(xen_xc, xen_domid, nr_pfn, 0, 0, pfn_list)) {
        hw_error("xen: failed to populate ram at %lx", ram_addr);
    }

    qemu_free(pfn_list);
}


/* VCPU Operations, MMIO, IO ring ... */

static void xen_reset_vcpu(void *opaque)
{
    CPUState *env = opaque;

    env->halted = 1;
}

void xen_vcpu_init(void)
{
    CPUState *first_cpu;

    if ((first_cpu = qemu_get_cpu(0))) {
        qemu_register_reset(xen_reset_vcpu, first_cpu);
        xen_reset_vcpu(first_cpu);
    }
}

/* Initialise Xen */

int xen_init(void)
{
    xen_xc = xen_xc_interface_open(0, 0, 0);
    if (xen_xc == XC_HANDLER_INITIAL_VALUE) {
        xen_be_printf(NULL, 0, "can't open xen interface\n");
        return -1;
    }

    return 0;
}

int xen_hvm_init(void)
{
    /* Init RAM management */
    qemu_map_cache_init();
    xen_ram_init(ram_size);

    return 0;
}
