/*
 * K230 Watchdog Compatible with kendryte K230 SDK
 *
 * Copyright (c) 2025 Mig Yang <temashking@foxmail.com>
 * Copyright (c) 2025 Chao Liu <chao.liu.zevorn@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Provides a board compatible with the kendryte K230 SDK
 *
 * K230 Technical Reference Manual V0.3.1 (2024-11-18):
 * https://github.com/revyos/external-docs/blob/master/K230/en-us/K230_Technical_Reference_Manual_V0.3.1_20241118.pdf
 *
 * For more information, see <https://www.kendryte.com/en/proDetail/230>
 */
#include "qemu/osdep.h"
#include "qemu/bitops.h"
#include "qemu/module.h"
#include "system/watchdog.h"
#include "migration/vmstate.h"
#include "hw/core/qdev-properties.h"
#include "hw/watchdog/k230_wdt.h"
#include "trace.h"

static void k230_wdt_timeout(void *opaque)
{
    K230WdtState *s = K230_WDT(opaque);

    trace_k230_wdt_timeout();

    /* Set interrupt status if in interrupt mode */
    if (s->cr & K230_WDT_CR_RMOD) {
        s->stat |= K230_WDT_STAT_INT;
        s->interrupt_pending = true;
        qemu_set_irq(s->irq, 1);
        trace_k230_wdt_interrupt();
    } else {
        /* Direct reset mode */
        trace_k230_wdt_reset();
        watchdog_perform_action();
    }

    /* Restart counter */
    s->current_count = s->timeout_value;
    ptimer_set_count(s->timer, s->current_count);
    ptimer_run(s->timer, 1);
}

static void k230_wdt_reset(DeviceState *dev)
{
    K230WdtState *s = K230_WDT(dev);

    trace_k230_wdt_reset_device();

    ptimer_transaction_begin(s->timer);
    ptimer_stop(s->timer);
    ptimer_transaction_commit(s->timer);

    /* Reset registers to default values */
    s->cr = 0;
    s->torr = 0;
    s->ccvr = 0xFFFFFFFF;
    s->stat = 0;
    s->prot_level = 0x2;

    s->interrupt_pending = false;
    s->enabled = false;
    s->timeout_value = 0;
    s->current_count = 0xFFFFFFFF;
}

static uint64_t k230_wdt_read(void *opaque, hwaddr addr, unsigned int size)
{
    K230WdtState *s = K230_WDT(opaque);
    uint32_t value = 0;

    switch (addr) {
    case K230_WDT_CR:
        value = s->cr;
        break;
    case K230_WDT_TORR:
        value = s->torr;
        break;
    case K230_WDT_CCVR:
        if (s->enabled) {
            value = ptimer_get_count(s->timer);
        } else {
            value = s->current_count;
        }
        break;
    case K230_WDT_STAT:
        value = s->stat;
        break;
    case K230_WDT_PROT_LEVEL:
        value = s->prot_level;
        break;
    case K230_WDT_COMP_PARAM_5:
        value = 0; /* Upper limit of Timeout Period parameters */
        break;
    case K230_WDT_COMP_PARAM_4:
        value = 0; /* Upper limit of Initial Timeout Period parameters */
        break;
    case K230_WDT_COMP_PARAM_3:
        value = 0; /* Derived from WDT_TOP_RST parameter */
        break;
    case K230_WDT_COMP_PARAM_2:
        value = 0xFFFFFFFF; /* Derived from WDT_RST_CNT parameter */
        break;
    case K230_WDT_COMP_PARAM_1:
        /* Component parameters */
        value = (32 << K230_WDT_CNT_WIDTH_SHIFT) |  /* 32-bit counter */
                (0 << K230_WDT_DFLT_TOP_INIT_SHIFT) |
                (0 << K230_WDT_DFLT_TOP_SHIFT) |
                (K230_WDT_RPL_16_CYCLES << K230_WDT_DFLT_RPL_SHIFT) |
                (2 << K230_WDT_APB_DATA_WIDTH_SHIFT) | /* 32-bit APB */
                K230_WDT_USE_FIX_TOP; /* Use fixed timeout values */
        break;
    case K230_WDT_COMP_VERSION:
        value = K230_WDT_COMP_VERSION_VAL;
        break;
    case K230_WDT_COMP_TYPE:
        value = K230_WDT_COMP_TYPE_VAL;
        break;
    default:
        /* Other registers return 0 */
        break;
    }

    trace_k230_wdt_read(addr, value);
    return value;
}

static void k230_wdt_update_timer(K230WdtState *s)
{
    ptimer_transaction_begin(s->timer);

    if (s->enabled && s->timeout_value > 0) {
        ptimer_set_count(s->timer, s->current_count);
        ptimer_run(s->timer, 1);
    } else {
        ptimer_stop(s->timer);
    }

    ptimer_transaction_commit(s->timer);
}

static uint32_t k230_wdt_calculate_timeout(uint32_t top_value)
{
    /* Calculate timeout based on TOP value */
    /* For fixed timeout mode: 2^(16 + top_value) */
    if (top_value <= 15) {
        return 1 << (16 + top_value);
    }
    return 1 << 31; /* Maximum value for 32-bit counter */
}

static void k230_wdt_write(void *opaque, hwaddr addr,
                           uint64_t value, unsigned int size)
{
    K230WdtState *s = K230_WDT(opaque);

    trace_k230_wdt_write(addr, value);

    switch (addr) {
    case K230_WDT_CR:
        s->cr = value & (K230_WDT_CR_RPL_MASK << K230_WDT_CR_RPL_SHIFT |
                         K230_WDT_CR_RMOD | K230_WDT_CR_WDT_EN);

        /* Update enabled state */
        s->enabled = (s->cr & K230_WDT_CR_WDT_EN) != 0;

        /* Update timer */
        k230_wdt_update_timer(s);
        break;

    case K230_WDT_TORR:
        s->torr = value & K230_WDT_TORR_TOP_MASK;

        /* Calculate new timeout value */
        s->timeout_value = k230_wdt_calculate_timeout(s->torr);
        s->current_count = s->timeout_value;

        /* Update timer if enabled */
        if (s->enabled) {
            k230_wdt_update_timer(s);
        }
        break;

    case K230_WDT_CRR:
        /* Restart counter with magic value 0x76 */
        if ((value & 0xFF) == K230_WDT_CRR_RESTART) {
            trace_k230_wdt_restart();
            s->current_count = s->timeout_value;

            /* Clear interrupt if pending */
            if (s->interrupt_pending) {
                s->stat &= ~K230_WDT_STAT_INT;
                s->interrupt_pending = false;
                qemu_set_irq(s->irq, 0);
            }

            /* Update timer */
            k230_wdt_update_timer(s);
        }
        break;

    case K230_WDT_EOI:
        /* Clear interrupt */
        s->stat &= ~K230_WDT_STAT_INT;
        s->interrupt_pending = false;
        qemu_set_irq(s->irq, 0);
        break;

    case K230_WDT_PROT_LEVEL:
        s->prot_level = value & 0x7;
        break;

    default:
        /* Read-only registers, ignore writes */
        break;
    }
}

static const MemoryRegionOps k230_wdt_ops = {
    .read  = k230_wdt_read,
    .write = k230_wdt_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false,
    },
};

static const VMStateDescription vmstate_k230_wdt = {
    .name = "k230.wdt",
    .fields = (const VMStateField[]) {
        VMSTATE_PTIMER(timer, K230WdtState),
        VMSTATE_UINT32(cr, K230WdtState),
        VMSTATE_UINT32(torr, K230WdtState),
        VMSTATE_UINT32(ccvr, K230WdtState),
        VMSTATE_UINT32(stat, K230WdtState),
        VMSTATE_UINT32(prot_level, K230WdtState),
        VMSTATE_BOOL(interrupt_pending, K230WdtState),
        VMSTATE_BOOL(enabled, K230WdtState),
        VMSTATE_UINT32(timeout_value, K230WdtState),
        VMSTATE_UINT32(current_count, K230WdtState),
        VMSTATE_END_OF_LIST()
    }
};

static void k230_wdt_realize(DeviceState *dev, Error **errp)
{
    K230WdtState *s = K230_WDT(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    memory_region_init_io(&s->mmio, OBJECT(dev),
                          &k230_wdt_ops, s,
                          TYPE_K230_WDT,
                          K230_WDT_MMIO_SIZE);
    sysbus_init_mmio(sbd, &s->mmio);
    sysbus_init_irq(sbd, &s->irq);

    s->timer = ptimer_init(k230_wdt_timeout, s,
                           PTIMER_POLICY_NO_IMMEDIATE_TRIGGER |
                           PTIMER_POLICY_NO_IMMEDIATE_RELOAD |
                           PTIMER_POLICY_NO_COUNTER_ROUND_DOWN);

    ptimer_transaction_begin(s->timer);
    ptimer_set_freq(s->timer, K230_WDT_DEFAULT_FREQ);
    ptimer_transaction_commit(s->timer);
}

static void k230_wdt_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = k230_wdt_realize;
    device_class_set_legacy_reset(dc, k230_wdt_reset);
    dc->vmsd = &vmstate_k230_wdt;
    dc->desc = "K230 watchdog timer";
    set_bit(DEVICE_CATEGORY_WATCHDOG, dc->categories);
}

static const TypeInfo k230_wdt_info = {
    .name          = TYPE_K230_WDT,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(K230WdtState),
    .class_init    = k230_wdt_class_init,
};

static void k230_wdt_register_type(void)
{
    type_register_static(&k230_wdt_info);
}
type_init(k230_wdt_register_type)
