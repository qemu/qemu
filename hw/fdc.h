#ifndef HW_FDC_H
#define HW_FDC_H

#include "isa.h"
#include "blockdev.h"

/* fdc.c */
#define MAX_FD 2

static inline void fdctrl_init_isa(DriveInfo **fds)
{
    ISADevice *dev;

    dev = isa_try_create("isa-fdc");
    if (!dev) {
        return;
    }
    if (fds[0]) {
        qdev_prop_set_drive_nofail(&dev->qdev, "driveA", fds[0]->bdrv);
    }
    if (fds[1]) {
        qdev_prop_set_drive_nofail(&dev->qdev, "driveB", fds[1]->bdrv);
    }
    qdev_init_nofail(&dev->qdev);
}

void fdctrl_init_sysbus(qemu_irq irq, int dma_chann,
                        target_phys_addr_t mmio_base, DriveInfo **fds);
void sun4m_fdctrl_init(qemu_irq irq, target_phys_addr_t io_base,
                       DriveInfo **fds, qemu_irq *fdc_tc);
#endif
