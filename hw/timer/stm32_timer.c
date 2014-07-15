/*
 * STM32 Microcontroller Timer module
 *
 * Copyright (C) 2010 Andrew Hankins
 *
 * Source code based on pl011.c
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "hw/sysbus.h"
#include "qemu/timer.h"
#include "sysemu/sysemu.h"


/* DEFINITIONS*/

/* See the README file for details on these settings. */
#define DEBUG_STM32_TIMER
//#define STM32_TIMER_NO_BAUD_DELAY
//#define STM32_TIMER_ENABLE_OVERRUN

#ifdef DEBUG_STM32_TIMER
#define DPRINTF(fmt, ...)                                       \
    do { printf("STM32_TIMER: " fmt , ## __VA_ARGS__); } while (0)
#else
#define DPRINTF(fmt, ...)
#endif

#define TIMER_CR1_OFFSET   0x00
#define TIMER_CR2_OFFSET   0x04
#define TIMER_SMCR_OFFSET  0x08
#define TIMER_DIER_OFFSET  0x0c
#define TIMER_SR_OFFSET    0x10
#define TIMER_EGR_OFFSET   0x14
#define TIMER_CCMR1_OFFSET 0x18
#define TIMER_CCMR2_OFFSET 0x1c
#define TIMER_CCER_OFFSET  0x20
#define TIMER_CNT_OFFSET   0x24
#define TIMER_PSC_OFFSET   0x28
#define TIMER_APR_OFFSET   0x2c
#define TIMER_RCR_OFFSET   0x30
#define TIMER_CCR1_OFFSET  0x34
#define TIMER_CCR2_OFFSET  0x38
#define TIMER_CCR3_OFFSET  0x3c
#define TIMER_CCR4_OFFSET  0x40
#define TIMER_DCR_OFFSET   0x48
#define TIMER_DMAR_OFFSET  0x4C

typedef struct {
    SysBusDevice busdev;
    MemoryRegion iomem;
    QEMUTimer   *timer;
    qemu_irq     irq;

    /* Needed to preserve the tick_count across migration, even if the
     * absolute value of the rtc_clock is different on the source and
     * destination.
     */
    uint32_t tick_offset_vmstate;
    uint32_t tick_offset;

	uint32_t cr1;
	uint32_t cr2;
	uint32_t smcr;
	uint32_t dier;
	uint32_t sr;
	uint32_t egr;
	uint32_t ccmr1;
	uint32_t ccmr2;
	uint32_t ccer;
	uint32_t cnt;
	uint32_t psc;
	uint32_t apr;
	uint32_t rcr;
	uint32_t ccr1;
	uint32_t ccr2;
	uint32_t ccr3;
	uint32_t ccr4;
	uint32_t dcr;
	uint32_t dmar;

} stm32_tm_state;

static void stm32_update(stm32_tm_state *s)
{
    qemu_set_irq(s->irq, s->is & s->im);
}

static void stm32_interrupt(void * opaque)
{
    stm32_tm_state *s = (stm32_tm_state *)opaque;

    s->is = 1;
    DPRINTF("Alarm raised\n");
    stm32_update(s);
}

static uint32_t stm32_get_count(stm32_tm_state *s)
{
    int64_t now = qemu_get_clock_ns(rtc_clock);
    return s->tick_offset + now / get_ticks_per_sec();
}

static void stm32_set_alarm(stm32_tm_state *s)
{
    uint32_t ticks;

    /* The timer wraps around.  This subtraction also wraps in the same way,
       and gives correct results when alarm < now_ticks.  */
    ticks = s->mr - stm32_get_count(s);
    DPRINTF("Alarm set in %ud ticks\n", ticks);
    if (ticks == 0) {
        qemu_del_timer(s->timer);
        stm32_interrupt(s);
    } else {
        int64_t now = qemu_get_clock_ns(rtc_clock);
        qemu_mod_timer(s->timer, now + (int64_t)ticks * get_ticks_per_sec());
    }
}

static uint64_t stm32_read(void *opaque, hwaddr offset,
                           unsigned size)
{
    stm32_tm_state *s = (stm32_tm_state *)opaque;

    switch (offset) {
	case TIMER_CR1_OFFSET:
		return s->cr1;
	case TIMER_CR2_OFFSET:
		return s->cr2;
	case TIMER_SMCR_OFFSET:
		return s->smcr;
	case TIMER_DIER_OFFSET:
		return s->dier;
	case TIMER_SR_OFFSET:
		return s->sr;
	case TIMER_EGR_OFFSET:
		return s->egr;
	case TIMER_CCMR1_OFFSET:
		return s->ccmr1;
	case TIMER_CCMR2_OFFSET:
		return s->ccmr2;
	case TIMER_CCER_OFFSET:
		return s->ccer;
	case TIMER_CNT_OFFSET:
		return s->cnt;
	case TIMER_PSC_OFFSET:
		return s->psc;
	case TIMER_APR_OFFSET:
		return s->apr;
	case TIMER_RCR_OFFSET:
		return s->rcr;
	case TIMER_CCR1_OFFSET:
		return s->ccr1;
	case TIMER_CCR2_OFFSET:
		return s->ccr2;
	case TIMER_CCR3_OFFSET:
		return s->ccr3;
	case TIMER_CCR4_OFFSET:
		return s->ccr4;
	case TIMER_DCR_OFFSET:
		return s->dcr;
	case TIMER_DMAR_OFFSET:
		return s->dmar;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "stm32_read: Bad offset 0x%x\n", (int)offset);
        break;
    }

    return 0;
}

static void stm32_write(void * opaque, hwaddr offset,
                        uint64_t value, unsigned size)
{
    stm32_tm_state *s = (stm32_tm_state *)opaque;


    switch (offset) {
    case RTC_LR:
        s->tick_offset += value - stm32_get_count(s);
        stm32_set_alarm(s);
        break;
    case RTC_MR:
        s->mr = value;
        stm32_set_alarm(s);
        break;
    case RTC_IMSC:
        s->im = value & 1;
        DPRINTF("Interrupt mask %d\n", s->im);
        stm32_update(s);
        break;
    case RTC_ICR:
        /* The PL031 documentation (DDI0224B) states that the interrupt is
           cleared when bit 0 of the written value is set.  However the
           arm926e documentation (DDI0287B) states that the interrupt is
           cleared when any value is written.  */
        DPRINTF("Interrupt cleared");
        s->is = 0;
        stm32_update(s);
        break;
    case RTC_CR:
        /* Written value is ignored.  */
        break;

    case RTC_DR:
    case RTC_MIS:
    case RTC_RIS:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "stm32: write to read-only register at offset 0x%x\n",
                      (int)offset);
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "stm32_write: Bad offset 0x%x\n", (int)offset);
        break;
    }
}

static const MemoryRegionOps stm32_ops = {
    .read = stm32_read,
    .write = stm32_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static int stm32_init(SysBusDevice *dev)
{
    stm32_tm_state *s = FROM_SYSBUS(stm32_tm_state, dev);
    struct tm tm;

    memory_region_init_io(&s->iomem, &stm32_ops, s, "stm32", 0x1000);
    sysbus_init_mmio(dev, &s->iomem);

    sysbus_init_irq(dev, &s->irq);
    qemu_get_timedate(&tm, 0);
    s->tick_offset = mktimegm(&tm) - qemu_get_clock_ns(rtc_clock) / get_ticks_per_sec();

    s->timer = qemu_new_timer_ns(rtc_clock, stm32_interrupt, s);
    return 0;
}

static void stm32_pre_save(void *opaque)
{
    stm32_tm_state *s = opaque;

    /* tick_offset is base_time - rtc_clock base time.  Instead, we want to
     * store the base time relative to the vm_clock for backwards-compatibility.  */
    int64_t delta = qemu_get_clock_ns(rtc_clock) - qemu_get_clock_ns(vm_clock);
    s->tick_offset_vmstate = s->tick_offset + delta / get_ticks_per_sec();
}

static int stm32_post_load(void *opaque, int version_id)
{
    stm32_tm_state *s = opaque;

    int64_t delta = qemu_get_clock_ns(rtc_clock) - qemu_get_clock_ns(vm_clock);
    s->tick_offset = s->tick_offset_vmstate - delta / get_ticks_per_sec();
    stm32_set_alarm(s);
    return 0;
}

static const VMStateDescription vmstate_stm32 = {
    .name = "stm32",
    .version_id = 1,
    .minimum_version_id = 1,
    .pre_save = stm32_pre_save,
    .post_load = stm32_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(tick_offset_vmstate, stm32_tm_state),
        VMSTATE_UINT32(mr, stm32_tm_state),
        VMSTATE_UINT32(lr, stm32_tm_state),
        VMSTATE_UINT32(cr, stm32_tm_state),
        VMSTATE_UINT32(im, stm32_tm_state),
        VMSTATE_UINT32(is, stm32_tm_state),
        VMSTATE_END_OF_LIST()
    }
};

static void stm32_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SysBusDeviceClass *k = SYS_BUS_DEVICE_CLASS(klass);

    k->init = stm32_init;
    dc->no_user = 1;
    dc->vmsd = &vmstate_stm32;
}

static const TypeInfo stm32_info = {
    .name          = "stm32",
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(stm32_tm_state),
    .class_init    = stm32_class_init,
};

static void stm32_register_types(void)
{
    type_register_static(&stm32_info);
}

type_init(stm32_register_types)
