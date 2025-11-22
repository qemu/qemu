/*
 * Infineon TriCore STM (System Timer Module) device model
 *
 * The STM is a 64-bit free-running up-counter designed for global
 * system timing applications. It provides:
 * - 64-bit counter with multiple 32-bit views (TIM0-TIM6)
 * - Two compare registers (CMP0, CMP1) for interrupt generation
 * - Configurable compare size and start bit position
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
#include "qemu/timer.h"
#include "hw/tricore/tc_stm.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"

#define STM_TIMER_PERIOD_NS 10000  /* Update every 10us */

static void tc_stm_update_counter(TcStmState *s)
{
    int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    static int64_t last_time = 0;

    if (last_time == 0) {
        last_time = now;
    }

    /* Update counter based on elapsed time and frequency */
    int64_t elapsed_ns = now - last_time;
    uint64_t ticks = (elapsed_ns * s->freq_hz) / 1000000000ULL;
    s->counter += ticks;
    last_time = now;
}

static void tc_stm_check_compare(TcStmState *s)
{
    uint32_t msize0, mstart0, msize1, mstart1;
    uint64_t mask0, mask1, cmp_val0, cmp_val1;
    uint64_t counter_masked0, counter_masked1;

    /* Get compare configuration from CMCON */
    msize0 = s->cmcon & STM_CMCON_MSIZE0_MASK;
    mstart0 = (s->cmcon & STM_CMCON_MSTART0_MASK) >> STM_CMCON_MSTART0_SHIFT;
    msize1 = (s->cmcon & STM_CMCON_MSIZE1_MASK) >> STM_CMCON_MSIZE1_SHIFT;
    mstart1 = (s->cmcon & STM_CMCON_MSTART1_MASK) >> STM_CMCON_MSTART1_SHIFT;

    /* Create masks based on msize (number of bits - 1) */
    mask0 = ((1ULL << (msize0 + 1)) - 1) << mstart0;
    mask1 = ((1ULL << (msize1 + 1)) - 1) << mstart1;

    /* Compare values positioned at the compare start bit */
    cmp_val0 = ((uint64_t)s->cmp0 << mstart0) & mask0;
    cmp_val1 = ((uint64_t)s->cmp1 << mstart1) & mask1;

    /* Counter values masked */
    counter_masked0 = s->counter & mask0;
    counter_masked1 = s->counter & mask1;

    /* Check Compare 0 */
    if ((s->icr & STM_ICR_CMP0EN) && (counter_masked0 == cmp_val0)) {
        s->icr |= STM_ICR_CMP0IR;  /* Set interrupt request flag */
        qemu_irq_raise(s->irq_cmp0);
    }

    /* Check Compare 1 */
    if ((s->icr & STM_ICR_CMP1EN) && (counter_masked1 == cmp_val1)) {
        s->icr |= STM_ICR_CMP1IR;  /* Set interrupt request flag */
        qemu_irq_raise(s->irq_cmp1);
    }
}

static void tc_stm_timer_tick(void *opaque)
{
    TcStmState *s = opaque;

    tc_stm_update_counter(s);
    tc_stm_check_compare(s);

    /* Reschedule timer */
    timer_mod(s->timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
              STM_TIMER_PERIOD_NS);
}

static uint64_t tc_stm_read(void *opaque, hwaddr offset, unsigned size)
{
    TcStmState *s = opaque;
    uint64_t val = 0;

    tc_stm_update_counter(s);

    switch (offset) {
    case STM_CLC:
        val = s->clc;
        break;
    case STM_ID:
        /* Module ID - fixed value for STM */
        val = 0x0000C000;  /* Module ID */
        break;
    case STM_TIM0:
        /* Bits 31:0, also latches high part to CAP */
        val = (uint32_t)(s->counter & 0xFFFFFFFF);
        s->cap = (uint32_t)(s->counter >> 32);
        break;
    case STM_TIM1:
        /* Bits 35:4 */
        val = (uint32_t)((s->counter >> 4) & 0xFFFFFFFF);
        s->cap = (uint32_t)(s->counter >> 32);
        break;
    case STM_TIM2:
        /* Bits 39:8 */
        val = (uint32_t)((s->counter >> 8) & 0xFFFFFFFF);
        s->cap = (uint32_t)(s->counter >> 32);
        break;
    case STM_TIM3:
        /* Bits 47:16 */
        val = (uint32_t)((s->counter >> 16) & 0xFFFFFFFF);
        s->cap = (uint32_t)(s->counter >> 32);
        break;
    case STM_TIM4:
        /* Bits 51:20 */
        val = (uint32_t)((s->counter >> 20) & 0xFFFFFFFF);
        s->cap = (uint32_t)(s->counter >> 32);
        break;
    case STM_TIM5:
        /* Bits 55:24 */
        val = (uint32_t)((s->counter >> 24) & 0xFFFFFFFF);
        s->cap = (uint32_t)(s->counter >> 32);
        break;
    case STM_TIM6:
        /* Bits 63:32 */
        val = (uint32_t)(s->counter >> 32);
        break;
    case STM_CAP:
        val = s->cap;
        break;
    case STM_CMP0:
        val = s->cmp0;
        break;
    case STM_CMP1:
        val = s->cmp1;
        break;
    case STM_CMCON:
        val = s->cmcon;
        break;
    case STM_ICR:
        val = s->icr;
        break;
    case STM_ISCR:
        val = 0;  /* Write-only register */
        break;
    case STM_OCS:
        val = s->ocs;
        break;
    case STM_ACCEN0:
        val = s->accen0;
        break;
    case STM_ACCEN1:
        val = s->accen1;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "tc_stm: read from unknown register 0x%"
                      HWADDR_PRIx "\n", offset);
        break;
    }

    return val;
}

static void tc_stm_write(void *opaque, hwaddr offset, uint64_t val,
                         unsigned size)
{
    TcStmState *s = opaque;

    switch (offset) {
    case STM_CLC:
        s->clc = val;
        break;
    case STM_CMP0:
        s->cmp0 = val;
        break;
    case STM_CMP1:
        s->cmp1 = val;
        break;
    case STM_CMCON:
        s->cmcon = val;
        break;
    case STM_ICR:
        /* Only enable bits are writable */
        s->icr = (s->icr & ~(STM_ICR_CMP0EN | STM_ICR_CMP0OS |
                             STM_ICR_CMP1EN | STM_ICR_CMP1OS)) |
                 (val & (STM_ICR_CMP0EN | STM_ICR_CMP0OS |
                         STM_ICR_CMP1EN | STM_ICR_CMP1OS));
        break;
    case STM_ISCR:
        /* Interrupt Set/Clear Register */
        if (val & STM_ISCR_CMP0IRR) {
            s->icr &= ~STM_ICR_CMP0IR;
            qemu_irq_lower(s->irq_cmp0);
        }
        if (val & STM_ISCR_CMP0IRS) {
            s->icr |= STM_ICR_CMP0IR;
            qemu_irq_raise(s->irq_cmp0);
        }
        if (val & STM_ISCR_CMP1IRR) {
            s->icr &= ~STM_ICR_CMP1IR;
            qemu_irq_lower(s->irq_cmp1);
        }
        if (val & STM_ISCR_CMP1IRS) {
            s->icr |= STM_ICR_CMP1IR;
            qemu_irq_raise(s->irq_cmp1);
        }
        break;
    case STM_OCS:
        s->ocs = val;
        break;
    case STM_ACCEN0:
        s->accen0 = val;
        break;
    case STM_ACCEN1:
        s->accen1 = val;
        break;
    case STM_ID:
    case STM_TIM0:
    case STM_TIM1:
    case STM_TIM2:
    case STM_TIM3:
    case STM_TIM4:
    case STM_TIM5:
    case STM_TIM6:
    case STM_CAP:
        /* Read-only registers */
        qemu_log_mask(LOG_GUEST_ERROR, "tc_stm: write to read-only register "
                      "0x%" HWADDR_PRIx "\n", offset);
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "tc_stm: write to unknown register 0x%"
                      HWADDR_PRIx "\n", offset);
        break;
    }
}

static const MemoryRegionOps tc_stm_ops = {
    .read = tc_stm_read,
    .write = tc_stm_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void tc_stm_reset(DeviceState *dev)
{
    TcStmState *s = TC_STM(dev);

    s->counter = 0;
    s->cap = 0;
    s->cmp0 = 0;
    s->cmp1 = 0;
    s->clc = 0;
    s->cmcon = 0;
    s->icr = 0;
    s->ocs = 0;
    s->accen0 = 0xFFFFFFFF;
    s->accen1 = 0xFFFFFFFF;

    /* Start the timer */
    timer_mod(s->timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
              STM_TIMER_PERIOD_NS);
}

static void tc_stm_realize(DeviceState *dev, Error **errp)
{
    TcStmState *s = TC_STM(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    memory_region_init_io(&s->iomem, OBJECT(dev), &tc_stm_ops, s,
                          "tc-stm", 0x100);
    sysbus_init_mmio(sbd, &s->iomem);

    /* Initialize IRQs for compare match interrupts */
    sysbus_init_irq(sbd, &s->irq_cmp0);
    sysbus_init_irq(sbd, &s->irq_cmp1);

    /* Create timer for periodic counter updates */
    s->timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, tc_stm_timer_tick, s);
}

static Property tc_stm_properties[] = {
    DEFINE_PROP_UINT32("freq-hz", TcStmState, freq_hz, 100000000), /* 100 MHz */
    DEFINE_PROP_END_OF_LIST(),
};

static const VMStateDescription vmstate_tc_stm = {
    .name = "tc-stm",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT64(counter, TcStmState),
        VMSTATE_UINT32(cap, TcStmState),
        VMSTATE_UINT32(cmp0, TcStmState),
        VMSTATE_UINT32(cmp1, TcStmState),
        VMSTATE_UINT32(clc, TcStmState),
        VMSTATE_UINT32(cmcon, TcStmState),
        VMSTATE_UINT32(icr, TcStmState),
        VMSTATE_UINT32(ocs, TcStmState),
        VMSTATE_UINT32(accen0, TcStmState),
        VMSTATE_UINT32(accen1, TcStmState),
        VMSTATE_END_OF_LIST()
    }
};

static void tc_stm_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = tc_stm_realize;
    device_class_set_legacy_reset(dc, tc_stm_reset);
    dc->vmsd = &vmstate_tc_stm;
    device_class_set_props(dc, tc_stm_properties);
}

static const TypeInfo tc_stm_info = {
    .name = TYPE_TC_STM,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(TcStmState),
    .class_init = tc_stm_class_init,
};

static void tc_stm_register_types(void)
{
    type_register_static(&tc_stm_info);
}

type_init(tc_stm_register_types)
