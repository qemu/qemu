#ifndef HW_FDC_H
#define HW_FDC_H

/* fdc.c */
#define MAX_FD 2

typedef struct FDCtrl FDCtrl;

FDCtrl *fdctrl_init_isa(DriveInfo **fds);
FDCtrl *fdctrl_init_sysbus(qemu_irq irq, int dma_chann,
                           target_phys_addr_t mmio_base, DriveInfo **fds);
FDCtrl *sun4m_fdctrl_init(qemu_irq irq, target_phys_addr_t io_base,
                          DriveInfo **fds, qemu_irq *fdc_tc);
int fdctrl_get_drive_type(FDCtrl *fdctrl, int drive_num);

#endif
