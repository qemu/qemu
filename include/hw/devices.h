#ifndef QEMU_DEVICES_H
#define QEMU_DEVICES_H

/* Devices that have nowhere better to go.  */

#include "hw/hw.h"
#include "ui/console.h"

/* smc91c111.c */
void smc91c111_init(NICInfo *, uint32_t, qemu_irq);

/* lan9118.c */
void lan9118_init(NICInfo *, uint32_t, qemu_irq);

/* tsc210x.c */
uWireSlave *tsc2102_init(qemu_irq pint);
uWireSlave *tsc2301_init(qemu_irq penirq, qemu_irq kbirq, qemu_irq dav);
I2SCodec *tsc210x_codec(uWireSlave *chip);
uint32_t tsc210x_txrx(void *opaque, uint32_t value, int len);
void tsc210x_set_transform(uWireSlave *chip,
                MouseTransformInfo *info);
void tsc210x_key_event(uWireSlave *chip, int key, int down);

/* tsc2005.c */
void *tsc2005_init(qemu_irq pintdav);
uint32_t tsc2005_txrx(void *opaque, uint32_t value, int len);
void tsc2005_set_transform(void *opaque, MouseTransformInfo *info);

/* stellaris_input.c */
void stellaris_gamepad_init(int n, qemu_irq *irq, const int *keycode);

/* cbus.c */
typedef struct {
    qemu_irq clk;
    qemu_irq dat;
    qemu_irq sel;
} CBus;
CBus *cbus_init(qemu_irq dat_out);
void cbus_attach(CBus *bus, void *slave_opaque);

void *retu_init(qemu_irq irq, int vilma);
void *tahvo_init(qemu_irq irq, int betty);

void retu_key_event(void *retu, int state);

#endif
