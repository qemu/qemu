/*
 * Axiado SoC AX3000
 *
 * Author: Kuan-Jui Chiu <kchiu@axiado.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "system/address-spaces.h"
#include "hw/arm/bsa.h"
#include "hw/arm/ax3000-soc.h"
#include "hw/misc/unimp.h"
#include "system/system.h"
#include "qobject/qlist.h"
#include "qom/object.h"
#include "hw/core/boards.h"

static void ax3000_init(Object *obj)
{
    Ax3000SoCState *s = AX3000_SOC(obj);
    Ax3000SoCClass *sc = AX3000_SOC_GET_CLASS(s);

    for (int i = 0; i < sc->num_cpus; i++) {
        g_autofree char *name = g_strdup_printf("cpu%d", i);
        object_initialize_child(obj, name, &s->cpu[i],
                                ARM_CPU_TYPE_NAME("cortex-a53"));
    }

    object_initialize_child(obj, "gic", &s->gic, gicv3_class_name());

    for (int i = 0; i < AX3000_NUM_UARTS; i++) {
        g_autofree char *name = g_strdup_printf("uart%d", i);
        object_initialize_child(obj, name, &s->uart[i], TYPE_CADENCE_UART);
    }
}

static void ax3000_realize(DeviceState *dev, Error **errp)
{
    Ax3000SoCState *s = AX3000_SOC(dev);
    Ax3000SoCClass *sc = AX3000_SOC_GET_CLASS(s);
    SysBusDevice *gic_sbd = SYS_BUS_DEVICE(&s->gic);
    DeviceState *gic_dev = DEVICE(&s->gic);
    QList *redist_region_count;
    SysBusDevice *sdhci0_sbd;
    DeviceState *card;

    /* CPUs */
    for (int i = 0; i < sc->num_cpus; i++) {
        object_property_set_int(OBJECT(&s->cpu[i]), "cntfrq", 8000000,
                                &error_abort);

        if (object_property_find(OBJECT(&s->cpu[i]), "has_el3")) {
            object_property_set_bool(OBJECT(&s->cpu[i]), "has_el3",
                                     false, &error_abort);
        }

        if (!qdev_realize(DEVICE(&s->cpu[i]), NULL, errp)) {
            return;
        }
    }

    /* GIC */
    qdev_prop_set_uint32(gic_dev, "num-cpu", sc->num_cpus);
    qdev_prop_set_uint32(gic_dev, "num-irq",
                         AX3000_NUM_IRQS + GIC_INTERNAL);

    redist_region_count = qlist_new();
    qlist_append_int(redist_region_count, sc->num_cpus);
    qdev_prop_set_array(gic_dev, "redist-region-count", redist_region_count);

    if (!sysbus_realize(gic_sbd, errp)) {
        return;
    }

    sysbus_mmio_map(gic_sbd, 0, AX3000_GIC_DIST_BASE);
    sysbus_mmio_map(gic_sbd, 1, AX3000_GIC_REDIST_BASE);

    /*
     * Mapping from the output timer irq lines from the CPU to the
     * GIC PPI inputs.
     */
    const int timer_irqs[] = {
        [GTIMER_PHYS] = ARCH_TIMER_NS_EL1_IRQ,
        [GTIMER_VIRT] = ARCH_TIMER_VIRT_IRQ,
        [GTIMER_HYP]  = ARCH_TIMER_NS_EL2_IRQ,
        [GTIMER_SEC]  = ARCH_TIMER_S_EL1_IRQ
    };

    /*
     * Wire the outputs from each CPU's generic timer and the GICv3
     * maintenance interrupt signal to the appropriate GIC PPI inputs, and
     * the GIC's IRQ/FIQ interrupt outputs to the CPU's inputs.
     */
    for (int i = 0; i < sc->num_cpus; i++) {
        DeviceState *cpu_dev = DEVICE(&s->cpu[i]);
        int intidbase = AX3000_NUM_IRQS + i * GIC_INTERNAL;
        qemu_irq irq;

        for (int j = 0; j < ARRAY_SIZE(timer_irqs); j++) {
            irq = qdev_get_gpio_in(gic_dev, intidbase + timer_irqs[j]);
            qdev_connect_gpio_out(cpu_dev, j, irq);
        }

        irq = qdev_get_gpio_in(gic_dev, intidbase + ARCH_GIC_MAINT_IRQ);
        qdev_connect_gpio_out_named(cpu_dev, "gicv3-maintenance-interrupt",
                                        0, irq);

        sysbus_connect_irq(gic_sbd, i,
                           qdev_get_gpio_in(cpu_dev, ARM_CPU_IRQ));
        sysbus_connect_irq(gic_sbd, i + sc->num_cpus,
                           qdev_get_gpio_in(cpu_dev, ARM_CPU_FIQ));
        sysbus_connect_irq(gic_sbd, i + 2 * sc->num_cpus,
                           qdev_get_gpio_in(cpu_dev, ARM_CPU_VIRQ));
        sysbus_connect_irq(gic_sbd, i + 3 * sc->num_cpus,
                           qdev_get_gpio_in(cpu_dev, ARM_CPU_VFIQ));
    }

    /* DRAM */
    const struct {
        hwaddr addr;
        size_t size;
        const char *name;
    } dram_table[] = {
        { AX3000_DRAM0_BASE, AX3000_DRAM0_SIZE, "dram0" },
        { AX3000_DRAM1_BASE, AX3000_DRAM1_SIZE, "dram1" }
    };

    for (int i = 0; i < AX3000_NUM_BANKS; i++) {
        memory_region_init_ram(&s->dram[i], OBJECT(s), dram_table[i].name,
                               dram_table[i].size, &error_fatal);
        memory_region_add_subregion(get_system_memory(), dram_table[i].addr,
                                    &s->dram[i]);
    }

    /* UARTs */
    const struct {
        hwaddr addr;
        unsigned int irq;
    } serial_table[] = {
        { AX3000_UART0_BASE, AX3000_UART0_IRQ },
        { AX3000_UART1_BASE, AX3000_UART1_IRQ },
        { AX3000_UART2_BASE, AX3000_UART2_IRQ },
        { AX3000_UART3_BASE, AX3000_UART3_IRQ }
    };

    for (int i = 0; i < AX3000_NUM_UARTS; i++) {
        qdev_prop_set_chr(DEVICE(&s->uart[i]), "chardev", serial_hd(i));
        if (!sysbus_realize(SYS_BUS_DEVICE(&s->uart[i]), errp)) {
            return;
        }

        sysbus_mmio_map(SYS_BUS_DEVICE(&s->uart[i]), 0, serial_table[i].addr);
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->uart[i]), 0,
                           qdev_get_gpio_in(gic_dev, serial_table[i].irq));
    }

    /* Timer control */
    create_unimplemented_device("ax3000.timerctrl", AX3000_TIMER_CTRL, 32);
}

static void ax3000_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    Ax3000SoCClass *sc = AX3000_SOC_CLASS(oc);

    dc->desc = "Axiado SoC AX3000";
    dc->realize = ax3000_realize;
    sc->num_cpus = AX3000_NUM_CPUS;
}

static const TypeInfo axiado_soc_types[] = {
    {
        .name           = TYPE_AX3000_SOC,
        .parent         = TYPE_SYS_BUS_DEVICE,
        .instance_size  = sizeof(Ax3000SoCState),
        .instance_init  = ax3000_init,
        .class_init     = ax3000_class_init,
    }
};

DEFINE_TYPES(axiado_soc_types)
