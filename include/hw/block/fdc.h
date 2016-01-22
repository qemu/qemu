#ifndef HW_FDC_H
#define HW_FDC_H

#include "qemu-common.h"

/* fdc.c */
#define MAX_FD 2

#define TYPE_ISA_FDC "isa-fdc"

ISADevice *fdctrl_init_isa(ISABus *bus, DriveInfo **fds);
void fdctrl_init_sysbus(qemu_irq irq, int dma_chann,
                        hwaddr mmio_base, DriveInfo **fds);
void sun4m_fdctrl_init(qemu_irq irq, hwaddr io_base,
                       DriveInfo **fds, qemu_irq *fdc_tc);

FloppyDriveType isa_fdc_get_drive_type(ISADevice *fdc, int i);

#endif
