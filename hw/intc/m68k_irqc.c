/*
 * SPDX-License-Identifer: GPL-2.0-or-later
 *
 * QEMU Motorola 680x0 IRQ Controller
 *
 * (c) 2020 Laurent Vivier <laurent@vivier.eu>
 *
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "migration/vmstate.h"
#include "monitor/monitor.h"
#include "hw/nmi.h"
#include "hw/intc/intc.h"
#include "hw/intc/m68k_irqc.h"


static bool m68k_irqc_get_statistics(InterruptStatsProvider *obj,
                                     uint64_t **irq_counts, unsigned int *nb_irqs)
{
    M68KIRQCState *s = M68K_IRQC(obj);

    *irq_counts = s->stats_irq_count;
    *nb_irqs = ARRAY_SIZE(s->stats_irq_count);
    return true;
}

static void m68k_irqc_print_info(InterruptStatsProvider *obj, Monitor *mon)
{
    M68KIRQCState *s = M68K_IRQC(obj);
    monitor_printf(mon, "m68k-irqc: ipr=0x%x\n", s->ipr);
}

static void m68k_set_irq(void *opaque, int irq, int level)
{
    M68KIRQCState *s = opaque;
    M68kCPU *cpu = M68K_CPU(first_cpu);
    int i;

    if (level) {
        s->ipr |= 1 << irq;
        s->stats_irq_count[irq]++;
    } else {
        s->ipr &= ~(1 << irq);
    }

    for (i = M68K_IRQC_LEVEL_7; i >= M68K_IRQC_LEVEL_1; i--) {
        if ((s->ipr >> i) & 1) {
            m68k_set_irq_level(cpu, i + 1, i + M68K_IRQC_AUTOVECTOR_BASE);
            return;
        }
    }
    m68k_set_irq_level(cpu, 0, 0);
}

static void m68k_irqc_reset(DeviceState *d)
{
    M68KIRQCState *s = M68K_IRQC(d);
    int i;

    s->ipr = 0;
    for (i = 0; i < ARRAY_SIZE(s->stats_irq_count); i++) {
        s->stats_irq_count[i] = 0;
    }
}

static void m68k_irqc_instance_init(Object *obj)
{
    qdev_init_gpio_in(DEVICE(obj), m68k_set_irq, M68K_IRQC_LEVEL_NUM);
}

static void m68k_nmi(NMIState *n, int cpu_index, Error **errp)
{
    m68k_set_irq(n, M68K_IRQC_LEVEL_7, 1);
}

static const VMStateDescription vmstate_m68k_irqc = {
    .name = "m68k-irqc",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8(ipr, M68KIRQCState),
        VMSTATE_END_OF_LIST()
    }
};

static void m68k_irqc_class_init(ObjectClass *oc, void *data)
 {
    DeviceClass *dc = DEVICE_CLASS(oc);
    NMIClass *nc = NMI_CLASS(oc);
    InterruptStatsProviderClass *ic = INTERRUPT_STATS_PROVIDER_CLASS(oc);

    nc->nmi_monitor_handler = m68k_nmi;
    dc->reset = m68k_irqc_reset;
    dc->vmsd = &vmstate_m68k_irqc;
    ic->get_statistics = m68k_irqc_get_statistics;
    ic->print_info = m68k_irqc_print_info;
}

static const TypeInfo m68k_irqc_type_info = {
    .name = TYPE_M68K_IRQC,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(M68KIRQCState),
    .instance_init = m68k_irqc_instance_init,
    .class_init = m68k_irqc_class_init,
    .interfaces = (InterfaceInfo[]) {
         { TYPE_NMI },
         { TYPE_INTERRUPT_STATS_PROVIDER },
         { }
    },
};

static void q800_irq_register_types(void)
{
    type_register_static(&m68k_irqc_type_info);
}

type_init(q800_irq_register_types);
