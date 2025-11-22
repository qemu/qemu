/*
 * Infineon TriCore IR (Interrupt Router) device model
 *
 * The Interrupt Router receives service requests from peripherals
 * and routes them to CPUs or DMA based on priority and configuration.
 *
 * Copyright (c) 2024
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/tricore/tc_ir.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "exec/cpu-interrupt.h"

/*
 * Raise an interrupt to the CPU
 * This is called when a peripheral signals an interrupt through the IR
 */
void tc_ir_set_irq(void *opaque, int n, int level)
{
    TcIrState *s = opaque;
    uint32_t src_reg;
    uint32_t priority;
    uint32_t tos;
    bool enabled;

    if (n >= TC_IR_MAX_SRC) {
        qemu_log_mask(LOG_GUEST_ERROR, "tc_ir: invalid IRQ number %d\n", n);
        return;
    }

    src_reg = s->src[n];
    enabled = (src_reg & SRC_SRE) != 0;
    priority = src_reg & SRC_SRPN_MASK;
    tos = (src_reg & SRC_TOS_MASK) >> SRC_TOS_SHIFT;

    if (level) {
        /* Set Service Request flag */
        s->src[n] |= SRC_SRR;

        /* If enabled and TOS is CPU0 (we only support single core for now) */
        if (enabled && tos == 0 && s->cpu) {
            CPUState *cs = CPU(s->cpu);
            CPUTriCoreState *env = &s->cpu->env;

            /* Set pending interrupt if higher priority */
            if (priority > env->pending_int_level) {
                env->pending_int_level = priority;
                env->pending_int_vector = n;  /* Use SRC index as vector */
            }
            cpu_interrupt(cs, CPU_INTERRUPT_HARD);
        }
    } else {
        /* Clear Service Request flag (also done by writing CLRR) */
        s->src[n] &= ~SRC_SRR;

        if (s->cpu) {
            CPUState *cs = CPU(s->cpu);
            CPUTriCoreState *env = &s->cpu->env;

            /* Clear pending interrupt if this was it */
            if (env->pending_int_vector == (uint32_t)n) {
                env->pending_int_level = 0;
                env->pending_int_vector = 0;
                cpu_reset_interrupt(cs, CPU_INTERRUPT_HARD);
            }
        }
    }
}

static uint64_t tc_ir_read(void *opaque, hwaddr offset, unsigned size)
{
    TcIrState *s = opaque;
    uint64_t val = 0;

    if (offset >= IR_SRC_BASE && offset < IR_SRC_BASE + TC_IR_MAX_SRC * 4) {
        /* SRC register read */
        int idx = (offset - IR_SRC_BASE) / 4;
        val = s->src[idx];
    } else {
        switch (offset) {
        case IR_OITRIGLVL:
            val = s->oitriglvl;
            break;
        case IR_OITRIGCNT:
            val = s->oitrigcnt;
            break;
        case IR_OITMISSLVL:
            val = s->oitmisslvl;
            break;
        case IR_OITMISSCNT:
            val = s->oitmisscnt;
            break;
        default:
            qemu_log_mask(LOG_UNIMP, "tc_ir: read from unknown register 0x%"
                          HWADDR_PRIx "\n", offset);
            break;
        }
    }

    return val;
}

static void tc_ir_write(void *opaque, hwaddr offset, uint64_t val,
                        unsigned size)
{
    TcIrState *s = opaque;

    if (offset >= IR_SRC_BASE && offset < IR_SRC_BASE + TC_IR_MAX_SRC * 4) {
        /* SRC register write */
        int idx = (offset - IR_SRC_BASE) / 4;
        uint32_t old_val = s->src[idx];
        uint32_t new_val = val;

        /* Handle CLRR (Clear Request) bit - write 1 to clear SRR */
        if (new_val & SRC_CLRR) {
            new_val &= ~(SRC_CLRR | SRC_SRR);
            old_val &= ~SRC_SRR;

            /* Also clear pending interrupt if this was it */
            if (s->cpu) {
                CPUTriCoreState *env = &s->cpu->env;
                if (env->pending_int_vector == (uint32_t)idx) {
                    env->pending_int_level = 0;
                    env->pending_int_vector = 0;
                    cpu_reset_interrupt(CPU(s->cpu), CPU_INTERRUPT_HARD);
                }
            }
        }

        /* Handle SETR (Set Request) bit - write 1 to set SRR */
        if (new_val & SRC_SETR) {
            new_val = (new_val & ~SRC_SETR) | SRC_SRR;
            /* Trigger interrupt */
            tc_ir_set_irq(s, idx, 1);
        }

        /* SRR is read-only (except cleared by CLRR) */
        new_val = (new_val & ~SRC_SRR) | (old_val & SRC_SRR);

        s->src[idx] = new_val;
    } else {
        switch (offset) {
        case IR_OITRIGLVL:
            s->oitriglvl = val;
            break;
        case IR_OITRIGCNT:
            s->oitrigcnt = val;
            break;
        case IR_OITMISSLVL:
            s->oitmisslvl = val;
            break;
        case IR_OITMISSCNT:
            s->oitmisscnt = val;
            break;
        default:
            qemu_log_mask(LOG_UNIMP, "tc_ir: write to unknown register 0x%"
                          HWADDR_PRIx "\n", offset);
            break;
        }
    }
}

static const MemoryRegionOps tc_ir_ops = {
    .read = tc_ir_read,
    .write = tc_ir_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void tc_ir_reset(DeviceState *dev)
{
    TcIrState *s = TC_IR(dev);
    int i;

    for (i = 0; i < TC_IR_MAX_SRC; i++) {
        s->src[i] = 0;
    }

    s->oitriglvl = 0;
    s->oitrigcnt = 0;
    s->oitmisslvl = 0;
    s->oitmisscnt = 0;
}

static void tc_ir_realize(DeviceState *dev, Error **errp)
{
    TcIrState *s = TC_IR(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    /* Memory region for SRC registers - allocate enough for all SRCs */
    memory_region_init_io(&s->iomem, OBJECT(dev), &tc_ir_ops, s,
                          "tc-ir", IR_SRC_BASE + TC_IR_MAX_SRC * 4);
    sysbus_init_mmio(sbd, &s->iomem);

    /* Create GPIO inputs for peripheral IRQ signals */
    qdev_init_gpio_in(dev, tc_ir_set_irq, TC_IR_MAX_SRC);
}

static Property tc_ir_properties[] = {
    DEFINE_PROP_LINK("cpu", TcIrState, cpu, TYPE_TRICORE_CPU, TriCoreCPU *),
    DEFINE_PROP_END_OF_LIST(),
};

static const VMStateDescription vmstate_tc_ir = {
    .name = "tc-ir",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(src, TcIrState, TC_IR_MAX_SRC),
        VMSTATE_UINT32(oitriglvl, TcIrState),
        VMSTATE_UINT32(oitrigcnt, TcIrState),
        VMSTATE_UINT32(oitmisslvl, TcIrState),
        VMSTATE_UINT32(oitmisscnt, TcIrState),
        VMSTATE_END_OF_LIST()
    }
};

static void tc_ir_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = tc_ir_realize;
    device_class_set_legacy_reset(dc, tc_ir_reset);
    dc->vmsd = &vmstate_tc_ir;
    device_class_set_props(dc, tc_ir_properties);
}

static const TypeInfo tc_ir_info = {
    .name = TYPE_TC_IR,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(TcIrState),
    .class_init = tc_ir_class_init,
};

static void tc_ir_register_types(void)
{
    type_register_static(&tc_ir_info);
}

type_init(tc_ir_register_types)
