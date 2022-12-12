/*
 * public xen header
 *   stuff needed outside xen-*.c, i.e. interfaces to qemu.
 *   must not depend on any xen headers being present in
 *   /usr/include/xen, so it can be included unconditionally.
 */
#ifndef QEMU_HW_XEN_H
#define QEMU_HW_XEN_H

/*
 * As a temporary measure while the headers are being untangled, define
 * __XEN_TOOLS__ here before any Xen headers are included. Otherwise, if
 * the Xen toolstack library headers are later included, they will find
 * some of the "internal" definitions missing and the build will fail. In
 * later commits, we'll end up with a rule that the native libraries have
 * to be included first, which will ensure that the libraries get the
 * version of Xen libraries that they expect.
 */
#define __XEN_TOOLS__ 1

#include "exec/cpu-common.h"

/* xen-machine.c */
enum xen_mode {
    XEN_DISABLED = 0, /* xen support disabled (default) */
    XEN_ATTACH,       /* attach to xen domain created by libxl */
    XEN_EMULATE,      /* emulate Xen within QEMU */
};

extern uint32_t xen_domid;
extern enum xen_mode xen_mode;
extern bool xen_domid_restrict;

int xen_pci_slot_get_pirq(PCIDevice *pci_dev, int irq_num);
int xen_set_pci_link_route(uint8_t link, uint8_t irq);
void xen_piix3_set_irq(void *opaque, int irq_num, int level);
void xen_hvm_inject_msi(uint64_t addr, uint32_t data);
int xen_is_pirq_msi(uint32_t msi_data);

qemu_irq *xen_interrupt_controller_init(void);

void xenstore_store_pv_console_info(int i, Chardev *chr);

void xen_register_framebuffer(struct MemoryRegion *mr);

#endif /* QEMU_HW_XEN_H */
