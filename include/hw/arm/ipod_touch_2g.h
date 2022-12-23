#ifndef HW_ARM_IPOD_TOUCH_H
#define HW_ARM_IPOD_TOUCH_H

#include "exec/hwaddr.h"
#include "hw/boards.h"
#include "hw/arm/boot.h"
#include "cpu.h"
#include "hw/arm/ipod_touch_clock.h"
#include "hw/arm/ipod_touch_chipid.h"
#include "hw/arm/ipod_touch_gpio.h"

#define TYPE_IPOD_TOUCH "iPod-Touch"

#define IT2G_CPREG_VAR_NAME(name) cpreg_##name
#define IT2G_CPREG_VAR_DEF(name) uint64_t IT2G_CPREG_VAR_NAME(name)

#define TYPE_IPOD_TOUCH_MACHINE   MACHINE_TYPE_NAME(TYPE_IPOD_TOUCH)
#define IPOD_TOUCH_MACHINE(obj) \
    OBJECT_CHECK(IPodTouchMachineState, (obj), TYPE_IPOD_TOUCH_MACHINE)

// memory addresses
#define VROM_MEM_BASE   0x0
#define SRAM1_MEM_BASE  0x22020000
#define CLOCK0_MEM_BASE 0x3C500000
#define GPIO_MEM_BASE   0x3CF00000
#define CHIPID_MEM_BASE 0x3D100000
#define CLOCK1_MEM_BASE 0x3E000000

typedef struct {
    MachineClass parent;
} IPodTouchMachineClass;

typedef struct {
	MachineState parent;
	ARMCPU *cpu;
	IPodTouchClockState *clock0;
	IPodTouchClockState *clock1;
	IPodTouchChipIDState *chipid_state;
	IPodTouchGPIOState *gpio_state;
	IT2G_CPREG_VAR_DEF(REG0);
	IT2G_CPREG_VAR_DEF(REG1);
} IPodTouchMachineState;

#endif