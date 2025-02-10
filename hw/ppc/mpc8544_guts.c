/*
 * QEMU PowerPC MPC8544 global util pseudo-device
 *
 * Copyright (C) 2011 Freescale Semiconductor, Inc. All rights reserved.
 *
 * Author: Alexander Graf, <alex@csgraf.de>
 *
 * This is free software; you can redistribute it and/or modify
 * it under the terms of  the GNU General  Public License as published by
 * the Free Software Foundation;  either version 2 of the  License, or
 * (at your option) any later version.
 *
 * *****************************************************************
 *
 * The documentation for this device is noted in the MPC8544 documentation,
 * file name "MPC8544ERM.pdf". You can easily find it on the web.
 *
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "system/runstate.h"
#include "cpu.h"
#include "hw/sysbus.h"
#include "qom/object.h"

#define MPC8544_GUTS_MMIO_SIZE        0x1000
#define MPC8544_GUTS_RSTCR_RESET      0x02

#define MPC8544_GUTS_ADDR_PORPLLSR    0x00
REG32(GUTS_PORPLLSR, 0x00)
    FIELD(GUTS_PORPLLSR, E500_1_RATIO, 24, 6)
    FIELD(GUTS_PORPLLSR, E500_0_RATIO, 16, 6)
    FIELD(GUTS_PORPLLSR, DDR_RATIO, 9, 5)
    FIELD(GUTS_PORPLLSR, PLAT_RATIO, 1, 5)

#define MPC8544_GUTS_ADDR_PORBMSR     0x04
#define MPC8544_GUTS_ADDR_PORIMPSCR   0x08
#define MPC8544_GUTS_ADDR_PORDEVSR    0x0C
#define MPC8544_GUTS_ADDR_PORDBGMSR   0x10
#define MPC8544_GUTS_ADDR_PORDEVSR2   0x14
#define MPC8544_GUTS_ADDR_GPPORCR     0x20
#define MPC8544_GUTS_ADDR_GPIOCR      0x30
#define MPC8544_GUTS_ADDR_GPOUTDR     0x40
#define MPC8544_GUTS_ADDR_GPINDR      0x50
#define MPC8544_GUTS_ADDR_PMUXCR      0x60
#define MPC8544_GUTS_ADDR_DEVDISR     0x70
#define MPC8544_GUTS_ADDR_POWMGTCSR   0x80
#define MPC8544_GUTS_ADDR_MCPSUMR     0x90
#define MPC8544_GUTS_ADDR_RSTRSCR     0x94
#define MPC8544_GUTS_ADDR_PVR         0xA0
#define MPC8544_GUTS_ADDR_SVR         0xA4
#define MPC8544_GUTS_ADDR_RSTCR       0xB0
#define MPC8544_GUTS_ADDR_IOVSELSR    0xC0
#define MPC8544_GUTS_ADDR_DDRCSR      0xB20
#define MPC8544_GUTS_ADDR_DDRCDR      0xB24
#define MPC8544_GUTS_ADDR_DDRCLKDR    0xB28
#define MPC8544_GUTS_ADDR_CLKOCR      0xE00
#define MPC8544_GUTS_ADDR_SRDS1CR1    0xF04
#define MPC8544_GUTS_ADDR_SRDS2CR1    0xF10
#define MPC8544_GUTS_ADDR_SRDS2CR3    0xF18

#define TYPE_MPC8544_GUTS "mpc8544-guts"
OBJECT_DECLARE_SIMPLE_TYPE(GutsState, MPC8544_GUTS)

struct GutsState {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    MemoryRegion iomem;
};


static uint64_t mpc8544_guts_read(void *opaque, hwaddr addr,
                                  unsigned size)
{
    uint32_t value = 0;
    CPUPPCState *env = cpu_env(current_cpu);

    addr &= MPC8544_GUTS_MMIO_SIZE - 1;
    switch (addr) {
    case MPC8544_GUTS_ADDR_PORPLLSR:
        value = FIELD_DP32(value, GUTS_PORPLLSR, E500_1_RATIO, 6); /* 3:1 */
        value = FIELD_DP32(value, GUTS_PORPLLSR, E500_0_RATIO, 6); /* 3:1 */
        value = FIELD_DP32(value, GUTS_PORPLLSR, DDR_RATIO, 12); /* 12:1 */
        value = FIELD_DP32(value, GUTS_PORPLLSR, PLAT_RATIO, 6); /* 6:1 */
        break;
    case MPC8544_GUTS_ADDR_PVR:
        value = env->spr[SPR_PVR];
        break;
    case MPC8544_GUTS_ADDR_SVR:
        value = env->spr[SPR_E500_SVR];
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Unknown register 0x%" HWADDR_PRIx "\n",
                      __func__, addr);
        break;
    }

    return value;
}

static void mpc8544_guts_write(void *opaque, hwaddr addr,
                               uint64_t value, unsigned size)
{
    addr &= MPC8544_GUTS_MMIO_SIZE - 1;

    switch (addr) {
    case MPC8544_GUTS_ADDR_RSTCR:
        if (value & MPC8544_GUTS_RSTCR_RESET) {
            qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
        }
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Unknown register 0x%" HWADDR_PRIx
                       " = 0x%" PRIx64 "\n", __func__, addr, value);
        break;
    }
}

static const MemoryRegionOps mpc8544_guts_ops = {
    .read = mpc8544_guts_read,
    .write = mpc8544_guts_write,
    .endianness = DEVICE_BIG_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void mpc8544_guts_initfn(Object *obj)
{
    SysBusDevice *d = SYS_BUS_DEVICE(obj);
    GutsState *s = MPC8544_GUTS(obj);

    memory_region_init_io(&s->iomem, OBJECT(s), &mpc8544_guts_ops, s,
                          "mpc8544.guts", MPC8544_GUTS_MMIO_SIZE);
    sysbus_init_mmio(d, &s->iomem);
}

static const TypeInfo mpc8544_guts_types[] = {
    {
        .name          = TYPE_MPC8544_GUTS,
        .parent        = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(GutsState),
        .instance_init = mpc8544_guts_initfn,
    },
};

DEFINE_TYPES(mpc8544_guts_types)
