#ifndef QEMU_HW_XEN_H
#define QEMU_HW_XEN_H

/*
 * public xen header
 *   stuff needed outside xen-*.c, i.e. interfaces to qemu.
 *   must not depend on any xen headers being present in
 *   /usr/include/xen, so it can be included unconditionally.
 */

#include "exec/cpu-common.h"

/* xen-machine.c */
enum xen_mode {
    XEN_EMULATE = 0,  // xen emulation, using xenner (default)
    XEN_ATTACH        // attach to xen domain created by libxl
};

extern uint32_t xen_domid;
extern enum xen_mode xen_mode;
extern bool xen_domid_restrict;

int xen_pci_slot_get_pirq(PCIDevice *pci_dev, int irq_num);
void xen_piix3_set_irq(void *opaque, int irq_num, int level);
void xen_piix_pci_write_config_client(uint32_t address, uint32_t val, int len);
void xen_hvm_inject_msi(uint64_t addr, uint32_t data);
int xen_is_pirq_msi(uint32_t msi_data);

qemu_irq *xen_interrupt_controller_init(void);

void xenstore_store_pv_console_info(int i, Chardev *chr);

void xen_register_framebuffer(struct MemoryRegion *mr);

#endif /* QEMU_HW_XEN_H */
