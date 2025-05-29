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

#include "qemu/osdep.h"

#include "hw/boards.h"
#include "hw/arm/boot.h"
#include "hw/arm/bsa.h"
#include "hw/arm/npcm8xx.h"
#include "hw/char/serial-mm.h"
#include "hw/intc/arm_gic.h"
#include "hw/loader.h"
#include "hw/misc/unimp.h"
#include "hw/qdev-clock.h"
#include "hw/qdev-properties.h"
#include "qapi/error.h"
#include "qemu/units.h"
#include "system/system.h"

/*
 * This covers the whole MMIO space. We'll use this to catch any MMIO accesses
 * that aren't handled by a device.
 */
#define NPCM8XX_MMIO_BA         0x80000000
#define NPCM8XX_MMIO_SZ         0x7ffd0000

/* OTP fuse array */
#define NPCM8XX_OTP_BA          0xf0189000

/* GIC Distributor */
#define NPCM8XX_GICD_BA         0xdfff9000
#define NPCM8XX_GICC_BA         0xdfffa000

/* Core system modules. */
#define NPCM8XX_CPUP_BA         0xf03fe000
#define NPCM8XX_GCR_BA          0xf0800000
#define NPCM8XX_CLK_BA          0xf0801000
#define NPCM8XX_MC_BA           0xf0824000
#define NPCM8XX_RNG_BA          0xf000b000

/* ADC Module */
#define NPCM8XX_ADC_BA          0xf000c000

/* Internal AHB SRAM */
#define NPCM8XX_RAM3_BA         0xc0008000
#define NPCM8XX_RAM3_SZ         (4 * KiB)

/* Memory blocks at the end of the address space */
#define NPCM8XX_RAM2_BA         0xfffb0000
#define NPCM8XX_RAM2_SZ         (256 * KiB)
#define NPCM8XX_ROM_BA          0xffff0100
#define NPCM8XX_ROM_SZ          (64 * KiB)

/* SDHCI Modules */
#define NPCM8XX_MMC_BA          0xf0842000

/* PCS Module */
#define NPCM8XX_PCS_BA          0xf0780000

/* PSPI Modules */
#define NPCM8XX_PSPI_BA         0xf0201000

/* Run PLL1 at 1600 MHz */
#define NPCM8XX_PLLCON1_FIXUP_VAL   0x00402101
/* Run the CPU from PLL1 and UART from PLL2 */
#define NPCM8XX_CLKSEL_FIXUP_VAL    0x004aaba9

/* Clock configuration values to be fixed up when bypassing bootloader */

/*
 * Interrupt lines going into the GIC. This does not include internal Cortex-A35
 * interrupts.
 */
enum NPCM8xxInterrupt {
    NPCM8XX_ADC_IRQ             = 0,
    NPCM8XX_PECI_IRQ            = 6,
    NPCM8XX_KCS_HIB_IRQ         = 9,
    NPCM8XX_GMAC1_IRQ           = 14,
    NPCM8XX_GMAC2_IRQ,
    NPCM8XX_GMAC3_IRQ,
    NPCM8XX_GMAC4_IRQ,
    NPCM8XX_MMC_IRQ             = 26,
    NPCM8XX_PSPI_IRQ            = 28,
    NPCM8XX_TIMER0_IRQ          = 32,   /* Timer Module 0 */
    NPCM8XX_TIMER1_IRQ,
    NPCM8XX_TIMER2_IRQ,
    NPCM8XX_TIMER3_IRQ,
    NPCM8XX_TIMER4_IRQ,
    NPCM8XX_TIMER5_IRQ,                 /* Timer Module 1 */
    NPCM8XX_TIMER6_IRQ,
    NPCM8XX_TIMER7_IRQ,
    NPCM8XX_TIMER8_IRQ,
    NPCM8XX_TIMER9_IRQ,
    NPCM8XX_TIMER10_IRQ,                /* Timer Module 2 */
    NPCM8XX_TIMER11_IRQ,
    NPCM8XX_TIMER12_IRQ,
    NPCM8XX_TIMER13_IRQ,
    NPCM8XX_TIMER14_IRQ,
    NPCM8XX_WDG0_IRQ            = 47,   /* Timer Module 0 Watchdog */
    NPCM8XX_WDG1_IRQ,                   /* Timer Module 1 Watchdog */
    NPCM8XX_WDG2_IRQ,                   /* Timer Module 2 Watchdog */
    NPCM8XX_EHCI1_IRQ           = 61,
    NPCM8XX_OHCI1_IRQ,
    NPCM8XX_EHCI2_IRQ,
    NPCM8XX_OHCI2_IRQ,
    NPCM8XX_PWM0_IRQ            = 93,   /* PWM module 0 */
    NPCM8XX_PWM1_IRQ,                   /* PWM module 1 */
    NPCM8XX_MFT0_IRQ            = 96,   /* MFT module 0 */
    NPCM8XX_MFT1_IRQ,                   /* MFT module 1 */
    NPCM8XX_MFT2_IRQ,                   /* MFT module 2 */
    NPCM8XX_MFT3_IRQ,                   /* MFT module 3 */
    NPCM8XX_MFT4_IRQ,                   /* MFT module 4 */
    NPCM8XX_MFT5_IRQ,                   /* MFT module 5 */
    NPCM8XX_MFT6_IRQ,                   /* MFT module 6 */
    NPCM8XX_MFT7_IRQ,                   /* MFT module 7 */
    NPCM8XX_PCI_MBOX1_IRQ       = 105,
    NPCM8XX_PCI_MBOX2_IRQ,
    NPCM8XX_GPIO0_IRQ           = 116,
    NPCM8XX_GPIO1_IRQ,
    NPCM8XX_GPIO2_IRQ,
    NPCM8XX_GPIO3_IRQ,
    NPCM8XX_GPIO4_IRQ,
    NPCM8XX_GPIO5_IRQ,
    NPCM8XX_GPIO6_IRQ,
    NPCM8XX_GPIO7_IRQ,
    NPCM8XX_SMBUS0_IRQ          = 128,
    NPCM8XX_SMBUS1_IRQ,
    NPCM8XX_SMBUS2_IRQ,
    NPCM8XX_SMBUS3_IRQ,
    NPCM8XX_SMBUS4_IRQ,
    NPCM8XX_SMBUS5_IRQ,
    NPCM8XX_SMBUS6_IRQ,
    NPCM8XX_SMBUS7_IRQ,
    NPCM8XX_SMBUS8_IRQ,
    NPCM8XX_SMBUS9_IRQ,
    NPCM8XX_SMBUS10_IRQ,
    NPCM8XX_SMBUS11_IRQ,
    NPCM8XX_SMBUS12_IRQ,
    NPCM8XX_SMBUS13_IRQ,
    NPCM8XX_SMBUS14_IRQ,
    NPCM8XX_SMBUS15_IRQ,
    NPCM8XX_SMBUS16_IRQ,
    NPCM8XX_SMBUS17_IRQ,
    NPCM8XX_SMBUS18_IRQ,
    NPCM8XX_SMBUS19_IRQ,
    NPCM8XX_SMBUS20_IRQ,
    NPCM8XX_SMBUS21_IRQ,
    NPCM8XX_SMBUS22_IRQ,
    NPCM8XX_SMBUS23_IRQ,
    NPCM8XX_SMBUS24_IRQ,
    NPCM8XX_SMBUS25_IRQ,
    NPCM8XX_SMBUS26_IRQ,
    NPCM8XX_UART0_IRQ           = 192,
    NPCM8XX_UART1_IRQ,
    NPCM8XX_UART2_IRQ,
    NPCM8XX_UART3_IRQ,
    NPCM8XX_UART4_IRQ,
    NPCM8XX_UART5_IRQ,
    NPCM8XX_UART6_IRQ,
};

/* Total number of GIC interrupts, including internal Cortex-A35 interrupts. */
#define NPCM8XX_NUM_IRQ         (288)
#define NPCM8XX_PPI_BASE(cpu)   \
    ((NPCM8XX_NUM_IRQ - GIC_INTERNAL) + (cpu) * GIC_INTERNAL)

/* Register base address for each Timer Module */
static const hwaddr npcm8xx_tim_addr[] = {
    0xf0008000,
    0xf0009000,
    0xf000a000,
};

/* Register base address for each 16550 UART */
static const hwaddr npcm8xx_uart_addr[] = {
    0xf0000000,
    0xf0001000,
    0xf0002000,
    0xf0003000,
    0xf0004000,
    0xf0005000,
    0xf0006000,
};

/* Direct memory-mapped access to SPI0 CS0-1. */
static const hwaddr npcm8xx_fiu0_flash_addr[] = {
    0x80000000, /* CS0 */
    0x88000000, /* CS1 */
};

/* Direct memory-mapped access to SPI1 CS0-3. */
static const hwaddr npcm8xx_fiu1_flash_addr[] = {
    0x90000000, /* CS0 */
    0x91000000, /* CS1 */
    0x92000000, /* CS2 */
    0x93000000, /* CS3 */
};

/* Direct memory-mapped access to SPI3 CS0-3. */
static const hwaddr npcm8xx_fiu3_flash_addr[] = {
    0xa0000000, /* CS0 */
    0xa8000000, /* CS1 */
    0xb0000000, /* CS2 */
    0xb8000000, /* CS3 */
};

/* Register base address for each PWM Module */
static const hwaddr npcm8xx_pwm_addr[] = {
    0xf0103000,
    0xf0104000,
    0xf0105000,
};

/* Register base address for each MFT Module */
static const hwaddr npcm8xx_mft_addr[] = {
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
static const hwaddr npcm8xx_smbus_addr[] = {
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
    0xfff00000,
    0xfff01000,
    0xfff02000,
    0xfff03000,
    0xfff04000,
    0xfff05000,
    0xfff06000,
    0xfff07000,
    0xfff08000,
    0xfff09000,
    0xfff0a000,
};

/* Register base address for each GMAC Module */
static const hwaddr npcm8xx_gmac_addr[] = {
    0xf0802000,
    0xf0804000,
    0xf0806000,
    0xf0808000,
};

/* Register base address for each USB host EHCI registers */
static const hwaddr npcm8xx_ehci_addr[] = {
    0xf0828100,
    0xf082a100,
};

/* Register base address for each USB host OHCI registers */
static const hwaddr npcm8xx_ohci_addr[] = {
    0xf0829000,
    0xf082b000,
};

static const struct {
    hwaddr regs_addr;
    uint32_t reset_pu;
    uint32_t reset_pd;
    uint32_t reset_osrc;
    uint32_t reset_odsc;
} npcm8xx_gpio[] = {
    {
        .regs_addr = 0xf0010000,
        .reset_pu = 0x00000300,
        .reset_pd = 0x000f0000,
    }, {
        .regs_addr = 0xf0011000,
        .reset_pu = 0xe0fefe01,
        .reset_pd = 0x07000000,
    }, {
        .regs_addr = 0xf0012000,
        .reset_pu = 0xc00fffff,
        .reset_pd = 0x3ff00000,
    }, {
        .regs_addr = 0xf0013000,
        .reset_pd = 0x00003000,
    }, {
        .regs_addr = 0xf0014000,
        .reset_pu = 0xffff0000,
    }, {
        .regs_addr = 0xf0015000,
        .reset_pu = 0xff8387fe,
        .reset_pd = 0x007c0001,
        .reset_osrc = 0x08000000,
    }, {
        .regs_addr = 0xf0016000,
        .reset_pu = 0x00000801,
        .reset_pd = 0x00000302,
    }, {
        .regs_addr = 0xf0017000,
        .reset_pu = 0x000002ff,
        .reset_pd = 0x00000c00,
    },
};

static const struct {
    const char *name;
    hwaddr regs_addr;
    int cs_count;
    const hwaddr *flash_addr;
    size_t flash_size;
} npcm8xx_fiu[] = {
    {
        .name = "fiu0",
        .regs_addr = 0xfb000000,
        .cs_count = ARRAY_SIZE(npcm8xx_fiu0_flash_addr),
        .flash_addr = npcm8xx_fiu0_flash_addr,
        .flash_size = 128 * MiB,
    },
    {
        .name = "fiu1",
        .regs_addr = 0xfb002000,
        .cs_count = ARRAY_SIZE(npcm8xx_fiu1_flash_addr),
        .flash_addr = npcm8xx_fiu1_flash_addr,
        .flash_size = 16 * MiB,
    }, {
        .name = "fiu3",
        .regs_addr = 0xc0000000,
        .cs_count = ARRAY_SIZE(npcm8xx_fiu3_flash_addr),
        .flash_addr = npcm8xx_fiu3_flash_addr,
        .flash_size = 128 * MiB,
    },
};

static struct arm_boot_info npcm8xx_binfo = {
    .loader_start           = NPCM8XX_LOADER_START,
    .smp_loader_start       = NPCM8XX_SMP_LOADER_START,
    .smp_bootreg_addr       = NPCM8XX_SMP_BOOTREG_ADDR,
    .gic_cpu_if_addr        = NPCM8XX_GICC_BA,
    .secure_boot            = false,
    .board_id               = -1,
    .board_setup_addr       = NPCM8XX_BOARD_SETUP_ADDR,
    .psci_conduit           = QEMU_PSCI_CONDUIT_SMC,
};

void npcm8xx_load_kernel(MachineState *machine, NPCM8xxState *soc)
{
    npcm8xx_binfo.ram_size = machine->ram_size;

    arm_load_kernel(&soc->cpu[0], machine, &npcm8xx_binfo);
}

static void npcm8xx_init_fuses(NPCM8xxState *s)
{
    NPCM8xxClass *nc = NPCM8XX_GET_CLASS(s);
    uint32_t value;

    /*
     * The initial mask of disabled modules indicates the chip derivative (e.g.
     * NPCM750 or NPCM730).
     */
    value = cpu_to_le32(nc->disabled_modules);
    npcm7xx_otp_array_write(&s->fuse_array, &value, NPCM7XX_FUSE_DERIVATIVE,
                            sizeof(value));
}

static void npcm8xx_write_adc_calibration(NPCM8xxState *s)
{
    /* Both ADC and the fuse array must have realized. */
    QEMU_BUILD_BUG_ON(sizeof(s->adc.calibration_r_values) != 4);
    npcm7xx_otp_array_write(&s->fuse_array, s->adc.calibration_r_values,
            NPCM7XX_FUSE_ADC_CALIB, sizeof(s->adc.calibration_r_values));
}

static qemu_irq npcm8xx_irq(NPCM8xxState *s, int n)
{
    return qdev_get_gpio_in(DEVICE(&s->gic), n);
}

static void npcm8xx_init(Object *obj)
{
    NPCM8xxState *s = NPCM8XX(obj);
    int i;

    object_initialize_child(obj, "cpu-cluster", &s->cpu_cluster,
                            TYPE_CPU_CLUSTER);
    for (i = 0; i < NPCM8XX_MAX_NUM_CPUS; i++) {
        object_initialize_child(OBJECT(&s->cpu_cluster), "cpu[*]", &s->cpu[i],
                                ARM_CPU_TYPE_NAME("cortex-a35"));
    }
    object_initialize_child(obj, "gic", &s->gic, TYPE_ARM_GIC);
    object_initialize_child(obj, "gcr", &s->gcr, TYPE_NPCM8XX_GCR);
    object_property_add_alias(obj, "power-on-straps", OBJECT(&s->gcr),
                              "power-on-straps");
    object_initialize_child(obj, "clk", &s->clk, TYPE_NPCM8XX_CLK);
    object_initialize_child(obj, "otp", &s->fuse_array,
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
        DEVICE(&s->smbus[i])->id = g_strdup_printf("smbus[%d]", i);
    }

    for (i = 0; i < ARRAY_SIZE(s->ehci); i++) {
        object_initialize_child(obj, "ehci[*]", &s->ehci[i], TYPE_NPCM7XX_EHCI);
    }
    for (i = 0; i < ARRAY_SIZE(s->ohci); i++) {
        object_initialize_child(obj, "ohci[*]", &s->ohci[i], TYPE_SYSBUS_OHCI);
    }

    QEMU_BUILD_BUG_ON(ARRAY_SIZE(npcm8xx_fiu) != ARRAY_SIZE(s->fiu));
    for (i = 0; i < ARRAY_SIZE(s->fiu); i++) {
        object_initialize_child(obj, npcm8xx_fiu[i].name, &s->fiu[i],
                                TYPE_NPCM7XX_FIU);
    }

    for (i = 0; i < ARRAY_SIZE(s->pwm); i++) {
        object_initialize_child(obj, "pwm[*]", &s->pwm[i], TYPE_NPCM7XX_PWM);
    }

    for (i = 0; i < ARRAY_SIZE(s->mft); i++) {
        object_initialize_child(obj, "mft[*]", &s->mft[i], TYPE_NPCM7XX_MFT);
    }

    for (i = 0; i < ARRAY_SIZE(s->gmac); i++) {
        object_initialize_child(obj, "gmac[*]", &s->gmac[i], TYPE_NPCM_GMAC);
    }
    object_initialize_child(obj, "pcs", &s->pcs, TYPE_NPCM_PCS);

    object_initialize_child(obj, "mmc", &s->mmc, TYPE_NPCM7XX_SDHCI);
    object_initialize_child(obj, "pspi", &s->pspi, TYPE_NPCM_PSPI);
}

static void npcm8xx_realize(DeviceState *dev, Error **errp)
{
    NPCM8xxState *s = NPCM8XX(dev);
    NPCM8xxClass *nc = NPCM8XX_GET_CLASS(s);
    int i;

    if (memory_region_size(s->dram) > NPCM8XX_DRAM_SZ) {
        error_setg(errp, "%s: NPCM8xx cannot address more than %" PRIu64
                   " MiB of DRAM", __func__, NPCM8XX_DRAM_SZ / MiB);
        return;
    }

    /* CPUs */
    for (i = 0; i < nc->num_cpus; i++) {
        object_property_set_int(OBJECT(&s->cpu[i]), "mp-affinity",
                                arm_build_mp_affinity(i, NPCM8XX_MAX_NUM_CPUS),
                                &error_abort);
        object_property_set_bool(OBJECT(&s->cpu[i]), "reset-hivecs", true,
                                 &error_abort);
        object_property_set_int(OBJECT(&s->cpu[i]), "core-count",
                                nc->num_cpus, &error_abort);

        /* Disable security extensions. */
        object_property_set_bool(OBJECT(&s->cpu[i]), "has_el3", false,
                                 &error_abort);

        if (!qdev_realize(DEVICE(&s->cpu[i]), NULL, errp)) {
            return;
        }
    }

    /* ARM GIC for Cortex A35. Can only fail if we pass bad parameters here. */
    object_property_set_uint(OBJECT(&s->gic), "num-cpu", nc->num_cpus, errp);
    object_property_set_uint(OBJECT(&s->gic), "num-irq", NPCM8XX_NUM_IRQ, errp);
    object_property_set_uint(OBJECT(&s->gic), "revision", 2, errp);
    object_property_set_bool(OBJECT(&s->gic), "has-security-extensions", true,
                             errp);
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->gic), errp)) {
        return;
    }
    for (i = 0; i < nc->num_cpus; i++) {
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->gic), i,
                           qdev_get_gpio_in(DEVICE(&s->cpu[i]), ARM_CPU_IRQ));
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->gic), i + nc->num_cpus,
                           qdev_get_gpio_in(DEVICE(&s->cpu[i]), ARM_CPU_FIQ));
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->gic), i + nc->num_cpus * 2,
                           qdev_get_gpio_in(DEVICE(&s->cpu[i]), ARM_CPU_VIRQ));
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->gic), i + nc->num_cpus * 3,
                           qdev_get_gpio_in(DEVICE(&s->cpu[i]), ARM_CPU_VFIQ));

        qdev_connect_gpio_out(DEVICE(&s->cpu[i]), GTIMER_PHYS,
            qdev_get_gpio_in(DEVICE(&s->gic),
                NPCM8XX_PPI_BASE(i) + ARCH_TIMER_NS_EL1_IRQ));
        qdev_connect_gpio_out(DEVICE(&s->cpu[i]), GTIMER_VIRT,
            qdev_get_gpio_in(DEVICE(&s->gic),
                NPCM8XX_PPI_BASE(i) + ARCH_TIMER_VIRT_IRQ));
        qdev_connect_gpio_out(DEVICE(&s->cpu[i]), GTIMER_HYP,
            qdev_get_gpio_in(DEVICE(&s->gic),
                NPCM8XX_PPI_BASE(i) + ARCH_TIMER_NS_EL2_IRQ));
        qdev_connect_gpio_out(DEVICE(&s->cpu[i]), GTIMER_SEC,
            qdev_get_gpio_in(DEVICE(&s->gic),
                NPCM8XX_PPI_BASE(i) + ARCH_TIMER_S_EL1_IRQ));
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->gic), 0, NPCM8XX_GICD_BA);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->gic), 1, NPCM8XX_GICC_BA);

    /* CPU cluster */
    qdev_prop_set_uint32(DEVICE(&s->cpu_cluster), "cluster-id", 0);
    qdev_realize(DEVICE(&s->cpu_cluster), NULL, &error_fatal);

    /* System Global Control Registers (GCR). Can fail due to user input. */
    object_property_set_int(OBJECT(&s->gcr), "disabled-modules",
                            nc->disabled_modules, &error_abort);
    object_property_add_const_link(OBJECT(&s->gcr), "dram-mr", OBJECT(s->dram));
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->gcr), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->gcr), 0, NPCM8XX_GCR_BA);

    /* Clock Control Registers (CLK). Cannot fail. */
    sysbus_realize(SYS_BUS_DEVICE(&s->clk), &error_abort);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->clk), 0, NPCM8XX_CLK_BA);

    /* OTP fuse strap array. Cannot fail. */
    sysbus_realize(SYS_BUS_DEVICE(&s->fuse_array), &error_abort);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->fuse_array), 0, NPCM8XX_OTP_BA);
    npcm8xx_init_fuses(s);

    /* Fake Memory Controller (MC). Cannot fail. */
    sysbus_realize(SYS_BUS_DEVICE(&s->mc), &error_abort);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->mc), 0, NPCM8XX_MC_BA);

    /* ADC Modules. Cannot fail. */
    qdev_connect_clock_in(DEVICE(&s->adc), "clock", qdev_get_clock_out(
                          DEVICE(&s->clk), "adc-clock"));
    sysbus_realize(SYS_BUS_DEVICE(&s->adc), &error_abort);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->adc), 0, NPCM8XX_ADC_BA);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->adc), 0,
                       npcm8xx_irq(s, NPCM8XX_ADC_IRQ));
    npcm8xx_write_adc_calibration(s);

    /* Timer Modules (TIM). Cannot fail. */
    QEMU_BUILD_BUG_ON(ARRAY_SIZE(npcm8xx_tim_addr) != ARRAY_SIZE(s->tim));
    for (i = 0; i < ARRAY_SIZE(s->tim); i++) {
        SysBusDevice *sbd = SYS_BUS_DEVICE(&s->tim[i]);
        int first_irq;
        int j;

        /* Connect the timer clock. */
        qdev_connect_clock_in(DEVICE(&s->tim[i]), "clock", qdev_get_clock_out(
                    DEVICE(&s->clk), "timer-clock"));

        sysbus_realize(sbd, &error_abort);
        sysbus_mmio_map(sbd, 0, npcm8xx_tim_addr[i]);

        first_irq = NPCM8XX_TIMER0_IRQ + i * NPCM7XX_TIMERS_PER_CTRL;
        for (j = 0; j < NPCM7XX_TIMERS_PER_CTRL; j++) {
            qemu_irq irq = npcm8xx_irq(s, first_irq + j);
            sysbus_connect_irq(sbd, j, irq);
        }

        /* IRQ for watchdogs */
        sysbus_connect_irq(sbd, NPCM7XX_TIMERS_PER_CTRL,
                npcm8xx_irq(s, NPCM8XX_WDG0_IRQ + i));
        /* GPIO that connects clk module with watchdog */
        qdev_connect_gpio_out_named(DEVICE(&s->tim[i]),
                NPCM7XX_WATCHDOG_RESET_GPIO_OUT, 0,
                qdev_get_gpio_in_named(DEVICE(&s->clk),
                        NPCM7XX_WATCHDOG_RESET_GPIO_IN, i));
    }

    /* UART0..6 (16550 compatible) */
    for (i = 0; i < ARRAY_SIZE(npcm8xx_uart_addr); i++) {
        serial_mm_init(get_system_memory(), npcm8xx_uart_addr[i], 2,
                       npcm8xx_irq(s, NPCM8XX_UART0_IRQ + i), 115200,
                       serial_hd(i), DEVICE_LITTLE_ENDIAN);
    }

    /* Random Number Generator. Cannot fail. */
    sysbus_realize(SYS_BUS_DEVICE(&s->rng), &error_abort);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->rng), 0, NPCM8XX_RNG_BA);

    /* GPIO modules. Cannot fail. */
    QEMU_BUILD_BUG_ON(ARRAY_SIZE(npcm8xx_gpio) != ARRAY_SIZE(s->gpio));
    for (i = 0; i < ARRAY_SIZE(s->gpio); i++) {
        Object *obj = OBJECT(&s->gpio[i]);

        object_property_set_uint(obj, "reset-pullup",
                                 npcm8xx_gpio[i].reset_pu, &error_abort);
        object_property_set_uint(obj, "reset-pulldown",
                                 npcm8xx_gpio[i].reset_pd, &error_abort);
        object_property_set_uint(obj, "reset-osrc",
                                 npcm8xx_gpio[i].reset_osrc, &error_abort);
        object_property_set_uint(obj, "reset-odsc",
                                 npcm8xx_gpio[i].reset_odsc, &error_abort);
        sysbus_realize(SYS_BUS_DEVICE(obj), &error_abort);
        sysbus_mmio_map(SYS_BUS_DEVICE(obj), 0, npcm8xx_gpio[i].regs_addr);
        sysbus_connect_irq(SYS_BUS_DEVICE(obj), 0,
                           npcm8xx_irq(s, NPCM8XX_GPIO0_IRQ + i));
    }

    /* SMBus modules. Cannot fail. */
    QEMU_BUILD_BUG_ON(ARRAY_SIZE(npcm8xx_smbus_addr) != ARRAY_SIZE(s->smbus));
    for (i = 0; i < ARRAY_SIZE(s->smbus); i++) {
        Object *obj = OBJECT(&s->smbus[i]);

        sysbus_realize(SYS_BUS_DEVICE(obj), &error_abort);
        sysbus_mmio_map(SYS_BUS_DEVICE(obj), 0, npcm8xx_smbus_addr[i]);
        sysbus_connect_irq(SYS_BUS_DEVICE(obj), 0,
                           npcm8xx_irq(s, NPCM8XX_SMBUS0_IRQ + i));
    }

    /* USB Host */
    QEMU_BUILD_BUG_ON(ARRAY_SIZE(s->ohci) != ARRAY_SIZE(s->ehci));
    for (i = 0; i < ARRAY_SIZE(s->ehci); i++) {
        object_property_set_bool(OBJECT(&s->ehci[i]), "companion-enable", true,
                                 &error_abort);
        sysbus_realize(SYS_BUS_DEVICE(&s->ehci[i]), &error_abort);
        sysbus_mmio_map(SYS_BUS_DEVICE(&s->ehci[i]), 0, npcm8xx_ehci_addr[i]);
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->ehci[i]), 0,
                           npcm8xx_irq(s, NPCM8XX_EHCI1_IRQ + 2 * i));
    }
    for (i = 0; i < ARRAY_SIZE(s->ohci); i++) {
        object_property_set_str(OBJECT(&s->ohci[i]), "masterbus", "usb-bus.0",
                                &error_abort);
        object_property_set_uint(OBJECT(&s->ohci[i]), "num-ports", 1,
                                 &error_abort);
        object_property_set_uint(OBJECT(&s->ohci[i]), "firstport", i,
                                 &error_abort);
        sysbus_realize(SYS_BUS_DEVICE(&s->ohci[i]), &error_abort);
        sysbus_mmio_map(SYS_BUS_DEVICE(&s->ohci[i]), 0, npcm8xx_ohci_addr[i]);
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->ohci[i]), 0,
                           npcm8xx_irq(s, NPCM8XX_OHCI1_IRQ + 2 * i));
    }

    /* PWM Modules. Cannot fail. */
    QEMU_BUILD_BUG_ON(ARRAY_SIZE(npcm8xx_pwm_addr) != ARRAY_SIZE(s->pwm));
    for (i = 0; i < ARRAY_SIZE(s->pwm); i++) {
        SysBusDevice *sbd = SYS_BUS_DEVICE(&s->pwm[i]);

        qdev_connect_clock_in(DEVICE(&s->pwm[i]), "clock", qdev_get_clock_out(
                    DEVICE(&s->clk), "apb3-clock"));
        sysbus_realize(sbd, &error_abort);
        sysbus_mmio_map(sbd, 0, npcm8xx_pwm_addr[i]);
        sysbus_connect_irq(sbd, i, npcm8xx_irq(s, NPCM8XX_PWM0_IRQ + i));
    }

    /* MFT Modules. Cannot fail. */
    QEMU_BUILD_BUG_ON(ARRAY_SIZE(npcm8xx_mft_addr) != ARRAY_SIZE(s->mft));
    for (i = 0; i < ARRAY_SIZE(s->mft); i++) {
        SysBusDevice *sbd = SYS_BUS_DEVICE(&s->mft[i]);

        qdev_connect_clock_in(DEVICE(&s->mft[i]), "clock-in",
                              qdev_get_clock_out(DEVICE(&s->clk),
                                                 "apb4-clock"));
        sysbus_realize(sbd, &error_abort);
        sysbus_mmio_map(sbd, 0, npcm8xx_mft_addr[i]);
        sysbus_connect_irq(sbd, 0, npcm8xx_irq(s, NPCM8XX_MFT0_IRQ + i));
    }

    /*
     * GMAC Modules. Cannot fail.
     */
    QEMU_BUILD_BUG_ON(ARRAY_SIZE(npcm8xx_gmac_addr) != ARRAY_SIZE(s->gmac));
    for (i = 0; i < ARRAY_SIZE(s->gmac); i++) {
        SysBusDevice *sbd = SYS_BUS_DEVICE(&s->gmac[i]);

        /* This is used to make sure that the NIC can create the device */
        qemu_configure_nic_device(DEVICE(sbd), false, NULL);

        /*
         * The device exists regardless of whether it's connected to a QEMU
         * netdev backend. So always instantiate it even if there is no
         * backend.
         */
        sysbus_realize(sbd, &error_abort);
        sysbus_mmio_map(sbd, 0, npcm8xx_gmac_addr[i]);
        /*
         * N.B. The values for the second argument sysbus_connect_irq are
         * chosen to match the registration order in npcm7xx_emc_realize.
         */
        sysbus_connect_irq(sbd, 0, npcm8xx_irq(s, NPCM8XX_GMAC1_IRQ + i));
    }
    /*
     * GMAC Physical Coding Sublayer(PCS) Module. Cannot fail.
     */
    sysbus_realize(SYS_BUS_DEVICE(&s->pcs), &error_abort);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->pcs), 0, NPCM8XX_PCS_BA);

    /*
     * Flash Interface Unit (FIU). Can fail if incorrect number of chip selects
     * specified, but this is a programming error.
     */
    QEMU_BUILD_BUG_ON(ARRAY_SIZE(npcm8xx_fiu) != ARRAY_SIZE(s->fiu));
    for (i = 0; i < ARRAY_SIZE(s->fiu); i++) {
        SysBusDevice *sbd = SYS_BUS_DEVICE(&s->fiu[i]);
        int j;

        object_property_set_int(OBJECT(sbd), "cs-count",
                                npcm8xx_fiu[i].cs_count, &error_abort);
        object_property_set_int(OBJECT(sbd), "flash-size",
                                npcm8xx_fiu[i].flash_size, &error_abort);
        sysbus_realize(sbd, &error_abort);

        sysbus_mmio_map(sbd, 0, npcm8xx_fiu[i].regs_addr);
        for (j = 0; j < npcm8xx_fiu[i].cs_count; j++) {
            sysbus_mmio_map(sbd, j + 1, npcm8xx_fiu[i].flash_addr[j]);
        }
    }

    /* RAM2 (SRAM) */
    memory_region_init_ram(&s->sram, OBJECT(dev), "ram2",
                           NPCM8XX_RAM2_SZ, &error_abort);
    memory_region_add_subregion(get_system_memory(), NPCM8XX_RAM2_BA, &s->sram);

    /* RAM3 (SRAM) */
    memory_region_init_ram(&s->ram3, OBJECT(dev), "ram3",
                           NPCM8XX_RAM3_SZ, &error_abort);
    memory_region_add_subregion(get_system_memory(), NPCM8XX_RAM3_BA, &s->ram3);

    /* Internal ROM */
    memory_region_init_rom(&s->irom, OBJECT(dev), "irom", NPCM8XX_ROM_SZ,
                           &error_abort);
    memory_region_add_subregion(get_system_memory(), NPCM8XX_ROM_BA, &s->irom);

    /* SDHCI */
    sysbus_realize(SYS_BUS_DEVICE(&s->mmc), &error_abort);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->mmc), 0, NPCM8XX_MMC_BA);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->mmc), 0,
            npcm8xx_irq(s, NPCM8XX_MMC_IRQ));

    /* PSPI */
    sysbus_realize(SYS_BUS_DEVICE(&s->pspi), &error_abort);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->pspi), 0, NPCM8XX_PSPI_BA);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->pspi), 0,
            npcm8xx_irq(s, NPCM8XX_PSPI_IRQ));

    create_unimplemented_device("npcm8xx.shm",          0xc0001000,   4 * KiB);
    create_unimplemented_device("npcm8xx.gicextra",     0xdfffa000,  24 * KiB);
    create_unimplemented_device("npcm8xx.vdmx",         0xe0800000,   4 * KiB);
    create_unimplemented_device("npcm8xx.pcierc",       0xe1000000,  64 * KiB);
    create_unimplemented_device("npcm8xx.rootc",        0xe8000000, 128 * MiB);
    create_unimplemented_device("npcm8xx.kcs",          0xf0007000,   4 * KiB);
    create_unimplemented_device("npcm8xx.gfxi",         0xf000e000,   4 * KiB);
    create_unimplemented_device("npcm8xx.fsw",          0xf000f000,   4 * KiB);
    create_unimplemented_device("npcm8xx.bt",           0xf0030000,   4 * KiB);
    create_unimplemented_device("npcm8xx.espi",         0xf009f000,   4 * KiB);
    create_unimplemented_device("npcm8xx.peci",         0xf0100000,   4 * KiB);
    create_unimplemented_device("npcm8xx.siox[1]",      0xf0101000,   4 * KiB);
    create_unimplemented_device("npcm8xx.siox[2]",      0xf0102000,   4 * KiB);
    create_unimplemented_device("npcm8xx.tmps",         0xf0188000,   4 * KiB);
    create_unimplemented_device("npcm8xx.viru1",        0xf0204000,   4 * KiB);
    create_unimplemented_device("npcm8xx.viru2",        0xf0205000,   4 * KiB);
    create_unimplemented_device("npcm8xx.jtm1",         0xf0208000,   4 * KiB);
    create_unimplemented_device("npcm8xx.jtm2",         0xf0209000,   4 * KiB);
    create_unimplemented_device("npcm8xx.flm0",         0xf0210000,   4 * KiB);
    create_unimplemented_device("npcm8xx.flm1",         0xf0211000,   4 * KiB);
    create_unimplemented_device("npcm8xx.flm2",         0xf0212000,   4 * KiB);
    create_unimplemented_device("npcm8xx.flm3",         0xf0213000,   4 * KiB);
    create_unimplemented_device("npcm8xx.ahbpci",       0xf0400000,   1 * MiB);
    create_unimplemented_device("npcm8xx.dap",          0xf0500000, 960 * KiB);
    create_unimplemented_device("npcm8xx.mcphy",        0xf05f0000,  64 * KiB);
    create_unimplemented_device("npcm8xx.tsgen",        0xf07fc000,   8 * KiB);
    create_unimplemented_device("npcm8xx.copctl",       0xf080c000,   4 * KiB);
    create_unimplemented_device("npcm8xx.tipctl",       0xf080d000,   4 * KiB);
    create_unimplemented_device("npcm8xx.rst",          0xf080e000,   4 * KiB);
    create_unimplemented_device("npcm8xx.vcd",          0xf0810000,  64 * KiB);
    create_unimplemented_device("npcm8xx.ece",          0xf0820000,   8 * KiB);
    create_unimplemented_device("npcm8xx.vdma",         0xf0822000,   8 * KiB);
    create_unimplemented_device("npcm8xx.usbd[0]",      0xf0830000,   4 * KiB);
    create_unimplemented_device("npcm8xx.usbd[1]",      0xf0831000,   4 * KiB);
    create_unimplemented_device("npcm8xx.usbd[2]",      0xf0832000,   4 * KiB);
    create_unimplemented_device("npcm8xx.usbd[3]",      0xf0833000,   4 * KiB);
    create_unimplemented_device("npcm8xx.usbd[4]",      0xf0834000,   4 * KiB);
    create_unimplemented_device("npcm8xx.usbd[5]",      0xf0835000,   4 * KiB);
    create_unimplemented_device("npcm8xx.usbd[6]",      0xf0836000,   4 * KiB);
    create_unimplemented_device("npcm8xx.usbd[7]",      0xf0837000,   4 * KiB);
    create_unimplemented_device("npcm8xx.usbd[8]",      0xf0838000,   4 * KiB);
    create_unimplemented_device("npcm8xx.usbd[9]",      0xf0839000,   4 * KiB);
    create_unimplemented_device("npcm8xx.pci_mbox1",    0xf0848000,  64 * KiB);
    create_unimplemented_device("npcm8xx.gdma0",        0xf0850000,   4 * KiB);
    create_unimplemented_device("npcm8xx.gdma1",        0xf0851000,   4 * KiB);
    create_unimplemented_device("npcm8xx.gdma2",        0xf0852000,   4 * KiB);
    create_unimplemented_device("npcm8xx.aes",          0xf0858000,   4 * KiB);
    create_unimplemented_device("npcm8xx.des",          0xf0859000,   4 * KiB);
    create_unimplemented_device("npcm8xx.sha",          0xf085a000,   4 * KiB);
    create_unimplemented_device("npcm8xx.pci_mbox2",    0xf0868000,  64 * KiB);
    create_unimplemented_device("npcm8xx.i3c0",         0xfff10000,   4 * KiB);
    create_unimplemented_device("npcm8xx.i3c1",         0xfff11000,   4 * KiB);
    create_unimplemented_device("npcm8xx.i3c2",         0xfff12000,   4 * KiB);
    create_unimplemented_device("npcm8xx.i3c3",         0xfff13000,   4 * KiB);
    create_unimplemented_device("npcm8xx.i3c4",         0xfff14000,   4 * KiB);
    create_unimplemented_device("npcm8xx.i3c5",         0xfff15000,   4 * KiB);
    create_unimplemented_device("npcm8xx.spixcs0",      0xf8000000,  16 * MiB);
    create_unimplemented_device("npcm8xx.spixcs1",      0xf9000000,  16 * MiB);
    create_unimplemented_device("npcm8xx.spix",         0xfb001000,   4 * KiB);
    create_unimplemented_device("npcm8xx.vect",         0xffff0000,   256);
}

static const Property npcm8xx_properties[] = {
    DEFINE_PROP_LINK("dram-mr", NPCM8xxState, dram, TYPE_MEMORY_REGION,
                     MemoryRegion *),
};

static void npcm8xx_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    NPCM8xxClass *nc = NPCM8XX_CLASS(oc);

    dc->realize = npcm8xx_realize;
    dc->user_creatable = false;
    nc->disabled_modules = 0x00000000;
    nc->num_cpus = NPCM8XX_MAX_NUM_CPUS;
    device_class_set_props(dc, npcm8xx_properties);
}

static const TypeInfo npcm8xx_soc_types[] = {
    {
        .name           = TYPE_NPCM8XX,
        .parent         = TYPE_DEVICE,
        .instance_size  = sizeof(NPCM8xxState),
        .instance_init  = npcm8xx_init,
        .class_size     = sizeof(NPCM8xxClass),
        .class_init     = npcm8xx_class_init,
    },
};

DEFINE_TYPES(npcm8xx_soc_types);
