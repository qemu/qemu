/* fdc.c */
#define MAX_FD 2

typedef struct fdctrl a_fdctrl;

a_fdctrl *fdctrl_init_isa(BlockDriverState **fds);
a_fdctrl *fdctrl_init_sysbus(qemu_irq irq, int dma_chann,
                             a_target_phys_addr mmio_base,
                             BlockDriverState **fds);
a_fdctrl *sun4m_fdctrl_init (qemu_irq irq, a_target_phys_addr io_base,
                             BlockDriverState **fds, qemu_irq *fdc_tc);
int fdctrl_get_drive_type(a_fdctrl *fdctrl, int drive_num);
