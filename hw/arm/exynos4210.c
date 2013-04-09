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

#include "hw/boards.h"
#include "sysemu/sysemu.h"
#include "hw/sysbus.h"
#include "hw/arm/arm.h"
#include "hw/loader.h"
#include "hw/arm/exynos4210.h"
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

/* PMU SFR base address */
#define EXYNOS4210_PMU_BASE_ADDR            0x10020000

/* Display controllers (FIMD) */
#define EXYNOS4210_FIMD0_BASE_ADDR          0x11C00000

/* EHCI */
#define EXYNOS4210_EHCI_BASE_ADDR           0x12580000

static uint8_t chipid_and_omr[] = { 0x11, 0x02, 0x21, 0x43,
                                    0x09, 0x00, 0x00, 0x00 };

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

Exynos4210State *exynos4210_init(MemoryRegion *system_mem,
        unsigned long ram_size)
{
    qemu_irq cpu_irq[EXYNOS4210_NCPUS];
    int i, n;
    Exynos4210State *s = g_new(Exynos4210State, 1);
    qemu_irq *irqp;
    qemu_irq gate_irq[EXYNOS4210_NCPUS][EXYNOS4210_IRQ_GATE_NINPUTS];
    unsigned long mem_size;
    DeviceState *dev;
    SysBusDevice *busdev;

    for (n = 0; n < EXYNOS4210_NCPUS; n++) {
        s->cpu[n] = cpu_arm_init("cortex-a9");
        if (!s->cpu[n]) {
            fprintf(stderr, "Unable to find CPU %d definition\n", n);
            exit(1);
        }

        /* Create PIC controller for each processor instance */
        irqp = arm_pic_init_cpu(s->cpu[n]);

        /*
         * Get GICs gpio_in cpu_irq to connect a combiner to them later.
         * Use only IRQ for a while.
         */
        cpu_irq[n] = irqp[ARM_PIC_CPU_IRQ];
    }

    /*** IRQs ***/

    s->irq_table = exynos4210_init_irq(&s->irqs);

    /* IRQ Gate */
    for (i = 0; i < EXYNOS4210_NCPUS; i++) {
        dev = qdev_create(NULL, "exynos4210.irq_gate");
        qdev_prop_set_uint32(dev, "n_in", EXYNOS4210_IRQ_GATE_NINPUTS);
        qdev_init_nofail(dev);
        /* Get IRQ Gate input in gate_irq */
        for (n = 0; n < EXYNOS4210_IRQ_GATE_NINPUTS; n++) {
            gate_irq[i][n] = qdev_get_gpio_in(dev, n);
        }
        busdev = SYS_BUS_DEVICE(dev);

        /* Connect IRQ Gate output to cpu_irq */
        sysbus_connect_irq(busdev, 0, cpu_irq[i]);
    }

    /* Private memory region and Internal GIC */
    dev = qdev_create(NULL, "a9mpcore_priv");
    qdev_prop_set_uint32(dev, "num-cpu", EXYNOS4210_NCPUS);
    qdev_init_nofail(dev);
    busdev = SYS_BUS_DEVICE(dev);
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
    dev = qdev_create(NULL, "exynos4210.gic");
    qdev_prop_set_uint32(dev, "num-cpu", EXYNOS4210_NCPUS);
    qdev_init_nofail(dev);
    busdev = SYS_BUS_DEVICE(dev);
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
    dev = qdev_create(NULL, "exynos4210.combiner");
    qdev_init_nofail(dev);
    busdev = SYS_BUS_DEVICE(dev);
    for (n = 0; n < EXYNOS4210_MAX_INT_COMBINER_OUT_IRQ; n++) {
        sysbus_connect_irq(busdev, n, s->irqs.int_gic_irq[n]);
    }
    exynos4210_combiner_get_gpioin(&s->irqs, dev, 0);
    sysbus_mmio_map(busdev, 0, EXYNOS4210_INT_COMBINER_BASE_ADDR);

    /* External Interrupt Combiner */
    dev = qdev_create(NULL, "exynos4210.combiner");
    qdev_prop_set_uint32(dev, "external", 1);
    qdev_init_nofail(dev);
    busdev = SYS_BUS_DEVICE(dev);
    for (n = 0; n < EXYNOS4210_MAX_INT_COMBINER_OUT_IRQ; n++) {
        sysbus_connect_irq(busdev, n, s->irqs.ext_gic_irq[n]);
    }
    exynos4210_combiner_get_gpioin(&s->irqs, dev, 1);
    sysbus_mmio_map(busdev, 0, EXYNOS4210_EXT_COMBINER_BASE_ADDR);

    /* Initialize board IRQs. */
    exynos4210_init_board_irqs(&s->irqs);

    /*** Memory ***/

    /* Chip-ID and OMR */
    memory_region_init_ram_ptr(&s->chipid_mem, "exynos4210.chipid",
            sizeof(chipid_and_omr), chipid_and_omr);
    memory_region_set_readonly(&s->chipid_mem, true);
    memory_region_add_subregion(system_mem, EXYNOS4210_CHIPID_ADDR,
                                &s->chipid_mem);

    /* Internal ROM */
    memory_region_init_ram(&s->irom_mem, "exynos4210.irom",
                           EXYNOS4210_IROM_SIZE);
    memory_region_set_readonly(&s->irom_mem, true);
    memory_region_add_subregion(system_mem, EXYNOS4210_IROM_BASE_ADDR,
                                &s->irom_mem);
    /* mirror of iROM */
    memory_region_init_alias(&s->irom_alias_mem, "exynos4210.irom_alias",
                             &s->irom_mem,
                             0,
                             EXYNOS4210_IROM_SIZE);
    memory_region_set_readonly(&s->irom_alias_mem, true);
    memory_region_add_subregion(system_mem, EXYNOS4210_IROM_MIRROR_BASE_ADDR,
                                &s->irom_alias_mem);

    /* Internal RAM */
    memory_region_init_ram(&s->iram_mem, "exynos4210.iram",
                           EXYNOS4210_IRAM_SIZE);
    vmstate_register_ram_global(&s->iram_mem);
    memory_region_add_subregion(system_mem, EXYNOS4210_IRAM_BASE_ADDR,
                                &s->iram_mem);

    /* DRAM */
    mem_size = ram_size;
    if (mem_size > EXYNOS4210_DRAM_MAX_SIZE) {
        memory_region_init_ram(&s->dram1_mem, "exynos4210.dram1",
                mem_size - EXYNOS4210_DRAM_MAX_SIZE);
        vmstate_register_ram_global(&s->dram1_mem);
        memory_region_add_subregion(system_mem, EXYNOS4210_DRAM1_BASE_ADDR,
                &s->dram1_mem);
        mem_size = EXYNOS4210_DRAM_MAX_SIZE;
    }
    memory_region_init_ram(&s->dram0_mem, "exynos4210.dram0", mem_size);
    vmstate_register_ram_global(&s->dram0_mem);
    memory_region_add_subregion(system_mem, EXYNOS4210_DRAM0_BASE_ADDR,
            &s->dram0_mem);

   /* PMU.
    * The only reason of existence at the moment is that secondary CPU boot
    * loader uses PMU INFORM5 register as a holding pen.
    */
    sysbus_create_simple("exynos4210.pmu", EXYNOS4210_PMU_BASE_ADDR, NULL);

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
    dev = qdev_create(NULL, "exynos4210.mct");
    qdev_init_nofail(dev);
    busdev = SYS_BUS_DEVICE(dev);
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

        dev = qdev_create(NULL, "exynos4210.i2c");
        qdev_init_nofail(dev);
        busdev = SYS_BUS_DEVICE(dev);
        sysbus_connect_irq(busdev, 0, i2c_irq);
        sysbus_mmio_map(busdev, 0, addr);
        s->i2c_if[n] = (i2c_bus *)qdev_get_child_bus(dev, "i2c");
    }


    /*** UARTs ***/
    exynos4210_uart_create(EXYNOS4210_UART0_BASE_ADDR,
                           EXYNOS4210_UART0_FIFO_SIZE, 0, NULL,
                  s->irq_table[exynos4210_get_irq(EXYNOS4210_UART_INT_GRP, 0)]);

    exynos4210_uart_create(EXYNOS4210_UART1_BASE_ADDR,
                           EXYNOS4210_UART1_FIFO_SIZE, 1, NULL,
                  s->irq_table[exynos4210_get_irq(EXYNOS4210_UART_INT_GRP, 1)]);

    exynos4210_uart_create(EXYNOS4210_UART2_BASE_ADDR,
                           EXYNOS4210_UART2_FIFO_SIZE, 2, NULL,
                  s->irq_table[exynos4210_get_irq(EXYNOS4210_UART_INT_GRP, 2)]);

    exynos4210_uart_create(EXYNOS4210_UART3_BASE_ADDR,
                           EXYNOS4210_UART3_FIFO_SIZE, 3, NULL,
                  s->irq_table[exynos4210_get_irq(EXYNOS4210_UART_INT_GRP, 3)]);

    /*** Display controller (FIMD) ***/
    sysbus_create_varargs("exynos4210.fimd", EXYNOS4210_FIMD0_BASE_ADDR,
            s->irq_table[exynos4210_get_irq(11, 0)],
            s->irq_table[exynos4210_get_irq(11, 1)],
            s->irq_table[exynos4210_get_irq(11, 2)],
            NULL);

    sysbus_create_simple(TYPE_EXYNOS4210_EHCI, EXYNOS4210_EHCI_BASE_ADDR,
            s->irq_table[exynos4210_get_irq(28, 3)]);

    return s;
}
