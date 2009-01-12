/* escc.c */
#define ESCC_SIZE 4
int escc_init(target_phys_addr_t base, qemu_irq irq, CharDriverState *chr1,
              CharDriverState *chr2, int clock, int it_shift);

void slavio_serial_ms_kbd_init(target_phys_addr_t base, qemu_irq irq,
                               int disabled, int clock, int it_shift);
