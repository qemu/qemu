#ifndef HW_FDC_H
#define HW_FDC_H

#include "qemu-common.h"

/* fdc.c */
#define MAX_FD 2

ISADevice *fdctrl_init_isa(ISABus *bus, DriveInfo **fds);
void fdctrl_init_sysbus(qemu_irq irq, int dma_chann,
                        target_phys_addr_t mmio_base, DriveInfo **fds);
void sun4m_fdctrl_init(qemu_irq irq, target_phys_addr_t io_base,
                       DriveInfo **fds, qemu_irq *fdc_tc);
void fdc_get_bs(BlockDriverState *bs[], ISADevice *dev);

#endif
