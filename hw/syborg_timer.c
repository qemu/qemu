/*
 * Syborg Interval Timer.
 *
 * Copyright (c) 2008 CodeSourcery
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

#include "sysbus.h"
#include "qemu-timer.h"
#include "syborg.h"

//#define DEBUG_SYBORG_TIMER

#ifdef DEBUG_SYBORG_TIMER
#define DPRINTF(fmt, ...) \
do { printf("syborg_timer: " fmt , ##args); } while (0)
#define BADF(fmt, ...) \
do { fprintf(stderr, "syborg_timer: error: " fmt , ## __VA_ARGS__); \
    exit(1);} while (0)
#else
#define DPRINTF(fmt, ...) do {} while(0)
#define BADF(fmt, ...) \
do { fprintf(stderr, "syborg_timer: error: " fmt , ## __VA_ARGS__);} while (0)
#endif

enum {
    TIMER_ID          = 0,
    TIMER_RUNNING     = 1,
    TIMER_ONESHOT     = 2,
    TIMER_LIMIT       = 3,
    TIMER_VALUE       = 4,
    TIMER_INT_ENABLE  = 5,
    TIMER_INT_STATUS  = 6,
    TIMER_FREQ        = 7
};

typedef struct {
    SysBusDevice busdev;
    ptimer_state *timer;
    int running;
    int oneshot;
    uint32_t limit;
    uint32_t freq;
    uint32_t int_level;
    uint32_t int_enabled;
    qemu_irq irq;
} SyborgTimerState;

static void syborg_timer_update(SyborgTimerState *s)
{
    /* Update interrupt.  */
    if (s->int_level && s->int_enabled) {
        qemu_irq_raise(s->irq);
    } else {
        qemu_irq_lower(s->irq);
    }
}

static void syborg_timer_tick(void *opaque)
{
    SyborgTimerState *s = (SyborgTimerState *)opaque;
    //DPRINTF("Timer Tick\n");
    s->int_level = 1;
    if (s->oneshot)
        s->running = 0;
    syborg_timer_update(s);
}

static uint32_t syborg_timer_read(void *opaque, target_phys_addr_t offset)
{
    SyborgTimerState *s = (SyborgTimerState *)opaque;

    DPRINTF("Reg read %d\n", (int)offset);
    offset &= 0xfff;
    switch (offset >> 2) {
    case TIMER_ID:
        return SYBORG_ID_TIMER;
    case TIMER_RUNNING:
        return s->running;
    case TIMER_ONESHOT:
        return s->oneshot;
    case TIMER_LIMIT:
        return s->limit;
    case TIMER_VALUE:
        return ptimer_get_count(s->timer);
    case TIMER_INT_ENABLE:
        return s->int_enabled;
    case TIMER_INT_STATUS:
        return s->int_level;
    case TIMER_FREQ:
        return s->freq;
    default:
        cpu_abort(cpu_single_env, "syborg_timer_read: Bad offset %x\n",
                  (int)offset);
        return 0;
    }
}

static void syborg_timer_write(void *opaque, target_phys_addr_t offset,
                               uint32_t value)
{
    SyborgTimerState *s = (SyborgTimerState *)opaque;

    DPRINTF("Reg write %d\n", (int)offset);
    offset &= 0xfff;
    switch (offset >> 2) {
    case TIMER_RUNNING:
        if (value == s->running)
            break;
        s->running = value;
        if (value) {
            ptimer_run(s->timer, s->oneshot);
        } else {
            ptimer_stop(s->timer);
        }
        break;
    case TIMER_ONESHOT:
        if (s->running) {
            ptimer_stop(s->timer);
        }
        s->oneshot = value;
        if (s->running) {
            ptimer_run(s->timer, s->oneshot);
        }
        break;
    case TIMER_LIMIT:
        s->limit = value;
        ptimer_set_limit(s->timer, value, 1);
        break;
    case TIMER_VALUE:
        ptimer_set_count(s->timer, value);
        break;
    case TIMER_INT_ENABLE:
        s->int_enabled = value;
        syborg_timer_update(s);
        break;
    case TIMER_INT_STATUS:
        s->int_level &= ~value;
        syborg_timer_update(s);
        break;
    default:
        cpu_abort(cpu_single_env, "syborg_timer_write: Bad offset %x\n",
                  (int)offset);
        break;
    }
}

static CPUReadMemoryFunc * const syborg_timer_readfn[] = {
    syborg_timer_read,
    syborg_timer_read,
    syborg_timer_read
};

static CPUWriteMemoryFunc * const syborg_timer_writefn[] = {
    syborg_timer_write,
    syborg_timer_write,
    syborg_timer_write
};

static const VMStateDescription vmstate_syborg_timer = {
    .name = "syborg_timer",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields      = (VMStateField[]) {
        VMSTATE_INT32(running, SyborgTimerState),
        VMSTATE_INT32(oneshot, SyborgTimerState),
        VMSTATE_UINT32(limit, SyborgTimerState),
        VMSTATE_UINT32(int_level, SyborgTimerState),
        VMSTATE_UINT32(int_enabled, SyborgTimerState),
        VMSTATE_PTIMER(timer, SyborgTimerState),
        VMSTATE_END_OF_LIST()
    }
};

static int syborg_timer_init(SysBusDevice *dev)
{
    SyborgTimerState *s = FROM_SYSBUS(SyborgTimerState, dev);
    QEMUBH *bh;
    int iomemtype;

    if (s->freq == 0) {
        fprintf(stderr, "syborg_timer: Zero/unset frequency\n");
        exit(1);
    }
    sysbus_init_irq(dev, &s->irq);
    iomemtype = cpu_register_io_memory(syborg_timer_readfn,
                                       syborg_timer_writefn, s,
                                       DEVICE_NATIVE_ENDIAN);
    sysbus_init_mmio(dev, 0x1000, iomemtype);

    bh = qemu_bh_new(syborg_timer_tick, s);
    s->timer = ptimer_init(bh);
    ptimer_set_freq(s->timer, s->freq);
    vmstate_register(&dev->qdev, -1, &vmstate_syborg_timer, s);
    return 0;
}

static SysBusDeviceInfo syborg_timer_info = {
    .init = syborg_timer_init,
    .qdev.name  = "syborg,timer",
    .qdev.size  = sizeof(SyborgTimerState),
    .qdev.props = (Property[]) {
        DEFINE_PROP_UINT32("frequency",SyborgTimerState, freq, 0),
        DEFINE_PROP_END_OF_LIST(),
    }
};

static void syborg_timer_register_devices(void)
{
    sysbus_register_withprop(&syborg_timer_info);
}

device_init(syborg_timer_register_devices)
