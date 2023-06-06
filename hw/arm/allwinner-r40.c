/*
 * Allwinner R40/A40i/T3 System on Chip emulation
 *
 * Copyright (C) 2023 qianfan Zhao <qianfanguijin@163.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/bswap.h"
#include "qemu/module.h"
#include "qemu/units.h"
#include "hw/qdev-core.h"
#include "hw/sysbus.h"
#include "hw/char/serial.h"
#include "hw/misc/unimp.h"
#include "hw/usb/hcd-ehci.h"
#include "hw/loader.h"
#include "sysemu/sysemu.h"
#include "hw/arm/allwinner-r40.h"
#include "hw/misc/allwinner-r40-dramc.h"

/* Memory map */
const hwaddr allwinner_r40_memmap[] = {
    [AW_R40_DEV_SRAM_A1]    = 0x00000000,
    [AW_R40_DEV_SRAM_A2]    = 0x00004000,
    [AW_R40_DEV_SRAM_A3]    = 0x00008000,
    [AW_R40_DEV_SRAM_A4]    = 0x0000b400,
    [AW_R40_DEV_SRAMC]      = 0x01c00000,
    [AW_R40_DEV_EMAC]       = 0x01c0b000,
    [AW_R40_DEV_MMC0]       = 0x01c0f000,
    [AW_R40_DEV_MMC1]       = 0x01c10000,
    [AW_R40_DEV_MMC2]       = 0x01c11000,
    [AW_R40_DEV_MMC3]       = 0x01c12000,
    [AW_R40_DEV_CCU]        = 0x01c20000,
    [AW_R40_DEV_PIT]        = 0x01c20c00,
    [AW_R40_DEV_UART0]      = 0x01c28000,
    [AW_R40_DEV_UART1]      = 0x01c28400,
    [AW_R40_DEV_UART2]      = 0x01c28800,
    [AW_R40_DEV_UART3]      = 0x01c28c00,
    [AW_R40_DEV_UART4]      = 0x01c29000,
    [AW_R40_DEV_UART5]      = 0x01c29400,
    [AW_R40_DEV_UART6]      = 0x01c29800,
    [AW_R40_DEV_UART7]      = 0x01c29c00,
    [AW_R40_DEV_TWI0]       = 0x01c2ac00,
    [AW_R40_DEV_GMAC]       = 0x01c50000,
    [AW_R40_DEV_DRAMCOM]    = 0x01c62000,
    [AW_R40_DEV_DRAMCTL]    = 0x01c63000,
    [AW_R40_DEV_DRAMPHY]    = 0x01c65000,
    [AW_R40_DEV_GIC_DIST]   = 0x01c81000,
    [AW_R40_DEV_GIC_CPU]    = 0x01c82000,
    [AW_R40_DEV_GIC_HYP]    = 0x01c84000,
    [AW_R40_DEV_GIC_VCPU]   = 0x01c86000,
    [AW_R40_DEV_SDRAM]      = 0x40000000
};

/* List of unimplemented devices */
struct AwR40Unimplemented {
    const char *device_name;
    hwaddr base;
    hwaddr size;
};

static struct AwR40Unimplemented r40_unimplemented[] = {
    { "d-engine",   0x01000000, 4 * MiB },
    { "d-inter",    0x01400000, 128 * KiB },
    { "dma",        0x01c02000, 4 * KiB },
    { "nfdc",       0x01c03000, 4 * KiB },
    { "ts",         0x01c04000, 4 * KiB },
    { "spi0",       0x01c05000, 4 * KiB },
    { "spi1",       0x01c06000, 4 * KiB },
    { "cs0",        0x01c09000, 4 * KiB },
    { "keymem",     0x01c0a000, 4 * KiB },
    { "usb0-otg",   0x01c13000, 4 * KiB },
    { "usb0-host",  0x01c14000, 4 * KiB },
    { "crypto",     0x01c15000, 4 * KiB },
    { "spi2",       0x01c17000, 4 * KiB },
    { "sata",       0x01c18000, 4 * KiB },
    { "usb1-host",  0x01c19000, 4 * KiB },
    { "sid",        0x01c1b000, 4 * KiB },
    { "usb2-host",  0x01c1c000, 4 * KiB },
    { "cs1",        0x01c1d000, 4 * KiB },
    { "spi3",       0x01c1f000, 4 * KiB },
    { "rtc",        0x01c20400, 1 * KiB },
    { "pio",        0x01c20800, 1 * KiB },
    { "owa",        0x01c21000, 1 * KiB },
    { "ac97",       0x01c21400, 1 * KiB },
    { "cir0",       0x01c21800, 1 * KiB },
    { "cir1",       0x01c21c00, 1 * KiB },
    { "pcm0",       0x01c22000, 1 * KiB },
    { "pcm1",       0x01c22400, 1 * KiB },
    { "pcm2",       0x01c22800, 1 * KiB },
    { "audio",      0x01c22c00, 1 * KiB },
    { "keypad",     0x01c23000, 1 * KiB },
    { "pwm",        0x01c23400, 1 * KiB },
    { "keyadc",     0x01c24400, 1 * KiB },
    { "ths",        0x01c24c00, 1 * KiB },
    { "rtp",        0x01c25000, 1 * KiB },
    { "pmu",        0x01c25400, 1 * KiB },
    { "cpu-cfg",    0x01c25c00, 1 * KiB },
    { "uart0",      0x01c28000, 1 * KiB },
    { "uart1",      0x01c28400, 1 * KiB },
    { "uart2",      0x01c28800, 1 * KiB },
    { "uart3",      0x01c28c00, 1 * KiB },
    { "uart4",      0x01c29000, 1 * KiB },
    { "uart5",      0x01c29400, 1 * KiB },
    { "uart6",      0x01c29800, 1 * KiB },
    { "uart7",      0x01c29c00, 1 * KiB },
    { "ps20",       0x01c2a000, 1 * KiB },
    { "ps21",       0x01c2a400, 1 * KiB },
    { "twi1",       0x01c2b000, 1 * KiB },
    { "twi2",       0x01c2b400, 1 * KiB },
    { "twi3",       0x01c2b800, 1 * KiB },
    { "twi4",       0x01c2c000, 1 * KiB },
    { "scr",        0x01c2c400, 1 * KiB },
    { "tvd-top",    0x01c30000, 4 * KiB },
    { "tvd0",       0x01c31000, 4 * KiB },
    { "tvd1",       0x01c32000, 4 * KiB },
    { "tvd2",       0x01c33000, 4 * KiB },
    { "tvd3",       0x01c34000, 4 * KiB },
    { "gpu",        0x01c40000, 64 * KiB },
    { "hstmr",      0x01c60000, 4 * KiB },
    { "tcon-top",   0x01c70000, 4 * KiB },
    { "lcd0",       0x01c71000, 4 * KiB },
    { "lcd1",       0x01c72000, 4 * KiB },
    { "tv0",        0x01c73000, 4 * KiB },
    { "tv1",        0x01c74000, 4 * KiB },
    { "tve-top",    0x01c90000, 16 * KiB },
    { "tve0",       0x01c94000, 16 * KiB },
    { "tve1",       0x01c98000, 16 * KiB },
    { "mipi_dsi",   0x01ca0000, 4 * KiB },
    { "mipi_dphy",  0x01ca1000, 4 * KiB },
    { "ve",         0x01d00000, 1024 * KiB },
    { "mp",         0x01e80000, 128 * KiB },
    { "hdmi",       0x01ee0000, 128 * KiB },
    { "prcm",       0x01f01400, 1 * KiB },
    { "debug",      0x3f500000, 64 * KiB },
    { "cpubist",    0x3f501000, 4 * KiB },
    { "dcu",        0x3fff0000, 64 * KiB },
    { "hstmr",      0x01c60000, 4 * KiB },
    { "brom",       0xffff0000, 36 * KiB }
};

/* Per Processor Interrupts */
enum {
    AW_R40_GIC_PPI_MAINT     =  9,
    AW_R40_GIC_PPI_HYPTIMER  = 10,
    AW_R40_GIC_PPI_VIRTTIMER = 11,
    AW_R40_GIC_PPI_SECTIMER  = 13,
    AW_R40_GIC_PPI_PHYSTIMER = 14
};

/* Shared Processor Interrupts */
enum {
    AW_R40_GIC_SPI_UART0     =  1,
    AW_R40_GIC_SPI_UART1     =  2,
    AW_R40_GIC_SPI_UART2     =  3,
    AW_R40_GIC_SPI_UART3     =  4,
    AW_R40_GIC_SPI_TWI0      =  7,
    AW_R40_GIC_SPI_UART4     = 17,
    AW_R40_GIC_SPI_UART5     = 18,
    AW_R40_GIC_SPI_UART6     = 19,
    AW_R40_GIC_SPI_UART7     = 20,
    AW_R40_GIC_SPI_TIMER0    = 22,
    AW_R40_GIC_SPI_TIMER1    = 23,
    AW_R40_GIC_SPI_MMC0      = 32,
    AW_R40_GIC_SPI_MMC1      = 33,
    AW_R40_GIC_SPI_MMC2      = 34,
    AW_R40_GIC_SPI_MMC3      = 35,
    AW_R40_GIC_SPI_EMAC      = 55,
    AW_R40_GIC_SPI_GMAC      = 85,
};

/* Allwinner R40 general constants */
enum {
    AW_R40_GIC_NUM_SPI       = 128
};

#define BOOT0_MAGIC             "eGON.BT0"

/* The low 8-bits of the 'boot_media' field in the SPL header */
#define SUNXI_BOOTED_FROM_MMC0  0
#define SUNXI_BOOTED_FROM_NAND  1
#define SUNXI_BOOTED_FROM_MMC2  2
#define SUNXI_BOOTED_FROM_SPI   3

struct boot_file_head {
    uint32_t            b_instruction;
    uint8_t             magic[8];
    uint32_t            check_sum;
    uint32_t            length;
    uint32_t            pub_head_size;
    uint32_t            fel_script_address;
    uint32_t            fel_uEnv_length;
    uint32_t            dt_name_offset;
    uint32_t            dram_size;
    uint32_t            boot_media;
    uint32_t            string_pool[13];
};

bool allwinner_r40_bootrom_setup(AwR40State *s, BlockBackend *blk, int unit)
{
    const int64_t rom_size = 32 * KiB;
    g_autofree uint8_t *buffer = g_new0(uint8_t, rom_size);
    struct boot_file_head *head = (struct boot_file_head *)buffer;

    if (blk_pread(blk, 8 * KiB, rom_size, buffer, 0) < 0) {
        error_setg(&error_fatal, "%s: failed to read BlockBackend data",
                   __func__);
        return false;
    }

    /* we only check the magic string here. */
    if (memcmp(head->magic, BOOT0_MAGIC, sizeof(head->magic))) {
        return false;
    }

    /*
     * Simulate the behavior of the bootROM, it will change the boot_media
     * flag to indicate where the chip is booting from. R40 can boot from
     * mmc0 or mmc2, the default value of boot_media is zero
     * (SUNXI_BOOTED_FROM_MMC0), let's fix this flag when it is booting from
     * the others.
     */
    if (unit == 2) {
        head->boot_media = cpu_to_le32(SUNXI_BOOTED_FROM_MMC2);
    } else {
        head->boot_media = cpu_to_le32(SUNXI_BOOTED_FROM_MMC0);
    }

    rom_add_blob("allwinner-r40.bootrom", buffer, rom_size,
                  rom_size, s->memmap[AW_R40_DEV_SRAM_A1],
                  NULL, NULL, NULL, NULL, false);
    return true;
}

static void allwinner_r40_init(Object *obj)
{
    static const char *mmc_names[AW_R40_NUM_MMCS] = {
        "mmc0", "mmc1", "mmc2", "mmc3"
    };
    AwR40State *s = AW_R40(obj);

    s->memmap = allwinner_r40_memmap;

    for (int i = 0; i < AW_R40_NUM_CPUS; i++) {
        object_initialize_child(obj, "cpu[*]", &s->cpus[i],
                                ARM_CPU_TYPE_NAME("cortex-a7"));
    }

    object_initialize_child(obj, "gic", &s->gic, TYPE_ARM_GIC);

    object_initialize_child(obj, "timer", &s->timer, TYPE_AW_A10_PIT);
    object_property_add_alias(obj, "clk0-freq", OBJECT(&s->timer),
                              "clk0-freq");
    object_property_add_alias(obj, "clk1-freq", OBJECT(&s->timer),
                              "clk1-freq");

    object_initialize_child(obj, "ccu", &s->ccu, TYPE_AW_R40_CCU);

    for (int i = 0; i < AW_R40_NUM_MMCS; i++) {
        object_initialize_child(obj, mmc_names[i], &s->mmc[i],
                                TYPE_AW_SDHOST_SUN50I_A64);
    }

    object_initialize_child(obj, "twi0", &s->i2c0, TYPE_AW_I2C_SUN6I);

    object_initialize_child(obj, "emac", &s->emac, TYPE_AW_EMAC);
    object_initialize_child(obj, "gmac", &s->gmac, TYPE_AW_SUN8I_EMAC);
    object_property_add_alias(obj, "gmac-phy-addr",
                              OBJECT(&s->gmac), "phy-addr");

    object_initialize_child(obj, "dramc", &s->dramc, TYPE_AW_R40_DRAMC);
    object_property_add_alias(obj, "ram-addr", OBJECT(&s->dramc),
                             "ram-addr");
    object_property_add_alias(obj, "ram-size", OBJECT(&s->dramc),
                              "ram-size");

    object_initialize_child(obj, "sramc", &s->sramc, TYPE_AW_SRAMC_SUN8I_R40);
}

static void allwinner_r40_realize(DeviceState *dev, Error **errp)
{
    const char *r40_nic_models[] = { "gmac", "emac", NULL };
    AwR40State *s = AW_R40(dev);
    unsigned i;

    /* CPUs */
    for (i = 0; i < AW_R40_NUM_CPUS; i++) {

        /*
         * Disable secondary CPUs. Guest EL3 firmware will start
         * them via CPU reset control registers.
         */
        qdev_prop_set_bit(DEVICE(&s->cpus[i]), "start-powered-off",
                          i > 0);

        /* All exception levels required */
        qdev_prop_set_bit(DEVICE(&s->cpus[i]), "has_el3", true);
        qdev_prop_set_bit(DEVICE(&s->cpus[i]), "has_el2", true);

        /* Mark realized */
        qdev_realize(DEVICE(&s->cpus[i]), NULL, &error_fatal);
    }

    /* Generic Interrupt Controller */
    qdev_prop_set_uint32(DEVICE(&s->gic), "num-irq", AW_R40_GIC_NUM_SPI +
                                                     GIC_INTERNAL);
    qdev_prop_set_uint32(DEVICE(&s->gic), "revision", 2);
    qdev_prop_set_uint32(DEVICE(&s->gic), "num-cpu", AW_R40_NUM_CPUS);
    qdev_prop_set_bit(DEVICE(&s->gic), "has-security-extensions", false);
    qdev_prop_set_bit(DEVICE(&s->gic), "has-virtualization-extensions", true);
    sysbus_realize(SYS_BUS_DEVICE(&s->gic), &error_fatal);

    sysbus_mmio_map(SYS_BUS_DEVICE(&s->gic), 0, s->memmap[AW_R40_DEV_GIC_DIST]);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->gic), 1, s->memmap[AW_R40_DEV_GIC_CPU]);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->gic), 2, s->memmap[AW_R40_DEV_GIC_HYP]);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->gic), 3, s->memmap[AW_R40_DEV_GIC_VCPU]);

    /*
     * Wire the outputs from each CPU's generic timer and the GICv2
     * maintenance interrupt signal to the appropriate GIC PPI inputs,
     * and the GIC's IRQ/FIQ/VIRQ/VFIQ interrupt outputs to the CPU's inputs.
     */
    for (i = 0; i < AW_R40_NUM_CPUS; i++) {
        DeviceState *cpudev = DEVICE(&s->cpus[i]);
        int ppibase = AW_R40_GIC_NUM_SPI + i * GIC_INTERNAL + GIC_NR_SGIS;
        int irq;
        /*
         * Mapping from the output timer irq lines from the CPU to the
         * GIC PPI inputs used for this board.
         */
        const int timer_irq[] = {
            [GTIMER_PHYS] = AW_R40_GIC_PPI_PHYSTIMER,
            [GTIMER_VIRT] = AW_R40_GIC_PPI_VIRTTIMER,
            [GTIMER_HYP]  = AW_R40_GIC_PPI_HYPTIMER,
            [GTIMER_SEC]  = AW_R40_GIC_PPI_SECTIMER,
        };

        /* Connect CPU timer outputs to GIC PPI inputs */
        for (irq = 0; irq < ARRAY_SIZE(timer_irq); irq++) {
            qdev_connect_gpio_out(cpudev, irq,
                                  qdev_get_gpio_in(DEVICE(&s->gic),
                                                   ppibase + timer_irq[irq]));
        }

        /* Connect GIC outputs to CPU interrupt inputs */
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->gic), i,
                           qdev_get_gpio_in(cpudev, ARM_CPU_IRQ));
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->gic), i + AW_R40_NUM_CPUS,
                           qdev_get_gpio_in(cpudev, ARM_CPU_FIQ));
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->gic), i + (2 * AW_R40_NUM_CPUS),
                           qdev_get_gpio_in(cpudev, ARM_CPU_VIRQ));
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->gic), i + (3 * AW_R40_NUM_CPUS),
                           qdev_get_gpio_in(cpudev, ARM_CPU_VFIQ));

        /* GIC maintenance signal */
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->gic), i + (4 * AW_R40_NUM_CPUS),
                           qdev_get_gpio_in(DEVICE(&s->gic),
                                            ppibase + AW_R40_GIC_PPI_MAINT));
    }

    /* Timer */
    sysbus_realize(SYS_BUS_DEVICE(&s->timer), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->timer), 0, s->memmap[AW_R40_DEV_PIT]);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->timer), 0,
                       qdev_get_gpio_in(DEVICE(&s->gic),
                       AW_R40_GIC_SPI_TIMER0));
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->timer), 1,
                       qdev_get_gpio_in(DEVICE(&s->gic),
                       AW_R40_GIC_SPI_TIMER1));

    /* SRAM */
    sysbus_realize(SYS_BUS_DEVICE(&s->sramc), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->sramc), 0, s->memmap[AW_R40_DEV_SRAMC]);

    memory_region_init_ram(&s->sram_a1, OBJECT(dev), "sram A1",
                            16 * KiB, &error_abort);
    memory_region_init_ram(&s->sram_a2, OBJECT(dev), "sram A2",
                            16 * KiB, &error_abort);
    memory_region_init_ram(&s->sram_a3, OBJECT(dev), "sram A3",
                            13 * KiB, &error_abort);
    memory_region_init_ram(&s->sram_a4, OBJECT(dev), "sram A4",
                            3 * KiB, &error_abort);
    memory_region_add_subregion(get_system_memory(),
                                s->memmap[AW_R40_DEV_SRAM_A1], &s->sram_a1);
    memory_region_add_subregion(get_system_memory(),
                                s->memmap[AW_R40_DEV_SRAM_A2], &s->sram_a2);
    memory_region_add_subregion(get_system_memory(),
                                s->memmap[AW_R40_DEV_SRAM_A3], &s->sram_a3);
    memory_region_add_subregion(get_system_memory(),
                                s->memmap[AW_R40_DEV_SRAM_A4], &s->sram_a4);

    /* Clock Control Unit */
    sysbus_realize(SYS_BUS_DEVICE(&s->ccu), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->ccu), 0, s->memmap[AW_R40_DEV_CCU]);

    /* SD/MMC */
    for (int i = 0; i < AW_R40_NUM_MMCS; i++) {
        qemu_irq irq = qdev_get_gpio_in(DEVICE(&s->gic),
                                        AW_R40_GIC_SPI_MMC0 + i);
        const hwaddr addr = s->memmap[AW_R40_DEV_MMC0 + i];

        object_property_set_link(OBJECT(&s->mmc[i]), "dma-memory",
                                 OBJECT(get_system_memory()), &error_fatal);
        sysbus_realize(SYS_BUS_DEVICE(&s->mmc[i]), &error_fatal);
        sysbus_mmio_map(SYS_BUS_DEVICE(&s->mmc[i]), 0, addr);
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->mmc[i]), 0, irq);
    }

    /* UART0. For future clocktree API: All UARTS are connected to APB2_CLK. */
    for (int i = 0; i < AW_R40_NUM_UARTS; i++) {
        static const int uart_irqs[AW_R40_NUM_UARTS] = {
            AW_R40_GIC_SPI_UART0,
            AW_R40_GIC_SPI_UART1,
            AW_R40_GIC_SPI_UART2,
            AW_R40_GIC_SPI_UART3,
            AW_R40_GIC_SPI_UART4,
            AW_R40_GIC_SPI_UART5,
            AW_R40_GIC_SPI_UART6,
            AW_R40_GIC_SPI_UART7,
        };
        const hwaddr addr = s->memmap[AW_R40_DEV_UART0 + i];

        serial_mm_init(get_system_memory(), addr, 2,
                       qdev_get_gpio_in(DEVICE(&s->gic), uart_irqs[i]),
                       115200, serial_hd(i), DEVICE_NATIVE_ENDIAN);
    }

    /* I2C */
    sysbus_realize(SYS_BUS_DEVICE(&s->i2c0), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->i2c0), 0, s->memmap[AW_R40_DEV_TWI0]);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->i2c0), 0,
                       qdev_get_gpio_in(DEVICE(&s->gic), AW_R40_GIC_SPI_TWI0));

    /* DRAMC */
    sysbus_realize(SYS_BUS_DEVICE(&s->dramc), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->dramc), 0,
                    s->memmap[AW_R40_DEV_DRAMCOM]);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->dramc), 1,
                    s->memmap[AW_R40_DEV_DRAMCTL]);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->dramc), 2,
                    s->memmap[AW_R40_DEV_DRAMPHY]);

    /* nic support gmac and emac */
    for (int i = 0; i < ARRAY_SIZE(r40_nic_models) - 1; i++) {
        NICInfo *nic = &nd_table[i];

        if (!nic->used) {
            continue;
        }
        if (qemu_show_nic_models(nic->model, r40_nic_models)) {
            exit(0);
        }

        switch (qemu_find_nic_model(nic, r40_nic_models, r40_nic_models[0])) {
        case 0: /* gmac */
            qdev_set_nic_properties(DEVICE(&s->gmac), nic);
            break;
        case 1: /* emac */
            qdev_set_nic_properties(DEVICE(&s->emac), nic);
            break;
        default:
            exit(1);
            break;
        }
    }

    /* GMAC */
    object_property_set_link(OBJECT(&s->gmac), "dma-memory",
                                     OBJECT(get_system_memory()), &error_fatal);
    sysbus_realize(SYS_BUS_DEVICE(&s->gmac), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->gmac), 0, s->memmap[AW_R40_DEV_GMAC]);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->gmac), 0,
                       qdev_get_gpio_in(DEVICE(&s->gic), AW_R40_GIC_SPI_GMAC));

    /* EMAC */
    sysbus_realize(SYS_BUS_DEVICE(&s->emac), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->emac), 0, s->memmap[AW_R40_DEV_EMAC]);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->emac), 0,
                       qdev_get_gpio_in(DEVICE(&s->gic), AW_R40_GIC_SPI_EMAC));

    /* Unimplemented devices */
    for (i = 0; i < ARRAY_SIZE(r40_unimplemented); i++) {
        create_unimplemented_device(r40_unimplemented[i].device_name,
                                    r40_unimplemented[i].base,
                                    r40_unimplemented[i].size);
    }
}

static void allwinner_r40_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = allwinner_r40_realize;
    /* Reason: uses serial_hd() in realize function */
    dc->user_creatable = false;
}

static const TypeInfo allwinner_r40_type_info = {
    .name = TYPE_AW_R40,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(AwR40State),
    .instance_init = allwinner_r40_init,
    .class_init = allwinner_r40_class_init,
};

static void allwinner_r40_register_types(void)
{
    type_register_static(&allwinner_r40_type_info);
}

type_init(allwinner_r40_register_types)
