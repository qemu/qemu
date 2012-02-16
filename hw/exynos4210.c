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

#include "boards.h"
#include "sysemu.h"
#include "sysbus.h"
#include "arm-misc.h"
#include "exynos4210.h"

#define EXYNOS4210_CHIPID_ADDR         0x10000000

/* External GIC */
#define EXYNOS4210_EXT_GIC_CPU_BASE_ADDR    0x10480000
#define EXYNOS4210_EXT_GIC_DIST_BASE_ADDR   0x10490000

/* Combiner */
#define EXYNOS4210_EXT_COMBINER_BASE_ADDR   0x10440000
#define EXYNOS4210_INT_COMBINER_BASE_ADDR   0x10448000

static uint8_t chipid_and_omr[] = { 0x11, 0x02, 0x21, 0x43,
                                    0x09, 0x00, 0x00, 0x00 };

Exynos4210State *exynos4210_init(MemoryRegion *system_mem,
        unsigned long ram_size)
{
    qemu_irq cpu_irq[4];
    int n;
    Exynos4210State *s = g_new(Exynos4210State, 1);
    qemu_irq *irqp;
    qemu_irq gate_irq[EXYNOS4210_IRQ_GATE_NINPUTS];
    unsigned long mem_size;
    DeviceState *dev;
    SysBusDevice *busdev;

    for (n = 0; n < EXYNOS4210_NCPUS; n++) {
        s->env[n] = cpu_init("cortex-a9");
        if (!s->env[n]) {
            fprintf(stderr, "Unable to find CPU %d definition\n", n);
            exit(1);
        }
        /* Create PIC controller for each processor instance */
        irqp = arm_pic_init_cpu(s->env[n]);

        /*
         * Get GICs gpio_in cpu_irq to connect a combiner to them later.
         * Use only IRQ for a while.
         */
        cpu_irq[n] = irqp[ARM_PIC_CPU_IRQ];
    }

    /*** IRQs ***/

    s->irq_table = exynos4210_init_irq(&s->irqs);

    /* IRQ Gate */
    dev = qdev_create(NULL, "exynos4210.irq_gate");
    qdev_init_nofail(dev);
    /* Get IRQ Gate input in gate_irq */
    for (n = 0; n < EXYNOS4210_IRQ_GATE_NINPUTS; n++) {
        gate_irq[n] = qdev_get_gpio_in(dev, n);
    }
    busdev = sysbus_from_qdev(dev);
    /* Connect IRQ Gate output to cpu_irq */
    for (n = 0; n < EXYNOS4210_NCPUS; n++) {
        sysbus_connect_irq(busdev, n, cpu_irq[n]);
    }

    /* Private memory region and Internal GIC */
    dev = qdev_create(NULL, "a9mpcore_priv");
    qdev_prop_set_uint32(dev, "num-cpu", EXYNOS4210_NCPUS);
    qdev_init_nofail(dev);
    busdev = sysbus_from_qdev(dev);
    sysbus_mmio_map(busdev, 0, EXYNOS4210_SMP_PRIVATE_BASE_ADDR);
    for (n = 0; n < EXYNOS4210_NCPUS; n++) {
        sysbus_connect_irq(busdev, n, gate_irq[n * 2]);
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
    busdev = sysbus_from_qdev(dev);
    /* Map CPU interface */
    sysbus_mmio_map(busdev, 0, EXYNOS4210_EXT_GIC_CPU_BASE_ADDR);
    /* Map Distributer interface */
    sysbus_mmio_map(busdev, 1, EXYNOS4210_EXT_GIC_DIST_BASE_ADDR);
    for (n = 0; n < EXYNOS4210_NCPUS; n++) {
        sysbus_connect_irq(busdev, n, gate_irq[n * 2 + 1]);
    }
    for (n = 0; n < EXYNOS4210_EXT_GIC_NIRQ; n++) {
        s->irqs.ext_gic_irq[n] = qdev_get_gpio_in(dev, n);
    }

    /* Internal Interrupt Combiner */
    dev = qdev_create(NULL, "exynos4210.combiner");
    qdev_init_nofail(dev);
    busdev = sysbus_from_qdev(dev);
    for (n = 0; n < EXYNOS4210_MAX_INT_COMBINER_OUT_IRQ; n++) {
        sysbus_connect_irq(busdev, n, s->irqs.int_gic_irq[n]);
    }
    exynos4210_combiner_get_gpioin(&s->irqs, dev, 0);
    sysbus_mmio_map(busdev, 0, EXYNOS4210_INT_COMBINER_BASE_ADDR);

    /* External Interrupt Combiner */
    dev = qdev_create(NULL, "exynos4210.combiner");
    qdev_prop_set_uint32(dev, "external", 1);
    qdev_init_nofail(dev);
    busdev = sysbus_from_qdev(dev);
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
                             EXYNOS4210_IROM_BASE_ADDR,
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

    return s;
}
