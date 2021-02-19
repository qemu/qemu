/*
 * ARM MPS2 FPGAIO emulation
 *
 * Copyright (c) 2018 Linaro Limited
 * Written by Peter Maydell
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 or
 *  (at your option) any later version.
 */

/* This is a model of the FPGAIO register block in the AN505
 * FPGA image for the MPS2 dev board; it is documented in the
 * application note:
 * https://developer.arm.com/documentation/dai0505/latest/
 *
 * QEMU interface:
 *  + sysbus MMIO region 0: the register bank
 */

#ifndef MPS2_FPGAIO_H
#define MPS2_FPGAIO_H

#include "hw/sysbus.h"
#include "hw/misc/led.h"
#include "qom/object.h"

#define TYPE_MPS2_FPGAIO "mps2-fpgaio"
OBJECT_DECLARE_SIMPLE_TYPE(MPS2FPGAIO, MPS2_FPGAIO)

#define MPS2FPGAIO_MAX_LEDS 32

struct MPS2FPGAIO {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion iomem;
    LEDState *led[MPS2FPGAIO_MAX_LEDS];
    uint32_t num_leds;
    bool has_switches;
    bool has_dbgctrl;

    uint32_t led0;
    uint32_t prescale;
    uint32_t misc;
    uint32_t dbgctrl;

    /* QEMU_CLOCK_VIRTUAL time at which counter and pscntr were last synced */
    int64_t pscntr_sync_ticks;
    /* Values of COUNTER and PSCNTR at time pscntr_sync_ticks */
    uint32_t counter;
    uint32_t pscntr;

    uint32_t prescale_clk;

    /* These hold the CLOCK_VIRTUAL ns tick when the CLK1HZ/CLK100HZ was zero */
    int64_t clk1hz_tick_offset;
    int64_t clk100hz_tick_offset;
};

#endif
