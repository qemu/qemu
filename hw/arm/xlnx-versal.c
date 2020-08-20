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
#include "qapi/error.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/sysbus.h"
#include "net/net.h"
#include "sysemu/sysemu.h"
#include "sysemu/kvm.h"
#include "hw/arm/boot.h"
#include "kvm_arm.h"
#include "hw/misc/unimp.h"
#include "hw/arm/xlnx-versal.h"

#define XLNX_VERSAL_ACPU_TYPE ARM_CPU_TYPE_NAME("cortex-a72")
#define GEM_REVISION        0x40070106

static void versal_create_apu_cpus(Versal *s)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(s->fpd.apu.cpu); i++) {
        Object *obj;

        object_initialize_child(OBJECT(s), "apu-cpu[*]", &s->fpd.apu.cpu[i],
                                XLNX_VERSAL_ACPU_TYPE);
        obj = OBJECT(&s->fpd.apu.cpu[i]);
        object_property_set_int(obj, "psci-conduit", s->cfg.psci_conduit,
                                &error_abort);
        if (i) {
            /* Secondary CPUs start in PSCI powered-down state */
            object_property_set_bool(obj, "start-powered-off", true,
                                     &error_abort);
        }

        object_property_set_int(obj, "core-count", ARRAY_SIZE(s->fpd.apu.cpu),
                                &error_abort);
        object_property_set_link(obj, "memory", OBJECT(&s->fpd.apu.mr),
                                 &error_abort);
        qdev_realize(DEVICE(obj), NULL, &error_fatal);
    }
}

static void versal_create_apu_gic(Versal *s, qemu_irq *pic)
{
    static const uint64_t addrs[] = {
        MM_GIC_APU_DIST_MAIN,
        MM_GIC_APU_REDIST_0
    };
    SysBusDevice *gicbusdev;
    DeviceState *gicdev;
    int nr_apu_cpus = ARRAY_SIZE(s->fpd.apu.cpu);
    int i;

    object_initialize_child(OBJECT(s), "apu-gic", &s->fpd.apu.gic,
                            gicv3_class_name());
    gicbusdev = SYS_BUS_DEVICE(&s->fpd.apu.gic);
    gicdev = DEVICE(&s->fpd.apu.gic);
    qdev_prop_set_uint32(gicdev, "revision", 3);
    qdev_prop_set_uint32(gicdev, "num-cpu", 2);
    qdev_prop_set_uint32(gicdev, "num-irq", XLNX_VERSAL_NR_IRQS + 32);
    qdev_prop_set_uint32(gicdev, "len-redist-region-count", 1);
    qdev_prop_set_uint32(gicdev, "redist-region-count[0]", 2);
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

static void versal_create_gems(Versal *s, qemu_irq *pic)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(s->lpd.iou.gem); i++) {
        static const int irqs[] = { VERSAL_GEM0_IRQ_0, VERSAL_GEM1_IRQ_0};
        static const uint64_t addrs[] = { MM_GEM0, MM_GEM1 };
        char *name = g_strdup_printf("gem%d", i);
        NICInfo *nd = &nd_table[i];
        DeviceState *dev;
        MemoryRegion *mr;

        object_initialize_child(OBJECT(s), name, &s->lpd.iou.gem[i],
                                TYPE_CADENCE_GEM);
        dev = DEVICE(&s->lpd.iou.gem[i]);
        /* FIXME use qdev NIC properties instead of nd_table[] */
        if (nd->used) {
            qemu_check_nic_model(nd, "cadence_gem");
            qdev_set_nic_properties(dev, nd);
        }
        object_property_set_int(OBJECT(dev), "num-priority-queues", 2,
                                &error_abort);
        object_property_set_link(OBJECT(dev), "dma", OBJECT(&s->mr_ps),
                                 &error_abort);
        sysbus_realize(SYS_BUS_DEVICE(dev), &error_fatal);

        mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 0);
        memory_region_add_subregion(&s->mr_ps, addrs[i], mr);

        sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, pic[irqs[i]]);
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

static void versal_create_rtc(Versal *s, qemu_irq *pic)
{
    SysBusDevice *sbd;
    MemoryRegion *mr;

    object_initialize_child(OBJECT(s), "rtc", &s->pmc.rtc,
                            TYPE_XLNX_ZYNQMP_RTC);
    sbd = SYS_BUS_DEVICE(&s->pmc.rtc);
    sysbus_realize(SYS_BUS_DEVICE(sbd), &error_fatal);

    mr = sysbus_mmio_get_region(sbd, 0);
    memory_region_add_subregion(&s->mr_ps, MM_PMC_RTC, mr);

    /*
     * TODO: Connect the ALARM and SECONDS interrupts once our RTC model
     * supports them.
     */
    sysbus_connect_irq(sbd, 1, pic[VERSAL_RTC_APB_ERR_IRQ]);
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

static void versal_unimp(Versal *s)
{
    versal_unimp_area(s, "psm", &s->mr_ps,
                        MM_PSM_START, MM_PSM_END - MM_PSM_START);
    versal_unimp_area(s, "crl", &s->mr_ps,
                        MM_CRL, MM_CRL_SIZE);
    versal_unimp_area(s, "crf", &s->mr_ps,
                        MM_FPD_CRF, MM_FPD_CRF_SIZE);
    versal_unimp_area(s, "crp", &s->mr_ps,
                        MM_PMC_CRP, MM_PMC_CRP_SIZE);
    versal_unimp_area(s, "iou-scntr", &s->mr_ps,
                        MM_IOU_SCNTR, MM_IOU_SCNTR_SIZE);
    versal_unimp_area(s, "iou-scntr-seucre", &s->mr_ps,
                        MM_IOU_SCNTRS, MM_IOU_SCNTRS_SIZE);
}

static void versal_realize(DeviceState *dev, Error **errp)
{
    Versal *s = XLNX_VERSAL(dev);
    qemu_irq pic[XLNX_VERSAL_NR_IRQS];

    versal_create_apu_cpus(s);
    versal_create_apu_gic(s, pic);
    versal_create_uarts(s, pic);
    versal_create_gems(s, pic);
    versal_create_admas(s, pic);
    versal_create_sds(s, pic);
    versal_create_rtc(s, pic);
    versal_map_ddr(s);
    versal_unimp(s);

    /* Create the On Chip Memory (OCM).  */
    memory_region_init_ram(&s->lpd.mr_ocm, OBJECT(s), "ocm",
                           MM_OCM_SIZE, &error_fatal);

    memory_region_add_subregion_overlap(&s->mr_ps, MM_OCM, &s->lpd.mr_ocm, 0);
    memory_region_add_subregion_overlap(&s->fpd.apu.mr, 0, &s->mr_ps, 0);
}

static void versal_init(Object *obj)
{
    Versal *s = XLNX_VERSAL(obj);

    memory_region_init(&s->fpd.apu.mr, obj, "mr-apu", UINT64_MAX);
    memory_region_init(&s->mr_ps, obj, "mr-ps-switch", UINT64_MAX);
}

static Property versal_properties[] = {
    DEFINE_PROP_LINK("ddr", Versal, cfg.mr_ddr, TYPE_MEMORY_REGION,
                     MemoryRegion *),
    DEFINE_PROP_UINT32("psci-conduit", Versal, cfg.psci_conduit, 0),
    DEFINE_PROP_END_OF_LIST()
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
