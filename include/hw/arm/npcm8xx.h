/*
 * Nuvoton NPCM8xx SoC family.
 *
 * Copyright 2022 Google LLC
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 */
#ifndef NPCM8XX_H
#define NPCM8XX_H

#include "hw/adc/npcm7xx_adc.h"
#include "hw/core/split-irq.h"
#include "hw/cpu/cluster.h"
#include "hw/gpio/npcm7xx_gpio.h"
#include "hw/i2c/npcm7xx_smbus.h"
#include "hw/intc/arm_gic_common.h"
#include "hw/mem/npcm7xx_mc.h"
#include "hw/misc/npcm_clk.h"
#include "hw/misc/npcm_gcr.h"
#include "hw/misc/npcm7xx_mft.h"
#include "hw/misc/npcm7xx_pwm.h"
#include "hw/misc/npcm7xx_rng.h"
#include "hw/net/npcm_gmac.h"
#include "hw/net/npcm_pcs.h"
#include "hw/nvram/npcm7xx_otp.h"
#include "hw/sd/npcm7xx_sdhci.h"
#include "hw/timer/npcm7xx_timer.h"
#include "hw/ssi/npcm7xx_fiu.h"
#include "hw/usb/hcd-ehci.h"
#include "hw/usb/hcd-ohci.h"
#include "target/arm/cpu.h"
#include "hw/ssi/npcm_pspi.h"

#define NPCM8XX_MAX_NUM_CPUS    (4)

/* The first half of the address space is reserved for DDR4 DRAM. */
#define NPCM8XX_DRAM_BA         (0x00000000)
#define NPCM8XX_DRAM_SZ         (2 * GiB)

/* Magic addresses for setting up direct kernel booting and SMP boot stubs. */
#define NPCM8XX_LOADER_START            (0x00000000)  /* Start of SDRAM */
#define NPCM8XX_SMP_LOADER_START        (0xffff0000)  /* Boot ROM */
#define NPCM8XX_SMP_BOOTREG_ADDR        (0xf080013c)  /* GCR.SCRPAD */
#define NPCM8XX_BOARD_SETUP_ADDR        (0xffff1000)  /* Boot ROM */

#define NPCM8XX_NR_PWM_MODULES 3

struct NPCM8xxMachine {
    MachineState        parent_obj;

    /*
     * PWM fan splitter. each splitter connects to one PWM output and
     * multiple MFT inputs.
     */
    SplitIRQ            fan_splitter[NPCM8XX_NR_PWM_MODULES *
                                     NPCM7XX_PWM_PER_MODULE];
};


struct NPCM8xxMachineClass {
    MachineClass        parent_class;

    const char          *soc_type;
};

#define TYPE_NPCM8XX_MACHINE MACHINE_TYPE_NAME("npcm8xx")
OBJECT_DECLARE_TYPE(NPCM8xxMachine, NPCM8xxMachineClass, NPCM8XX_MACHINE)

struct NPCM8xxState {
    DeviceState         parent_obj;

    ARMCPU              cpu[NPCM8XX_MAX_NUM_CPUS];
    CPUClusterState     cpu_cluster;
    GICState            gic;

    MemoryRegion        sram;
    MemoryRegion        irom;
    MemoryRegion        ram3;
    MemoryRegion        *dram;

    NPCMGCRState        gcr;
    NPCMCLKState        clk;
    NPCM7xxTimerCtrlState tim[3];
    NPCM7xxADCState     adc;
    NPCM7xxPWMState     pwm[NPCM8XX_NR_PWM_MODULES];
    NPCM7xxMFTState     mft[8];
    NPCM7xxOTPState     fuse_array;
    NPCM7xxMCState      mc;
    NPCM7xxRNGState     rng;
    NPCM7xxGPIOState    gpio[8];
    NPCM7xxSMBusState   smbus[27];
    EHCISysBusState     ehci[2];
    OHCISysBusState     ohci[2];
    NPCM7xxFIUState     fiu[3];
    NPCMGMACState       gmac[4];
    NPCMPCSState        pcs;
    NPCM7xxSDHCIState   mmc;
    NPCMPSPIState       pspi;
};

struct NPCM8xxClass {
    DeviceClass         parent_class;

    /* Bitmask of modules that are permanently disabled on this chip. */
    uint32_t            disabled_modules;
    /* Number of CPU cores enabled in this SoC class. */
    uint32_t            num_cpus;
};

#define TYPE_NPCM8XX    "npcm8xx"
OBJECT_DECLARE_TYPE(NPCM8xxState, NPCM8xxClass, NPCM8XX)

/**
 * npcm8xx_load_kernel - Loads memory with everything needed to boot
 * @machine - The machine containing the SoC to be booted.
 * @soc - The SoC containing the CPU to be booted.
 *
 * This will set up the ARM boot info structure for the specific NPCM8xx
 * derivative and call arm_load_kernel() to set up loading of the kernel, etc.
 * into memory, if requested by the user.
 */
void npcm8xx_load_kernel(MachineState *machine, NPCM8xxState *soc);

#endif /* NPCM8XX_H */
