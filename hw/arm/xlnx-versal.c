/*
 * Xilinx Versal SoC model.
 *
 * Copyright (c) 2018 Xilinx Inc.
 * Written by Edgar E. Iglesias
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "qobject/qlist.h"
#include "qemu/module.h"
#include "hw/sysbus.h"
#include "net/net.h"
#include "system/system.h"
#include "hw/arm/boot.h"
#include "hw/misc/unimp.h"
#include "hw/arm/xlnx-versal.h"
#include "qemu/log.h"
#include "target/arm/cpu-qom.h"
#include "target/arm/gtimer.h"

#define XLNX_VERSAL_ACPU_TYPE ARM_CPU_TYPE_NAME("cortex-a72")
#define XLNX_VERSAL_RCPU_TYPE ARM_CPU_TYPE_NAME("cortex-r5f")
#define GEM_REVISION        0x40070106

#define VERSAL_NUM_PMC_APB_IRQS 18
#define NUM_OSPI_IRQ_LINES 3

static void versal_create_apu_cpus(Versal *s)
{
    int i;

    object_initialize_child(OBJECT(s), "apu-cluster", &s->fpd.apu.cluster,
                            TYPE_CPU_CLUSTER);
    qdev_prop_set_uint32(DEVICE(&s->fpd.apu.cluster), "cluster-id", 0);

    for (i = 0; i < ARRAY_SIZE(s->fpd.apu.cpu); i++) {
        Object *obj;

        object_initialize_child(OBJECT(&s->fpd.apu.cluster),
                                "apu-cpu[*]", &s->fpd.apu.cpu[i],
                                XLNX_VERSAL_ACPU_TYPE);
        obj = OBJECT(&s->fpd.apu.cpu[i]);
        if (i) {
            /* Secondary CPUs start in powered-down state */
            object_property_set_bool(obj, "start-powered-off", true,
                                     &error_abort);
        }

        object_property_set_int(obj, "core-count", ARRAY_SIZE(s->fpd.apu.cpu),
                                &error_abort);
        object_property_set_link(obj, "memory", OBJECT(&s->fpd.apu.mr),
                                 &error_abort);
        qdev_realize(DEVICE(obj), NULL, &error_fatal);
    }

    qdev_realize(DEVICE(&s->fpd.apu.cluster), NULL, &error_fatal);
}

static void versal_create_apu_gic(Versal *s, qemu_irq *pic)
{
    static const uint64_t addrs[] = {
        MM_GIC_APU_DIST_MAIN,
        MM_GIC_APU_REDIST_0
    };
    SysBusDevice *gicbusdev;
    DeviceState *gicdev;
    QList *redist_region_count;
    int nr_apu_cpus = ARRAY_SIZE(s->fpd.apu.cpu);
    int i;

    object_initialize_child(OBJECT(s), "apu-gic", &s->fpd.apu.gic,
                            gicv3_class_name());
    gicbusdev = SYS_BUS_DEVICE(&s->fpd.apu.gic);
    gicdev = DEVICE(&s->fpd.apu.gic);
    qdev_prop_set_uint32(gicdev, "revision", 3);
    qdev_prop_set_uint32(gicdev, "num-cpu", nr_apu_cpus);
    qdev_prop_set_uint32(gicdev, "num-irq", XLNX_VERSAL_NR_IRQS + 32);

    redist_region_count = qlist_new();
    qlist_append_int(redist_region_count, nr_apu_cpus);
    qdev_prop_set_array(gicdev, "redist-region-count", redist_region_count);

    qdev_prop_set_bit(gicdev, "has-security-extensions", true);

    sysbus_realize(SYS_BUS_DEVICE(&s->fpd.apu.gic), &error_fatal);

    for (i = 0; i < ARRAY_SIZE(addrs); i++) {
        MemoryRegion *mr;

        mr = sysbus_mmio_get_region(gicbusdev, i);
        memory_region_add_subregion(&s->fpd.apu.mr, addrs[i], mr);
    }

    for (i = 0; i < nr_apu_cpus; i++) {
        DeviceState *cpudev = DEVICE(&s->fpd.apu.cpu[i]);
        int ppibase = XLNX_VERSAL_NR_IRQS + i * GIC_INTERNAL + GIC_NR_SGIS;
        qemu_irq maint_irq;
        int ti;
        /* Mapping from the output timer irq lines from the CPU to the
         * GIC PPI inputs.
         */
        const int timer_irq[] = {
            [GTIMER_PHYS] = VERSAL_TIMER_NS_EL1_IRQ,
            [GTIMER_VIRT] = VERSAL_TIMER_VIRT_IRQ,
            [GTIMER_HYP]  = VERSAL_TIMER_NS_EL2_IRQ,
            [GTIMER_SEC]  = VERSAL_TIMER_S_EL1_IRQ,
        };

        for (ti = 0; ti < ARRAY_SIZE(timer_irq); ti++) {
            qdev_connect_gpio_out(cpudev, ti,
                                  qdev_get_gpio_in(gicdev,
                                                   ppibase + timer_irq[ti]));
        }
        maint_irq = qdev_get_gpio_in(gicdev,
                                        ppibase + VERSAL_GIC_MAINT_IRQ);
        qdev_connect_gpio_out_named(cpudev, "gicv3-maintenance-interrupt",
                                    0, maint_irq);
        sysbus_connect_irq(gicbusdev, i, qdev_get_gpio_in(cpudev, ARM_CPU_IRQ));
        sysbus_connect_irq(gicbusdev, i + nr_apu_cpus,
                           qdev_get_gpio_in(cpudev, ARM_CPU_FIQ));
        sysbus_connect_irq(gicbusdev, i + 2 * nr_apu_cpus,
                           qdev_get_gpio_in(cpudev, ARM_CPU_VIRQ));
        sysbus_connect_irq(gicbusdev, i + 3 * nr_apu_cpus,
                           qdev_get_gpio_in(cpudev, ARM_CPU_VFIQ));
    }

    for (i = 0; i < XLNX_VERSAL_NR_IRQS; i++) {
        pic[i] = qdev_get_gpio_in(gicdev, i);
    }
}

static void versal_create_rpu_cpus(Versal *s)
{
    int i;

    object_initialize_child(OBJECT(s), "rpu-cluster", &s->lpd.rpu.cluster,
                            TYPE_CPU_CLUSTER);
    qdev_prop_set_uint32(DEVICE(&s->lpd.rpu.cluster), "cluster-id", 1);

    for (i = 0; i < ARRAY_SIZE(s->lpd.rpu.cpu); i++) {
        Object *obj;

        object_initialize_child(OBJECT(&s->lpd.rpu.cluster),
                                "rpu-cpu[*]", &s->lpd.rpu.cpu[i],
                                XLNX_VERSAL_RCPU_TYPE);
        obj = OBJECT(&s->lpd.rpu.cpu[i]);
        object_property_set_bool(obj, "start-powered-off", true,
                                 &error_abort);

        object_property_set_int(obj, "mp-affinity", 0x100 | i, &error_abort);
        object_property_set_int(obj, "core-count", ARRAY_SIZE(s->lpd.rpu.cpu),
                                &error_abort);
        object_property_set_link(obj, "memory", OBJECT(&s->lpd.rpu.mr),
                                 &error_abort);
        qdev_realize(DEVICE(obj), NULL, &error_fatal);
    }

    qdev_realize(DEVICE(&s->lpd.rpu.cluster), NULL, &error_fatal);
}

static void versal_create_uarts(Versal *s, qemu_irq *pic)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(s->lpd.iou.uart); i++) {
        static const int irqs[] = { VERSAL_UART0_IRQ_0, VERSAL_UART1_IRQ_0};
        static const uint64_t addrs[] = { MM_UART0, MM_UART1 };
        char *name = g_strdup_printf("uart%d", i);
        DeviceState *dev;
        MemoryRegion *mr;

        object_initialize_child(OBJECT(s), name, &s->lpd.iou.uart[i],
                                TYPE_PL011);
        dev = DEVICE(&s->lpd.iou.uart[i]);
        qdev_prop_set_chr(dev, "chardev", serial_hd(i));
        sysbus_realize(SYS_BUS_DEVICE(dev), &error_fatal);

        mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 0);
        memory_region_add_subregion(&s->mr_ps, addrs[i], mr);

        sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, pic[irqs[i]]);
        g_free(name);
    }
}

static void versal_create_canfds(Versal *s, qemu_irq *pic)
{
    int i;
    uint32_t irqs[] = { VERSAL_CANFD0_IRQ_0, VERSAL_CANFD1_IRQ_0};
    uint64_t addrs[] = { MM_CANFD0, MM_CANFD1 };

    for (i = 0; i < ARRAY_SIZE(s->lpd.iou.canfd); i++) {
        char *name = g_strdup_printf("canfd%d", i);
        SysBusDevice *sbd;
        MemoryRegion *mr;

        object_initialize_child(OBJECT(s), name, &s->lpd.iou.canfd[i],
                                TYPE_XILINX_CANFD);
        sbd = SYS_BUS_DEVICE(&s->lpd.iou.canfd[i]);

        object_property_set_int(OBJECT(&s->lpd.iou.canfd[i]), "ext_clk_freq",
                                XLNX_VERSAL_CANFD_REF_CLK , &error_abort);

        object_property_set_link(OBJECT(&s->lpd.iou.canfd[i]), "canfdbus",
                                 OBJECT(s->lpd.iou.canbus[i]),
                                 &error_abort);

        sysbus_realize(sbd, &error_fatal);

        mr = sysbus_mmio_get_region(sbd, 0);
        memory_region_add_subregion(&s->mr_ps, addrs[i], mr);

        sysbus_connect_irq(sbd, 0, pic[irqs[i]]);
        g_free(name);
    }
}

static void versal_create_usbs(Versal *s, qemu_irq *pic)
{
    DeviceState *dev;
    MemoryRegion *mr;

    object_initialize_child(OBJECT(s), "usb2", &s->lpd.iou.usb,
                            TYPE_XILINX_VERSAL_USB2);
    dev = DEVICE(&s->lpd.iou.usb);

    object_property_set_link(OBJECT(dev), "dma", OBJECT(&s->mr_ps),
                             &error_abort);
    qdev_prop_set_uint32(dev, "intrs", 1);
    qdev_prop_set_uint32(dev, "slots", 2);

    sysbus_realize(SYS_BUS_DEVICE(dev), &error_fatal);

    mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 0);
    memory_region_add_subregion(&s->mr_ps, MM_USB_0, mr);

    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, pic[VERSAL_USB0_IRQ_0]);

    mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 1);
    memory_region_add_subregion(&s->mr_ps, MM_USB2_CTRL_REGS, mr);
}

static void versal_create_gems(Versal *s, qemu_irq *pic)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(s->lpd.iou.gem); i++) {
        static const int irqs[] = { VERSAL_GEM0_IRQ_0, VERSAL_GEM1_IRQ_0};
        static const uint64_t addrs[] = { MM_GEM0, MM_GEM1 };
        char *name = g_strdup_printf("gem%d", i);
        DeviceState *dev;
        MemoryRegion *mr;
        OrIRQState *or_irq;

        object_initialize_child(OBJECT(s), name, &s->lpd.iou.gem[i],
                                TYPE_CADENCE_GEM);
        or_irq = &s->lpd.iou.gem_irq_orgate[i];
        object_initialize_child(OBJECT(s), "gem-irq-orgate[*]",
                                or_irq, TYPE_OR_IRQ);
        dev = DEVICE(&s->lpd.iou.gem[i]);
        qemu_configure_nic_device(dev, true, NULL);
        object_property_set_int(OBJECT(dev), "phy-addr", 23, &error_abort);
        object_property_set_int(OBJECT(dev), "num-priority-queues", 2,
                                &error_abort);
        object_property_set_int(OBJECT(or_irq),
                                "num-lines", 2, &error_fatal);
        qdev_realize(DEVICE(or_irq), NULL, &error_fatal);
        qdev_connect_gpio_out(DEVICE(or_irq), 0, pic[irqs[i]]);

        object_property_set_link(OBJECT(dev), "dma", OBJECT(&s->mr_ps),
                                 &error_abort);
        sysbus_realize(SYS_BUS_DEVICE(dev), &error_fatal);

        mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 0);
        memory_region_add_subregion(&s->mr_ps, addrs[i], mr);

        sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, qdev_get_gpio_in(DEVICE(or_irq), 0));
        sysbus_connect_irq(SYS_BUS_DEVICE(dev), 1, qdev_get_gpio_in(DEVICE(or_irq), 1));
        g_free(name);
    }
}

static void versal_create_admas(Versal *s, qemu_irq *pic)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(s->lpd.iou.adma); i++) {
        char *name = g_strdup_printf("adma%d", i);
        DeviceState *dev;
        MemoryRegion *mr;

        object_initialize_child(OBJECT(s), name, &s->lpd.iou.adma[i],
                                TYPE_XLNX_ZDMA);
        dev = DEVICE(&s->lpd.iou.adma[i]);
        object_property_set_int(OBJECT(dev), "bus-width", 128, &error_abort);
        object_property_set_link(OBJECT(dev), "dma",
                                 OBJECT(get_system_memory()), &error_fatal);
        sysbus_realize(SYS_BUS_DEVICE(dev), &error_fatal);

        mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 0);
        memory_region_add_subregion(&s->mr_ps,
                                    MM_ADMA_CH0 + i * MM_ADMA_CH0_SIZE, mr);

        sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, pic[VERSAL_ADMA_IRQ_0 + i]);
        g_free(name);
    }
}

#define SDHCI_CAPABILITIES  0x280737ec6481 /* Same as on ZynqMP.  */
static void versal_create_sds(Versal *s, qemu_irq *pic)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(s->pmc.iou.sd); i++) {
        DeviceState *dev;
        MemoryRegion *mr;

        object_initialize_child(OBJECT(s), "sd[*]", &s->pmc.iou.sd[i],
                                TYPE_SYSBUS_SDHCI);
        dev = DEVICE(&s->pmc.iou.sd[i]);

        object_property_set_uint(OBJECT(dev), "sd-spec-version", 3,
                                 &error_fatal);
        object_property_set_uint(OBJECT(dev), "capareg", SDHCI_CAPABILITIES,
                                 &error_fatal);
        object_property_set_uint(OBJECT(dev), "uhs", UHS_I, &error_fatal);
        sysbus_realize(SYS_BUS_DEVICE(dev), &error_fatal);

        mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 0);
        memory_region_add_subregion(&s->mr_ps,
                                    MM_PMC_SD0 + i * MM_PMC_SD0_SIZE, mr);

        sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0,
                           pic[VERSAL_SD0_IRQ_0 + i * 2]);
    }
}

static void versal_create_pmc_apb_irq_orgate(Versal *s, qemu_irq *pic)
{
    DeviceState *orgate;

    /*
     * The VERSAL_PMC_APB_IRQ is an 'or' of the interrupts from the following
     * models:
     *  - RTC
     *  - BBRAM
     *  - PMC SLCR
     *  - CFRAME regs (input 3 - 17 to the orgate)
     */
    object_initialize_child(OBJECT(s), "pmc-apb-irq-orgate",
                            &s->pmc.apb_irq_orgate, TYPE_OR_IRQ);
    orgate = DEVICE(&s->pmc.apb_irq_orgate);
    object_property_set_int(OBJECT(orgate),
                            "num-lines", VERSAL_NUM_PMC_APB_IRQS, &error_fatal);
    qdev_realize(orgate, NULL, &error_fatal);
    qdev_connect_gpio_out(orgate, 0, pic[VERSAL_PMC_APB_IRQ]);
}

static void versal_create_rtc(Versal *s, qemu_irq *pic)
{
    SysBusDevice *sbd;
    MemoryRegion *mr;

    object_initialize_child(OBJECT(s), "rtc", &s->pmc.rtc,
                            TYPE_XLNX_ZYNQMP_RTC);
    sbd = SYS_BUS_DEVICE(&s->pmc.rtc);
    sysbus_realize(sbd, &error_fatal);

    mr = sysbus_mmio_get_region(sbd, 0);
    memory_region_add_subregion(&s->mr_ps, MM_PMC_RTC, mr);

    /*
     * TODO: Connect the ALARM and SECONDS interrupts once our RTC model
     * supports them.
     */
    sysbus_connect_irq(sbd, 1,
                       qdev_get_gpio_in(DEVICE(&s->pmc.apb_irq_orgate), 0));
}

static void versal_create_trng(Versal *s, qemu_irq *pic)
{
    SysBusDevice *sbd;
    MemoryRegion *mr;

    object_initialize_child(OBJECT(s), "trng", &s->pmc.trng,
                            TYPE_XLNX_VERSAL_TRNG);
    sbd = SYS_BUS_DEVICE(&s->pmc.trng);
    sysbus_realize(sbd, &error_fatal);

    mr = sysbus_mmio_get_region(sbd, 0);
    memory_region_add_subregion(&s->mr_ps, MM_PMC_TRNG, mr);
    sysbus_connect_irq(sbd, 0, pic[VERSAL_TRNG_IRQ]);
}

static void versal_create_xrams(Versal *s, qemu_irq *pic)
{
    int nr_xrams = ARRAY_SIZE(s->lpd.xram.ctrl);
    DeviceState *orgate;
    int i;

    /* XRAM IRQs get ORed into a single line.  */
    object_initialize_child(OBJECT(s), "xram-irq-orgate",
                            &s->lpd.xram.irq_orgate, TYPE_OR_IRQ);
    orgate = DEVICE(&s->lpd.xram.irq_orgate);
    object_property_set_int(OBJECT(orgate),
                            "num-lines", nr_xrams, &error_fatal);
    qdev_realize(orgate, NULL, &error_fatal);
    qdev_connect_gpio_out(orgate, 0, pic[VERSAL_XRAM_IRQ_0]);

    for (i = 0; i < ARRAY_SIZE(s->lpd.xram.ctrl); i++) {
        SysBusDevice *sbd;
        MemoryRegion *mr;

        object_initialize_child(OBJECT(s), "xram[*]", &s->lpd.xram.ctrl[i],
                                TYPE_XLNX_XRAM_CTRL);
        sbd = SYS_BUS_DEVICE(&s->lpd.xram.ctrl[i]);
        sysbus_realize(sbd, &error_fatal);

        mr = sysbus_mmio_get_region(sbd, 0);
        memory_region_add_subregion(&s->mr_ps,
                                    MM_XRAMC + i * MM_XRAMC_SIZE, mr);
        mr = sysbus_mmio_get_region(sbd, 1);
        memory_region_add_subregion(&s->mr_ps, MM_XRAM + i * MiB, mr);

        sysbus_connect_irq(sbd, 0, qdev_get_gpio_in(orgate, i));
    }
}

static void versal_create_bbram(Versal *s, qemu_irq *pic)
{
    SysBusDevice *sbd;

    object_initialize_child_with_props(OBJECT(s), "bbram", &s->pmc.bbram,
                                       sizeof(s->pmc.bbram), TYPE_XLNX_BBRAM,
                                       &error_fatal,
                                       "crc-zpads", "0",
                                       NULL);
    sbd = SYS_BUS_DEVICE(&s->pmc.bbram);

    sysbus_realize(sbd, &error_fatal);
    memory_region_add_subregion(&s->mr_ps, MM_PMC_BBRAM_CTRL,
                                sysbus_mmio_get_region(sbd, 0));
    sysbus_connect_irq(sbd, 0,
                       qdev_get_gpio_in(DEVICE(&s->pmc.apb_irq_orgate), 1));
}

static void versal_realize_efuse_part(Versal *s, Object *dev, hwaddr base)
{
    SysBusDevice *part = SYS_BUS_DEVICE(dev);

    object_property_set_link(OBJECT(part), "efuse",
                             OBJECT(&s->pmc.efuse), &error_abort);

    sysbus_realize(part, &error_abort);
    memory_region_add_subregion(&s->mr_ps, base,
                                sysbus_mmio_get_region(part, 0));
}

static void versal_create_efuse(Versal *s, qemu_irq *pic)
{
    Object *bits = OBJECT(&s->pmc.efuse);
    Object *ctrl = OBJECT(&s->pmc.efuse_ctrl);
    Object *cache = OBJECT(&s->pmc.efuse_cache);

    object_initialize_child(OBJECT(s), "efuse-ctrl", &s->pmc.efuse_ctrl,
                            TYPE_XLNX_VERSAL_EFUSE_CTRL);

    object_initialize_child(OBJECT(s), "efuse-cache", &s->pmc.efuse_cache,
                            TYPE_XLNX_VERSAL_EFUSE_CACHE);

    object_initialize_child_with_props(ctrl, "xlnx-efuse@0", bits,
                                       sizeof(s->pmc.efuse),
                                       TYPE_XLNX_EFUSE, &error_abort,
                                       "efuse-nr", "3",
                                       "efuse-size", "8192",
                                       NULL);

    qdev_realize(DEVICE(bits), NULL, &error_abort);
    versal_realize_efuse_part(s, ctrl, MM_PMC_EFUSE_CTRL);
    versal_realize_efuse_part(s, cache, MM_PMC_EFUSE_CACHE);

    sysbus_connect_irq(SYS_BUS_DEVICE(ctrl), 0, pic[VERSAL_EFUSE_IRQ]);
}

static void versal_create_pmc_iou_slcr(Versal *s, qemu_irq *pic)
{
    SysBusDevice *sbd;

    object_initialize_child(OBJECT(s), "versal-pmc-iou-slcr", &s->pmc.iou.slcr,
                            TYPE_XILINX_VERSAL_PMC_IOU_SLCR);

    sbd = SYS_BUS_DEVICE(&s->pmc.iou.slcr);
    sysbus_realize(sbd, &error_fatal);

    memory_region_add_subregion(&s->mr_ps, MM_PMC_PMC_IOU_SLCR,
                                sysbus_mmio_get_region(sbd, 0));

    sysbus_connect_irq(sbd, 0,
                       qdev_get_gpio_in(DEVICE(&s->pmc.apb_irq_orgate), 2));
}

static void versal_create_ospi(Versal *s, qemu_irq *pic)
{
    SysBusDevice *sbd;
    MemoryRegion *mr_dac;
    qemu_irq ospi_mux_sel;
    DeviceState *orgate;

    memory_region_init(&s->pmc.iou.ospi.linear_mr, OBJECT(s),
                       "versal-ospi-linear-mr" , MM_PMC_OSPI_DAC_SIZE);

    object_initialize_child(OBJECT(s), "versal-ospi", &s->pmc.iou.ospi.ospi,
                            TYPE_XILINX_VERSAL_OSPI);

    mr_dac = sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->pmc.iou.ospi.ospi), 1);
    memory_region_add_subregion(&s->pmc.iou.ospi.linear_mr, 0x0, mr_dac);

    /* Create the OSPI destination DMA */
    object_initialize_child(OBJECT(s), "versal-ospi-dma-dst",
                            &s->pmc.iou.ospi.dma_dst,
                            TYPE_XLNX_CSU_DMA);

    object_property_set_link(OBJECT(&s->pmc.iou.ospi.dma_dst),
                            "dma", OBJECT(get_system_memory()),
                             &error_abort);

    sbd = SYS_BUS_DEVICE(&s->pmc.iou.ospi.dma_dst);
    sysbus_realize(sbd, &error_fatal);

    memory_region_add_subregion(&s->mr_ps, MM_PMC_OSPI_DMA_DST,
                                sysbus_mmio_get_region(sbd, 0));

    /* Create the OSPI source DMA */
    object_initialize_child(OBJECT(s), "versal-ospi-dma-src",
                            &s->pmc.iou.ospi.dma_src,
                            TYPE_XLNX_CSU_DMA);

    object_property_set_bool(OBJECT(&s->pmc.iou.ospi.dma_src), "is-dst",
                             false, &error_abort);

    object_property_set_link(OBJECT(&s->pmc.iou.ospi.dma_src),
                            "dma", OBJECT(mr_dac), &error_abort);

    object_property_set_link(OBJECT(&s->pmc.iou.ospi.dma_src),
                            "stream-connected-dma",
                             OBJECT(&s->pmc.iou.ospi.dma_dst),
                             &error_abort);

    sbd = SYS_BUS_DEVICE(&s->pmc.iou.ospi.dma_src);
    sysbus_realize(sbd, &error_fatal);

    memory_region_add_subregion(&s->mr_ps, MM_PMC_OSPI_DMA_SRC,
                                sysbus_mmio_get_region(sbd, 0));

    /* Realize the OSPI */
    object_property_set_link(OBJECT(&s->pmc.iou.ospi.ospi), "dma-src",
                             OBJECT(&s->pmc.iou.ospi.dma_src), &error_abort);

    sbd = SYS_BUS_DEVICE(&s->pmc.iou.ospi.ospi);
    sysbus_realize(sbd, &error_fatal);

    memory_region_add_subregion(&s->mr_ps, MM_PMC_OSPI,
                                sysbus_mmio_get_region(sbd, 0));

    memory_region_add_subregion(&s->mr_ps, MM_PMC_OSPI_DAC,
                                &s->pmc.iou.ospi.linear_mr);

    /* ospi_mux_sel */
    ospi_mux_sel = qdev_get_gpio_in_named(DEVICE(&s->pmc.iou.ospi.ospi),
                                          "ospi-mux-sel", 0);
    qdev_connect_gpio_out_named(DEVICE(&s->pmc.iou.slcr), "ospi-mux-sel", 0,
                                ospi_mux_sel);

    /* OSPI irq */
    object_initialize_child(OBJECT(s), "ospi-irq-orgate",
                            &s->pmc.iou.ospi.irq_orgate, TYPE_OR_IRQ);
    object_property_set_int(OBJECT(&s->pmc.iou.ospi.irq_orgate),
                            "num-lines", NUM_OSPI_IRQ_LINES, &error_fatal);

    orgate = DEVICE(&s->pmc.iou.ospi.irq_orgate);
    qdev_realize(orgate, NULL, &error_fatal);

    sysbus_connect_irq(SYS_BUS_DEVICE(&s->pmc.iou.ospi.ospi), 0,
                       qdev_get_gpio_in(orgate, 0));
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->pmc.iou.ospi.dma_src), 0,
                       qdev_get_gpio_in(orgate, 1));
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->pmc.iou.ospi.dma_dst), 0,
                       qdev_get_gpio_in(orgate, 2));

    qdev_connect_gpio_out(orgate, 0, pic[VERSAL_OSPI_IRQ]);
}

static void versal_create_cfu(Versal *s, qemu_irq *pic)
{
    SysBusDevice *sbd;
    DeviceState *dev;
    int i;
    const struct {
        uint64_t reg_base;
        uint64_t fdri_base;
    } cframe_addr[] = {
        { MM_PMC_CFRAME0_REG, MM_PMC_CFRAME0_FDRI },
        { MM_PMC_CFRAME1_REG, MM_PMC_CFRAME1_FDRI },
        { MM_PMC_CFRAME2_REG, MM_PMC_CFRAME2_FDRI },
        { MM_PMC_CFRAME3_REG, MM_PMC_CFRAME3_FDRI },
        { MM_PMC_CFRAME4_REG, MM_PMC_CFRAME4_FDRI },
        { MM_PMC_CFRAME5_REG, MM_PMC_CFRAME5_FDRI },
        { MM_PMC_CFRAME6_REG, MM_PMC_CFRAME6_FDRI },
        { MM_PMC_CFRAME7_REG, MM_PMC_CFRAME7_FDRI },
        { MM_PMC_CFRAME8_REG, MM_PMC_CFRAME8_FDRI },
        { MM_PMC_CFRAME9_REG, MM_PMC_CFRAME9_FDRI },
        { MM_PMC_CFRAME10_REG, MM_PMC_CFRAME10_FDRI },
        { MM_PMC_CFRAME11_REG, MM_PMC_CFRAME11_FDRI },
        { MM_PMC_CFRAME12_REG, MM_PMC_CFRAME12_FDRI },
        { MM_PMC_CFRAME13_REG, MM_PMC_CFRAME13_FDRI },
        { MM_PMC_CFRAME14_REG, MM_PMC_CFRAME14_FDRI },
    };
    const struct {
        uint32_t blktype0_frames;
        uint32_t blktype1_frames;
        uint32_t blktype2_frames;
        uint32_t blktype3_frames;
        uint32_t blktype4_frames;
        uint32_t blktype5_frames;
        uint32_t blktype6_frames;
    } cframe_cfg[] = {
        [0] = { 34111, 3528, 12800, 11, 5, 1, 1 },
        [1] = { 38498, 3841, 15361, 13, 7, 3, 1 },
        [2] = { 38498, 3841, 15361, 13, 7, 3, 1 },
        [3] = { 38498, 3841, 15361, 13, 7, 3, 1 },
    };

    /* CFU FDRO */
    object_initialize_child(OBJECT(s), "cfu-fdro", &s->pmc.cfu_fdro,
                            TYPE_XLNX_VERSAL_CFU_FDRO);
    sbd = SYS_BUS_DEVICE(&s->pmc.cfu_fdro);

    sysbus_realize(sbd, &error_fatal);
    memory_region_add_subregion(&s->mr_ps, MM_PMC_CFU_FDRO,
                                sysbus_mmio_get_region(sbd, 0));

    /* CFRAME REG */
    for (i = 0; i < ARRAY_SIZE(s->pmc.cframe); i++) {
        g_autofree char *name = g_strdup_printf("cframe%d", i);

        object_initialize_child(OBJECT(s), name, &s->pmc.cframe[i],
                                TYPE_XLNX_VERSAL_CFRAME_REG);

        sbd = SYS_BUS_DEVICE(&s->pmc.cframe[i]);
        dev = DEVICE(&s->pmc.cframe[i]);

        if (i < ARRAY_SIZE(cframe_cfg)) {
            object_property_set_int(OBJECT(dev), "blktype0-frames",
                                    cframe_cfg[i].blktype0_frames,
                                    &error_abort);
            object_property_set_int(OBJECT(dev), "blktype1-frames",
                                    cframe_cfg[i].blktype1_frames,
                                    &error_abort);
            object_property_set_int(OBJECT(dev), "blktype2-frames",
                                    cframe_cfg[i].blktype2_frames,
                                    &error_abort);
            object_property_set_int(OBJECT(dev), "blktype3-frames",
                                    cframe_cfg[i].blktype3_frames,
                                    &error_abort);
            object_property_set_int(OBJECT(dev), "blktype4-frames",
                                    cframe_cfg[i].blktype4_frames,
                                    &error_abort);
            object_property_set_int(OBJECT(dev), "blktype5-frames",
                                    cframe_cfg[i].blktype5_frames,
                                    &error_abort);
            object_property_set_int(OBJECT(dev), "blktype6-frames",
                                    cframe_cfg[i].blktype6_frames,
                                    &error_abort);
        }
        object_property_set_link(OBJECT(dev), "cfu-fdro",
                                 OBJECT(&s->pmc.cfu_fdro), &error_fatal);

        sysbus_realize(SYS_BUS_DEVICE(dev), &error_fatal);

        memory_region_add_subregion(&s->mr_ps, cframe_addr[i].reg_base,
                                    sysbus_mmio_get_region(sbd, 0));
        memory_region_add_subregion(&s->mr_ps, cframe_addr[i].fdri_base,
                                    sysbus_mmio_get_region(sbd, 1));
        sysbus_connect_irq(sbd, 0,
                           qdev_get_gpio_in(DEVICE(&s->pmc.apb_irq_orgate),
                                            3 + i));
    }

    /* CFRAME BCAST */
    object_initialize_child(OBJECT(s), "cframe_bcast", &s->pmc.cframe_bcast,
                            TYPE_XLNX_VERSAL_CFRAME_BCAST_REG);

    sbd = SYS_BUS_DEVICE(&s->pmc.cframe_bcast);
    dev = DEVICE(&s->pmc.cframe_bcast);

    for (i = 0; i < ARRAY_SIZE(s->pmc.cframe); i++) {
        g_autofree char *propname = g_strdup_printf("cframe%d", i);
        object_property_set_link(OBJECT(dev), propname,
                                 OBJECT(&s->pmc.cframe[i]), &error_fatal);
    }

    sysbus_realize(sbd, &error_fatal);

    memory_region_add_subregion(&s->mr_ps, MM_PMC_CFRAME_BCAST_REG,
                                sysbus_mmio_get_region(sbd, 0));
    memory_region_add_subregion(&s->mr_ps, MM_PMC_CFRAME_BCAST_FDRI,
                                sysbus_mmio_get_region(sbd, 1));

    /* CFU APB */
    object_initialize_child(OBJECT(s), "cfu-apb", &s->pmc.cfu_apb,
                            TYPE_XLNX_VERSAL_CFU_APB);
    sbd = SYS_BUS_DEVICE(&s->pmc.cfu_apb);
    dev = DEVICE(&s->pmc.cfu_apb);

    for (i = 0; i < ARRAY_SIZE(s->pmc.cframe); i++) {
        g_autofree char *propname = g_strdup_printf("cframe%d", i);
        object_property_set_link(OBJECT(dev), propname,
                                 OBJECT(&s->pmc.cframe[i]), &error_fatal);
    }

    sysbus_realize(sbd, &error_fatal);
    memory_region_add_subregion(&s->mr_ps, MM_PMC_CFU_APB,
                                sysbus_mmio_get_region(sbd, 0));
    memory_region_add_subregion(&s->mr_ps, MM_PMC_CFU_STREAM,
                                sysbus_mmio_get_region(sbd, 1));
    memory_region_add_subregion(&s->mr_ps, MM_PMC_CFU_STREAM_2,
                                sysbus_mmio_get_region(sbd, 2));
    sysbus_connect_irq(sbd, 0, pic[VERSAL_CFU_IRQ_0]);

    /* CFU SFR */
    object_initialize_child(OBJECT(s), "cfu-sfr", &s->pmc.cfu_sfr,
                            TYPE_XLNX_VERSAL_CFU_SFR);

    sbd = SYS_BUS_DEVICE(&s->pmc.cfu_sfr);

    object_property_set_link(OBJECT(&s->pmc.cfu_sfr),
                            "cfu", OBJECT(&s->pmc.cfu_apb), &error_abort);

    sysbus_realize(sbd, &error_fatal);
    memory_region_add_subregion(&s->mr_ps, MM_PMC_CFU_SFR,
                                sysbus_mmio_get_region(sbd, 0));
}

static void versal_create_crl(Versal *s, qemu_irq *pic)
{
    SysBusDevice *sbd;
    int i;

    object_initialize_child(OBJECT(s), "crl", &s->lpd.crl,
                            TYPE_XLNX_VERSAL_CRL);
    sbd = SYS_BUS_DEVICE(&s->lpd.crl);

    for (i = 0; i < ARRAY_SIZE(s->lpd.rpu.cpu); i++) {
        g_autofree gchar *name = g_strdup_printf("cpu_r5[%d]", i);

        object_property_set_link(OBJECT(&s->lpd.crl),
                                 name, OBJECT(&s->lpd.rpu.cpu[i]),
                                 &error_abort);
    }

    for (i = 0; i < ARRAY_SIZE(s->lpd.iou.gem); i++) {
        g_autofree gchar *name = g_strdup_printf("gem[%d]", i);

        object_property_set_link(OBJECT(&s->lpd.crl),
                                 name, OBJECT(&s->lpd.iou.gem[i]),
                                 &error_abort);
    }

    for (i = 0; i < ARRAY_SIZE(s->lpd.iou.adma); i++) {
        g_autofree gchar *name = g_strdup_printf("adma[%d]", i);

        object_property_set_link(OBJECT(&s->lpd.crl),
                                 name, OBJECT(&s->lpd.iou.adma[i]),
                                 &error_abort);
    }

    for (i = 0; i < ARRAY_SIZE(s->lpd.iou.uart); i++) {
        g_autofree gchar *name = g_strdup_printf("uart[%d]", i);

        object_property_set_link(OBJECT(&s->lpd.crl),
                                 name, OBJECT(&s->lpd.iou.uart[i]),
                                 &error_abort);
    }

    object_property_set_link(OBJECT(&s->lpd.crl),
                             "usb", OBJECT(&s->lpd.iou.usb),
                             &error_abort);

    sysbus_realize(sbd, &error_fatal);
    memory_region_add_subregion(&s->mr_ps, MM_CRL,
                                sysbus_mmio_get_region(sbd, 0));
    sysbus_connect_irq(sbd, 0, pic[VERSAL_CRL_IRQ]);
}

/* This takes the board allocated linear DDR memory and creates aliases
 * for each split DDR range/aperture on the Versal address map.
 */
static void versal_map_ddr(Versal *s)
{
    uint64_t size = memory_region_size(s->cfg.mr_ddr);
    /* Describes the various split DDR access regions.  */
    static const struct {
        uint64_t base;
        uint64_t size;
    } addr_ranges[] = {
        { MM_TOP_DDR, MM_TOP_DDR_SIZE },
        { MM_TOP_DDR_2, MM_TOP_DDR_2_SIZE },
        { MM_TOP_DDR_3, MM_TOP_DDR_3_SIZE },
        { MM_TOP_DDR_4, MM_TOP_DDR_4_SIZE }
    };
    uint64_t offset = 0;
    int i;

    assert(ARRAY_SIZE(addr_ranges) == ARRAY_SIZE(s->noc.mr_ddr_ranges));
    for (i = 0; i < ARRAY_SIZE(addr_ranges) && size; i++) {
        char *name;
        uint64_t mapsize;

        mapsize = size < addr_ranges[i].size ? size : addr_ranges[i].size;
        name = g_strdup_printf("noc-ddr-range%d", i);
        /* Create the MR alias.  */
        memory_region_init_alias(&s->noc.mr_ddr_ranges[i], OBJECT(s),
                                 name, s->cfg.mr_ddr,
                                 offset, mapsize);

        /* Map it onto the NoC MR.  */
        memory_region_add_subregion(&s->mr_ps, addr_ranges[i].base,
                                    &s->noc.mr_ddr_ranges[i]);
        offset += mapsize;
        size -= mapsize;
        g_free(name);
    }
}

static void versal_unimp_area(Versal *s, const char *name,
                                MemoryRegion *mr,
                                hwaddr base, hwaddr size)
{
    DeviceState *dev = qdev_new(TYPE_UNIMPLEMENTED_DEVICE);
    MemoryRegion *mr_dev;

    qdev_prop_set_string(dev, "name", name);
    qdev_prop_set_uint64(dev, "size", size);
    object_property_add_child(OBJECT(s), name, OBJECT(dev));
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);

    mr_dev = sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 0);
    memory_region_add_subregion(mr, base, mr_dev);
}

static void versal_unimp_sd_emmc_sel(void *opaque, int n, int level)
{
    qemu_log_mask(LOG_UNIMP,
                  "Selecting between enabling SD mode or eMMC mode on "
                  "controller %d is not yet implemented\n", n);
}

static void versal_unimp_qspi_ospi_mux_sel(void *opaque, int n, int level)
{
    qemu_log_mask(LOG_UNIMP,
                  "Selecting between enabling the QSPI or OSPI linear address "
                  "region is not yet implemented\n");
}

static void versal_unimp_irq_parity_imr(void *opaque, int n, int level)
{
    qemu_log_mask(LOG_UNIMP,
                  "PMC SLCR parity interrupt behaviour "
                  "is not yet implemented\n");
}

static void versal_unimp(Versal *s)
{
    qemu_irq gpio_in;

    versal_unimp_area(s, "psm", &s->mr_ps,
                        MM_PSM_START, MM_PSM_END - MM_PSM_START);
    versal_unimp_area(s, "crf", &s->mr_ps,
                        MM_FPD_CRF, MM_FPD_CRF_SIZE);
    versal_unimp_area(s, "apu", &s->mr_ps,
                        MM_FPD_FPD_APU, MM_FPD_FPD_APU_SIZE);
    versal_unimp_area(s, "crp", &s->mr_ps,
                        MM_PMC_CRP, MM_PMC_CRP_SIZE);
    versal_unimp_area(s, "iou-scntr", &s->mr_ps,
                        MM_IOU_SCNTR, MM_IOU_SCNTR_SIZE);
    versal_unimp_area(s, "iou-scntr-seucre", &s->mr_ps,
                        MM_IOU_SCNTRS, MM_IOU_SCNTRS_SIZE);

    qdev_init_gpio_in_named(DEVICE(s), versal_unimp_sd_emmc_sel,
                            "sd-emmc-sel-dummy", 2);
    qdev_init_gpio_in_named(DEVICE(s), versal_unimp_qspi_ospi_mux_sel,
                            "qspi-ospi-mux-sel-dummy", 1);
    qdev_init_gpio_in_named(DEVICE(s), versal_unimp_irq_parity_imr,
                            "irq-parity-imr-dummy", 1);

    gpio_in = qdev_get_gpio_in_named(DEVICE(s), "sd-emmc-sel-dummy", 0);
    qdev_connect_gpio_out_named(DEVICE(&s->pmc.iou.slcr), "sd-emmc-sel", 0,
                                gpio_in);

    gpio_in = qdev_get_gpio_in_named(DEVICE(s), "sd-emmc-sel-dummy", 1);
    qdev_connect_gpio_out_named(DEVICE(&s->pmc.iou.slcr), "sd-emmc-sel", 1,
                                gpio_in);

    gpio_in = qdev_get_gpio_in_named(DEVICE(s), "qspi-ospi-mux-sel-dummy", 0);
    qdev_connect_gpio_out_named(DEVICE(&s->pmc.iou.slcr),
                                "qspi-ospi-mux-sel", 0,
                                gpio_in);

    gpio_in = qdev_get_gpio_in_named(DEVICE(s), "irq-parity-imr-dummy", 0);
    qdev_connect_gpio_out_named(DEVICE(&s->pmc.iou.slcr),
                                SYSBUS_DEVICE_GPIO_IRQ, 0,
                                gpio_in);
}

static void versal_realize(DeviceState *dev, Error **errp)
{
    Versal *s = XLNX_VERSAL(dev);
    qemu_irq pic[XLNX_VERSAL_NR_IRQS];

    versal_create_apu_cpus(s);
    versal_create_apu_gic(s, pic);
    versal_create_rpu_cpus(s);
    versal_create_uarts(s, pic);
    versal_create_canfds(s, pic);
    versal_create_usbs(s, pic);
    versal_create_gems(s, pic);
    versal_create_admas(s, pic);
    versal_create_sds(s, pic);
    versal_create_pmc_apb_irq_orgate(s, pic);
    versal_create_rtc(s, pic);
    versal_create_trng(s, pic);
    versal_create_xrams(s, pic);
    versal_create_bbram(s, pic);
    versal_create_efuse(s, pic);
    versal_create_pmc_iou_slcr(s, pic);
    versal_create_ospi(s, pic);
    versal_create_crl(s, pic);
    versal_create_cfu(s, pic);
    versal_map_ddr(s);
    versal_unimp(s);

    /* Create the On Chip Memory (OCM).  */
    memory_region_init_ram(&s->lpd.mr_ocm, OBJECT(s), "ocm",
                           MM_OCM_SIZE, &error_fatal);

    memory_region_add_subregion_overlap(&s->mr_ps, MM_OCM, &s->lpd.mr_ocm, 0);
    memory_region_add_subregion_overlap(&s->fpd.apu.mr, 0, &s->mr_ps, 0);
    memory_region_add_subregion_overlap(&s->lpd.rpu.mr, 0,
                                        &s->lpd.rpu.mr_ps_alias, 0);
}

static void versal_init(Object *obj)
{
    Versal *s = XLNX_VERSAL(obj);

    memory_region_init(&s->fpd.apu.mr, obj, "mr-apu", UINT64_MAX);
    memory_region_init(&s->lpd.rpu.mr, obj, "mr-rpu", UINT64_MAX);
    memory_region_init(&s->mr_ps, obj, "mr-ps-switch", UINT64_MAX);
    memory_region_init_alias(&s->lpd.rpu.mr_ps_alias, OBJECT(s),
                             "mr-rpu-ps-alias", &s->mr_ps, 0, UINT64_MAX);
}

static const Property versal_properties[] = {
    DEFINE_PROP_LINK("ddr", Versal, cfg.mr_ddr, TYPE_MEMORY_REGION,
                     MemoryRegion *),
    DEFINE_PROP_LINK("canbus0", Versal, lpd.iou.canbus[0],
                      TYPE_CAN_BUS, CanBusState *),
    DEFINE_PROP_LINK("canbus1", Versal, lpd.iou.canbus[1],
                      TYPE_CAN_BUS, CanBusState *),
};

static void versal_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = versal_realize;
    device_class_set_props(dc, versal_properties);
    /* No VMSD since we haven't got any top-level SoC state to save.  */
}

static const TypeInfo versal_info = {
    .name = TYPE_XLNX_VERSAL,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Versal),
    .instance_init = versal_init,
    .class_init = versal_class_init,
};

static void versal_register_types(void)
{
    type_register_static(&versal_info);
}

type_init(versal_register_types);
