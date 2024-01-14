#ifndef HW_FDC_H
#define HW_FDC_H

#include "exec/hwaddr.h"
#include "qapi/qapi-types-block.h"

/* fdc.c */
#define MAX_FD 2

#define TYPE_ISA_FDC "isa-fdc"

void isa_fdc_init_drives(ISADevice *fdc, DriveInfo **fds);
void fdctrl_init_sysbus(qemu_irq irq, hwaddr mmio_base, DriveInfo **fds);
void sun4m_fdctrl_init(qemu_irq irq, hwaddr io_base,
                       DriveInfo **fds, qemu_irq *fdc_tc);

void isa_fdc_set_iobase(ISADevice *fdc, hwaddr iobase);
void isa_fdc_set_enabled(ISADevice *fdc, bool enabled);

FloppyDriveType isa_fdc_get_drive_type(ISADevice *fdc, int i);
int cmos_get_fd_drive_type(FloppyDriveType fd0);

#endif
