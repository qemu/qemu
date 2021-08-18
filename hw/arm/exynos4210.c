/*
 *  Samsung exynos4210 SoC emulation
 *
 *  Copyright (c) 2011 Samsung Electronics Co., Ltd. All rights reserved.
 *    Maksim Kozlov <m.kozlov@samsung.com>
 *    Evgeny Voevodin <e.voevodin@samsung.com>
 *    Igor Mitsyanko  <i.mitsyanko@samsung.com>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "cpu.h"
#include "hw/cpu/a9mpcore.h"
#include "hw/irq.h"
#include "sysemu/blockdev.h"
#include "sysemu/sysemu.h"
#include "hw/sysbus.h"
#include "hw/arm/boot.h"
#include "hw/loader.h"
#include "hw/qdev-properties.h"
#include "hw/arm/exynos4210.h"
#include "hw/sd/sdhci.h"
#include "hw/usb/hcd-ehci.h"

#define EXYNOS4210_CHIPID_ADDR         0x10000000

/* PWM */
#define EXYNOS4210_PWM_BASE_ADDR       0x139D0000

/* RTC */
#define EXYNOS4210_RTC_BASE_ADDR       0x10070000

/* MCT */
#define EXYNOS4210_MCT_BASE_ADDR       0x10050000

/* I2C */
#define EXYNOS4210_I2C_SHIFT           0x00010000
#define EXYNOS4210_I2C_BASE_ADDR       0x13860000
/* Interrupt Group of External Interrupt Combiner for I2C */
#define EXYNOS4210_I2C_INTG            27
#define EXYNOS4210_HDMI_INTG           16

/* UART's definitions */
#define EXYNOS4210_UART0_BASE_ADDR     0x13800000
#define EXYNOS4210_UART1_BASE_ADDR     0x13810000
#define EXYNOS4210_UART2_BASE_ADDR     0x13820000
#define EXYNOS4210_UART3_BASE_ADDR     0x13830000
#define EXYNOS4210_UART0_FIFO_SIZE     256
#define EXYNOS4210_UART1_FIFO_SIZE     64
#define EXYNOS4210_UART2_FIFO_SIZE     16
#define EXYNOS4210_UART3_FIFO_SIZE     16
/* Interrupt Group of External Interrupt Combiner for UART */
#define EXYNOS4210_UART_INT_GRP        26

/* External GIC */
#define EXYNOS4210_EXT_GIC_CPU_BASE_ADDR    0x10480000
#define EXYNOS4210_EXT_GIC_DIST_BASE_ADDR   0x10490000

/* Combiner */
#define EXYNOS4210_EXT_COMBINER_BASE_ADDR   0x10440000
#define EXYNOS4210_INT_COMBINER_BASE_ADDR   0x10448000

/* SD/MMC host controllers */
#define EXYNOS4210_SDHCI_CAPABILITIES       0x05E80080
#define EXYNOS4210_SDHCI_BASE_ADDR          0x12510000
#define EXYNOS4210_SDHCI_ADDR(n)            (EXYNOS4210_SDHCI_BASE_ADDR + \
                                                0x00010000 * (n))
#define EXYNOS4210_SDHCI_NUMBER             4

/* PMU SFR base address */
#define EXYNOS4210_PMU_BASE_ADDR            0x10020000

/* Clock controller SFR base address */
#define EXYNOS4210_CLK_BASE_ADDR            0x10030000

/* PRNG/HASH SFR base address */
#define EXYNOS4210_RNG_BASE_ADDR            0x10830400

/* Display controllers (FIMD) */
#define EXYNOS4210_FIMD0_BASE_ADDR          0x11C00000

/* EHCI */
#define EXYNOS4210_EHCI_BASE_ADDR           0x12580000

/* DMA */
#define EXYNOS4210_PL330_BASE0_ADDR         0x12680000
#define EXYNOS4210_PL330_BASE1_ADDR         0x12690000
#define EXYNOS4210_PL330_BASE2_ADDR         0x12850000

static uint8_t chipid_and_omr[] = { 0x11, 0x02, 0x21, 0x43,
                                    0x09, 0x00, 0x00, 0x00 };

static uint64_t exynos4210_chipid_and_omr_read(void *opaque, hwaddr offset,
                                               unsigned size)
{
    assert(offset < sizeof(chipid_and_omr));
    return chipid_and_omr[offset];
}

static void exynos4210_chipid_and_omr_write(void *opaque, hwaddr offset,
                                            uint64_t value, unsigned size)
{
    return;
}

static const MemoryRegionOps exynos4210_chipid_and_omr_ops = {
    .read = exynos4210_chipid_and_omr_read,
    .write = exynos4210_chipid_and_omr_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl = {
        .max_access_size = 1,
    }
};

void exynos4210_write_secondary(ARMCPU *cpu,
        const struct arm_boot_info *info)
{
    int n;
    uint32_t smpboot[] = {
        0xe59f3034, /* ldr r3, External gic_cpu_if */
        0xe59f2034, /* ldr r2, Internal gic_cpu_if */
        0xe59f0034, /* ldr r0, startaddr */
        0xe3a01001, /* mov r1, #1 */
        0xe5821000, /* str r1, [r2] */
        0xe5831000, /* str r1, [r3] */
        0xe3a010ff, /* mov r1, #0xff */
        0xe5821004, /* str r1, [r2, #4] */
        0xe5831004, /* str r1, [r3, #4] */
        0xf57ff04f, /* dsb */
        0xe320f003, /* wfi */
        0xe5901000, /* ldr     r1, [r0] */
        0xe1110001, /* tst     r1, r1 */
        0x0afffffb, /* beq     <wfi> */
        0xe12fff11, /* bx      r1 */
        EXYNOS4210_EXT_GIC_CPU_BASE_ADDR,
        0,          /* gic_cpu_if: base address of Internal GIC CPU interface */
        0           /* bootreg: Boot register address is held here */
    };
    smpboot[ARRAY_SIZE(smpboot) - 1] = info->smp_bootreg_addr;
    smpboot[ARRAY_SIZE(smpboot) - 2] = info->gic_cpu_if_addr;
    for (n = 0; n < ARRAY_SIZE(smpboot); n++) {
        smpboot[n] = tswap32(smpboot[n]);
    }
    rom_add_blob_fixed("smpboot", smpboot, sizeof(smpboot),
                       info->smp_loader_start);
}

static uint64_t exynos4210_calc_affinity(int cpu)
{
    /* Exynos4210 has 0x9 as cluster ID */
    return (0x9 << ARM_AFF1_SHIFT) | cpu;
}

static DeviceState *pl330_create(uint32_t base, qemu_or_irq *orgate,
                                 qemu_irq irq, int nreq, int nevents, int width)
{
    SysBusDevice *busdev;
    DeviceState *dev;
    int i;

    dev = qdev_new("pl330");
    object_property_set_link(OBJECT(dev), "memory",
                             OBJECT(get_system_memory()),
                             &error_fatal);
    qdev_prop_set_uint8(dev, "num_events", nevents);
    qdev_prop_set_uint8(dev, "num_chnls",  8);
    qdev_prop_set_uint8(dev, "num_periph_req",  nreq);

    qdev_prop_set_uint8(dev, "wr_cap", 4);
    qdev_prop_set_uint8(dev, "wr_q_dep", 8);
    qdev_prop_set_uint8(dev, "rd_cap", 4);
    qdev_prop_set_uint8(dev, "rd_q_dep", 8);
    qdev_prop_set_uint8(dev, "data_width", width);
    qdev_prop_set_uint16(dev, "data_buffer_dep", width);
    busdev = SYS_BUS_DEVICE(dev);
    sysbus_realize_and_unref(busdev, &error_fatal);
    sysbus_mmio_map(busdev, 0, base);

    object_property_set_int(OBJECT(orgate), "num-lines", nevents + 1,
                            &error_abort);
    qdev_realize(DEVICE(orgate), NULL, &error_abort);

    for (i = 0; i < nevents + 1; i++) {
        sysbus_connect_irq(busdev, i, qdev_get_gpio_in(DEVICE(orgate), i));
    }
    qdev_connect_gpio_out(DEVICE(orgate), 0, irq);
    return dev;
}

static void exynos4210_realize(DeviceState *socdev, Error **errp)
{
    Exynos4210State *s = EXYNOS4210_SOC(socdev);
    MemoryRegion *system_mem = get_system_memory();
    qemu_irq gate_irq[EXYNOS4210_NCPUS][EXYNOS4210_IRQ_GATE_NINPUTS];
    SysBusDevice *busdev;
    DeviceState *dev, *uart[4], *pl330[3];
    int i, n;

    for (n = 0; n < EXYNOS4210_NCPUS; n++) {
        Object *cpuobj = object_new(ARM_CPU_TYPE_NAME("cortex-a9"));

        /* By default A9 CPUs have EL3 enabled.  This board does not currently
         * support EL3 so the CPU EL3 property is disabled before realization.
         */
        if (object_property_find(cpuobj, "has_el3")) {
            object_property_set_bool(cpuobj, "has_el3", false, &error_fatal);
        }

        s->cpu[n] = ARM_CPU(cpuobj);
        object_property_set_int(cpuobj, "mp-affinity",
                                exynos4210_calc_affinity(n), &error_abort);
        object_property_set_int(cpuobj, "reset-cbar",
                                EXYNOS4210_SMP_PRIVATE_BASE_ADDR,
                                &error_abort);
        qdev_realize(DEVICE(cpuobj), NULL, &error_fatal);
    }

    /*** IRQs ***/

    s->irq_table = exynos4210_init_irq(&s->irqs);

    /* IRQ Gate */
    for (i = 0; i < EXYNOS4210_NCPUS; i++) {
        dev = qdev_new("exynos4210.irq_gate");
        qdev_prop_set_uint32(dev, "n_in", EXYNOS4210_IRQ_GATE_NINPUTS);
        sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
        /* Get IRQ Gate input in gate_irq */
        for (n = 0; n < EXYNOS4210_IRQ_GATE_NINPUTS; n++) {
            gate_irq[i][n] = qdev_get_gpio_in(dev, n);
        }
        busdev = SYS_BUS_DEVICE(dev);

        /* Connect IRQ Gate output to CPU's IRQ line */
        sysbus_connect_irq(busdev, 0,
                           qdev_get_gpio_in(DEVICE(s->cpu[i]), ARM_CPU_IRQ));
    }

    /* Private memory region and Internal GIC */
    dev = qdev_new(TYPE_A9MPCORE_PRIV);
    qdev_prop_set_uint32(dev, "num-cpu", EXYNOS4210_NCPUS);
    busdev = SYS_BUS_DEVICE(dev);
    sysbus_realize_and_unref(busdev, &error_fatal);
    sysbus_mmio_map(busdev, 0, EXYNOS4210_SMP_PRIVATE_BASE_ADDR);
    for (n = 0; n < EXYNOS4210_NCPUS; n++) {
        sysbus_connect_irq(busdev, n, gate_irq[n][0]);
    }
    for (n = 0; n < EXYNOS4210_INT_GIC_NIRQ; n++) {
        s->irqs.int_gic_irq[n] = qdev_get_gpio_in(dev, n);
    }

    /* Cache controller */
    sysbus_create_simple("l2x0", EXYNOS4210_L2X0_BASE_ADDR, NULL);

    /* External GIC */
    dev = qdev_new("exynos4210.gic");
    qdev_prop_set_uint32(dev, "num-cpu", EXYNOS4210_NCPUS);
    busdev = SYS_BUS_DEVICE(dev);
    sysbus_realize_and_unref(busdev, &error_fatal);
    /* Map CPU interface */
    sysbus_mmio_map(busdev, 0, EXYNOS4210_EXT_GIC_CPU_BASE_ADDR);
    /* Map Distributer interface */
    sysbus_mmio_map(busdev, 1, EXYNOS4210_EXT_GIC_DIST_BASE_ADDR);
    for (n = 0; n < EXYNOS4210_NCPUS; n++) {
        sysbus_connect_irq(busdev, n, gate_irq[n][1]);
    }
    for (n = 0; n < EXYNOS4210_EXT_GIC_NIRQ; n++) {
        s->irqs.ext_gic_irq[n] = qdev_get_gpio_in(dev, n);
    }

    /* Internal Interrupt Combiner */
    dev = qdev_new("exynos4210.combiner");
    busdev = SYS_BUS_DEVICE(dev);
    sysbus_realize_and_unref(busdev, &error_fatal);
    for (n = 0; n < EXYNOS4210_MAX_INT_COMBINER_OUT_IRQ; n++) {
        sysbus_connect_irq(busdev, n, s->irqs.int_gic_irq[n]);
    }
    exynos4210_combiner_get_gpioin(&s->irqs, dev, 0);
    sysbus_mmio_map(busdev, 0, EXYNOS4210_INT_COMBINER_BASE_ADDR);

    /* External Interrupt Combiner */
    dev = qdev_new("exynos4210.combiner");
    qdev_prop_set_uint32(dev, "external", 1);
    busdev = SYS_BUS_DEVICE(dev);
    sysbus_realize_and_unref(busdev, &error_fatal);
    for (n = 0; n < EXYNOS4210_MAX_INT_COMBINER_OUT_IRQ; n++) {
        sysbus_connect_irq(busdev, n, s->irqs.ext_gic_irq[n]);
    }
    exynos4210_combiner_get_gpioin(&s->irqs, dev, 1);
    sysbus_mmio_map(busdev, 0, EXYNOS4210_EXT_COMBINER_BASE_ADDR);

    /* Initialize board IRQs. */
    exynos4210_init_board_irqs(&s->irqs);

    /*** Memory ***/

    /* Chip-ID and OMR */
    memory_region_init_io(&s->chipid_mem, OBJECT(socdev),
                          &exynos4210_chipid_and_omr_ops, NULL,
                          "exynos4210.chipid", sizeof(chipid_and_omr));
    memory_region_add_subregion(system_mem, EXYNOS4210_CHIPID_ADDR,
                                &s->chipid_mem);

    /* Internal ROM */
    memory_region_init_rom(&s->irom_mem, OBJECT(socdev), "exynos4210.irom",
                           EXYNOS4210_IROM_SIZE, &error_fatal);
    memory_region_add_subregion(system_mem, EXYNOS4210_IROM_BASE_ADDR,
                                &s->irom_mem);
    /* mirror of iROM */
    memory_region_init_alias(&s->irom_alias_mem, OBJECT(socdev),
                             "exynos4210.irom_alias", &s->irom_mem, 0,
                             EXYNOS4210_IROM_SIZE);
    memory_region_add_subregion(system_mem, EXYNOS4210_IROM_MIRROR_BASE_ADDR,
                                &s->irom_alias_mem);

    /* Internal RAM */
    memory_region_init_ram(&s->iram_mem, NULL, "exynos4210.iram",
                           EXYNOS4210_IRAM_SIZE, &error_fatal);
    memory_region_add_subregion(system_mem, EXYNOS4210_IRAM_BASE_ADDR,
                                &s->iram_mem);

   /* PMU.
    * The only reason of existence at the moment is that secondary CPU boot
    * loader uses PMU INFORM5 register as a holding pen.
    */
    sysbus_create_simple("exynos4210.pmu", EXYNOS4210_PMU_BASE_ADDR, NULL);

    sysbus_create_simple("exynos4210.clk", EXYNOS4210_CLK_BASE_ADDR, NULL);
    sysbus_create_simple("exynos4210.rng", EXYNOS4210_RNG_BASE_ADDR, NULL);

    /* PWM */
    sysbus_create_varargs("exynos4210.pwm", EXYNOS4210_PWM_BASE_ADDR,
                          s->irq_table[exynos4210_get_irq(22, 0)],
                          s->irq_table[exynos4210_get_irq(22, 1)],
                          s->irq_table[exynos4210_get_irq(22, 2)],
                          s->irq_table[exynos4210_get_irq(22, 3)],
                          s->irq_table[exynos4210_get_irq(22, 4)],
                          NULL);
    /* RTC */
    sysbus_create_varargs("exynos4210.rtc", EXYNOS4210_RTC_BASE_ADDR,
                          s->irq_table[exynos4210_get_irq(23, 0)],
                          s->irq_table[exynos4210_get_irq(23, 1)],
                          NULL);

    /* Multi Core Timer */
    dev = qdev_new("exynos4210.mct");
    busdev = SYS_BUS_DEVICE(dev);
    sysbus_realize_and_unref(busdev, &error_fatal);
    for (n = 0; n < 4; n++) {
        /* Connect global timer interrupts to Combiner gpio_in */
        sysbus_connect_irq(busdev, n,
                s->irq_table[exynos4210_get_irq(1, 4 + n)]);
    }
    /* Connect local timer interrupts to Combiner gpio_in */
    sysbus_connect_irq(busdev, 4,
            s->irq_table[exynos4210_get_irq(51, 0)]);
    sysbus_connect_irq(busdev, 5,
            s->irq_table[exynos4210_get_irq(35, 3)]);
    sysbus_mmio_map(busdev, 0, EXYNOS4210_MCT_BASE_ADDR);

    /*** I2C ***/
    for (n = 0; n < EXYNOS4210_I2C_NUMBER; n++) {
        uint32_t addr = EXYNOS4210_I2C_BASE_ADDR + EXYNOS4210_I2C_SHIFT * n;
        qemu_irq i2c_irq;

        if (n < 8) {
            i2c_irq = s->irq_table[exynos4210_get_irq(EXYNOS4210_I2C_INTG, n)];
        } else {
            i2c_irq = s->irq_table[exynos4210_get_irq(EXYNOS4210_HDMI_INTG, 1)];
        }

        dev = qdev_new("exynos4210.i2c");
        busdev = SYS_BUS_DEVICE(dev);
        sysbus_realize_and_unref(busdev, &error_fatal);
        sysbus_connect_irq(busdev, 0, i2c_irq);
        sysbus_mmio_map(busdev, 0, addr);
        s->i2c_if[n] = (I2CBus *)qdev_get_child_bus(dev, "i2c");
    }


    /*** UARTs ***/
    uart[0] = exynos4210_uart_create(EXYNOS4210_UART0_BASE_ADDR,
                           EXYNOS4210_UART0_FIFO_SIZE, 0, serial_hd(0),
                  s->irq_table[exynos4210_get_irq(EXYNOS4210_UART_INT_GRP, 0)]);

    uart[1] = exynos4210_uart_create(EXYNOS4210_UART1_BASE_ADDR,
                           EXYNOS4210_UART1_FIFO_SIZE, 1, serial_hd(1),
                  s->irq_table[exynos4210_get_irq(EXYNOS4210_UART_INT_GRP, 1)]);

    uart[2] = exynos4210_uart_create(EXYNOS4210_UART2_BASE_ADDR,
                           EXYNOS4210_UART2_FIFO_SIZE, 2, serial_hd(2),
                  s->irq_table[exynos4210_get_irq(EXYNOS4210_UART_INT_GRP, 2)]);

    uart[3] = exynos4210_uart_create(EXYNOS4210_UART3_BASE_ADDR,
                           EXYNOS4210_UART3_FIFO_SIZE, 3, serial_hd(3),
                  s->irq_table[exynos4210_get_irq(EXYNOS4210_UART_INT_GRP, 3)]);

    /*** SD/MMC host controllers ***/
    for (n = 0; n < EXYNOS4210_SDHCI_NUMBER; n++) {
        DeviceState *carddev;
        BlockBackend *blk;
        DriveInfo *di;

        /* Compatible with:
         * - SD Host Controller Specification Version 2.0
         * - SDIO Specification Version 2.0
         * - MMC Specification Version 4.3
         * - SDMA
         * - ADMA2
         *
         * As this part of the Exynos4210 is not publically available,
         * we used the "HS-MMC Controller S3C2416X RISC Microprocessor"
         * public datasheet which is very similar (implementing
         * MMC Specification Version 4.0 being the only difference noted)
         */
        dev = qdev_new(TYPE_S3C_SDHCI);
        qdev_prop_set_uint64(dev, "capareg", EXYNOS4210_SDHCI_CAPABILITIES);

        busdev = SYS_BUS_DEVICE(dev);
        sysbus_realize_and_unref(busdev, &error_fatal);
        sysbus_mmio_map(busdev, 0, EXYNOS4210_SDHCI_ADDR(n));
        sysbus_connect_irq(busdev, 0, s->irq_table[exynos4210_get_irq(29, n)]);

        di = drive_get(IF_SD, 0, n);
        blk = di ? blk_by_legacy_dinfo(di) : NULL;
        carddev = qdev_new(TYPE_SD_CARD);
        qdev_prop_set_drive(carddev, "drive", blk);
        qdev_realize_and_unref(carddev, qdev_get_child_bus(dev, "sd-bus"),
                               &error_fatal);
    }

    /*** Display controller (FIMD) ***/
    sysbus_create_varargs("exynos4210.fimd", EXYNOS4210_FIMD0_BASE_ADDR,
            s->irq_table[exynos4210_get_irq(11, 0)],
            s->irq_table[exynos4210_get_irq(11, 1)],
            s->irq_table[exynos4210_get_irq(11, 2)],
            NULL);

    sysbus_create_simple(TYPE_EXYNOS4210_EHCI, EXYNOS4210_EHCI_BASE_ADDR,
            s->irq_table[exynos4210_get_irq(28, 3)]);

    /*** DMA controllers ***/
    pl330[0] = pl330_create(EXYNOS4210_PL330_BASE0_ADDR,
                            &s->pl330_irq_orgate[0],
                            s->irq_table[exynos4210_get_irq(21, 0)],
                            32, 32, 32);
    pl330[1] = pl330_create(EXYNOS4210_PL330_BASE1_ADDR,
                            &s->pl330_irq_orgate[1],
                            s->irq_table[exynos4210_get_irq(21, 1)],
                            32, 32, 32);
    pl330[2] = pl330_create(EXYNOS4210_PL330_BASE2_ADDR,
                            &s->pl330_irq_orgate[2],
                            s->irq_table[exynos4210_get_irq(20, 1)],
                            1, 31, 64);

    sysbus_connect_irq(SYS_BUS_DEVICE(uart[0]), 1,
                       qdev_get_gpio_in(pl330[0], 15));
    sysbus_connect_irq(SYS_BUS_DEVICE(uart[1]), 1,
                       qdev_get_gpio_in(pl330[1], 15));
    sysbus_connect_irq(SYS_BUS_DEVICE(uart[2]), 1,
                       qdev_get_gpio_in(pl330[0], 17));
    sysbus_connect_irq(SYS_BUS_DEVICE(uart[3]), 1,
                       qdev_get_gpio_in(pl330[1], 17));
}

static void exynos4210_init(Object *obj)
{
    Exynos4210State *s = EXYNOS4210_SOC(obj);
    int i;

    for (i = 0; i < ARRAY_SIZE(s->pl330_irq_orgate); i++) {
        char *name = g_strdup_printf("pl330-irq-orgate%d", i);
        qemu_or_irq *orgate = &s->pl330_irq_orgate[i];

        object_initialize_child(obj, name, orgate, TYPE_OR_IRQ);
        g_free(name);
    }
}

static void exynos4210_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = exynos4210_realize;
}

static const TypeInfo exynos4210_info = {
    .name = TYPE_EXYNOS4210_SOC,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Exynos4210State),
    .instance_init = exynos4210_init,
    .class_init = exynos4210_class_init,
};

static void exynos4210_register_types(void)
{
    type_register_static(&exynos4210_info);
}

type_init(exynos4210_register_types)
