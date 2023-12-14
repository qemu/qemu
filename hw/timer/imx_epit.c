/*
 * IMX EPIT Timer
 *
 * Copyright (c) 2008 OK Labs
 * Copyright (c) 2011 NICTA Pty Ltd
 * Originally written by Hans Jiang
 * Updated by Peter Chubb
 * Updated by Jean-Christophe Dubois <jcd@tribudubois.net>
 * Updated by Axel Heider
 *
 * This code is licensed under GPL version 2 or later.  See
 * the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "hw/timer/imx_epit.h"
#include "migration/vmstate.h"
#include "hw/irq.h"
#include "hw/misc/imx_ccm.h"
#include "qemu/module.h"
#include "qemu/log.h"

#ifndef DEBUG_IMX_EPIT
#define DEBUG_IMX_EPIT 0
#endif

#define DPRINTF(fmt, args...) \
    do { \
        if (DEBUG_IMX_EPIT) { \
            fprintf(stderr, "[%s]%s: " fmt , TYPE_IMX_EPIT, \
                                             __func__, ##args); \
        } \
    } while (0)

static const char *imx_epit_reg_name(uint32_t reg)
{
    switch (reg) {
    case 0:
        return "CR";
    case 1:
        return "SR";
    case 2:
        return "LR";
    case 3:
        return "CMP";
    case 4:
        return "CNT";
    default:
        return "[?]";
    }
}

/*
 * Exact clock frequencies vary from board to board.
 * These are typical.
 */
static const IMXClk imx_epit_clocks[] =  {
    CLK_NONE,      /* 00 disabled */
    CLK_IPG,       /* 01 ipg_clk, ~532MHz */
    CLK_IPG_HIGH,  /* 10 ipg_clk_highfreq */
    CLK_32k,       /* 11 ipg_clk_32k -- ~32kHz */
};

/*
 * Update interrupt status
 */
static void imx_epit_update_int(IMXEPITState *s)
{
    if ((s->sr & SR_OCIF) && (s->cr & CR_OCIEN) && (s->cr & CR_EN)) {
        qemu_irq_raise(s->irq);
    } else {
        qemu_irq_lower(s->irq);
    }
}

static uint32_t imx_epit_get_freq(IMXEPITState *s)
{
    uint32_t clksrc = extract32(s->cr, CR_CLKSRC_SHIFT, CR_CLKSRC_BITS);
    uint32_t prescaler = 1 + extract32(s->cr, CR_PRESCALE_SHIFT, CR_PRESCALE_BITS);
    uint32_t f_in = imx_ccm_get_clock_frequency(s->ccm, imx_epit_clocks[clksrc]);
    uint32_t freq = f_in / prescaler;
    DPRINTF("ptimer frequency is %u\n", freq);
    return freq;
}

/*
 * This is called both on hardware (device) reset and software reset.
 */
static void imx_epit_reset(IMXEPITState *s, bool is_hard_reset)
{
    /* Soft reset doesn't touch some bits; hard reset clears them */
    if (is_hard_reset) {
        s->cr = 0;
    } else {
        s->cr &= (CR_EN|CR_ENMOD|CR_STOPEN|CR_DOZEN|CR_WAITEN|CR_DBGEN);
    }
    s->sr = 0;
    s->lr = EPIT_TIMER_MAX;
    s->cmp = 0;
    ptimer_transaction_begin(s->timer_cmp);
    ptimer_transaction_begin(s->timer_reload);

    /*
     * The reset switches off the input clock, so even if the CR.EN is still
     * set, the timers are no longer running.
     */
    assert(imx_epit_get_freq(s) == 0);
    ptimer_stop(s->timer_cmp);
    ptimer_stop(s->timer_reload);
    /* init both timers to EPIT_TIMER_MAX */
    ptimer_set_limit(s->timer_cmp, EPIT_TIMER_MAX, 1);
    ptimer_set_limit(s->timer_reload, EPIT_TIMER_MAX, 1);
    ptimer_transaction_commit(s->timer_cmp);
    ptimer_transaction_commit(s->timer_reload);
}

static uint64_t imx_epit_read(void *opaque, hwaddr offset, unsigned size)
{
    IMXEPITState *s = IMX_EPIT(opaque);
    uint32_t reg_value = 0;

    switch (offset >> 2) {
    case 0: /* Control Register */
        reg_value = s->cr;
        break;

    case 1: /* Status Register */
        reg_value = s->sr;
        break;

    case 2: /* LR - ticks*/
        reg_value = s->lr;
        break;

    case 3: /* CMP */
        reg_value = s->cmp;
        break;

    case 4: /* CNT */
        reg_value = ptimer_get_count(s->timer_reload);
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR, "[%s]%s: Bad register at offset 0x%"
                      HWADDR_PRIx "\n", TYPE_IMX_EPIT, __func__, offset);
        break;
    }

    DPRINTF("(%s) = 0x%08x\n", imx_epit_reg_name(offset >> 2), reg_value);

    return reg_value;
}

/*
 * Must be called from a ptimer_transaction_begin/commit block for
 * s->timer_cmp, but outside of a transaction block of s->timer_reload,
 * so the proper counter value is read.
 */
static void imx_epit_update_compare_timer(IMXEPITState *s)
{
    uint64_t counter = 0;
    bool is_oneshot = false;
    /*
     * The compare timer only has to run if the timer peripheral is active
     * and there is an input clock, Otherwise it can be switched off.
     */
    bool is_active = (s->cr & CR_EN) && imx_epit_get_freq(s);
    if (is_active) {
        /*
         * Calculate next timeout for compare timer. Reading the reload
         * counter returns proper results only if pending transactions
         * on it are committed here. Otherwise stale values are be read.
         */
        counter = ptimer_get_count(s->timer_reload);
        uint64_t limit = ptimer_get_limit(s->timer_cmp);
        /*
         * The compare timer is a periodic timer if the limit is at least
         * the compare value. Otherwise it may fire at most once in the
         * current round.
         */
        is_oneshot = (limit < s->cmp);
        if (counter >= s->cmp) {
            /* The compare timer fires in the current round. */
            counter -= s->cmp;
        } else if (!is_oneshot) {
            /*
             * The compare timer fires after a reload, as it is below the
             * compare value already in this round. Note that the counter
             * value calculated below can be above the 32-bit limit, which
             * is legal here because the compare timer is an internal
             * helper ptimer only.
             */
            counter += limit - s->cmp;
        } else {
            /*
             * The compare timer won't fire in this round, and the limit is
             * set to a value below the compare value. This practically means
             * it will never fire, so it can be switched off.
             */
            is_active = false;
        }
    }

    /*
     * Set the compare timer and let it run, or stop it. This is agnostic
     * of CR.OCIEN bit, as this bit affects interrupt generation only. The
     * compare timer needs to run even if no interrupts are to be generated,
     * because the SR.OCIF bit must be updated also.
     * Note that the timer might already be stopped or be running with
     * counter values. However, finding out when an update is needed and
     * when not is not trivial. It's much easier applying the setting again,
     * as this does not harm either and the overhead is negligible.
     */
    if (is_active) {
        ptimer_set_count(s->timer_cmp, counter);
        ptimer_run(s->timer_cmp, is_oneshot ? 1 : 0);
    } else {
        ptimer_stop(s->timer_cmp);
    }

}

static void imx_epit_write_cr(IMXEPITState *s, uint32_t value)
{
    uint32_t oldcr = s->cr;

    s->cr = value & 0x03ffffff;

    if (s->cr & CR_SWR) {
        /*
         * Reset clears CR.SWR again. It does not touch CR.EN, but the timers
         * are still stopped because the input clock is disabled.
         */
        imx_epit_reset(s, false);
    } else {
        uint32_t freq;
        uint32_t toggled_cr_bits = oldcr ^ s->cr;
        /* re-initialize the limits if CR.RLD has changed */
        bool set_limit = toggled_cr_bits & CR_RLD;
        /* set the counter if the timer got just enabled and CR.ENMOD is set */
        bool is_switched_on = (toggled_cr_bits & s->cr) & CR_EN;
        bool set_counter = is_switched_on && (s->cr & CR_ENMOD);

        ptimer_transaction_begin(s->timer_cmp);
        ptimer_transaction_begin(s->timer_reload);
        freq = imx_epit_get_freq(s);
        if (freq) {
            ptimer_set_freq(s->timer_reload, freq);
            ptimer_set_freq(s->timer_cmp, freq);
        }

        if (set_limit || set_counter) {
            uint64_t limit = (s->cr & CR_RLD) ? s->lr : EPIT_TIMER_MAX;
            ptimer_set_limit(s->timer_reload, limit, set_counter ? 1 : 0);
            if (set_limit) {
                ptimer_set_limit(s->timer_cmp, limit, 0);
            }
        }
        /*
         * If there is an input clock and the peripheral is enabled, then
         * ensure the wall clock timer is ticking. Otherwise stop the timers.
         * The compare timer will be updated later.
         */
        if (freq && (s->cr & CR_EN)) {
            ptimer_run(s->timer_reload, 0);
        } else {
            ptimer_stop(s->timer_reload);
        }
        /* Commit changes to reload timer, so they can propagate. */
        ptimer_transaction_commit(s->timer_reload);
        /* Update compare timer based on the committed reload timer value. */
        imx_epit_update_compare_timer(s);
        ptimer_transaction_commit(s->timer_cmp);
    }

    /*
     * The interrupt state can change due to:
     * - reset clears both SR.OCIF and CR.OCIE
     * - write to CR.EN or CR.OCIE
     */
    imx_epit_update_int(s);
}

static void imx_epit_write_sr(IMXEPITState *s, uint32_t value)
{
    /* writing 1 to SR.OCIF clears this bit and turns the interrupt off */
    if (value & SR_OCIF) {
        s->sr = 0; /* SR.OCIF is the only bit in this register anyway */
        imx_epit_update_int(s);
    }
}

static void imx_epit_write_lr(IMXEPITState *s, uint32_t value)
{
    s->lr = value;

    ptimer_transaction_begin(s->timer_cmp);
    ptimer_transaction_begin(s->timer_reload);
    if (s->cr & CR_RLD) {
        /* Also set the limit if the LRD bit is set */
        /* If IOVW bit is set then set the timer value */
        ptimer_set_limit(s->timer_reload, s->lr, s->cr & CR_IOVW);
        ptimer_set_limit(s->timer_cmp, s->lr, 0);
    } else if (s->cr & CR_IOVW) {
        /* If IOVW bit is set then set the timer value */
        ptimer_set_count(s->timer_reload, s->lr);
    }
    /* Commit the changes to s->timer_reload, so they can propagate. */
    ptimer_transaction_commit(s->timer_reload);
    /* Update the compare timer based on the committed reload timer value. */
    imx_epit_update_compare_timer(s);
    ptimer_transaction_commit(s->timer_cmp);
}

static void imx_epit_write_cmp(IMXEPITState *s, uint32_t value)
{
    s->cmp = value;

    /* Update the compare timer based on the committed reload timer value. */
    ptimer_transaction_begin(s->timer_cmp);
    imx_epit_update_compare_timer(s);
    ptimer_transaction_commit(s->timer_cmp);
}

static void imx_epit_write(void *opaque, hwaddr offset, uint64_t value,
                           unsigned size)
{
    IMXEPITState *s = IMX_EPIT(opaque);

    DPRINTF("(%s, value = 0x%08x)\n", imx_epit_reg_name(offset >> 2),
            (uint32_t)value);

    switch (offset >> 2) {
    case 0: /* CR */
        imx_epit_write_cr(s, (uint32_t)value);
        break;

    case 1: /* SR */
        imx_epit_write_sr(s, (uint32_t)value);
        break;

    case 2: /* LR */
        imx_epit_write_lr(s, (uint32_t)value);
        break;

    case 3: /* CMP */
        imx_epit_write_cmp(s, (uint32_t)value);
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR, "[%s]%s: Bad register at offset 0x%"
                      HWADDR_PRIx "\n", TYPE_IMX_EPIT, __func__, offset);
        break;
    }
}

static void imx_epit_cmp(void *opaque)
{
    IMXEPITState *s = IMX_EPIT(opaque);

    /* The cmp ptimer can't be running when the peripheral is disabled */
    assert(s->cr & CR_EN);

    DPRINTF("sr was %d\n", s->sr);
    /* Set interrupt status bit SR.OCIF and update the interrupt state */
    s->sr |= SR_OCIF;
    imx_epit_update_int(s);
}

static void imx_epit_reload(void *opaque)
{
    /* No action required on rollover of timer_reload */
}

static const MemoryRegionOps imx_epit_ops = {
    .read = imx_epit_read,
    .write = imx_epit_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static const VMStateDescription vmstate_imx_timer_epit = {
    .name = TYPE_IMX_EPIT,
    .version_id = 3,
    .minimum_version_id = 3,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(cr, IMXEPITState),
        VMSTATE_UINT32(sr, IMXEPITState),
        VMSTATE_UINT32(lr, IMXEPITState),
        VMSTATE_UINT32(cmp, IMXEPITState),
        VMSTATE_PTIMER(timer_reload, IMXEPITState),
        VMSTATE_PTIMER(timer_cmp, IMXEPITState),
        VMSTATE_END_OF_LIST()
    }
};

static void imx_epit_realize(DeviceState *dev, Error **errp)
{
    IMXEPITState *s = IMX_EPIT(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    DPRINTF("\n");

    sysbus_init_irq(sbd, &s->irq);
    memory_region_init_io(&s->iomem, OBJECT(s), &imx_epit_ops, s, TYPE_IMX_EPIT,
                          0x00001000);
    sysbus_init_mmio(sbd, &s->iomem);

    /*
     * The reload timer keeps running when the peripheral is enabled. It is a
     * kind of wall clock that does not generate any interrupts. The callback
     * needs to be provided, but it does nothing as the ptimer already supports
     * all necessary reloading functionality.
     */
    s->timer_reload = ptimer_init(imx_epit_reload, s, PTIMER_POLICY_LEGACY);

    /*
     * The compare timer is running only when the peripheral configuration is
     * in a state that will generate compare interrupts.
     */
    s->timer_cmp = ptimer_init(imx_epit_cmp, s, PTIMER_POLICY_LEGACY);
}

static void imx_epit_dev_reset(DeviceState *dev)
{
    IMXEPITState *s = IMX_EPIT(dev);
    imx_epit_reset(s, true);
}

static void imx_epit_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc  = DEVICE_CLASS(klass);

    dc->realize = imx_epit_realize;
    dc->reset = imx_epit_dev_reset;
    dc->vmsd = &vmstate_imx_timer_epit;
    dc->desc = "i.MX periodic timer";
}

static const TypeInfo imx_epit_info = {
    .name = TYPE_IMX_EPIT,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(IMXEPITState),
    .class_init = imx_epit_class_init,
};

static void imx_epit_register_types(void)
{
    type_register_static(&imx_epit_info);
}

type_init(imx_epit_register_types)
