#ifndef QEMU_HW_XEN_H
#define QEMU_HW_XEN_H 1
/*
 * public xen header
 *   stuff needed outside xen-*.c, i.e. interfaces to qemu.
 *   must not depend on any xen headers being present in
 *   /usr/include/xen, so it can be included unconditionally.
 */
#include <inttypes.h>

#include "qemu-common.h"

/* xen-machine.c */
enum xen_mode {
    XEN_EMULATE = 0,  // xen emulation, using xenner (default)
    XEN_CREATE,       // create xen domain
    XEN_ATTACH        // attach to xen domain created by xend
};

extern uint32_t xen_domid;
extern enum xen_mode xen_mode;

extern int xen_allowed;

static inline int xen_enabled(void)
{
#if defined(CONFIG_XEN_BACKEND) && !defined(CONFIG_NO_XEN)
    return xen_allowed;
#else
    return 0;
#endif
}

int xen_pci_slot_get_pirq(PCIDevice *pci_dev, int irq_num);
void xen_piix3_set_irq(void *opaque, int irq_num, int level);
void xen_piix_pci_write_config_client(uint32_t address, uint32_t val, int len);
void xen_cmos_set_s3_resume(void *opaque, int irq, int level);

qemu_irq *xen_interrupt_controller_init(void);

int xen_init(void);
int xen_hvm_init(void);
void xen_vcpu_init(void);
void xenstore_store_pv_console_info(int i, struct CharDriverState *chr);

#if defined(NEED_CPU_H) && !defined(CONFIG_USER_ONLY)
void xen_ram_alloc(ram_addr_t ram_addr, ram_addr_t size);
#endif

#if defined(CONFIG_XEN) && CONFIG_XEN_CTRL_INTERFACE_VERSION < 400
#  define HVM_MAX_VCPUS 32
#endif

#endif /* QEMU_HW_XEN_H */
