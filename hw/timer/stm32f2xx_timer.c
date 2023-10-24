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

#define STM_TIMER_ERR_DEBUG 0
#ifndef STM_TIMER_ERR_DEBUG
#define STM_TIMER_ERR_DEBUG 0
#endif

//PCLK /4
#define CLOCK_FREQUENCY 48000000ULL

#define DB_PRINT_L(lvl, fmt, args...) do { \
    if (STM_TIMER_ERR_DEBUG >= lvl) { \
        qemu_log("%s: " fmt, __func__, ## args); \
    } \
} while (0)

#define DB_PRINT(fmt, args...) DB_PRINT_L(1, fmt, ## args)


static uint32_t stm32f2xx_timer_get_count(STM32F2XXTimerState *s)
{
    uint64_t cnt = ptimer_get_count(s->timer);
    if (s->count_mode == TIMER_UP_COUNT)
    {
        return s->tim_arr - (cnt & 0xfffff);
    }
    else
    {
        return (cnt & 0xffff);
    }
}


static void stm32f2xx_timer_set_count(STM32F2XXTimerState *s, uint32_t cnt)
{
    if (s->count_mode == TIMER_UP_COUNT)
    {
        ptimer_set_count(s->timer, s->tim_arr - (cnt & 0xfffff));
    }
    else
    {
        ptimer_set_count(s->timer, cnt & 0xffff);
    }
}

static void stm32f2xx_timer_update(STM32F2XXTimerState *s)
{
    if (s->tim_cr1 & 0x10) /* dir bit */
    {
        s->count_mode = TIMER_DOWN_COUNT;
    }
    else
    {
        s->count_mode = TIMER_UP_COUNT;
    }

    if (s->tim_cr1 & 0x060) /* CMS */
    {
        s->count_mode = TIMER_UP_COUNT;
    }

    if (s->tim_cr1 & 0x01) /* timer enable */
    {
        DB_PRINT("Enabling timer\n");
        ptimer_set_freq(s->timer, s->freq_hz);
        ptimer_run(s->timer, !(s->tim_cr1 & 0x04));
    }
    else
    {
        DB_PRINT("Disabling timer\n");
        ptimer_stop(s->timer);
    }
}

static void stm32f2xx_timer_update_uif(STM32F2XXTimerState *s, uint8_t value) {
    s->tim_sr &= ~0x1;
    s->tim_sr |= (value & 0x1);
    qemu_set_irq(s->irq, value);
}

static void stm32f2xx_timer_tick(void *opaque)
{
    STM32F2XXTimerState *s = (STM32F2XXTimerState *)opaque;
    DB_PRINT("Alarm raised\n");
    stm32f2xx_timer_update_uif(s, 1);

    if (s->count_mode == TIMER_UP_COUNT)
    {
        stm32f2xx_timer_set_count(s, 0);
    }
    else
    {
        stm32f2xx_timer_set_count(s, s->tim_arr);
    }
    if (s->tim_cr1 & 0x0060) /* CMS */
    {
        if (s->count_mode == TIMER_UP_COUNT)
        {
            s->count_mode = TIMER_DOWN_COUNT;
        }
        else
        {
            s->count_mode = TIMER_UP_COUNT;
        }
    }

    if (s->tim_cr1 & 0x04) /* one shot */
    {
        s->tim_cr1 &= 0xFFFE;
    }
    else
    {
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

static void stm32f2xx_update_sr(STM32F2XXTimerState *s, uint64_t value)
{
    s->tim_sr ^= (value ^ 0xFFFF);
    s->tim_sr &= 0x1eFF;
    ptimer_transaction_begin(s->timer);
    stm32f2xx_timer_update_uif(s, s->tim_sr & 0x1);
    ptimer_transaction_commit(s->timer);
    DB_PRINT("write sr = %x\n",s->tim_sr);
}

static void stm32f2xx_update_psc(STM32F2XXTimerState *s, uint64_t value)
{
   s->tim_psc = value & 0xffff;
    ptimer_transaction_begin(s->timer);
    ptimer_set_freq(s->timer, s->freq_hz);
    ptimer_transaction_commit(s->timer);
    DB_PRINT("write psc = %x\n",  s->tim_psc);
}

static void stm32f2xx_update_egr(STM32F2XXTimerState *s, uint64_t value)
{
    s->tim_egr = value & 0x1E;
    if (value & 0x40) {
        /* TG bit */
        s->tim_sr |= 0x40;
    }
    if (value & 0x1) {
            /* UG bit - reload count */
        ptimer_transaction_begin(s->timer);
        ptimer_set_limit(s->timer, s->tim_arr, 1);
        ptimer_transaction_commit(s->timer);
    }
    DB_PRINT("write EGR = %x\n", s->tim_egr);
}

static void stm32f2xx_update_cnt(STM32F2XXTimerState *s, uint64_t value)
{
    ptimer_transaction_begin(s->timer);
    stm32f2xx_timer_set_count(s, value & 0xffff);
    ptimer_transaction_commit(s->timer);
    DB_PRINT("write cnt = %x\n", stm32f2xx_timer_get_count(s));
}

static void stm32f2xx_update_arr(STM32F2XXTimerState *s, uint64_t value)
{
    s->tim_arr = value & 0xffff;
    ptimer_transaction_begin(s->timer);
    ptimer_set_limit(s->timer, s->tim_arr, 1);
    ptimer_transaction_commit(s->timer);
    DB_PRINT("write arr = %x\n",  s->tim_arr);
}

static void stm32f2xx_timer_write(void *opaque, hwaddr offset,
                        uint64_t value, unsigned size)
{
    STM32F2XXTimerState *s = opaque;

    switch (offset) {
    case TIM_CR1:
       stm32f2xx_update_cr1(s, value);
       break;
    case TIM_CR2:
        qemu_log_mask(LOG_GUEST_ERROR, "stm32_timer: CR2 not supported");
        break;
    case TIM_SMCR:
        qemu_log_mask(LOG_GUEST_ERROR, "stm32_timer: SCMR not supported");
        break;
    case TIM_DIER:
        s->tim_dier = value & 0x5F5F;
        DB_PRINT("write dier = %x\n", s->tim_dier);
        break;
    case TIM_SR:
        stm32f2xx_update_sr(s, value);
        break;
    case TIM_EGR:
        stm32f2xx_update_egr(s, value);
        break;
    case TIM_CCMR1:
        s->tim_ccmr1 = value & 0xffff;
        DB_PRINT("write ccmr1 = %x\n", s->tim_ccmr1);
        break;
    case TIM_CCMR2:
        s->tim_ccmr2 = value & 0xffff;
        DB_PRINT("write ccmr2 = %x\n", s->tim_ccmr2);
        break;
    case TIM_CCER:
        s->tim_ccer = value & 0x3333;
        DB_PRINT("write ccer = %x\n", s->tim_ccer);
        break;
    case TIM_PSC:
        stm32f2xx_update_psc(s, value);
        break;
    case TIM_CNT:
        stm32f2xx_update_cnt(s, value);
        break;
    case TIM_ARR:
        stm32f2xx_update_arr(s, value);
        break;
    case TIM_CCR1:
        s->tim_ccr1 = value & 0xffff;
        break;
    case TIM_CCR2:
        s->tim_ccr2 = value & 0xffff;
        break;
    case TIM_CCR3:
        s->tim_ccr3 = value & 0xffff;
        break;
    case TIM_CCR4:
        s->tim_ccr4 = value & 0xffff;
        break;
    case TIM_DCR:
        qemu_log_mask(LOG_GUEST_ERROR, "stm32_timer: DCR not supported");
        break;
    case TIM_DMAR:
        qemu_log_mask(LOG_GUEST_ERROR, "stm32_timer: DMAR not supported");
        break;
    case TIM_OR:
        qemu_log_mask(LOG_GUEST_ERROR, "stm32_timer: OR not supported");
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Bad offset 0x%"HWADDR_PRIx"\n", __func__, offset);
        break;
    }
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
        VMSTATE_INT32(count_mode, STM32F2XXTimerState),
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
                       freq_hz, CLOCK_FREQUENCY),
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
    s->timer = ptimer_init(stm32f2xx_timer_tick, s, PTIMER_POLICY_LEGACY);
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
