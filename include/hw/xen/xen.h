/*
 * public xen header
 *   stuff needed outside xen-*.c, i.e. interfaces to qemu.
 *   must not depend on any xen headers being present in
 *   /usr/include/xen, so it can be included unconditionally.
 */
#ifndef QEMU_HW_XEN_H
#define QEMU_HW_XEN_H

/*
 * C files using Xen toolstack libraries will have included those headers
 * already via xen_native.h, and having __XEM_TOOLS__ defined will have
 * automatically set __XEN_INTERFACE_VERSION__ to the latest supported
 * by the *system* Xen headers which were transitively included.
 *
 * C files which are part of the internal emulation, and which did not
 * include xen_native.h, may need this defined so that the Xen headers
 * imported to include/hw/xen/interface/ will expose the appropriate API
 * version.
 *
 * This is why there's a rule that xen_native.h must be included first.
 */
#ifndef __XEN_INTERFACE_VERSION__
#define __XEN_INTERFACE_VERSION__ 0x00040e00
#endif

/* xen-machine.c */
enum xen_mode {
    XEN_DISABLED = 0, /* xen support disabled (default) */
    XEN_ATTACH,       /* attach to xen domain created by libxl */
    XEN_EMULATE,      /* emulate Xen within QEMU */
};

extern uint32_t xen_domid;
extern enum xen_mode xen_mode;
extern bool xen_domid_restrict;
extern bool xen_is_stubdomain;

int xen_pci_slot_get_pirq(PCIDevice *pci_dev, int irq_num);
int xen_set_pci_link_route(uint8_t link, uint8_t irq);
void xen_intx_set_irq(void *opaque, int irq_num, int level);
void xen_hvm_inject_msi(uint64_t addr, uint32_t data);
int xen_is_pirq_msi(uint32_t msi_data);

qemu_irq *xen_interrupt_controller_init(void);

void xen_register_framebuffer(struct MemoryRegion *mr);

#endif /* QEMU_HW_XEN_H */
