#ifndef HW_CD3272_MIKEY_H
#define HW_CD3272_MIKEY_H

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "hw/sysbus.h"
#include "hw/i2c/i2c.h"
#include "hw/irq.h"
#include "time.h"

#define TYPE_CD3272MIKEY                 "cd3272mikey"
OBJECT_DECLARE_SIMPLE_TYPE(CD3272MikeyState, CD3272MIKEY)

typedef struct CD3272MikeyState {
	I2CSlave i2c;
	uint32_t cmd;
} CD3272MikeyState;

#endif