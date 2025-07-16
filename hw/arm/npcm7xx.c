/*
 * Nuvoton NPCM7xx SoC family.
 *
 * Copyright 2020 Google LLC
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

#include "qemu/osdep.h"

#include "hw/arm/boot.h"
#include "hw/arm/npcm7xx.h"
#include "hw/char/serial-mm.h"
#include "hw/loader.h"
#include "hw/misc/unimp.h"
#include "hw/qdev-clock.h"
#include "hw/qdev-properties.h"
#include "qapi/error.h"
#include "exec/tswap.h"
#include "qemu/units.h"
#include "system/system.h"
#include "target/arm/cpu-qom.h"

/*
 * This covers the whole MMIO space. We'll use this to catch any MMIO accesses
 * that aren't handled by any device.
 */
#define NPCM7XX_MMIO_BA         (0x80000000)
#define NPCM7XX_MMIO_SZ         (0x7ffd0000)

/* OTP key storage and fuse strap array */
#define NPCM7XX_OTP1_BA         (0xf0189000)
#define NPCM7XX_OTP2_BA         (0xf018a000)

/* Core system modules. */
#define NPCM7XX_L2C_BA          (0xf03fc000)
#define NPCM7XX_CPUP_BA         (0xf03fe000)
#define NPCM7XX_GCR_BA          (0xf0800000)
#define NPCM7XX_CLK_BA          (0xf0801000)
#define NPCM7XX_MC_BA           (0xf0824000)
#define NPCM7XX_RNG_BA          (0xf000b000)

/* USB Host modules */
#define NPCM7XX_EHCI_BA         (0xf0806000)
#define NPCM7XX_OHCI_BA         (0xf0807000)

/* ADC Module */
#define NPCM7XX_ADC_BA          (0xf000c000)

/* Internal AHB SRAM */
#define NPCM7XX_RAM3_BA         (0xc0008000)
#define NPCM7XX_RAM3_SZ         (4 * KiB)

/* Memory blocks at the end of the address space */
#define NPCM7XX_RAM2_BA         (0xfffd0000)
#define NPCM7XX_RAM2_SZ         (128 * KiB)
#define NPCM7XX_ROM_BA          (0xffff0000)
#define NPCM7XX_ROM_SZ          (64 * KiB)

/* SDHCI Modules */
#define NPCM7XX_MMC_BA          (0xf0842000)

/* Clock configuration values to be fixed up when bypassing bootloader */

/* Run PLL1 at 1600 MHz */
#define NPCM7XX_PLLCON1_FIXUP_VAL   (0x00402101)
/* Run the CPU from PLL1 and UART from PLL2 */
#define NPCM7XX_CLKSEL_FIXUP_VAL    (0x004aaba9)

/*
 * Interrupt lines going into the GIC. This does not include internal Cortex-A9
 * interrupts.
 */
enum NPCM7xxInterrupt {
    NPCM7XX_ADC_IRQ             = 0,
    NPCM7XX_UART0_IRQ           = 2,
    NPCM7XX_UART1_IRQ,
    NPCM7XX_UART2_IRQ,
    NPCM7XX_UART3_IRQ,
    NPCM7XX_GMAC1_IRQ           = 14,
    NPCM7XX_EMC1RX_IRQ          = 15,
    NPCM7XX_EMC1TX_IRQ,
    NPCM7XX_GMAC2_IRQ,
    NPCM7XX_MMC_IRQ             = 26,
    NPCM7XX_PSPI2_IRQ           = 28,
    NPCM7XX_PSPI1_IRQ           = 31,
    NPCM7XX_TIMER0_IRQ          = 32,   /* Timer Module 0 */
    NPCM7XX_TIMER1_IRQ,
    NPCM7XX_TIMER2_IRQ,
    NPCM7XX_TIMER3_IRQ,
    NPCM7XX_TIMER4_IRQ,
    NPCM7XX_TIMER5_IRQ,                 /* Timer Module 1 */
    NPCM7XX_TIMER6_IRQ,
    NPCM7XX_TIMER7_IRQ,
    NPCM7XX_TIMER8_IRQ,
    NPCM7XX_TIMER9_IRQ,
    NPCM7XX_TIMER10_IRQ,                /* Timer Module 2 */
    NPCM7XX_TIMER11_IRQ,
    NPCM7XX_TIMER12_IRQ,
    NPCM7XX_TIMER13_IRQ,
    NPCM7XX_TIMER14_IRQ,
    NPCM7XX_WDG0_IRQ            = 47,   /* Timer Module 0 Watchdog */
    NPCM7XX_WDG1_IRQ,                   /* Timer Module 1 Watchdog */
    NPCM7XX_WDG2_IRQ,                   /* Timer Module 2 Watchdog */
    NPCM7XX_EHCI_IRQ            = 61,
    NPCM7XX_OHCI_IRQ            = 62,
    NPCM7XX_SMBUS0_IRQ          = 64,
    NPCM7XX_SMBUS1_IRQ,
    NPCM7XX_SMBUS2_IRQ,
    NPCM7XX_SMBUS3_IRQ,
    NPCM7XX_SMBUS4_IRQ,
    NPCM7XX_SMBUS5_IRQ,
    NPCM7XX_SMBUS6_IRQ,
    NPCM7XX_SMBUS7_IRQ,
    NPCM7XX_SMBUS8_IRQ,
    NPCM7XX_SMBUS9_IRQ,
    NPCM7XX_SMBUS10_IRQ,
    NPCM7XX_SMBUS11_IRQ,
    NPCM7XX_SMBUS12_IRQ,
    NPCM7XX_SMBUS13_IRQ,
    NPCM7XX_SMBUS14_IRQ,
    NPCM7XX_SMBUS15_IRQ,
    NPCM7XX_PWM0_IRQ            = 93,   /* PWM module 0 */
    NPCM7XX_PWM1_IRQ,                   /* PWM module 1 */
    NPCM7XX_MFT0_IRQ            = 96,   /* MFT module 0 */
    NPCM7XX_MFT1_IRQ,                   /* MFT module 1 */
    NPCM7XX_MFT2_IRQ,                   /* MFT module 2 */
    NPCM7XX_MFT3_IRQ,                   /* MFT module 3 */
    NPCM7XX_MFT4_IRQ,                   /* MFT module 4 */
    NPCM7XX_MFT5_IRQ,                   /* MFT module 5 */
    NPCM7XX_MFT6_IRQ,                   /* MFT module 6 */
    NPCM7XX_MFT7_IRQ,                   /* MFT module 7 */
    NPCM7XX_EMC2RX_IRQ          = 114,
    NPCM7XX_EMC2TX_IRQ,
    NPCM7XX_GPIO0_IRQ           = 116,
    NPCM7XX_GPIO1_IRQ,
    NPCM7XX_GPIO2_IRQ,
    NPCM7XX_GPIO3_IRQ,
    NPCM7XX_GPIO4_IRQ,
    NPCM7XX_GPIO5_IRQ,
    NPCM7XX_GPIO6_IRQ,
    NPCM7XX_GPIO7_IRQ,
};

/* Total number of GIC interrupts, including internal Cortex-A9 interrupts. */
#define NPCM7XX_NUM_IRQ         (160)

/* Register base address for each Timer Module */
static const hwaddr npcm7xx_tim_addr[] = {
    0xf0008000,
    0xf0009000,
    0xf000a000,
};

/* Register base address for each 16550 UART */
static const hwaddr npcm7xx_uart_addr[] = {
    0xf0001000,
    0xf0002000,
    0xf0003000,
    0xf0004000,
};

/* Direct memory-mapped access to SPI0 CS0-1. */
static const hwaddr npcm7xx_fiu0_flash_addr[] = {
    0x80000000, /* CS0 */
    0x88000000, /* CS1 */
};

/* Direct memory-mapped access to SPI3 CS0-3. */
static const hwaddr npcm7xx_fiu3_flash_addr[] = {
    0xa0000000, /* CS0 */
    0xa8000000, /* CS1 */
    0xb0000000, /* CS2 */
    0xb8000000, /* CS3 */
};

/* Register base address for each PWM Module */
static const hwaddr npcm7xx_pwm_addr[] = {
    0xf0103000,
    0xf0104000,
};

/* Register base address for each MFT Module */
static const hwaddr npcm7xx_mft_addr[] = {
    0xf0180000,
    0xf0181000,
    0xf0182000,
    0xf0183000,
    0xf0184000,
    0xf0185000,
    0xf0186000,
    0xf0187000,
};

/* Direct memory-mapped access to each SMBus Module. */
static const hwaddr npcm7xx_smbus_addr[] = {
    0xf0080000,
    0xf0081000,
    0xf0082000,
    0xf0083000,
    0xf0084000,
    0xf0085000,
    0xf0086000,
    0xf0087000,
    0xf0088000,
    0xf0089000,
    0xf008a000,
    0xf008b000,
    0xf008c000,
    0xf008d000,
    0xf008e000,
    0xf008f000,
};

/* Register base address for each EMC Module */
static const hwaddr npcm7xx_emc_addr[] = {
    0xf0825000,
    0xf0826000,
};

/* Register base address for each PSPI Module */
static const hwaddr npcm7xx_pspi_addr[] = {
    0xf0200000,
    0xf0201000,
};

/* Register base address for each GMAC Module */
static const hwaddr npcm7xx_gmac_addr[] = {
    0xf0802000,
    0xf0804000,
};

static const struct {
    hwaddr regs_addr;
    uint32_t unconnected_pins;
    uint32_t reset_pu;
    uint32_t reset_pd;
    uint32_t reset_osrc;
    uint32_t reset_odsc;
} npcm7xx_gpio[] = {
    {
        .regs_addr = 0xf0010000,
        .reset_pu = 0xff03ffff,
        .reset_pd = 0x00fc0000,
    }, {
        .regs_addr = 0xf0011000,
        .unconnected_pins = 0x0000001e,
        .reset_pu = 0xfefffe07,
        .reset_pd = 0x010001e0,
    }, {
        .regs_addr = 0xf0012000,
        .reset_pu = 0x780fffff,
        .reset_pd = 0x07f00000,
        .reset_odsc = 0x00700000,
    }, {
        .regs_addr = 0xf0013000,
        .reset_pu = 0x00fc0000,
        .reset_pd = 0xff000000,
    }, {
        .regs_addr = 0xf0014000,
        .reset_pu = 0xffffffff,
    }, {
        .regs_addr = 0xf0015000,
        .reset_pu = 0xbf83f801,
        .reset_pd = 0x007c0000,
        .reset_osrc = 0x000000f1,
        .reset_odsc = 0x3f9f80f1,
    }, {
        .regs_addr = 0xf0016000,
        .reset_pu = 0xfc00f801,
        .reset_pd = 0x000007fe,
        .reset_odsc = 0x00000800,
    }, {
        .regs_addr = 0xf0017000,
        .unconnected_pins = 0xffffff00,
        .reset_pu = 0x0000007f,
        .reset_osrc = 0x0000007f,
        .reset_odsc = 0x0000007f,
    },
};

static const struct {
    const char *name;
    hwaddr regs_addr;
    int cs_count;
    const hwaddr *flash_addr;
    size_t flash_size;
} npcm7xx_fiu[] = {
    {
        .name = "fiu0",
        .regs_addr = 0xfb000000,
        .cs_count = ARRAY_SIZE(npcm7xx_fiu0_flash_addr),
        .flash_addr = npcm7xx_fiu0_flash_addr,
        .flash_size = 128 * MiB,

    }, {
        .name = "fiu3",
        .regs_addr = 0xc0000000,
        .cs_count = ARRAY_SIZE(npcm7xx_fiu3_flash_addr),
        .flash_addr = npcm7xx_fiu3_flash_addr,
        .flash_size = 128 * MiB,
    },
};

static void npcm7xx_write_board_setup(ARMCPU *cpu,
                                      const struct arm_boot_info *info)
{
    uint32_t board_setup[] = {
        0xe59f0010,     /* ldr r0, clk_base_addr */
        0xe59f1010,     /* ldr r1, pllcon1_value */
        0xe5801010,     /* str r1, [r0, #16] */
        0xe59f100c,     /* ldr r1, clksel_value */
        0xe5801004,     /* str r1, [r0, #4] */
        0xe12fff1e,     /* bx lr */
        NPCM7XX_CLK_BA,
        NPCM7XX_PLLCON1_FIXUP_VAL,
        NPCM7XX_CLKSEL_FIXUP_VAL,
    };
    int i;

    for (i = 0; i < ARRAY_SIZE(board_setup); i++) {
        board_setup[i] = tswap32(board_setup[i]);
    }
    rom_add_blob_fixed("board-setup", board_setup, sizeof(board_setup),
                       info->board_setup_addr);
}

static void npcm7xx_write_secondary_boot(ARMCPU *cpu,
                                         const struct arm_boot_info *info)
{
    /*
     * The default smpboot stub halts the secondary CPU with a 'wfi'
     * instruction, but the arch/arm/mach-npcm/platsmp.c in the Linux kernel
     * does not send an IPI to wake it up, so the second CPU fails to boot. So
     * we need to provide our own smpboot stub that can not use 'wfi', it has
     * to spin the secondary CPU until the first CPU writes to the SCRPAD reg.
     */
    uint32_t smpboot[] = {
        0xe59f2018,     /* ldr r2, bootreg_addr */
        0xe3a00000,     /* mov r0, #0 */
        0xe5820000,     /* str r0, [r2] */
        0xe320f002,     /* wfe */
        0xe5921000,     /* ldr r1, [r2] */
        0xe1110001,     /* tst r1, r1 */
        0x0afffffb,     /* beq <wfe> */
        0xe12fff11,     /* bx r1 */
        NPCM7XX_SMP_BOOTREG_ADDR,
    };
    int i;

    for (i = 0; i < ARRAY_SIZE(smpboot); i++) {
        smpboot[i] = tswap32(smpboot[i]);
    }

    rom_add_blob_fixed("smpboot", smpboot, sizeof(smpboot),
                       NPCM7XX_SMP_LOADER_START);
}

static struct arm_boot_info npcm7xx_binfo = {
    .loader_start           = NPCM7XX_LOADER_START,
    .smp_loader_start       = NPCM7XX_SMP_LOADER_START,
    .smp_bootreg_addr       = NPCM7XX_SMP_BOOTREG_ADDR,
    .gic_cpu_if_addr        = NPCM7XX_GIC_CPU_IF_ADDR,
    .write_secondary_boot   = npcm7xx_write_secondary_boot,
    .board_id               = -1,
    .board_setup_addr       = NPCM7XX_BOARD_SETUP_ADDR,
    .write_board_setup      = npcm7xx_write_board_setup,
};

void npcm7xx_load_kernel(MachineState *machine, NPCM7xxState *soc)
{
    npcm7xx_binfo.ram_size = machine->ram_size;

    arm_load_kernel(&soc->cpu[0], machine, &npcm7xx_binfo);
}

static void npcm7xx_init_fuses(NPCM7xxState *s)
{
    NPCM7xxClass *nc = NPCM7XX_GET_CLASS(s);
    uint32_t value;

    /*
     * The initial mask of disabled modules indicates the chip derivative (e.g.
     * NPCM750 or NPCM730).
     */
    value = cpu_to_le32(nc->disabled_modules);
    npcm7xx_otp_array_write(&s->fuse_array, &value, NPCM7XX_FUSE_DERIVATIVE,
                            sizeof(value));
}

static void npcm7xx_write_adc_calibration(NPCM7xxState *s)
{
    /* Both ADC and the fuse array must have realized. */
    QEMU_BUILD_BUG_ON(sizeof(s->adc.calibration_r_values) != 4);
    npcm7xx_otp_array_write(&s->fuse_array, s->adc.calibration_r_values,
            NPCM7XX_FUSE_ADC_CALIB, sizeof(s->adc.calibration_r_values));
}

static qemu_irq npcm7xx_irq(NPCM7xxState *s, int n)
{
    return qdev_get_gpio_in(DEVICE(&s->a9mpcore), n);
}

static void npcm7xx_init(Object *obj)
{
    NPCM7xxState *s = NPCM7XX(obj);
    int i;

    for (i = 0; i < NPCM7XX_MAX_NUM_CPUS; i++) {
        object_initialize_child(obj, "cpu[*]", &s->cpu[i],
                                ARM_CPU_TYPE_NAME("cortex-a9"));
    }

    object_initialize_child(obj, "a9mpcore", &s->a9mpcore, TYPE_A9MPCORE_PRIV);
    object_initialize_child(obj, "gcr", &s->gcr, TYPE_NPCM7XX_GCR);
    object_property_add_alias(obj, "power-on-straps", OBJECT(&s->gcr),
                              "power-on-straps");
    object_initialize_child(obj, "clk", &s->clk, TYPE_NPCM7XX_CLK);
    object_initialize_child(obj, "otp1", &s->key_storage,
                            TYPE_NPCM7XX_KEY_STORAGE);
    object_initialize_child(obj, "otp2", &s->fuse_array,
                            TYPE_NPCM7XX_FUSE_ARRAY);
    object_initialize_child(obj, "mc", &s->mc, TYPE_NPCM7XX_MC);
    object_initialize_child(obj, "rng", &s->rng, TYPE_NPCM7XX_RNG);
    object_initialize_child(obj, "adc", &s->adc, TYPE_NPCM7XX_ADC);

    for (i = 0; i < ARRAY_SIZE(s->tim); i++) {
        object_initialize_child(obj, "tim[*]", &s->tim[i], TYPE_NPCM7XX_TIMER);
    }

    for (i = 0; i < ARRAY_SIZE(s->gpio); i++) {
        object_initialize_child(obj, "gpio[*]", &s->gpio[i], TYPE_NPCM7XX_GPIO);
    }

    for (i = 0; i < ARRAY_SIZE(s->smbus); i++) {
        object_initialize_child(obj, "smbus[*]", &s->smbus[i],
                                TYPE_NPCM7XX_SMBUS);
    }

    object_initialize_child(obj, "ehci", &s->ehci, TYPE_NPCM7XX_EHCI);
    object_initialize_child(obj, "ohci", &s->ohci, TYPE_SYSBUS_OHCI);

    QEMU_BUILD_BUG_ON(ARRAY_SIZE(npcm7xx_fiu) != ARRAY_SIZE(s->fiu));
    for (i = 0; i < ARRAY_SIZE(s->fiu); i++) {
        object_initialize_child(obj, npcm7xx_fiu[i].name, &s->fiu[i],
                                TYPE_NPCM7XX_FIU);
    }

    for (i = 0; i < ARRAY_SIZE(s->pwm); i++) {
        object_initialize_child(obj, "pwm[*]", &s->pwm[i], TYPE_NPCM7XX_PWM);
    }

    for (i = 0; i < ARRAY_SIZE(s->mft); i++) {
        object_initialize_child(obj, "mft[*]", &s->mft[i], TYPE_NPCM7XX_MFT);
    }

    for (i = 0; i < ARRAY_SIZE(s->emc); i++) {
        object_initialize_child(obj, "emc[*]", &s->emc[i], TYPE_NPCM7XX_EMC);
    }

    for (i = 0; i < ARRAY_SIZE(s->pspi); i++) {
        object_initialize_child(obj, "pspi[*]", &s->pspi[i], TYPE_NPCM_PSPI);
    }

    for (i = 0; i < ARRAY_SIZE(s->gmac); i++) {
        object_initialize_child(obj, "gmac[*]", &s->gmac[i], TYPE_NPCM_GMAC);
    }

    object_initialize_child(obj, "mmc", &s->mmc, TYPE_NPCM7XX_SDHCI);
}

static void npcm7xx_realize(DeviceState *dev, Error **errp)
{
    NPCM7xxState *s = NPCM7XX(dev);
    NPCM7xxClass *nc = NPCM7XX_GET_CLASS(s);
    int i;

    if (memory_region_size(s->dram) > NPCM7XX_DRAM_SZ) {
        error_setg(errp, "%s: NPCM7xx cannot address more than %" PRIu64
                   " MiB of DRAM", __func__, NPCM7XX_DRAM_SZ / MiB);
        return;
    }

    /* CPUs */
    for (i = 0; i < nc->num_cpus; i++) {
        object_property_set_int(OBJECT(&s->cpu[i]), "reset-cbar",
                                NPCM7XX_GIC_CPU_IF_ADDR, &error_abort);
        object_property_set_bool(OBJECT(&s->cpu[i]), "reset-hivecs", true,
                                 &error_abort);

        /* Disable security extensions. */
        object_property_set_bool(OBJECT(&s->cpu[i]), "has_el3", false,
                                 &error_abort);

        if (!qdev_realize(DEVICE(&s->cpu[i]), NULL, errp)) {
            return;
        }
    }

    /* A9MPCORE peripherals. Can only fail if we pass bad parameters here. */
    object_property_set_int(OBJECT(&s->a9mpcore), "num-cpu", nc->num_cpus,
                            &error_abort);
    object_property_set_int(OBJECT(&s->a9mpcore), "num-irq", NPCM7XX_NUM_IRQ,
                            &error_abort);
    sysbus_realize(SYS_BUS_DEVICE(&s->a9mpcore), &error_abort);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->a9mpcore), 0, NPCM7XX_CPUP_BA);

    for (i = 0; i < nc->num_cpus; i++) {
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->a9mpcore), i,
                           qdev_get_gpio_in(DEVICE(&s->cpu[i]), ARM_CPU_IRQ));
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->a9mpcore), i + nc->num_cpus,
                           qdev_get_gpio_in(DEVICE(&s->cpu[i]), ARM_CPU_FIQ));
    }

    /* L2 cache controller */
    sysbus_create_simple("l2x0", NPCM7XX_L2C_BA, NULL);

    /* System Global Control Registers (GCR). Can fail due to user input. */
    object_property_set_int(OBJECT(&s->gcr), "disabled-modules",
                            nc->disabled_modules, &error_abort);
    object_property_add_const_link(OBJECT(&s->gcr), "dram-mr", OBJECT(s->dram));
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->gcr), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->gcr), 0, NPCM7XX_GCR_BA);

    /* Clock Control Registers (CLK). Cannot fail. */
    sysbus_realize(SYS_BUS_DEVICE(&s->clk), &error_abort);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->clk), 0, NPCM7XX_CLK_BA);

    /* OTP key storage and fuse strap array. Cannot fail. */
    sysbus_realize(SYS_BUS_DEVICE(&s->key_storage), &error_abort);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->key_storage), 0, NPCM7XX_OTP1_BA);
    sysbus_realize(SYS_BUS_DEVICE(&s->fuse_array), &error_abort);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->fuse_array), 0, NPCM7XX_OTP2_BA);
    npcm7xx_init_fuses(s);

    /* Fake Memory Controller (MC). Cannot fail. */
    sysbus_realize(SYS_BUS_DEVICE(&s->mc), &error_abort);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->mc), 0, NPCM7XX_MC_BA);

    /* ADC Modules. Cannot fail. */
    qdev_connect_clock_in(DEVICE(&s->adc), "clock", qdev_get_clock_out(
                          DEVICE(&s->clk), "adc-clock"));
    sysbus_realize(SYS_BUS_DEVICE(&s->adc), &error_abort);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->adc), 0, NPCM7XX_ADC_BA);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->adc), 0,
            npcm7xx_irq(s, NPCM7XX_ADC_IRQ));
    npcm7xx_write_adc_calibration(s);

    /* Timer Modules (TIM). Cannot fail. */
    QEMU_BUILD_BUG_ON(ARRAY_SIZE(npcm7xx_tim_addr) != ARRAY_SIZE(s->tim));
    for (i = 0; i < ARRAY_SIZE(s->tim); i++) {
        SysBusDevice *sbd = SYS_BUS_DEVICE(&s->tim[i]);
        int first_irq;
        int j;

        /* Connect the timer clock. */
        qdev_connect_clock_in(DEVICE(&s->tim[i]), "clock", qdev_get_clock_out(
                    DEVICE(&s->clk), "timer-clock"));

        sysbus_realize(sbd, &error_abort);
        sysbus_mmio_map(sbd, 0, npcm7xx_tim_addr[i]);

        first_irq = NPCM7XX_TIMER0_IRQ + i * NPCM7XX_TIMERS_PER_CTRL;
        for (j = 0; j < NPCM7XX_TIMERS_PER_CTRL; j++) {
            qemu_irq irq = npcm7xx_irq(s, first_irq + j);
            sysbus_connect_irq(sbd, j, irq);
        }

        /* IRQ for watchdogs */
        sysbus_connect_irq(sbd, NPCM7XX_TIMERS_PER_CTRL,
                npcm7xx_irq(s, NPCM7XX_WDG0_IRQ + i));
        /* GPIO that connects clk module with watchdog */
        qdev_connect_gpio_out_named(DEVICE(&s->tim[i]),
                NPCM7XX_WATCHDOG_RESET_GPIO_OUT, 0,
                qdev_get_gpio_in_named(DEVICE(&s->clk),
                        NPCM7XX_WATCHDOG_RESET_GPIO_IN, i));
    }

    /* UART0..3 (16550 compatible) */
    for (i = 0; i < ARRAY_SIZE(npcm7xx_uart_addr); i++) {
        serial_mm_init(get_system_memory(), npcm7xx_uart_addr[i], 2,
                       npcm7xx_irq(s, NPCM7XX_UART0_IRQ + i), 115200,
                       serial_hd(i), DEVICE_LITTLE_ENDIAN);
    }

    /* Random Number Generator. Cannot fail. */
    sysbus_realize(SYS_BUS_DEVICE(&s->rng), &error_abort);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->rng), 0, NPCM7XX_RNG_BA);

    /* GPIO modules. Cannot fail. */
    QEMU_BUILD_BUG_ON(ARRAY_SIZE(npcm7xx_gpio) != ARRAY_SIZE(s->gpio));
    for (i = 0; i < ARRAY_SIZE(s->gpio); i++) {
        Object *obj = OBJECT(&s->gpio[i]);

        object_property_set_uint(obj, "reset-pullup",
                                 npcm7xx_gpio[i].reset_pu, &error_abort);
        object_property_set_uint(obj, "reset-pulldown",
                                 npcm7xx_gpio[i].reset_pd, &error_abort);
        object_property_set_uint(obj, "reset-osrc",
                                 npcm7xx_gpio[i].reset_osrc, &error_abort);
        object_property_set_uint(obj, "reset-odsc",
                                 npcm7xx_gpio[i].reset_odsc, &error_abort);
        sysbus_realize(SYS_BUS_DEVICE(obj), &error_abort);
        sysbus_mmio_map(SYS_BUS_DEVICE(obj), 0, npcm7xx_gpio[i].regs_addr);
        sysbus_connect_irq(SYS_BUS_DEVICE(obj), 0,
                           npcm7xx_irq(s, NPCM7XX_GPIO0_IRQ + i));
    }

    /* SMBus modules. Cannot fail. */
    QEMU_BUILD_BUG_ON(ARRAY_SIZE(npcm7xx_smbus_addr) != ARRAY_SIZE(s->smbus));
    for (i = 0; i < ARRAY_SIZE(s->smbus); i++) {
        Object *obj = OBJECT(&s->smbus[i]);

        sysbus_realize(SYS_BUS_DEVICE(obj), &error_abort);
        sysbus_mmio_map(SYS_BUS_DEVICE(obj), 0, npcm7xx_smbus_addr[i]);
        sysbus_connect_irq(SYS_BUS_DEVICE(obj), 0,
                           npcm7xx_irq(s, NPCM7XX_SMBUS0_IRQ + i));
    }

    /* USB Host */
    object_property_set_bool(OBJECT(&s->ehci), "companion-enable", true,
                             &error_abort);
    sysbus_realize(SYS_BUS_DEVICE(&s->ehci), &error_abort);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->ehci), 0, NPCM7XX_EHCI_BA);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->ehci), 0,
                       npcm7xx_irq(s, NPCM7XX_EHCI_IRQ));

    object_property_set_str(OBJECT(&s->ohci), "masterbus", "usb-bus.0",
                            &error_abort);
    object_property_set_uint(OBJECT(&s->ohci), "num-ports", 1, &error_abort);
    sysbus_realize(SYS_BUS_DEVICE(&s->ohci), &error_abort);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->ohci), 0, NPCM7XX_OHCI_BA);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->ohci), 0,
                       npcm7xx_irq(s, NPCM7XX_OHCI_IRQ));

    /* PWM Modules. Cannot fail. */
    QEMU_BUILD_BUG_ON(ARRAY_SIZE(npcm7xx_pwm_addr) != ARRAY_SIZE(s->pwm));
    for (i = 0; i < ARRAY_SIZE(s->pwm); i++) {
        SysBusDevice *sbd = SYS_BUS_DEVICE(&s->pwm[i]);

        qdev_connect_clock_in(DEVICE(&s->pwm[i]), "clock", qdev_get_clock_out(
                    DEVICE(&s->clk), "apb3-clock"));
        sysbus_realize(sbd, &error_abort);
        sysbus_mmio_map(sbd, 0, npcm7xx_pwm_addr[i]);
        sysbus_connect_irq(sbd, i, npcm7xx_irq(s, NPCM7XX_PWM0_IRQ + i));
    }

    /* MFT Modules. Cannot fail. */
    QEMU_BUILD_BUG_ON(ARRAY_SIZE(npcm7xx_mft_addr) != ARRAY_SIZE(s->mft));
    for (i = 0; i < ARRAY_SIZE(s->mft); i++) {
        SysBusDevice *sbd = SYS_BUS_DEVICE(&s->mft[i]);

        qdev_connect_clock_in(DEVICE(&s->mft[i]), "clock-in",
                              qdev_get_clock_out(DEVICE(&s->clk),
                                                 "apb4-clock"));
        sysbus_realize(sbd, &error_abort);
        sysbus_mmio_map(sbd, 0, npcm7xx_mft_addr[i]);
        sysbus_connect_irq(sbd, 0, npcm7xx_irq(s, NPCM7XX_MFT0_IRQ + i));
    }

    /*
     * EMC Modules. Cannot fail.
     * Use the available NIC configurations in order, allowing 'emc0' and
     * 'emc1' to by used as aliases for the model= parameter to override.
     *
     * This works around the inability to specify the netdev property for the
     * emc device: it's not pluggable and thus the -device option can't be
     * used.
     */
    QEMU_BUILD_BUG_ON(ARRAY_SIZE(npcm7xx_emc_addr) != ARRAY_SIZE(s->emc));
    QEMU_BUILD_BUG_ON(ARRAY_SIZE(s->emc) != 2);
    for (i = 0; i < ARRAY_SIZE(s->emc); i++) {
        SysBusDevice *sbd = SYS_BUS_DEVICE(&s->emc[i]);
        char alias[6];

        s->emc[i].emc_num = i;
        snprintf(alias, sizeof(alias), "emc%u", i);
        qemu_configure_nic_device(DEVICE(sbd), true, alias);

        /*
         * The device exists regardless of whether it's connected to a QEMU
         * netdev backend. So always instantiate it even if there is no
         * backend.
         */
        sysbus_realize(sbd, &error_abort);
        sysbus_mmio_map(sbd, 0, npcm7xx_emc_addr[i]);
        int tx_irq = i == 0 ? NPCM7XX_EMC1TX_IRQ : NPCM7XX_EMC2TX_IRQ;
        int rx_irq = i == 0 ? NPCM7XX_EMC1RX_IRQ : NPCM7XX_EMC2RX_IRQ;
        /*
         * N.B. The values for the second argument sysbus_connect_irq are
         * chosen to match the registration order in npcm7xx_emc_realize.
         */
        sysbus_connect_irq(sbd, 0, npcm7xx_irq(s, tx_irq));
        sysbus_connect_irq(sbd, 1, npcm7xx_irq(s, rx_irq));
    }

    /*
     * GMAC Modules. Cannot fail.
     */
    QEMU_BUILD_BUG_ON(ARRAY_SIZE(npcm7xx_gmac_addr) != ARRAY_SIZE(s->gmac));
    QEMU_BUILD_BUG_ON(ARRAY_SIZE(s->gmac) != 2);
    for (i = 0; i < ARRAY_SIZE(s->gmac); i++) {
        SysBusDevice *sbd = SYS_BUS_DEVICE(&s->gmac[i]);

        qemu_configure_nic_device(DEVICE(sbd), false, NULL);
        /*
         * The device exists regardless of whether it's connected to a QEMU
         * netdev backend. So always instantiate it even if there is no
         * backend.
         */
        sysbus_realize(sbd, &error_abort);
        sysbus_mmio_map(sbd, 0, npcm7xx_gmac_addr[i]);
        int irq = i == 0 ? NPCM7XX_GMAC1_IRQ : NPCM7XX_GMAC2_IRQ;
        /*
         * N.B. The values for the second argument sysbus_connect_irq are
         * chosen to match the registration order in npcm7xx_emc_realize.
         */
        sysbus_connect_irq(sbd, 0, npcm7xx_irq(s, irq));
    }

    /*
     * Flash Interface Unit (FIU). Can fail if incorrect number of chip selects
     * specified, but this is a programming error.
     */
    QEMU_BUILD_BUG_ON(ARRAY_SIZE(npcm7xx_fiu) != ARRAY_SIZE(s->fiu));
    for (i = 0; i < ARRAY_SIZE(s->fiu); i++) {
        SysBusDevice *sbd = SYS_BUS_DEVICE(&s->fiu[i]);
        int j;

        object_property_set_int(OBJECT(sbd), "cs-count",
                                npcm7xx_fiu[i].cs_count, &error_abort);
        object_property_set_int(OBJECT(sbd), "flash-size",
                                npcm7xx_fiu[i].flash_size, &error_abort);
        sysbus_realize(sbd, &error_abort);

        sysbus_mmio_map(sbd, 0, npcm7xx_fiu[i].regs_addr);
        for (j = 0; j < npcm7xx_fiu[i].cs_count; j++) {
            sysbus_mmio_map(sbd, j + 1, npcm7xx_fiu[i].flash_addr[j]);
        }
    }

    /* RAM2 (SRAM) */
    memory_region_init_ram(&s->sram, OBJECT(dev), "ram2",
                           NPCM7XX_RAM2_SZ, &error_abort);
    memory_region_add_subregion(get_system_memory(), NPCM7XX_RAM2_BA, &s->sram);

    /* RAM3 (SRAM) */
    memory_region_init_ram(&s->ram3, OBJECT(dev), "ram3",
                           NPCM7XX_RAM3_SZ, &error_abort);
    memory_region_add_subregion(get_system_memory(), NPCM7XX_RAM3_BA, &s->ram3);

    /* Internal ROM */
    memory_region_init_rom(&s->irom, OBJECT(dev), "irom", NPCM7XX_ROM_SZ,
                           &error_abort);
    memory_region_add_subregion(get_system_memory(), NPCM7XX_ROM_BA, &s->irom);

    /* SDHCI */
    sysbus_realize(SYS_BUS_DEVICE(&s->mmc), &error_abort);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->mmc), 0, NPCM7XX_MMC_BA);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->mmc), 0,
            npcm7xx_irq(s, NPCM7XX_MMC_IRQ));

    /* PSPI */
    QEMU_BUILD_BUG_ON(ARRAY_SIZE(npcm7xx_pspi_addr) != ARRAY_SIZE(s->pspi));
    for (i = 0; i < ARRAY_SIZE(s->pspi); i++) {
        SysBusDevice *sbd = SYS_BUS_DEVICE(&s->pspi[i]);
        int irq = (i == 0) ? NPCM7XX_PSPI1_IRQ : NPCM7XX_PSPI2_IRQ;

        sysbus_realize(sbd, &error_abort);
        sysbus_mmio_map(sbd, 0, npcm7xx_pspi_addr[i]);
        sysbus_connect_irq(sbd, 0, npcm7xx_irq(s, irq));
    }

    create_unimplemented_device("npcm7xx.shm",          0xc0001000,   4 * KiB);
    create_unimplemented_device("npcm7xx.vdmx",         0xe0800000,   4 * KiB);
    create_unimplemented_device("npcm7xx.pcierc",       0xe1000000,  64 * KiB);
    create_unimplemented_device("npcm7xx.kcs",          0xf0007000,   4 * KiB);
    create_unimplemented_device("npcm7xx.gfxi",         0xf000e000,   4 * KiB);
    create_unimplemented_device("npcm7xx.espi",         0xf009f000,   4 * KiB);
    create_unimplemented_device("npcm7xx.peci",         0xf0100000,   4 * KiB);
    create_unimplemented_device("npcm7xx.siox[1]",      0xf0101000,   4 * KiB);
    create_unimplemented_device("npcm7xx.siox[2]",      0xf0102000,   4 * KiB);
    create_unimplemented_device("npcm7xx.ahbpci",       0xf0400000,   1 * MiB);
    create_unimplemented_device("npcm7xx.mcphy",        0xf05f0000,  64 * KiB);
    create_unimplemented_device("npcm7xx.vcd",          0xf0810000,  64 * KiB);
    create_unimplemented_device("npcm7xx.ece",          0xf0820000,   8 * KiB);
    create_unimplemented_device("npcm7xx.vdma",         0xf0822000,   8 * KiB);
    create_unimplemented_device("npcm7xx.usbd[0]",      0xf0830000,   4 * KiB);
    create_unimplemented_device("npcm7xx.usbd[1]",      0xf0831000,   4 * KiB);
    create_unimplemented_device("npcm7xx.usbd[2]",      0xf0832000,   4 * KiB);
    create_unimplemented_device("npcm7xx.usbd[3]",      0xf0833000,   4 * KiB);
    create_unimplemented_device("npcm7xx.usbd[4]",      0xf0834000,   4 * KiB);
    create_unimplemented_device("npcm7xx.usbd[5]",      0xf0835000,   4 * KiB);
    create_unimplemented_device("npcm7xx.usbd[6]",      0xf0836000,   4 * KiB);
    create_unimplemented_device("npcm7xx.usbd[7]",      0xf0837000,   4 * KiB);
    create_unimplemented_device("npcm7xx.usbd[8]",      0xf0838000,   4 * KiB);
    create_unimplemented_device("npcm7xx.usbd[9]",      0xf0839000,   4 * KiB);
    create_unimplemented_device("npcm7xx.sd",           0xf0840000,   8 * KiB);
    create_unimplemented_device("npcm7xx.pcimbx",       0xf0848000, 512 * KiB);
    create_unimplemented_device("npcm7xx.aes",          0xf0858000,   4 * KiB);
    create_unimplemented_device("npcm7xx.des",          0xf0859000,   4 * KiB);
    create_unimplemented_device("npcm7xx.sha",          0xf085a000,   4 * KiB);
    create_unimplemented_device("npcm7xx.secacc",       0xf085b000,   4 * KiB);
    create_unimplemented_device("npcm7xx.spixcs0",      0xf8000000,  16 * MiB);
    create_unimplemented_device("npcm7xx.spixcs1",      0xf9000000,  16 * MiB);
    create_unimplemented_device("npcm7xx.spix",         0xfb001000,   4 * KiB);
}

static const Property npcm7xx_properties[] = {
    DEFINE_PROP_LINK("dram-mr", NPCM7xxState, dram, TYPE_MEMORY_REGION,
                     MemoryRegion *),
};

static void npcm7xx_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = npcm7xx_realize;
    dc->user_creatable = false;
    device_class_set_props(dc, npcm7xx_properties);
}

static void npcm730_class_init(ObjectClass *oc, const void *data)
{
    NPCM7xxClass *nc = NPCM7XX_CLASS(oc);

    /* NPCM730 is optimized for data center use, so no graphics, etc. */
    nc->disabled_modules = 0x00300395;
    nc->num_cpus = 2;
}

static void npcm750_class_init(ObjectClass *oc, const void *data)
{
    NPCM7xxClass *nc = NPCM7XX_CLASS(oc);

    /* NPCM750 has 2 cores and a full set of peripherals */
    nc->disabled_modules = 0x00000000;
    nc->num_cpus = 2;
}

static const TypeInfo npcm7xx_soc_types[] = {
    {
        .name           = TYPE_NPCM7XX,
        .parent         = TYPE_DEVICE,
        .instance_size  = sizeof(NPCM7xxState),
        .instance_init  = npcm7xx_init,
        .class_size     = sizeof(NPCM7xxClass),
        .class_init     = npcm7xx_class_init,
        .abstract       = true,
    }, {
        .name           = TYPE_NPCM730,
        .parent         = TYPE_NPCM7XX,
        .class_init     = npcm730_class_init,
    }, {
        .name           = TYPE_NPCM750,
        .parent         = TYPE_NPCM7XX,
        .class_init     = npcm750_class_init,
    },
};

DEFINE_TYPES(npcm7xx_soc_types);
