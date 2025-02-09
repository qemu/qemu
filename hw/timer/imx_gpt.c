/*
 * IMX GPT Timer
 *
 * Copyright (c) 2008 OK Labs
 * Copyright (c) 2011 NICTA Pty Ltd
 * Originally written by Hans Jiang
 * Updated by Peter Chubb
 * Updated by Jean-Christophe Dubois <jcd@tribudubois.net>
 *
 * This code is licensed under GPL version 2 or later.  See
 * the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "hw/irq.h"
#include "hw/timer/imx_gpt.h"
#include "migration/vmstate.h"
#include "qemu/module.h"
#include "qemu/log.h"
#include "trace.h"

static const char *imx_gpt_reg_name(uint32_t reg)
{
    switch (reg) {
    case 0:
        return "CR";
    case 1:
        return "PR";
    case 2:
        return "SR";
    case 3:
        return "IR";
    case 4:
        return "OCR1";
    case 5:
        return "OCR2";
    case 6:
        return "OCR3";
    case 7:
        return "ICR1";
    case 8:
        return "ICR2";
    case 9:
        return "CNT";
    default:
        return "[?]";
    }
}

static const VMStateDescription vmstate_imx_timer_gpt = {
    .name = TYPE_IMX_GPT,
    .version_id = 3,
    .minimum_version_id = 3,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(cr, IMXGPTState),
        VMSTATE_UINT32(pr, IMXGPTState),
        VMSTATE_UINT32(sr, IMXGPTState),
        VMSTATE_UINT32(ir, IMXGPTState),
        VMSTATE_UINT32(ocr1, IMXGPTState),
        VMSTATE_UINT32(ocr2, IMXGPTState),
        VMSTATE_UINT32(ocr3, IMXGPTState),
        VMSTATE_UINT32(icr1, IMXGPTState),
        VMSTATE_UINT32(icr2, IMXGPTState),
        VMSTATE_UINT32(cnt, IMXGPTState),
        VMSTATE_UINT32(next_timeout, IMXGPTState),
        VMSTATE_UINT32(next_int, IMXGPTState),
        VMSTATE_UINT32(freq, IMXGPTState),
        VMSTATE_PTIMER(timer, IMXGPTState),
        VMSTATE_END_OF_LIST()
    }
};

static const IMXClk imx25_gpt_clocks[] = {
    CLK_NONE,      /* 000 No clock source */
    CLK_IPG,       /* 001 ipg_clk, 532MHz*/
    CLK_IPG_HIGH,  /* 010 ipg_clk_highfreq */
    CLK_NONE,      /* 011 not defined */
    CLK_32k,       /* 100 ipg_clk_32k */
    CLK_32k,       /* 101 ipg_clk_32k */
    CLK_32k,       /* 110 ipg_clk_32k */
    CLK_32k,       /* 111 ipg_clk_32k */
};

static const IMXClk imx31_gpt_clocks[] = {
    CLK_NONE,      /* 000 No clock source */
    CLK_IPG,       /* 001 ipg_clk, 532MHz*/
    CLK_IPG_HIGH,  /* 010 ipg_clk_highfreq */
    CLK_NONE,      /* 011 not defined */
    CLK_32k,       /* 100 ipg_clk_32k */
    CLK_NONE,      /* 101 not defined */
    CLK_NONE,      /* 110 not defined */
    CLK_NONE,      /* 111 not defined */
};

static const IMXClk imx6_gpt_clocks[] = {
    CLK_NONE,      /* 000 No clock source */
    CLK_IPG,       /* 001 ipg_clk, 532MHz*/
    CLK_IPG_HIGH,  /* 010 ipg_clk_highfreq */
    CLK_EXT,       /* 011 External clock */
    CLK_32k,       /* 100 ipg_clk_32k */
    CLK_HIGH_DIV,  /* 101 reference clock / 8 */
    CLK_NONE,      /* 110 not defined */
    CLK_HIGH,      /* 111 reference clock */
};

static const IMXClk imx6ul_gpt_clocks[] = {
    CLK_NONE,      /* 000 No clock source */
    CLK_IPG,       /* 001 ipg_clk, 532MHz*/
    CLK_IPG_HIGH,  /* 010 ipg_clk_highfreq */
    CLK_EXT,       /* 011 External clock */
    CLK_32k,       /* 100 ipg_clk_32k */
    CLK_NONE,      /* 101 not defined */
    CLK_NONE,      /* 110 not defined */
    CLK_NONE,      /* 111 not defined */
};

static const IMXClk imx7_gpt_clocks[] = {
    CLK_NONE,      /* 000 No clock source */
    CLK_IPG,       /* 001 ipg_clk, 532MHz*/
    CLK_IPG_HIGH,  /* 010 ipg_clk_highfreq */
    CLK_EXT,       /* 011 External clock */
    CLK_32k,       /* 100 ipg_clk_32k */
    CLK_HIGH,      /* 101 reference clock */
    CLK_NONE,      /* 110 not defined */
    CLK_NONE,      /* 111 not defined */
};

static const IMXClk imx8mp_gpt_clocks[] = {
    CLK_NONE,      /* 000 No clock source */
    CLK_IPG,       /* 001 ipg_clk, 532MHz */
    CLK_IPG_HIGH,  /* 010 ipg_clk_highfreq */
    CLK_EXT,       /* 011 External clock */
    CLK_32k,       /* 100 ipg_clk_32k */
    CLK_HIGH,      /* 101 ipg_clk_16M */
    CLK_NONE,      /* 110 not defined */
    CLK_NONE,      /* 111 not defined */
};

/* Must be called from within ptimer_transaction_begin/commit block */
static void imx_gpt_set_freq(IMXGPTState *s)
{
    uint32_t clksrc = extract32(s->cr, GPT_CR_CLKSRC_SHIFT, 3);

    s->freq = imx_ccm_get_clock_frequency(s->ccm,
                                          s->clocks[clksrc]) / (1 + s->pr);

    trace_imx_gpt_set_freq(clksrc, s->freq);

    if (s->freq) {
        ptimer_set_freq(s->timer, s->freq);
    }
}

static void imx_gpt_update_int(IMXGPTState *s)
{
    if ((s->sr & s->ir) && (s->cr & GPT_CR_EN)) {
        qemu_irq_raise(s->irq);
    } else {
        qemu_irq_lower(s->irq);
    }
}

static uint32_t imx_gpt_update_count(IMXGPTState *s)
{
    s->cnt = s->next_timeout - (uint32_t)ptimer_get_count(s->timer);

    return s->cnt;
}

static inline uint32_t imx_gpt_find_limit(uint32_t count, uint32_t reg,
                                          uint32_t timeout)
{
    if ((count < reg) && (timeout > reg)) {
        timeout = reg;
    }

    return timeout;
}

/* Must be called from within ptimer_transaction_begin/commit block */
static void imx_gpt_compute_next_timeout(IMXGPTState *s, bool event)
{
    uint32_t timeout = GPT_TIMER_MAX;
    uint32_t count;
    long long limit;

    if (!(s->cr & GPT_CR_EN)) {
        /* if not enabled just return */
        return;
    }

    /* update the count */
    count = imx_gpt_update_count(s);

    if (event) {
        /*
         * This is an event (the ptimer reached 0 and stopped), and the
         * timer counter is now equal to s->next_timeout.
         */
        if (!(s->cr & GPT_CR_FRR) && (count == s->ocr1)) {
            /* We are in restart mode and we crossed the compare channel 1
             * value. We need to reset the counter to 0.
             */
            count = s->cnt = s->next_timeout = 0;
        } else if (count == GPT_TIMER_MAX) {
            /* We reached GPT_TIMER_MAX so we need to rollover */
            count = s->cnt = s->next_timeout = 0;
        }
    }

    /* now, find the next timeout related to count */

    if (s->ir & GPT_IR_OF1IE) {
        timeout = imx_gpt_find_limit(count, s->ocr1, timeout);
    }
    if (s->ir & GPT_IR_OF2IE) {
        timeout = imx_gpt_find_limit(count, s->ocr2, timeout);
    }
    if (s->ir & GPT_IR_OF3IE) {
        timeout = imx_gpt_find_limit(count, s->ocr3, timeout);
    }

    /* find the next set of interrupts to raise for next timer event */

    s->next_int = 0;
    if ((s->ir & GPT_IR_OF1IE) && (timeout == s->ocr1)) {
        s->next_int |= GPT_SR_OF1;
    }
    if ((s->ir & GPT_IR_OF2IE) && (timeout == s->ocr2)) {
        s->next_int |= GPT_SR_OF2;
    }
    if ((s->ir & GPT_IR_OF3IE) && (timeout == s->ocr3)) {
        s->next_int |= GPT_SR_OF3;
    }
    if ((s->ir & GPT_IR_ROVIE) && (timeout == GPT_TIMER_MAX)) {
        s->next_int |= GPT_SR_ROV;
    }

    /* the new range to count down from */
    limit = timeout - imx_gpt_update_count(s);

    if (limit < 0) {
        /*
         * if we reach here, then QEMU is running too slow and we pass the
         * timeout limit while computing it. Let's deliver the interrupt
         * and compute a new limit.
         */
        s->sr |= s->next_int;

        imx_gpt_compute_next_timeout(s, event);

        imx_gpt_update_int(s);
    } else {
        /* New timeout value */
        s->next_timeout = timeout;

        /* reset the limit to the computed range */
        ptimer_set_limit(s->timer, limit, 1);
    }
}

static uint64_t imx_gpt_read(void *opaque, hwaddr offset, unsigned size)
{
    IMXGPTState *s = IMX_GPT(opaque);
    uint32_t reg_value = 0;

    switch (offset >> 2) {
    case 0: /* Control Register */
        reg_value = s->cr;
        break;

    case 1: /* prescaler */
        reg_value = s->pr;
        break;

    case 2: /* Status Register */
        reg_value = s->sr;
        break;

    case 3: /* Interrupt Register */
        reg_value = s->ir;
        break;

    case 4: /* Output Compare Register 1 */
        reg_value = s->ocr1;
        break;

    case 5: /* Output Compare Register 2 */
        reg_value = s->ocr2;
        break;

    case 6: /* Output Compare Register 3 */
        reg_value = s->ocr3;
        break;

    case 7: /* input Capture Register 1 */
        qemu_log_mask(LOG_UNIMP, "[%s]%s: icr1 feature is not implemented\n",
                      TYPE_IMX_GPT, __func__);
        reg_value = s->icr1;
        break;

    case 8: /* input Capture Register 2 */
        qemu_log_mask(LOG_UNIMP, "[%s]%s: icr2 feature is not implemented\n",
                      TYPE_IMX_GPT, __func__);
        reg_value = s->icr2;
        break;

    case 9: /* cnt */
        imx_gpt_update_count(s);
        reg_value = s->cnt;
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR, "[%s]%s: Bad register at offset 0x%"
                      HWADDR_PRIx "\n", TYPE_IMX_GPT, __func__, offset);
        break;
    }

    trace_imx_gpt_read(imx_gpt_reg_name(offset >> 2), reg_value);

    return reg_value;
}


static void imx_gpt_reset_common(IMXGPTState *s, bool is_soft_reset)
{
    ptimer_transaction_begin(s->timer);
    /* stop timer */
    ptimer_stop(s->timer);

    /* Soft reset and hard reset differ only in their handling of the CR
     * register -- soft reset preserves the values of some bits there.
     */
    if (is_soft_reset) {
        /* Clear all CR bits except those that are preserved by soft reset. */
        s->cr &= GPT_CR_EN | GPT_CR_ENMOD | GPT_CR_STOPEN | GPT_CR_DOZEN |
            GPT_CR_WAITEN | GPT_CR_DBGEN |
            (GPT_CR_CLKSRC_MASK << GPT_CR_CLKSRC_SHIFT);
    } else {
        s->cr = 0;
    }
    s->sr = 0;
    s->pr = 0;
    s->ir = 0;
    s->cnt = 0;
    s->ocr1 = GPT_TIMER_MAX;
    s->ocr2 = GPT_TIMER_MAX;
    s->ocr3 = GPT_TIMER_MAX;
    s->icr1 = 0;
    s->icr2 = 0;

    s->next_timeout = GPT_TIMER_MAX;
    s->next_int = 0;

    /* compute new freq */
    imx_gpt_set_freq(s);

    /* reset the limit to GPT_TIMER_MAX */
    ptimer_set_limit(s->timer, GPT_TIMER_MAX, 1);

    /* if the timer is still enabled, restart it */
    if (s->freq && (s->cr & GPT_CR_EN)) {
        ptimer_run(s->timer, 1);
    }
    ptimer_transaction_commit(s->timer);
}

static void imx_gpt_soft_reset(DeviceState *dev)
{
    IMXGPTState *s = IMX_GPT(dev);
    imx_gpt_reset_common(s, true);
}

static void imx_gpt_reset(DeviceState *dev)
{
    IMXGPTState *s = IMX_GPT(dev);
    imx_gpt_reset_common(s, false);
}

static void imx_gpt_write(void *opaque, hwaddr offset, uint64_t value,
                          unsigned size)
{
    IMXGPTState *s = IMX_GPT(opaque);
    uint32_t oldreg;

    trace_imx_gpt_write(imx_gpt_reg_name(offset >> 2), (uint32_t)value);

    switch (offset >> 2) {
    case 0:
        oldreg = s->cr;
        s->cr = value & ~0x7c14;
        if (s->cr & GPT_CR_SWR) { /* force reset */
            /* handle the reset */
            imx_gpt_soft_reset(DEVICE(s));
        } else {
            /* set our freq, as the source might have changed */
            ptimer_transaction_begin(s->timer);
            imx_gpt_set_freq(s);

            if ((oldreg ^ s->cr) & GPT_CR_EN) {
                if (s->cr & GPT_CR_EN) {
                    if (s->cr & GPT_CR_ENMOD) {
                        s->next_timeout = GPT_TIMER_MAX;
                        ptimer_set_count(s->timer, GPT_TIMER_MAX);
                        imx_gpt_compute_next_timeout(s, false);
                    }
                    ptimer_run(s->timer, 1);
                } else {
                    /* stop timer */
                    ptimer_stop(s->timer);
                }
            }
            ptimer_transaction_commit(s->timer);
        }
        break;

    case 1: /* Prescaler */
        s->pr = value & 0xfff;
        ptimer_transaction_begin(s->timer);
        imx_gpt_set_freq(s);
        ptimer_transaction_commit(s->timer);
        break;

    case 2: /* SR */
        s->sr &= ~(value & 0x3f);
        imx_gpt_update_int(s);
        break;

    case 3: /* IR -- interrupt register */
        s->ir = value & 0x3f;
        imx_gpt_update_int(s);

        ptimer_transaction_begin(s->timer);
        imx_gpt_compute_next_timeout(s, false);
        ptimer_transaction_commit(s->timer);

        break;

    case 4: /* OCR1 -- output compare register */
        s->ocr1 = value;

        ptimer_transaction_begin(s->timer);
        /* In non-freerun mode, reset count when this register is written */
        if (!(s->cr & GPT_CR_FRR)) {
            s->next_timeout = GPT_TIMER_MAX;
            ptimer_set_limit(s->timer, GPT_TIMER_MAX, 1);
        }

        /* compute the new timeout */
        imx_gpt_compute_next_timeout(s, false);
        ptimer_transaction_commit(s->timer);

        break;

    case 5: /* OCR2 -- output compare register */
        s->ocr2 = value;

        /* compute the new timeout */
        ptimer_transaction_begin(s->timer);
        imx_gpt_compute_next_timeout(s, false);
        ptimer_transaction_commit(s->timer);

        break;

    case 6: /* OCR3 -- output compare register */
        s->ocr3 = value;

        /* compute the new timeout */
        ptimer_transaction_begin(s->timer);
        imx_gpt_compute_next_timeout(s, false);
        ptimer_transaction_commit(s->timer);

        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR, "[%s]%s: Bad register at offset 0x%"
                      HWADDR_PRIx "\n", TYPE_IMX_GPT, __func__, offset);
        break;
    }
}

static void imx_gpt_timeout(void *opaque)
{
    IMXGPTState *s = IMX_GPT(opaque);

    trace_imx_gpt_timeout();

    s->sr |= s->next_int;
    s->next_int = 0;

    imx_gpt_compute_next_timeout(s, true);

    imx_gpt_update_int(s);

    if (s->freq && (s->cr & GPT_CR_EN)) {
        ptimer_run(s->timer, 1);
    }
}

static const MemoryRegionOps imx_gpt_ops = {
    .read = imx_gpt_read,
    .write = imx_gpt_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};


static void imx_gpt_realize(DeviceState *dev, Error **errp)
{
    IMXGPTState *s = IMX_GPT(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    sysbus_init_irq(sbd, &s->irq);
    memory_region_init_io(&s->iomem, OBJECT(s), &imx_gpt_ops, s, TYPE_IMX_GPT,
                          0x00001000);
    sysbus_init_mmio(sbd, &s->iomem);

    s->timer = ptimer_init(imx_gpt_timeout, s, PTIMER_POLICY_LEGACY);
}

static void imx_gpt_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = imx_gpt_realize;
    device_class_set_legacy_reset(dc, imx_gpt_reset);
    dc->vmsd = &vmstate_imx_timer_gpt;
    dc->desc = "i.MX general timer";
}

static void imx25_gpt_init(Object *obj)
{
    IMXGPTState *s = IMX_GPT(obj);

    s->clocks = imx25_gpt_clocks;
}

static void imx31_gpt_init(Object *obj)
{
    IMXGPTState *s = IMX_GPT(obj);

    s->clocks = imx31_gpt_clocks;
}

static void imx6_gpt_init(Object *obj)
{
    IMXGPTState *s = IMX_GPT(obj);

    s->clocks = imx6_gpt_clocks;
}

static void imx6ul_gpt_init(Object *obj)
{
    IMXGPTState *s = IMX_GPT(obj);

    s->clocks = imx6ul_gpt_clocks;
}

static void imx7_gpt_init(Object *obj)
{
    IMXGPTState *s = IMX_GPT(obj);

    s->clocks = imx7_gpt_clocks;
}

static void imx8mp_gpt_init(Object *obj)
{
    IMXGPTState *s = IMX_GPT(obj);

    s->clocks = imx8mp_gpt_clocks;
}

static const TypeInfo imx25_gpt_info = {
    .name = TYPE_IMX25_GPT,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(IMXGPTState),
    .instance_init = imx25_gpt_init,
    .class_init = imx_gpt_class_init,
};

static const TypeInfo imx31_gpt_info = {
    .name = TYPE_IMX31_GPT,
    .parent = TYPE_IMX25_GPT,
    .instance_init = imx31_gpt_init,
};

static const TypeInfo imx6_gpt_info = {
    .name = TYPE_IMX6_GPT,
    .parent = TYPE_IMX25_GPT,
    .instance_init = imx6_gpt_init,
};

static const TypeInfo imx6ul_gpt_info = {
    .name = TYPE_IMX6UL_GPT,
    .parent = TYPE_IMX25_GPT,
    .instance_init = imx6ul_gpt_init,
};

static const TypeInfo imx7_gpt_info = {
    .name = TYPE_IMX7_GPT,
    .parent = TYPE_IMX25_GPT,
    .instance_init = imx7_gpt_init,
};

static const TypeInfo imx8mp_gpt_info = {
    .name = TYPE_IMX8MP_GPT,
    .parent = TYPE_IMX25_GPT,
    .instance_init = imx8mp_gpt_init,
};

static void imx_gpt_register_types(void)
{
    type_register_static(&imx25_gpt_info);
    type_register_static(&imx31_gpt_info);
    type_register_static(&imx6_gpt_info);
    type_register_static(&imx6ul_gpt_info);
    type_register_static(&imx7_gpt_info);
    type_register_static(&imx8mp_gpt_info);
}

type_init(imx_gpt_register_types)
