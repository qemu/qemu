/* fdc.c */
#include "sysemu.h"
#define MAX_FD 2

typedef struct fdctrl_t fdctrl_t;

fdctrl_t *fdctrl_init_isa(DriveInfo **fds);
fdctrl_t *fdctrl_init_sysbus(qemu_irq irq, int dma_chann,
                             target_phys_addr_t mmio_base,
                             DriveInfo **fds);
fdctrl_t *sun4m_fdctrl_init (qemu_irq irq, target_phys_addr_t io_base,
                             DriveInfo **fds, qemu_irq *fdc_tc);
int fdctrl_get_drive_type(fdctrl_t *fdctrl, int drive_num);
