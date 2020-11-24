/*
 * Copyright (c) 2020 Nanosonics
 *
 * Nanosonics IMX6UL PWM emulation.
 *
 *
 * This code is licensed under the GPL, version 2 or later.
 * See the file `COPYING' in the top level directory.
 *
 * It (partially) emulates nanosonics platform with a Freescale
 * i.MX6ul SoC
 */

#ifndef NANO_PWM_H
#define NANO_PWM_H

#include "hw/sysbus.h"


#define TYPE_NANOPWM "nano_pwm"

struct NANOPWMState {
    SysBusDevice        parent_obj;

    MemoryRegion        iomem;

    unsigned int        pwm_index;

    //pwm related
    qemu_irq            pwm_irq;
	unsigned int	    pwm_cr;
	unsigned int	    pwm_sr;
	unsigned int	    pwm_ir;
    unsigned int        pwm_sar;
	unsigned int	    pwm_pr;
	unsigned int	    pwm_cnr;
};
typedef struct NANOPWMState NANOPWMState;

#define NANOPWM(obj) OBJECT_CHECK(NANOPWMState, (obj), TYPE_NANOPWM)

#endif
