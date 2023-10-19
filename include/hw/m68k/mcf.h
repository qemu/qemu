#ifndef HW_MCF_H
#define HW_MCF_H
/* Motorola ColdFire device prototypes.  */

#include "exec/hwaddr.h"
#include "target/m68k/cpu-qom.h"

/* mcf_uart.c */
uint64_t mcf_uart_read(void *opaque, hwaddr addr,
                       unsigned size);
void mcf_uart_write(void *opaque, hwaddr addr,
                    uint64_t val, unsigned size);
DeviceState *mcf_uart_create(qemu_irq irq, Chardev *chr);
DeviceState *mcf_uart_create_mmap(hwaddr base, qemu_irq irq, Chardev *chr);

/* mcf_intc.c */
qemu_irq *mcf_intc_init(struct MemoryRegion *sysmem,
                        hwaddr base,
                        M68kCPU *cpu);

/* mcf5206.c */
#define TYPE_MCF5206_MBAR "mcf5206-mbar"

#endif
