/*
 * ARM IoTKit system control element
 *
 * Copyright (c) 2018 Linaro Limited
 * Written by Peter Maydell
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 or
 *  (at your option) any later version.
 */

/*
 * This is a model of the "system control element" which is part of the
 * Arm IoTKit and documented in
 * http://infocenter.arm.com/help/index.jsp?topic=/com.arm.doc.ecm0601256/index.html
 * Specifically, it implements the "system information block" and
 * "system control register" blocks.
 *
 * QEMU interface:
 *  + QOM property "SYS_VERSION": value of the SYS_VERSION register of the
 *    system information block of the SSE
 *    (used to identify whether to provide SSE-200-only registers)
 *  + sysbus MMIO region 0: the system information register bank
 *  + sysbus MMIO region 1: the system control register bank
 */

#ifndef HW_MISC_IOTKIT_SYSCTL_H
#define HW_MISC_IOTKIT_SYSCTL_H

#include "hw/sysbus.h"

#define TYPE_IOTKIT_SYSCTL "iotkit-sysctl"
#define IOTKIT_SYSCTL(obj) OBJECT_CHECK(IoTKitSysCtl, (obj), \
                                        TYPE_IOTKIT_SYSCTL)

typedef struct IoTKitSysCtl {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion iomem;

    uint32_t secure_debug;
    uint32_t reset_syndrome;
    uint32_t reset_mask;
    uint32_t gretreg;
    uint32_t initsvtor0;
    uint32_t cpuwait;
    uint32_t wicctrl;
    uint32_t scsecctrl;
    uint32_t fclk_div;
    uint32_t sysclk_div;
    uint32_t clock_force;
    uint32_t initsvtor1;
    uint32_t nmi_enable;
    uint32_t ewctrl;
    uint32_t pdcm_pd_sys_sense;
    uint32_t pdcm_pd_sram0_sense;
    uint32_t pdcm_pd_sram1_sense;
    uint32_t pdcm_pd_sram2_sense;
    uint32_t pdcm_pd_sram3_sense;

    /* Properties */
    uint32_t sys_version;
    uint32_t cpuwait_rst;
    uint32_t initsvtor0_rst;
    uint32_t initsvtor1_rst;

    bool is_sse200;
} IoTKitSysCtl;

#endif
