/*
 * BCM2838 SoC emulation
 *
 * Copyright (C) 2022 Ovchinnikov Vitalii <vitalii.ovchinnikov@auriga.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "hw/arm/raspi_platform.h"
#include "hw/sysbus.h"
#include "hw/arm/bcm2838.h"
#include "trace.h"

#define GIC400_MAINTENANCE_IRQ      9
#define GIC400_TIMER_NS_EL2_IRQ     10
#define GIC400_TIMER_VIRT_IRQ       11
#define GIC400_LEGACY_FIQ           12
#define GIC400_TIMER_S_EL1_IRQ      13
#define GIC400_TIMER_NS_EL1_IRQ     14
#define GIC400_LEGACY_IRQ           15

/* Number of external interrupt lines to configure the GIC with */
#define GIC_NUM_IRQS                192

#define PPI(cpu, irq) (GIC_NUM_IRQS + (cpu) * GIC_INTERNAL + GIC_NR_SGIS + irq)

#define GIC_BASE_OFS                0x0000
#define GIC_DIST_OFS                0x1000
#define GIC_CPU_OFS                 0x2000
#define GIC_VIFACE_THIS_OFS         0x4000
#define GIC_VIFACE_OTHER_OFS(cpu)  (0x5000 + (cpu) * 0x200)
#define GIC_VCPU_OFS                0x6000

#define VIRTUAL_PMU_IRQ 7

static void bcm2838_gic_set_irq(void *opaque, int irq, int level)
{
    BCM2838State *s = (BCM2838State *)opaque;

    trace_bcm2838_gic_set_irq(irq, level);
    qemu_set_irq(qdev_get_gpio_in(DEVICE(&s->gic), irq), level);
}

static void bcm2838_init(Object *obj)
{
    BCM2838State *s = BCM2838(obj);

    object_initialize_child(obj, "peripherals", &s->peripherals,
                            TYPE_BCM2838_PERIPHERALS);
    object_property_add_alias(obj, "board-rev", OBJECT(&s->peripherals),
                              "board-rev");
    object_property_add_alias(obj, "vcram-size", OBJECT(&s->peripherals),
                              "vcram-size");
    object_property_add_alias(obj, "vcram-base", OBJECT(&s->peripherals),
                              "vcram-base");
    object_property_add_alias(obj, "command-line", OBJECT(&s->peripherals),
                              "command-line");

    object_initialize_child(obj, "gic", &s->gic, TYPE_ARM_GIC);
}

static void bcm2838_realize(DeviceState *dev, Error **errp)
{
    BCM2838State *s = BCM2838(dev);
    BCM283XBaseState *s_base = BCM283X_BASE(dev);
    BCM283XBaseClass *bc_base = BCM283X_BASE_GET_CLASS(dev);
    BCM2838PeripheralState *ps = BCM2838_PERIPHERALS(&s->peripherals);
    BCMSocPeripheralBaseState *ps_base =
        BCM_SOC_PERIPHERALS_BASE(&s->peripherals);

    DeviceState *gicdev = NULL;

    if (!bcm283x_common_realize(dev, ps_base, errp)) {
        return;
    }
    sysbus_mmio_map_overlap(SYS_BUS_DEVICE(ps), 1, BCM2838_PERI_LOW_BASE, 1);

    /* bcm2836 interrupt controller (and mailboxes, etc.) */
    if (!sysbus_realize(SYS_BUS_DEVICE(&s_base->control), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s_base->control), 0, bc_base->ctrl_base);

    /* Create cores */
    for (int n = 0; n < bc_base->core_count; n++) {

        object_property_set_int(OBJECT(&s_base->cpu[n].core), "mp-affinity",
                                (bc_base->clusterid << 8) | n, &error_abort);

        /* set periphbase/CBAR value for CPU-local registers */
        object_property_set_int(OBJECT(&s_base->cpu[n].core), "reset-cbar",
                                bc_base->peri_base, &error_abort);

        /* start powered off if not enabled */
        object_property_set_bool(OBJECT(&s_base->cpu[n].core),
                                 "start-powered-off",
                                 n >= s_base->enabled_cpus, &error_abort);

        if (!qdev_realize(DEVICE(&s_base->cpu[n].core), NULL, errp)) {
            return;
        }
    }

    if (!object_property_set_uint(OBJECT(&s->gic), "revision", 2, errp)) {
        return;
    }

    if (!object_property_set_uint(OBJECT(&s->gic), "num-cpu", BCM283X_NCPUS,
                                  errp)) {
        return;
    }

    if (!object_property_set_uint(OBJECT(&s->gic), "num-irq",
                                  GIC_NUM_IRQS + GIC_INTERNAL, errp)) {
        return;
    }

    if (!object_property_set_bool(OBJECT(&s->gic),
                                  "has-virtualization-extensions", true,
                                  errp)) {
        return;
    }

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->gic), errp)) {
        return;
    }

    sysbus_mmio_map(SYS_BUS_DEVICE(&s->gic), 0,
                    bc_base->ctrl_base + BCM2838_GIC_BASE + GIC_DIST_OFS);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->gic), 1,
                    bc_base->ctrl_base + BCM2838_GIC_BASE + GIC_CPU_OFS);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->gic), 2,
                    bc_base->ctrl_base + BCM2838_GIC_BASE + GIC_VIFACE_THIS_OFS);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->gic), 3,
                    bc_base->ctrl_base + BCM2838_GIC_BASE + GIC_VCPU_OFS);

    for (int n = 0; n < BCM283X_NCPUS; n++) {
        sysbus_mmio_map(SYS_BUS_DEVICE(&s->gic), 4 + n,
                        bc_base->ctrl_base + BCM2838_GIC_BASE
                            + GIC_VIFACE_OTHER_OFS(n));
    }

    gicdev = DEVICE(&s->gic);

    for (int n = 0; n < BCM283X_NCPUS; n++) {
        DeviceState *cpudev = DEVICE(&s_base->cpu[n]);

        /* Connect the GICv2 outputs to the CPU */
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->gic), n,
                           qdev_get_gpio_in(cpudev, ARM_CPU_IRQ));
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->gic), n + BCM283X_NCPUS,
                           qdev_get_gpio_in(cpudev, ARM_CPU_FIQ));
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->gic), n + 2 * BCM283X_NCPUS,
                           qdev_get_gpio_in(cpudev, ARM_CPU_VIRQ));
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->gic), n + 3 * BCM283X_NCPUS,
                           qdev_get_gpio_in(cpudev, ARM_CPU_VFIQ));

        sysbus_connect_irq(SYS_BUS_DEVICE(&s->gic), n + 4 * BCM283X_NCPUS,
                           qdev_get_gpio_in(gicdev,
                                            PPI(n, GIC400_MAINTENANCE_IRQ)));

        /* Connect timers from the CPU to the interrupt controller */
        qdev_connect_gpio_out(cpudev, GTIMER_PHYS,
                    qdev_get_gpio_in(gicdev, PPI(n, GIC400_TIMER_NS_EL1_IRQ)));
        qdev_connect_gpio_out(cpudev, GTIMER_VIRT,
                    qdev_get_gpio_in(gicdev, PPI(n, GIC400_TIMER_VIRT_IRQ)));
        qdev_connect_gpio_out(cpudev, GTIMER_HYP,
                    qdev_get_gpio_in(gicdev, PPI(n, GIC400_TIMER_NS_EL2_IRQ)));
        qdev_connect_gpio_out(cpudev, GTIMER_SEC,
                    qdev_get_gpio_in(gicdev, PPI(n, GIC400_TIMER_S_EL1_IRQ)));
        /* PMU interrupt */
        qdev_connect_gpio_out_named(cpudev, "pmu-interrupt", 0,
                    qdev_get_gpio_in(gicdev, PPI(n, VIRTUAL_PMU_IRQ)));
    }

    /* Connect UART0 to the interrupt controller */
    sysbus_connect_irq(SYS_BUS_DEVICE(&ps_base->uart0), 0,
                       qdev_get_gpio_in(gicdev, GIC_SPI_INTERRUPT_UART0));

    /* Connect AUX / UART1 to the interrupt controller */
    sysbus_connect_irq(SYS_BUS_DEVICE(&ps_base->aux), 0,
                       qdev_get_gpio_in(gicdev, GIC_SPI_INTERRUPT_AUX_UART1));

    /* Connect VC mailbox to the interrupt controller */
    sysbus_connect_irq(SYS_BUS_DEVICE(&ps_base->mboxes), 0,
                       qdev_get_gpio_in(gicdev, GIC_SPI_INTERRUPT_MBOX));

    /* Connect SD host to the interrupt controller */
    sysbus_connect_irq(SYS_BUS_DEVICE(&ps_base->sdhost), 0,
                       qdev_get_gpio_in(gicdev, GIC_SPI_INTERRUPT_SDHOST));

    /* According to DTS, EMMC and EMMC2 share one irq */
    DeviceState *mmc_irq_orgate = DEVICE(&ps->mmc_irq_orgate);

    /* Connect EMMC and EMMC2 to the interrupt controller */
    qdev_connect_gpio_out(mmc_irq_orgate, 0,
                          qdev_get_gpio_in(gicdev, GIC_SPI_INTERRUPT_EMMC_EMMC2));

    /* Connect USB OTG and MPHI to the interrupt controller */
    sysbus_connect_irq(SYS_BUS_DEVICE(&ps_base->mphi), 0,
                       qdev_get_gpio_in(gicdev, GIC_SPI_INTERRUPT_MPHI));
    sysbus_connect_irq(SYS_BUS_DEVICE(&ps_base->dwc2), 0,
                       qdev_get_gpio_in(gicdev, GIC_SPI_INTERRUPT_DWC2));

    /* Connect DMA 0-6 to the interrupt controller */
    for (int n = GIC_SPI_INTERRUPT_DMA_0; n <= GIC_SPI_INTERRUPT_DMA_6; n++) {
        sysbus_connect_irq(SYS_BUS_DEVICE(&ps_base->dma),
                           n - GIC_SPI_INTERRUPT_DMA_0,
                           qdev_get_gpio_in(gicdev, n));
    }

    /* According to DTS, DMA 7 and 8 share one irq */
    DeviceState *dma_7_8_irq_orgate = DEVICE(&ps->dma_7_8_irq_orgate);

    /* Connect DMA 7-8 to the interrupt controller */
    qdev_connect_gpio_out(dma_7_8_irq_orgate, 0,
                          qdev_get_gpio_in(gicdev, GIC_SPI_INTERRUPT_DMA_7_8));

    /* According to DTS, DMA 9 and 10 share one irq */
    DeviceState *dma_9_10_irq_orgate = DEVICE(&ps->dma_9_10_irq_orgate);

    /* Connect DMA 9-10 to the interrupt controller */
    qdev_connect_gpio_out(dma_9_10_irq_orgate, 0,
                          qdev_get_gpio_in(gicdev, GIC_SPI_INTERRUPT_DMA_9_10));

    /* Pass through inbound GPIO lines to the GIC */
    qdev_init_gpio_in(dev, bcm2838_gic_set_irq, GIC_NUM_IRQS);

    /* Pass through outbound IRQ lines from the GIC */
    qdev_pass_gpios(DEVICE(&s->gic), DEVICE(&s->peripherals), NULL);
}

static void bcm2838_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    BCM283XBaseClass *bc_base = BCM283X_BASE_CLASS(oc);

    bc_base->cpu_type = ARM_CPU_TYPE_NAME("cortex-a72");
    bc_base->core_count = BCM283X_NCPUS;
    bc_base->peri_base = 0xfe000000;
    bc_base->ctrl_base = 0xff800000;
    bc_base->clusterid = 0x0;
    dc->realize = bcm2838_realize;
}

static const TypeInfo bcm2838_type = {
    .name           = TYPE_BCM2838,
    .parent         = TYPE_BCM283X_BASE,
    .instance_size  = sizeof(BCM2838State),
    .instance_init  = bcm2838_init,
    .class_size     = sizeof(BCM283XBaseClass),
    .class_init     = bcm2838_class_init,
};

static void bcm2838_register_types(void)
{
    type_register_static(&bcm2838_type);
}

type_init(bcm2838_register_types);
