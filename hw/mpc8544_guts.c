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

#include "hw.h"
#include "sysemu.h"
#include "sysbus.h"

#define MPC8544_GUTS_MMIO_SIZE        0x1000
#define MPC8544_GUTS_RSTCR_RESET      0x02

#define MPC8544_GUTS_ADDR_PORPLLSR    0x00
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

struct GutsState {
    SysBusDevice busdev;
};

typedef struct GutsState GutsState;

static uint32_t mpc8544_guts_read32(void *opaque, target_phys_addr_t addr)
{
    uint32_t value = 0;
    CPUState *env = cpu_single_env;

    addr &= MPC8544_GUTS_MMIO_SIZE - 1;
    switch (addr) {
    case MPC8544_GUTS_ADDR_PVR:
        value = env->spr[SPR_PVR];
        break;
    case MPC8544_GUTS_ADDR_SVR:
        value = env->spr[SPR_E500_SVR];
        break;
    default:
        fprintf(stderr, "guts: Unknown register read: %x\n", (int)addr);
        break;
    }

    return value;
}

static CPUReadMemoryFunc * const mpc8544_guts_read[] = {
    NULL,
    NULL,
    &mpc8544_guts_read32,
};

static void mpc8544_guts_write32(void *opaque, target_phys_addr_t addr,
                              uint32_t value)
{
    addr &= MPC8544_GUTS_MMIO_SIZE - 1;

    switch (addr) {
    case MPC8544_GUTS_ADDR_RSTCR:
        if (value & MPC8544_GUTS_RSTCR_RESET) {
            qemu_system_reset_request();
        }
        break;
    default:
        fprintf(stderr, "guts: Unknown register write: %x = %x\n",
                (int)addr, value);
        break;
    }
}

static CPUWriteMemoryFunc * const mpc8544_guts_write[] = {
    NULL,
    NULL,
    &mpc8544_guts_write32,
};

static int mpc8544_guts_initfn(SysBusDevice *dev)
{
    GutsState *s;
    int iomem;

    s = FROM_SYSBUS(GutsState, sysbus_from_qdev(dev));

    iomem = cpu_register_io_memory(mpc8544_guts_read, mpc8544_guts_write, s,
                                   DEVICE_BIG_ENDIAN);
    sysbus_init_mmio(dev, MPC8544_GUTS_MMIO_SIZE, iomem);

    return 0;
}

static SysBusDeviceInfo mpc8544_guts_info = {
    .init         = mpc8544_guts_initfn,
    .qdev.name    = "mpc8544-guts",
    .qdev.size    = sizeof(GutsState),
};

static void mpc8544_guts_register(void)
{
    sysbus_register_withprop(&mpc8544_guts_info);
}
device_init(mpc8544_guts_register);
