/*
 * IMX GPT Timer
 *
 * Copyright (c) 2008 OK Labs
 * Copyright (c) 2011 NICTA Pty Ltd
 * Originally written by Hans Jiang
 * Updated by Peter Chubb
 * Updated by Jean-Christophe Dubois
 *
 * This code is licensed under GPL version 2 or later.  See
 * the COPYING file in the top-level directory.
 *
 */

#include "hw/hw.h"
#include "qemu/bitops.h"
#include "qemu/timer.h"
#include "hw/ptimer.h"
#include "hw/sysbus.h"
#include "hw/arm/imx.h"
#include "qemu/main-loop.h"

#define TYPE_IMX_GPT "imx.gpt"

/*
 * Define to 1 for debug messages
 */
#define DEBUG_TIMER 0
#if DEBUG_TIMER

static char const *imx_gpt_reg_name(uint32_t reg)
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

#  define DPRINTF(fmt, args...) \
          do { printf("%s: " fmt , __func__, ##args); } while (0)
#else
#  define DPRINTF(fmt, args...) do {} while (0)
#endif

/*
 * Define to 1 for messages about attempts to
 * access unimplemented registers or similar.
 */
#define DEBUG_IMPLEMENTATION 1
#if DEBUG_IMPLEMENTATION
#  define IPRINTF(fmt, args...) \
          do { fprintf(stderr, "%s: " fmt, __func__, ##args); } while (0)
#else
#  define IPRINTF(fmt, args...) do {} while (0)
#endif

#define IMX_GPT(obj) \
        OBJECT_CHECK(IMXGPTState, (obj), TYPE_IMX_GPT)
/*
 * GPT : General purpose timer
 *
 * This timer counts up continuously while it is enabled, resetting itself
 * to 0 when it reaches GPT_TIMER_MAX (in freerun mode) or when it
 * reaches the value of one of the ocrX (in periodic mode).
 */

#define GPT_TIMER_MAX  0XFFFFFFFFUL

/* Control register.  Not all of these bits have any effect (yet) */
#define GPT_CR_EN     (1 << 0)  /* GPT Enable */
#define GPT_CR_ENMOD  (1 << 1)  /* GPT Enable Mode */
#define GPT_CR_DBGEN  (1 << 2)  /* GPT Debug mode enable */
#define GPT_CR_WAITEN (1 << 3)  /* GPT Wait Mode Enable  */
#define GPT_CR_DOZEN  (1 << 4)  /* GPT Doze mode enable */
#define GPT_CR_STOPEN (1 << 5)  /* GPT Stop Mode Enable */
#define GPT_CR_CLKSRC_SHIFT (6)
#define GPT_CR_CLKSRC_MASK  (0x7)

#define GPT_CR_FRR    (1 << 9)  /* Freerun or Restart */
#define GPT_CR_SWR    (1 << 15) /* Software Reset */
#define GPT_CR_IM1    (3 << 16) /* Input capture channel 1 mode (2 bits) */
#define GPT_CR_IM2    (3 << 18) /* Input capture channel 2 mode (2 bits) */
#define GPT_CR_OM1    (7 << 20) /* Output Compare Channel 1 Mode (3 bits) */
#define GPT_CR_OM2    (7 << 23) /* Output Compare Channel 2 Mode (3 bits) */
#define GPT_CR_OM3    (7 << 26) /* Output Compare Channel 3 Mode (3 bits) */
#define GPT_CR_FO1    (1 << 29) /* Force Output Compare Channel 1 */
#define GPT_CR_FO2    (1 << 30) /* Force Output Compare Channel 2 */
#define GPT_CR_FO3    (1 << 31) /* Force Output Compare Channel 3 */

#define GPT_SR_OF1  (1 << 0)
#define GPT_SR_OF2  (1 << 1)
#define GPT_SR_OF3  (1 << 2)
#define GPT_SR_ROV  (1 << 5)

#define GPT_IR_OF1IE  (1 << 0)
#define GPT_IR_OF2IE  (1 << 1)
#define GPT_IR_OF3IE  (1 << 2)
#define GPT_IR_ROVIE  (1 << 5)

typedef struct {
    SysBusDevice busdev;
    ptimer_state *timer;
    MemoryRegion iomem;
    DeviceState *ccm;

    uint32_t cr;
    uint32_t pr;
    uint32_t sr;
    uint32_t ir;
    uint32_t ocr1;
    uint32_t ocr2;
    uint32_t ocr3;
    uint32_t icr1;
    uint32_t icr2;
    uint32_t cnt;

    uint32_t next_timeout;
    uint32_t next_int;

    uint32_t freq;

    qemu_irq irq;
} IMXGPTState;

static const VMStateDescription vmstate_imx_timer_gpt = {
    .name = "imx.gpt",
    .version_id = 3,
    .minimum_version_id = 3,
    .fields = (VMStateField[]) {
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

static const IMXClk imx_gpt_clocks[] = {
    NOCLK,    /* 000 No clock source */
    IPG,      /* 001 ipg_clk, 532MHz*/
    IPG,      /* 010 ipg_clk_highfreq */
    NOCLK,    /* 011 not defined */
    CLK_32k,  /* 100 ipg_clk_32k */
    NOCLK,    /* 101 not defined */
    NOCLK,    /* 110 not defined */
    NOCLK,    /* 111 not defined */
};

static void imx_gpt_set_freq(IMXGPTState *s)
{
    uint32_t clksrc = extract32(s->cr, GPT_CR_CLKSRC_SHIFT, 3);
    uint32_t freq = imx_clock_frequency(s->ccm, imx_gpt_clocks[clksrc])
                                                / (1 + s->pr);
    s->freq = freq;

    DPRINTF("Setting clksrc %d to frequency %d\n", clksrc, freq);

    if (freq) {
        ptimer_set_freq(s->timer, freq);
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

static void imx_gpt_compute_next_timeout(IMXGPTState *s, bool event)
{
    uint32_t timeout = GPT_TIMER_MAX;
    uint32_t count = 0;
    long long limit;

    if (!(s->cr & GPT_CR_EN)) {
        /* if not enabled just return */
        return;
    }

    if (event) {
        /* This is a timer event  */

        if ((s->cr & GPT_CR_FRR)  && (s->next_timeout != GPT_TIMER_MAX)) {
            /*
             * if we are in free running mode and we have not reached
             * the GPT_TIMER_MAX limit, then update the count
             */
            count = imx_gpt_update_count(s);
        }
    } else {
        /* not a timer event, then just update the count */

        count = imx_gpt_update_count(s);
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
    uint32_t reg = offset >> 2;

    switch (reg) {
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
        qemu_log_mask(LOG_UNIMP, "icr1 feature is not implemented\n");
        reg_value = s->icr1;
        break;

    case 8: /* input Capture Register 2 */
        qemu_log_mask(LOG_UNIMP, "icr2 feature is not implemented\n");
        reg_value = s->icr2;
        break;

    case 9: /* cnt */
        imx_gpt_update_count(s);
        reg_value = s->cnt;
        break;

    default:
        IPRINTF("Bad offset %x\n", reg);
        break;
    }

    DPRINTF("(%s) = 0x%08x\n", imx_gpt_reg_name(reg), reg_value);

    return reg_value;
}

static void imx_gpt_reset(DeviceState *dev)
{
    IMXGPTState *s = IMX_GPT(dev);

    /* stop timer */
    ptimer_stop(s->timer);

    /*
     * Soft reset doesn't touch some bits; hard reset clears them
     */
    s->cr &= ~(GPT_CR_EN|GPT_CR_ENMOD|GPT_CR_STOPEN|GPT_CR_DOZEN|
               GPT_CR_WAITEN|GPT_CR_DBGEN);
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
}

static void imx_gpt_write(void *opaque, hwaddr offset, uint64_t value,
                          unsigned size)
{
    IMXGPTState *s = IMX_GPT(opaque);
    uint32_t oldreg;
    uint32_t reg = offset >> 2;

    DPRINTF("(%s, value = 0x%08x)\n", imx_gpt_reg_name(reg),
            (uint32_t)value);

    switch (reg) {
    case 0:
        oldreg = s->cr;
        s->cr = value & ~0x7c14;
        if (s->cr & GPT_CR_SWR) { /* force reset */
            /* handle the reset */
            imx_gpt_reset(DEVICE(s));
        } else {
            /* set our freq, as the source might have changed */
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
        }
        break;

    case 1: /* Prescaler */
        s->pr = value & 0xfff;
        imx_gpt_set_freq(s);
        break;

    case 2: /* SR */
        s->sr &= ~(value & 0x3f);
        imx_gpt_update_int(s);
        break;

    case 3: /* IR -- interrupt register */
        s->ir = value & 0x3f;
        imx_gpt_update_int(s);

        imx_gpt_compute_next_timeout(s, false);

        break;

    case 4: /* OCR1 -- output compare register */
        s->ocr1 = value;

        /* In non-freerun mode, reset count when this register is written */
        if (!(s->cr & GPT_CR_FRR)) {
            s->next_timeout = GPT_TIMER_MAX;
            ptimer_set_limit(s->timer, GPT_TIMER_MAX, 1);
        }

        /* compute the new timeout */
        imx_gpt_compute_next_timeout(s, false);

        break;

    case 5: /* OCR2 -- output compare register */
        s->ocr2 = value;

        /* compute the new timeout */
        imx_gpt_compute_next_timeout(s, false);

        break;

    case 6: /* OCR3 -- output compare register */
        s->ocr3 = value;

        /* compute the new timeout */
        imx_gpt_compute_next_timeout(s, false);

        break;

    default:
        IPRINTF("Bad offset %x\n", reg);
        break;
    }
}

static void imx_gpt_timeout(void *opaque)
{
    IMXGPTState *s = IMX_GPT(opaque);

    DPRINTF("\n");

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
    QEMUBH *bh;

    sysbus_init_irq(sbd, &s->irq);
    memory_region_init_io(&s->iomem, OBJECT(s), &imx_gpt_ops, s, TYPE_IMX_GPT,
                          0x00001000);
    sysbus_init_mmio(sbd, &s->iomem);

    bh = qemu_bh_new(imx_gpt_timeout, s);
    s->timer = ptimer_init(bh);
}

void imx_timerg_create(const hwaddr addr, qemu_irq irq, DeviceState *ccm)
{
    IMXGPTState *pp;
    DeviceState *dev;

    dev = sysbus_create_simple(TYPE_IMX_GPT, addr, irq);
    pp = IMX_GPT(dev);
    pp->ccm = ccm;
}

static void imx_gpt_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = imx_gpt_realize;
    dc->reset = imx_gpt_reset;
    dc->vmsd = &vmstate_imx_timer_gpt;
    dc->desc = "i.MX general timer";
}

static const TypeInfo imx_gpt_info = {
    .name = TYPE_IMX_GPT,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(IMXGPTState),
    .class_init = imx_gpt_class_init,
};

static void imx_gpt_register_types(void)
{
    type_register_static(&imx_gpt_info);
}

type_init(imx_gpt_register_types)
