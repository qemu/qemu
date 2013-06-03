/*
 * IMX EPIT Timer
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
#include "qemu/bitops.h"
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

#define TIMER_MAX  0XFFFFFFFFUL

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
    unsigned clksrc;
    unsigned prescaler;
    uint32_t freq;

    clksrc = extract32(s->cr, CR_CLKSRC_SHIFT, 2);
    prescaler = 1 + extract32(s->cr, CR_PRESCALE_SHIFT, 12);

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

    DPRINTF("p-read(offset=%x)", (unsigned int)(offset >> 2));
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

static void imx_timer_register_types(void)
{
    type_register_static(&imx_timerp_info);
}

type_init(imx_timer_register_types)
