#ifndef QEMU_HW_XEN_H
#define QEMU_HW_XEN_H 1
/*
 * public xen header
 *   stuff needed outside xen-*.c, i.e. interfaces to qemu.
 *   must not depend on any xen headers being present in
 *   /usr/include/xen, so it can be included unconditionally.
 */

#include "qemu-common.h"
#include "qemu/typedefs.h"
#include "exec/cpu-common.h"
#include "hw/irq.h"

/* xen-machine.c */
enum xen_mode {
    XEN_EMULATE = 0,  // xen emulation, using xenner (default)
    XEN_CREATE,       // create xen domain
    XEN_ATTACH        // attach to xen domain created by xend
};

extern uint32_t xen_domid;
extern enum xen_mode xen_mode;

extern bool xen_allowed;

static inline bool xen_enabled(void)
{
    return xen_allowed;
}

int xen_pci_slot_get_pirq(PCIDevice *pci_dev, int irq_num);
void xen_piix3_set_irq(void *opaque, int irq_num, int level);
void xen_piix_pci_write_config_client(uint32_t address, uint32_t val, int len);
void xen_hvm_inject_msi(uint64_t addr, uint32_t data);
int xen_is_pirq_msi(uint32_t msi_data);

qemu_irq *xen_interrupt_controller_init(void);

void xenstore_store_pv_console_info(int i, struct CharDriverState *chr);

void xen_hvm_init(PCMachineState *pcms, MemoryRegion **ram_memory);

void xen_ram_alloc(ram_addr_t ram_addr, ram_addr_t size,
                   struct MemoryRegion *mr, Error **errp);
void xen_modified_memory(ram_addr_t start, ram_addr_t length);

void xen_register_framebuffer(struct MemoryRegion *mr);

#endif /* QEMU_HW_XEN_H */
