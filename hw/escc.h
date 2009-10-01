/* escc.c */
#define ESCC_SIZE 4
int escc_init(a_target_phys_addr base, qemu_irq irqA, qemu_irq irqB,
              CharDriverState *chrA, CharDriverState *chrB,
              int clock, int it_shift);

void slavio_serial_ms_kbd_init(a_target_phys_addr base, qemu_irq irq,
                               int disabled, int clock, int it_shift);
