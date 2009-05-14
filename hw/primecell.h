#ifndef PRIMECELL_H
#define PRIMECELL_H

/* Declarations for ARM PrimeCell based periperals.  */
/* Also includes some devices that are currently only used by the
   ARM boards.  */

/* pl022.c */
typedef int (*ssi_xfer_cb)(void *, int);
void pl022_init(uint32_t base, qemu_irq irq, ssi_xfer_cb xfer_cb,
                void *opaque);

/* pl061.c */
void pl061_float_high(void *opaque, uint8_t mask);
qemu_irq *pl061_init(uint32_t base, qemu_irq irq, qemu_irq **out);

/* pl080.c */
void *pl080_init(uint32_t base, qemu_irq irq, int nchannels);

/* pl181.c */
void pl181_init(uint32_t base, BlockDriverState *bd,
                qemu_irq irq0, qemu_irq irq1);

/* pl190.c */
qemu_irq *pl190_init(uint32_t base, qemu_irq irq, qemu_irq fiq);

/* realview_gic.c */
qemu_irq *realview_gic_init(uint32_t base, qemu_irq parent_irq);

/* mpcore.c */
extern qemu_irq *mpcore_irq_init(qemu_irq *cpu_irq);

/* arm-timer.c */
void sp804_init(uint32_t base, qemu_irq irq);
void icp_pit_init(uint32_t base, qemu_irq *pic, int irq);

/* arm_sysctl.c */
void arm_sysctl_init(uint32_t base, uint32_t sys_id);

/* versatile_pci.c */
PCIBus *pci_vpb_init(qemu_irq *pic, int irq, int realview);

#endif
