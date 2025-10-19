#ifndef APIC_H
#define APIC_H

typedef struct APICCommonState APICCommonState;

/* apic.c */
void apic_set_max_apic_id(uint32_t max_apic_id);
int apic_accept_pic_intr(APICCommonState *s);
void apic_deliver_pic_intr(APICCommonState *s, int level);
void apic_deliver_nmi(APICCommonState *s);
int apic_get_interrupt(APICCommonState *s);
int cpu_set_apic_base(APICCommonState *s, uint64_t val);
uint64_t cpu_get_apic_base(APICCommonState *s);
bool cpu_is_apic_enabled(APICCommonState *s);
void cpu_set_apic_tpr(APICCommonState *s, uint8_t val);
uint8_t cpu_get_apic_tpr(APICCommonState *s);
void apic_init_reset(APICCommonState *s);
void apic_sipi(APICCommonState *s);
void apic_poll_irq(APICCommonState *s);
void apic_designate_bsp(APICCommonState *s, bool bsp);
int apic_get_highest_priority_irr(APICCommonState *s);
int apic_msr_read(APICCommonState *s, int index, uint64_t *val);
int apic_msr_write(APICCommonState *s, int index, uint64_t val);
bool is_x2apic_mode(APICCommonState *s);

/* pc.c */
APICCommonState *cpu_get_current_apic(void);

#endif
