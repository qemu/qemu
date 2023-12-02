/*
 * STM32F2XX Timer
 *
 * Copyright (c) 2014 Alistair Francis <alistair@alistair23.me>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/timer/stm32f2xx_timer.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/typedefs.h"
#include "qemu/timer.h"
#include "qemu/main-loop.h"
#include "sysemu/dma.h"

#ifndef STM_TIMER_ERR_DEBUG
#define STM_TIMER_ERR_DEBUG 0
#endif

#define DB_PRINT_L(lvl, fmt, args...) do { \
    if (STM_TIMER_ERR_DEBUG >= lvl) { \
        qemu_log("%s: " fmt, __func__, ## args); \
    } \
} while (0)

#define DB_PRINT(fmt, args...) DB_PRINT_L(1, fmt, ## args)


static uint32_t stm32f2xx_timer_get_count(STM32F2XXTimerState *s)
{
    uint64_t cnt = ptimer_get_count(s->timer);
    if (s->count_mode == TIMER_UP_COUNT) {
        return s->tim_arr - (cnt & 0xffff);
    } else {
        return cnt & 0xffff;
    }
}


static void stm32f2xx_timer_set_count(STM32F2XXTimerState *s, uint32_t cnt)
{
    if (s->count_mode == TIMER_UP_COUNT) {
        ptimer_set_count(s->timer, s->tim_arr - (cnt & 0xffff));
    } else {
        ptimer_set_count(s->timer, cnt & 0xffff);
    }
}

static void stm32f2xx_timer_update(STM32F2XXTimerState *s)
{
    if (s->tim_cr1 & TIM_CR1_DIR) {
        s->count_mode = TIMER_DOWN_COUNT;
    } else {
        s->count_mode = TIMER_UP_COUNT;
    }

    if (s->tim_cr1 & TIM_CR1_CMS) {
        s->count_mode = TIMER_UP_COUNT;
    }

    if (s->tim_cr1 & TIM_CR1_CEN) {
        DB_PRINT("Enabling timer\n");
        ptimer_set_freq(s->timer, s->freq_hz);
        ptimer_run(s->timer, !(s->tim_cr1 & 0x04));
    } else {
        DB_PRINT("Disabling timer\n");
        ptimer_stop(s->timer);
    }
}

static void stm32f2xx_timer_update_uif(STM32F2XXTimerState *s, uint8_t value)
{
    s->tim_sr &= ~TIM_SR1_UIF;
    s->tim_sr |= (value & TIM_SR1_UIF);
    qemu_set_irq(s->irq, value);
}

static void stm32f2xx_timer_tick(void *opaque)
{
    STM32F2XXTimerState *s = (STM32F2XXTimerState *)opaque;
    DB_PRINT("Alarm raised\n");
    stm32f2xx_timer_update_uif(s, 1);

    if (s->count_mode == TIMER_UP_COUNT) {
        stm32f2xx_timer_set_count(s, 0);
    } else {
        stm32f2xx_timer_set_count(s, s->tim_arr);
    }

    if (s->tim_cr1 & TIM_CR1_CMS) {
        if (s->count_mode == TIMER_UP_COUNT) {
            s->count_mode = TIMER_DOWN_COUNT;
        } else {
            s->count_mode = TIMER_UP_COUNT;
        }
    }

    if (s->tim_cr1 & TIM_CR1_OPM) {
        s->tim_cr1 &= ~TIM_CR1_CEN;
    } else {
        stm32f2xx_timer_update(s);
    }
}


static void stm32f2xx_timer_reset(DeviceState *dev)
{
    STM32F2XXTimerState *s = STM32F2XXTIMER(dev);
    s->tim_cr1 = 0;
    s->tim_cr2 = 0;
    s->tim_smcr = 0;
    s->tim_dier = 0;
    s->tim_sr = 0;
    s->tim_egr = 0;
    s->tim_ccmr1 = 0;
    s->tim_ccmr2 = 0;
    s->tim_ccer = 0;
    s->tim_psc = 0;
    s->tim_arr = 0;
    s->tim_ccr1 = 0;
    s->tim_ccr2 = 0;
    s->tim_ccr3 = 0;
    s->tim_ccr4 = 0;
    s->tim_dcr = 0;
    s->tim_dmar = 0;
    s->tim_or = 0;
}

static uint64_t stm32f2xx_timer_read(void *opaque, hwaddr offset,
                           unsigned size)
{
    STM32F2XXTimerState *s = opaque;

    DB_PRINT("Read 0x%"HWADDR_PRIx"\n", offset);

    switch (offset) {
    case TIM_CR1:
        return s->tim_cr1;
    case TIM_CR2:
        qemu_log_mask(LOG_GUEST_ERROR, "stm32_timer: CR2 not supported");
        return 0;
    case TIM_SMCR:
        qemu_log_mask(LOG_GUEST_ERROR, "stm32_timer: SMCR not supported");
        return 0;
    case TIM_DIER:
        return s->tim_dier;
    case TIM_SR:
        return s->tim_sr;
    case TIM_EGR:
        qemu_log_mask(LOG_GUEST_ERROR, "stm32_timer: EGR write only");
        return 0;
    case TIM_CCMR1:
        return s->tim_ccmr1;
    case TIM_CCMR2:
        return s->tim_ccmr2;
    case TIM_CCER:
        return s->tim_ccer;
    case TIM_CNT:
        return stm32f2xx_timer_get_count(s);
    case TIM_PSC:
        return s->tim_psc;
    case TIM_ARR:
        return s->tim_arr;
    case TIM_CCR1:
        return s->tim_ccr1;
    case TIM_CCR2:
        return s->tim_ccr2;
    case TIM_CCR3:
        return s->tim_ccr3;
    case TIM_CCR4:
        return s->tim_ccr4;
    case TIM_DCR:
        qemu_log_mask(LOG_GUEST_ERROR, "stm32_timer: DCR not supported");
        return 0;
    case TIM_DMAR:
        qemu_log_mask(LOG_GUEST_ERROR, "stm32_timer: CR2 not supported");
        return 0;
    case TIM_OR:
        return s->tim_or;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Bad offset 0x%"HWADDR_PRIx"\n", __func__, offset);
    }
    return 0;
}

static void stm32f2xx_update_cr1(STM32F2XXTimerState *s, uint64_t value)
{
    s->tim_cr1 = value & 0x3FF;
    ptimer_transaction_begin(s->timer);
    stm32f2xx_timer_update(s);
    ptimer_transaction_commit(s->timer);
    DB_PRINT("write cr1 = %x\n", s->tim_cr1);
}
static void stm32f2xx_timer_write(void *opaque, hwaddr offset,
                        uint64_t val64, unsigned size)
{
    STM32F2XXTimerState *s = opaque;
    uint32_t value = val64;
    int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    uint32_t timer_val = 0;

    DB_PRINT("Write 0x%x, 0x%"HWADDR_PRIx"\n", value, offset);

    switch (offset) {
    case TIM_CR1:
        s->tim_cr1 = value;
        return;
    case TIM_CR2:
        s->tim_cr2 = value;
        return;
    case TIM_SMCR:
        s->tim_smcr = value;
        return;
    case TIM_DIER:
        s->tim_dier = value;
        return;
    case TIM_SR:
        /* This is set by hardware and cleared by software */
        s->tim_sr &= value;
        return;
    case TIM_EGR:
        s->tim_egr = value;
        if (s->tim_egr & TIM_EGR_UG) {
            timer_val = 0;
            break;
        }
        return;
    case TIM_CCMR1:
        s->tim_ccmr1 = value;
        return;
    case TIM_CCMR2:
        s->tim_ccmr2 = value;
        return;
    case TIM_CCER:
        s->tim_ccer = value;
        return;
    case TIM_PSC:
        timer_val = stm32f2xx_ns_to_ticks(s, now) - s->tick_offset;
        s->tim_psc = value & 0xFFFF;
        break;
    case TIM_CNT:
        timer_val = value;
        break;
    case TIM_ARR:
        s->tim_arr = value;
        stm32f2xx_timer_set_alarm(s, now);
        return;
    case TIM_CCR1:
        s->tim_ccr1 = value;
        return;
    case TIM_CCR2:
        s->tim_ccr2 = value;
        return;
    case TIM_CCR3:
        s->tim_ccr3 = value;
        return;
    case TIM_CCR4:
        s->tim_ccr4 = value;
        return;
    case TIM_DCR:
        s->tim_dcr = value;
        return;
    case TIM_DMAR:
        s->tim_dmar = value;
        return;
    case TIM_OR:
        s->tim_or = value;
        return;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Bad offset 0x%"HWADDR_PRIx"\n", __func__, offset);
        return;
    }

    /* This means that a register write has affected the timer in a way that
     * requires a refresh of both tick_offset and the alarm.
     */
    s->tick_offset = stm32f2xx_ns_to_ticks(s, now) - timer_val;
    stm32f2xx_timer_set_alarm(s, now);
}

static const MemoryRegionOps stm32f2xx_timer_ops = {
    .read = stm32f2xx_timer_read,
    .write = stm32f2xx_timer_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static const VMStateDescription vmstate_stm32f2xx_timer = {
    .name = TYPE_STM32F2XX_TIMER,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_INT64(tick_offset, STM32F2XXTimerState),
        VMSTATE_UINT32(tim_cr1, STM32F2XXTimerState),
        VMSTATE_UINT32(tim_cr2, STM32F2XXTimerState),
        VMSTATE_UINT32(tim_smcr, STM32F2XXTimerState),
        VMSTATE_UINT32(tim_dier, STM32F2XXTimerState),
        VMSTATE_UINT32(tim_sr, STM32F2XXTimerState),
        VMSTATE_UINT32(tim_egr, STM32F2XXTimerState),
        VMSTATE_UINT32(tim_ccmr1, STM32F2XXTimerState),
        VMSTATE_UINT32(tim_ccmr2, STM32F2XXTimerState),
        VMSTATE_UINT32(tim_ccer, STM32F2XXTimerState),
        VMSTATE_UINT32(tim_psc, STM32F2XXTimerState),
        VMSTATE_UINT32(tim_arr, STM32F2XXTimerState),
        VMSTATE_UINT32(tim_ccr1, STM32F2XXTimerState),
        VMSTATE_UINT32(tim_ccr2, STM32F2XXTimerState),
        VMSTATE_UINT32(tim_ccr3, STM32F2XXTimerState),
        VMSTATE_UINT32(tim_ccr4, STM32F2XXTimerState),
        VMSTATE_UINT32(tim_dcr, STM32F2XXTimerState),
        VMSTATE_UINT32(tim_dmar, STM32F2XXTimerState),
        VMSTATE_UINT32(tim_or, STM32F2XXTimerState),
        VMSTATE_END_OF_LIST()
    }
};

static Property stm32f2xx_timer_properties[] = {
    DEFINE_PROP_UINT64("clock-frequency", struct STM32F2XXTimerState,
                       freq_hz, 1000000000),
    DEFINE_PROP_END_OF_LIST(),
};

static void stm32f2xx_timer_init(Object *obj)
{
    STM32F2XXTimerState *s = STM32F2XXTIMER(obj);

    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);

    memory_region_init_io(&s->iomem, obj, &stm32f2xx_timer_ops, s,
                          "stm32f2xx_timer", 0x400);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
}

static void stm32f2xx_timer_realize(DeviceState *dev, Error **errp)
{
    STM32F2XXTimerState *s = STM32F2XXTIMER(dev);
    s->timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, stm32f2xx_timer_interrupt, s);
}

static void stm32f2xx_timer_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = stm32f2xx_timer_reset;
    device_class_set_props(dc, stm32f2xx_timer_properties);
    dc->vmsd = &vmstate_stm32f2xx_timer;
    dc->realize = stm32f2xx_timer_realize;
}

static const TypeInfo stm32f2xx_timer_info = {
    .name          = TYPE_STM32F2XX_TIMER,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(STM32F2XXTimerState),
    .instance_init = stm32f2xx_timer_init,
    .class_init    = stm32f2xx_timer_class_init,
};

static void stm32f2xx_timer_register_types(void)
{
    type_register_static(&stm32f2xx_timer_info);
}

type_init(stm32f2xx_timer_register_types)
