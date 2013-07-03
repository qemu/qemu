/*
 * IMX31 Timer
 *
 * Copyright (c) 2008 OK Labs
 * Copyright (c) 2011 NICTA Pty Ltd
 * Originally written by Hans Jiang
 * Updated by Peter Chubb
 *
 * This code is licensed under GPL version 2 or later.  See
 * the COPYING file in the top-level directory.
 *
 */

#include "hw/hw.h"
#include "qemu/timer.h"
#include "hw/ptimer.h"
#include "hw/sysbus.h"
#include "hw/arm/imx.h"

//#define DEBUG_TIMER 1
#ifdef DEBUG_TIMER
#  define DPRINTF(fmt, args...) \
      do { printf("imx_timer: " fmt , ##args); } while (0)
#else
#  define DPRINTF(fmt, args...) do {} while (0)
#endif

/*
 * Define to 1 for messages about attempts to
 * access unimplemented registers or similar.
 */
#define DEBUG_IMPLEMENTATION 1
#if DEBUG_IMPLEMENTATION
#  define IPRINTF(fmt, args...)                                         \
    do  { fprintf(stderr, "imx_timer: " fmt, ##args); } while (0)
#else
#  define IPRINTF(fmt, args...) do {} while (0)
#endif

/*
 * GPT : General purpose timer
 *
 * This timer counts up continuously while it is enabled, resetting itself
 * to 0 when it reaches TIMER_MAX (in freerun mode) or when it
 * reaches the value of ocr1 (in periodic mode).  WE simulate this using a
 * QEMU ptimer counting down from ocr1 and reloading from ocr1 in
 * periodic mode, or counting from ocr1 to zero, then TIMER_MAX - ocr1.
 * waiting_rov is set when counting from TIMER_MAX.
 *
 * In the real hardware, there are three comparison registers that can
 * trigger interrupts, and compare channel 1 can be used to
 * force-reset the timer. However, this is a `bare-bones'
 * implementation: only what Linux 3.x uses has been implemented
 * (free-running timer from 0 to OCR1 or TIMER_MAX) .
 */


#define TIMER_MAX  0XFFFFFFFFUL

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
#define GPT_SR_ROV  (1 << 5)

#define GPT_IR_OF1IE  (1 << 0)
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

    uint32_t waiting_rov;
    qemu_irq irq;
} IMXTimerGState;

static const VMStateDescription vmstate_imx_timerg = {
    .name = "imx-timerg",
    .version_id = 2,
    .minimum_version_id = 2,
    .minimum_version_id_old = 2,
    .fields      = (VMStateField[]) {
        VMSTATE_UINT32(cr, IMXTimerGState),
        VMSTATE_UINT32(pr, IMXTimerGState),
        VMSTATE_UINT32(sr, IMXTimerGState),
        VMSTATE_UINT32(ir, IMXTimerGState),
        VMSTATE_UINT32(ocr1, IMXTimerGState),
        VMSTATE_UINT32(ocr2, IMXTimerGState),
        VMSTATE_UINT32(ocr3, IMXTimerGState),
        VMSTATE_UINT32(icr1, IMXTimerGState),
        VMSTATE_UINT32(icr2, IMXTimerGState),
        VMSTATE_UINT32(cnt, IMXTimerGState),
        VMSTATE_UINT32(waiting_rov, IMXTimerGState),
        VMSTATE_PTIMER(timer, IMXTimerGState),
        VMSTATE_END_OF_LIST()
    }
};

static const IMXClk imx_timerg_clocks[] = {
    NOCLK,    /* 000 No clock source */
    IPG,      /* 001 ipg_clk, 532MHz*/
    IPG,      /* 010 ipg_clk_highfreq */
    NOCLK,    /* 011 not defined */
    CLK_32k,  /* 100 ipg_clk_32k */
    NOCLK,    /* 101 not defined */
    NOCLK,    /* 110 not defined */
    NOCLK,    /* 111 not defined */
};


static void imx_timerg_set_freq(IMXTimerGState *s)
{
    int clksrc;
    uint32_t freq;

    clksrc = (s->cr >> GPT_CR_CLKSRC_SHIFT) & GPT_CR_CLKSRC_MASK;
    freq = imx_clock_frequency(s->ccm, imx_timerg_clocks[clksrc]) / (1 + s->pr);

    DPRINTF("Setting gtimer clksrc %d to frequency %d\n", clksrc, freq);
    if (freq) {
        ptimer_set_freq(s->timer, freq);
    }
}

static void imx_timerg_update(IMXTimerGState *s)
{
    uint32_t flags = s->sr & s->ir & (GPT_SR_OF1 | GPT_SR_ROV);

    DPRINTF("g-timer SR: %s %s IR=%s %s, %s\n",
            s->sr & GPT_SR_OF1 ? "OF1" : "",
            s->sr & GPT_SR_ROV ? "ROV" : "",
            s->ir & GPT_SR_OF1 ? "OF1" : "",
            s->ir & GPT_SR_ROV ? "ROV" : "",
            s->cr & GPT_CR_EN ? "CR_EN" : "Not Enabled");

    qemu_set_irq(s->irq, (s->cr & GPT_CR_EN) && flags);
}

static uint32_t imx_timerg_update_counts(IMXTimerGState *s)
{
    uint64_t target = s->waiting_rov ? TIMER_MAX : s->ocr1;
    uint64_t cnt = ptimer_get_count(s->timer);
    s->cnt = target - cnt;
    return s->cnt;
}

static void imx_timerg_reload(IMXTimerGState *s, uint32_t timeout)
{
    uint64_t diff_cnt;

    if (!(s->cr & GPT_CR_FRR)) {
        IPRINTF("IMX_timerg_reload --- called in reset-mode\n");
        return;
    }

    /*
     * For small timeouts, qemu sometimes runs too slow.
     * Better deliver a late interrupt than none.
     *
     * In Reset mode (FRR bit clear)
     * the ptimer reloads itself from OCR1;
     * in free-running mode we need to fake
     * running from 0 to ocr1 to TIMER_MAX
     */
    if (timeout > s->cnt) {
        diff_cnt = timeout - s->cnt;
    } else {
        diff_cnt = 0;
    }
    ptimer_set_count(s->timer, diff_cnt);
}

static uint64_t imx_timerg_read(void *opaque, hwaddr offset,
                                unsigned size)
{
    IMXTimerGState *s = (IMXTimerGState *)opaque;

    DPRINTF("g-read(offset=%x)", offset >> 2);
    switch (offset >> 2) {
    case 0: /* Control Register */
        DPRINTF(" cr = %x\n", s->cr);
        return s->cr;

    case 1: /* prescaler */
        DPRINTF(" pr = %x\n", s->pr);
        return s->pr;

    case 2: /* Status Register */
        DPRINTF(" sr = %x\n", s->sr);
        return s->sr;

    case 3: /* Interrupt Register */
        DPRINTF(" ir = %x\n", s->ir);
        return s->ir;

    case 4: /* Output Compare Register 1 */
        DPRINTF(" ocr1 = %x\n", s->ocr1);
        return s->ocr1;

    case 5: /* Output Compare Register 2 */
        DPRINTF(" ocr2 = %x\n", s->ocr2);
        return s->ocr2;

    case 6: /* Output Compare Register 3 */
        DPRINTF(" ocr3 = %x\n", s->ocr3);
        return s->ocr3;

    case 7: /* input Capture Register 1 */
        DPRINTF(" icr1 = %x\n", s->icr1);
        return s->icr1;

    case 8: /* input Capture Register 2 */
        DPRINTF(" icr2 = %x\n", s->icr2);
        return s->icr2;

    case 9: /* cnt */
        imx_timerg_update_counts(s);
        DPRINTF(" cnt = %x\n", s->cnt);
        return s->cnt;
    }

    IPRINTF("imx_timerg_read: Bad offset %x\n",
            (int)offset >> 2);

    return 0;
}

static void imx_timerg_reset(DeviceState *dev)
{
    IMXTimerGState *s = container_of(dev, IMXTimerGState, busdev.qdev);

    /*
     * Soft reset doesn't touch some bits; hard reset clears them
     */
    s->cr &= ~(GPT_CR_EN|GPT_CR_ENMOD|GPT_CR_STOPEN|GPT_CR_DOZEN|
               GPT_CR_WAITEN|GPT_CR_DBGEN);
    s->sr = 0;
    s->pr = 0;
    s->ir = 0;
    s->cnt = 0;
    s->ocr1 = TIMER_MAX;
    s->ocr2 = TIMER_MAX;
    s->ocr3 = TIMER_MAX;
    s->icr1 = 0;
    s->icr2 = 0;
    ptimer_stop(s->timer);
    ptimer_set_limit(s->timer, TIMER_MAX, 1);
    ptimer_set_count(s->timer, TIMER_MAX);
    imx_timerg_set_freq(s);
}

static void imx_timerg_write(void *opaque, hwaddr offset,
                             uint64_t value, unsigned size)
{
    IMXTimerGState *s = (IMXTimerGState *)opaque;
    DPRINTF("g-write(offset=%x, value = 0x%x)\n", (unsigned int)offset >> 2,
            (unsigned int)value);

    switch (offset >> 2) {
    case 0: {
        uint32_t oldcr = s->cr;
        /* CR */
        if (value & GPT_CR_SWR) { /* force reset */
            value &= ~GPT_CR_SWR;
            imx_timerg_reset(&s->busdev.qdev);
            imx_timerg_update(s);
        }

        s->cr = value & ~0x7c00;
        imx_timerg_set_freq(s);
        if ((oldcr ^ value) & GPT_CR_EN) {
            if (value & GPT_CR_EN) {
                if (value & GPT_CR_ENMOD) {
                    ptimer_set_count(s->timer, s->ocr1);
                    s->cnt = 0;
                }
                ptimer_run(s->timer,
                           (value & GPT_CR_FRR) && (s->ocr1 != TIMER_MAX));
            } else {
                ptimer_stop(s->timer);
            };
        }
        return;
    }

    case 1: /* Prescaler */
        s->pr = value & 0xfff;
        imx_timerg_set_freq(s);
        return;

    case 2: /* SR */
        /*
         * No point in implementing the status register bits to do with
         * external interrupt sources.
         */
        value &= GPT_SR_OF1 | GPT_SR_ROV;
        s->sr &= ~value;
        imx_timerg_update(s);
        return;

    case 3: /* IR -- interrupt register */
        s->ir = value & 0x3f;
        imx_timerg_update(s);
        return;

    case 4: /* OCR1 -- output compare register */
        /* In non-freerun mode, reset count when this register is written */
        if (!(s->cr & GPT_CR_FRR)) {
            s->waiting_rov = 0;
            ptimer_set_limit(s->timer, value, 1);
        } else {
            imx_timerg_update_counts(s);
            if (value > s->cnt) {
                s->waiting_rov = 0;
                imx_timerg_reload(s, value);
            } else {
                s->waiting_rov = 1;
                imx_timerg_reload(s, TIMER_MAX - s->cnt);
            }
        }
        s->ocr1 = value;
        return;

    case 5: /* OCR2 -- output compare register */
    case 6: /* OCR3 -- output compare register */
    default:
        IPRINTF("imx_timerg_write: Bad offset %x\n",
                (int)offset >> 2);
    }
}

static void imx_timerg_timeout(void *opaque)
{
    IMXTimerGState *s = (IMXTimerGState *)opaque;

    DPRINTF("imx_timerg_timeout, waiting rov=%d\n", s->waiting_rov);
    if (s->cr & GPT_CR_FRR) {
        /*
         * Free running timer from 0 -> TIMERMAX
         * Generates interrupt at TIMER_MAX and at cnt==ocr1
         * If ocr1 == TIMER_MAX, then no need to reload timer.
         */
        if (s->ocr1 == TIMER_MAX) {
            DPRINTF("s->ocr1 == TIMER_MAX, FRR\n");
            s->sr |= GPT_SR_OF1 | GPT_SR_ROV;
            imx_timerg_update(s);
            return;
        }

        if (s->waiting_rov) {
            /*
             * We were waiting for cnt==TIMER_MAX
             */
            s->sr |= GPT_SR_ROV;
            s->waiting_rov = 0;
            s->cnt = 0;
            imx_timerg_reload(s, s->ocr1);
        } else {
            /* Must have got a cnt==ocr1 timeout. */
            s->sr |= GPT_SR_OF1;
            s->cnt = s->ocr1;
            s->waiting_rov = 1;
            imx_timerg_reload(s, TIMER_MAX);
        }
        imx_timerg_update(s);
        return;
    }

    s->sr |= GPT_SR_OF1;
    imx_timerg_update(s);
}

static const MemoryRegionOps imx_timerg_ops = {
    .read = imx_timerg_read,
    .write = imx_timerg_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};


static int imx_timerg_init(SysBusDevice *dev)
{
    IMXTimerGState *s = FROM_SYSBUS(IMXTimerGState, dev);
    QEMUBH *bh;

    sysbus_init_irq(dev, &s->irq);
    memory_region_init_io(&s->iomem, &imx_timerg_ops,
                          s, "imxg-timer",
                          0x00001000);
    sysbus_init_mmio(dev, &s->iomem);

    bh = qemu_bh_new(imx_timerg_timeout, s);
    s->timer = ptimer_init(bh);

    /* Hard reset resets extra bits in CR */
    s->cr = 0;
    return 0;
}



/*
 * EPIT: Enhanced periodic interrupt timer
 */

#define CR_EN       (1 << 0)
#define CR_ENMOD    (1 << 1)
#define CR_OCIEN    (1 << 2)
#define CR_RLD      (1 << 3)
#define CR_PRESCALE_SHIFT (4)
#define CR_PRESCALE_MASK  (0xfff)
#define CR_SWR      (1 << 16)
#define CR_IOVW     (1 << 17)
#define CR_DBGEN    (1 << 18)
#define CR_WAITEN   (1 << 19)
#define CR_DOZEN    (1 << 20)
#define CR_STOPEN   (1 << 21)
#define CR_CLKSRC_SHIFT (24)
#define CR_CLKSRC_MASK  (0x3 << CR_CLKSRC_SHIFT)


/*
 * Exact clock frequencies vary from board to board.
 * These are typical.
 */
static const IMXClk imx_timerp_clocks[] =  {
    0,        /* 00 disabled */
    IPG,      /* 01 ipg_clk, ~532MHz */
    IPG,      /* 10 ipg_clk_highfreq */
    CLK_32k,  /* 11 ipg_clk_32k -- ~32kHz */
};

typedef struct {
    SysBusDevice busdev;
    ptimer_state *timer_reload;
    ptimer_state *timer_cmp;
    MemoryRegion iomem;
    DeviceState *ccm;

    uint32_t cr;
    uint32_t sr;
    uint32_t lr;
    uint32_t cmp;
    uint32_t cnt;

    uint32_t freq;
    qemu_irq irq;
} IMXTimerPState;

/*
 * Update interrupt status
 */
static void imx_timerp_update(IMXTimerPState *s)
{
    if (s->sr && (s->cr & CR_OCIEN)) {
        qemu_irq_raise(s->irq);
    } else {
        qemu_irq_lower(s->irq);
    }
}

static void set_timerp_freq(IMXTimerPState *s)
{
    int clksrc;
    unsigned prescaler;
    uint32_t freq;

    clksrc = (s->cr & CR_CLKSRC_MASK) >> CR_CLKSRC_SHIFT;
    prescaler = 1 + ((s->cr >> CR_PRESCALE_SHIFT) & CR_PRESCALE_MASK);
    freq = imx_clock_frequency(s->ccm, imx_timerp_clocks[clksrc]) / prescaler;

    s->freq = freq;
    DPRINTF("Setting ptimer frequency to %u\n", freq);

    if (freq) {
        ptimer_set_freq(s->timer_reload, freq);
        ptimer_set_freq(s->timer_cmp, freq);
    }
}

static void imx_timerp_reset(DeviceState *dev)
{
    IMXTimerPState *s = container_of(dev, IMXTimerPState, busdev.qdev);

    /*
     * Soft reset doesn't touch some bits; hard reset clears them
     */
    s->cr &= ~(CR_EN|CR_ENMOD|CR_STOPEN|CR_DOZEN|CR_WAITEN|CR_DBGEN);
    s->sr = 0;
    s->lr = TIMER_MAX;
    s->cmp = 0;
    s->cnt = 0;
    /* stop both timers */
    ptimer_stop(s->timer_cmp);
    ptimer_stop(s->timer_reload);
    /* compute new frequency */
    set_timerp_freq(s);
    /* init both timers to TIMER_MAX */
    ptimer_set_limit(s->timer_cmp, TIMER_MAX, 1);
    ptimer_set_limit(s->timer_reload, TIMER_MAX, 1);
    if (s->freq && (s->cr & CR_EN)) {
        /* if the timer is still enabled, restart it */
        ptimer_run(s->timer_reload, 1);
    }
}

static uint32_t imx_timerp_update_counts(IMXTimerPState *s)
{
     s->cnt = ptimer_get_count(s->timer_reload);

     return s->cnt;
}

static uint64_t imx_timerp_read(void *opaque, hwaddr offset,
                                unsigned size)
{
    IMXTimerPState *s = (IMXTimerPState *)opaque;

    DPRINTF("p-read(offset=%x)", offset >> 2);
    switch (offset >> 2) {
    case 0: /* Control Register */
        DPRINTF("cr %x\n", s->cr);
        return s->cr;

    case 1: /* Status Register */
        DPRINTF("sr %x\n", s->sr);
        return s->sr;

    case 2: /* LR - ticks*/
        DPRINTF("lr %x\n", s->lr);
        return s->lr;

    case 3: /* CMP */
        DPRINTF("cmp %x\n", s->cmp);
        return s->cmp;

    case 4: /* CNT */
        imx_timerp_update_counts(s);
        DPRINTF(" cnt = %x\n", s->cnt);
        return s->cnt;
    }

    IPRINTF("imx_timerp_read: Bad offset %x\n",
            (int)offset >> 2);
    return 0;
}

static void imx_reload_compare_timer(IMXTimerPState *s)
{
    if ((s->cr & CR_OCIEN) && s->cmp) {
        /* if the compare feature is on */
        uint32_t tmp = imx_timerp_update_counts(s);
        if (tmp > s->cmp) {
            /* reinit the cmp timer if required */
            ptimer_set_count(s->timer_cmp, tmp - s->cmp);
            if ((s->cr & CR_EN)) {
                /* Restart the cmp timer if required */
                ptimer_run(s->timer_cmp, 0);
            }
        }
    }
}

static void imx_timerp_write(void *opaque, hwaddr offset,
                             uint64_t value, unsigned size)
{
    IMXTimerPState *s = (IMXTimerPState *)opaque;
    DPRINTF("p-write(offset=%x, value = %x)\n", (unsigned int)offset >> 2,
            (unsigned int)value);

    switch (offset >> 2) {
    case 0: /* CR */
        s->cr = value & 0x03ffffff;
        if (s->cr & CR_SWR) {
            /* handle the reset */
            imx_timerp_reset(&s->busdev.qdev);
        } else {
            set_timerp_freq(s);
        }

        if (s->freq && (s->cr & CR_EN)) {
            if (s->cr & CR_ENMOD) {
                if (s->cr & CR_RLD) {
                    ptimer_set_limit(s->timer_reload, s->lr, 1);
                } else {
                    ptimer_set_limit(s->timer_reload, TIMER_MAX, 1);
                }
            }

            imx_reload_compare_timer(s);

            ptimer_run(s->timer_reload, 1);
        } else {
            /* stop both timers */
            ptimer_stop(s->timer_reload);
            ptimer_stop(s->timer_cmp);
        }
        break;

    case 1: /* SR - ACK*/
        /* writing 1 to OCIF clear the OCIF bit */
        if (value & 0x01) {
            s->sr = 0;
            imx_timerp_update(s);
        }
        break;

    case 2: /* LR - set ticks */
        s->lr = value;

        if (s->cr & CR_RLD) {
            /* Also set the limit if the LRD bit is set */
            /* If IOVW bit is set then set the timer value */
            ptimer_set_limit(s->timer_reload, s->lr, s->cr & CR_IOVW);
        } else if (s->cr & CR_IOVW) {
            /* If IOVW bit is set then set the timer value */
            ptimer_set_count(s->timer_reload, s->lr);
        }

        imx_reload_compare_timer(s);

        break;

    case 3: /* CMP */
        s->cmp = value;

        imx_reload_compare_timer(s);

        break;

    default:
        IPRINTF("imx_timerp_write: Bad offset %x\n",
                   (int)offset >> 2);
    }
}

static void imx_timerp_reload(void *opaque)
{
    IMXTimerPState *s = (IMXTimerPState *)opaque;

    DPRINTF("imxp reload\n");

    if (!(s->cr & CR_EN)) {
        return;
    }

    if (s->cr & CR_RLD) {
        ptimer_set_limit(s->timer_reload, s->lr, 1);
    } else {
        ptimer_set_limit(s->timer_reload, TIMER_MAX, 1);
    }

    if (s->cr & CR_OCIEN) {
        /* if compare register is 0 then we handle the interrupt here */
        if (s->cmp == 0) {
            s->sr = 1;
            imx_timerp_update(s);
        } else if (s->cmp <= s->lr) {
            /* We should launch the compare register */
            ptimer_set_count(s->timer_cmp, s->lr - s->cmp);
            ptimer_run(s->timer_cmp, 0);
        } else {
            IPRINTF("imxp reload: s->lr < s->cmp\n");
        }
    }
}

static void imx_timerp_cmp(void *opaque)
{
    IMXTimerPState *s = (IMXTimerPState *)opaque;

    DPRINTF("imxp compare\n");

    ptimer_stop(s->timer_cmp);

    /* compare register is not 0 */
    if (s->cmp) {
        s->sr = 1;
        imx_timerp_update(s);
    }
}

void imx_timerp_create(const hwaddr addr,
                              qemu_irq irq,
                              DeviceState *ccm)
{
    IMXTimerPState *pp;
    DeviceState *dev;

    dev = sysbus_create_simple("imx_timerp", addr, irq);
    pp = container_of(dev, IMXTimerPState, busdev.qdev);
    pp->ccm = ccm;
}

static const MemoryRegionOps imx_timerp_ops = {
  .read = imx_timerp_read,
  .write = imx_timerp_write,
  .endianness = DEVICE_NATIVE_ENDIAN,
};

static const VMStateDescription vmstate_imx_timerp = {
    .name = "imx-timerp",
    .version_id = 2,
    .minimum_version_id = 2,
    .minimum_version_id_old = 2,
    .fields      = (VMStateField[]) {
        VMSTATE_UINT32(cr, IMXTimerPState),
        VMSTATE_UINT32(sr, IMXTimerPState),
        VMSTATE_UINT32(lr, IMXTimerPState),
        VMSTATE_UINT32(cmp, IMXTimerPState),
        VMSTATE_UINT32(cnt, IMXTimerPState),
        VMSTATE_UINT32(freq, IMXTimerPState),
        VMSTATE_PTIMER(timer_reload, IMXTimerPState),
        VMSTATE_PTIMER(timer_cmp, IMXTimerPState),
        VMSTATE_END_OF_LIST()
    }
};

static int imx_timerp_init(SysBusDevice *dev)
{
    IMXTimerPState *s = FROM_SYSBUS(IMXTimerPState, dev);
    QEMUBH *bh;

    DPRINTF("imx_timerp_init\n");
    sysbus_init_irq(dev, &s->irq);
    memory_region_init_io(&s->iomem, &imx_timerp_ops,
                          s, "imxp-timer",
                          0x00001000);
    sysbus_init_mmio(dev, &s->iomem);

    bh = qemu_bh_new(imx_timerp_reload, s);
    s->timer_reload = ptimer_init(bh);

    bh = qemu_bh_new(imx_timerp_cmp, s);
    s->timer_cmp = ptimer_init(bh);

    return 0;
}


void imx_timerg_create(const hwaddr addr,
                              qemu_irq irq,
                              DeviceState *ccm)
{
    IMXTimerGState *pp;
    DeviceState *dev;

    dev = sysbus_create_simple("imx_timerg", addr, irq);
    pp = container_of(dev, IMXTimerGState, busdev.qdev);
    pp->ccm = ccm;
}

static void imx_timerg_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc  = DEVICE_CLASS(klass);
    SysBusDeviceClass *k = SYS_BUS_DEVICE_CLASS(klass);
    k->init = imx_timerg_init;
    dc->vmsd = &vmstate_imx_timerg;
    dc->reset = imx_timerg_reset;
    dc->desc = "i.MX general timer";
}

static void imx_timerp_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc  = DEVICE_CLASS(klass);
    SysBusDeviceClass *k = SYS_BUS_DEVICE_CLASS(klass);
    k->init = imx_timerp_init;
    dc->vmsd = &vmstate_imx_timerp;
    dc->reset = imx_timerp_reset;
    dc->desc = "i.MX periodic timer";
}

static const TypeInfo imx_timerp_info = {
    .name = "imx_timerp",
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(IMXTimerPState),
    .class_init = imx_timerp_class_init,
};

static const TypeInfo imx_timerg_info = {
    .name = "imx_timerg",
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(IMXTimerGState),
    .class_init = imx_timerg_class_init,
};

static void imx_timer_register_types(void)
{
    type_register_static(&imx_timerp_info);
    type_register_static(&imx_timerg_info);
}

type_init(imx_timer_register_types)
