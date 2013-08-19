#ifndef HW_FDC_H
#define HW_FDC_H

#include "qemu-common.h"

/* fdc.c */
#define MAX_FD 2

typedef enum FDriveType {
    FDRIVE_DRV_144  = 0x00,   /* 1.44 MB 3"5 drive      */
    FDRIVE_DRV_288  = 0x01,   /* 2.88 MB 3"5 drive      */
    FDRIVE_DRV_120  = 0x02,   /* 1.2  MB 5"25 drive     */
    FDRIVE_DRV_NONE = 0x03,   /* No drive connected     */
} FDriveType;

#define TYPE_ISA_FDC "isa-fdc"

ISADevice *fdctrl_init_isa(ISABus *bus, DriveInfo **fds);
void fdctrl_init_sysbus(qemu_irq irq, int dma_chann,
                        hwaddr mmio_base, DriveInfo **fds);
void sun4m_fdctrl_init(qemu_irq irq, hwaddr io_base,
                       DriveInfo **fds, qemu_irq *fdc_tc);

FDriveType isa_fdc_get_drive_type(ISADevice *fdc, int i);

#endif
