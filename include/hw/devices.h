#ifndef QEMU_DEVICES_H
#define QEMU_DEVICES_H

/* Devices that have nowhere better to go.  */

#include "hw/hw.h"

/* smc91c111.c */
void smc91c111_init(NICInfo *, uint32_t, qemu_irq);

/* lan9118.c */
void lan9118_init(NICInfo *, uint32_t, qemu_irq);

#endif
