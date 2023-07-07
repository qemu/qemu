#ifndef HW_LIS302DL_H
#define HW_LIS302DL_H

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "hw/sysbus.h"
#include "hw/i2c/i2c.h"
#include "hw/irq.h"

#define TYPE_LIS302DL                 "lis302dl"
OBJECT_DECLARE_SIMPLE_TYPE(LIS302DLState, LIS302DL)

#define ACCEL_WHOAMI	0x0F

#define ACCEL_WHOAMI_VALUE	0x3B

typedef struct LIS302DLState {
	I2CSlave i2c;
	uint32_t cmd;
} LIS302DLState;

#endif