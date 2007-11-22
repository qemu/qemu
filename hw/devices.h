#ifndef QEMU_DEVICES_H
#define QEMU_DEVICES_H

/* Devices that have nowhere better to go.  */

/* smc91c111.c */
void smc91c111_init(NICInfo *, uint32_t, qemu_irq);

/* ssd0323.c */
int ssd0323_xfer_ssi(void *opaque, int data);
void *ssd0323_init(DisplayState *ds, qemu_irq *cmd_p);

/* ads7846.c */
struct ads7846_state_s;
uint32_t ads7846_read(void *opaque);
void ads7846_write(void *opaque, uint32_t value);
struct ads7846_state_s *ads7846_init(qemu_irq penirq);

/* stellaris_input.c */
void stellaris_gamepad_init(int n, qemu_irq *irq, const int *keycode);

#endif
