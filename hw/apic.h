#ifndef APIC_H
#define APIC_H

/* apic.c */
typedef struct APICState APICState;
void apic_deliver_irq(uint8_t dest, uint8_t dest_mode,
                             uint8_t delivery_mode,
                             uint8_t vector_num, uint8_t polarity,
                             uint8_t trigger_mode);
int apic_init(CPUState *env);
int apic_accept_pic_intr(APICState *s);
void apic_deliver_pic_intr(APICState *s, int level);
int apic_get_interrupt(APICState *s);
void apic_reset_irq_delivered(void);
int apic_get_irq_delivered(void);

int cpu_is_bsp(CPUState *env);

#endif
