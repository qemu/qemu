#ifndef APIC_H
#define APIC_H

typedef struct IOAPICState IOAPICState;
void apic_deliver_irq(uint8_t dest, uint8_t dest_mode,
                             uint8_t delivery_mode,
                             uint8_t vector_num, uint8_t polarity,
                             uint8_t trigger_mode);
int apic_init(CPUState *env);
int apic_accept_pic_intr(CPUState *env);
void apic_deliver_pic_intr(CPUState *env, int level);
int apic_get_interrupt(CPUState *env);
qemu_irq *ioapic_init(void);
void apic_reset_irq_delivered(void);
int apic_get_irq_delivered(void);

int cpu_is_bsp(CPUState *env);

#endif
