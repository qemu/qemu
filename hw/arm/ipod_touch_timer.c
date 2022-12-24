#include "hw/arm/ipod_touch_timer.h"

static void s5l8900_st_update(IPodTouchTimerState *s)
{
    s->freq_out = 1000000000 / 100; 
    s->tick_interval = /* bcount1 * get_ticks / freq  + ((bcount2 * get_ticks / freq)*/
    muldiv64((s->bcount1 < 1000) ? 1000 : s->bcount1, NANOSECONDS_PER_SECOND, s->freq_out);
    s->next_planned_tick = 0;
}

static void s5l8900_st_set_timer(IPodTouchTimerState *s)
{
    uint64_t last = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) - s->base_time;

    s->next_planned_tick = last + (s->tick_interval - last % s->tick_interval);
    timer_mod(s->st_timer, s->next_planned_tick + s->base_time);
    s->last_tick = last;
}

static void s5l8900_st_tick(void *opaque)
{
    IPodTouchTimerState *s = (IPodTouchTimerState *)opaque;

    if (s->status & TIMER_STATE_START) {
        //fprintf(stderr, "%s: Raising irq\n", __func__);
        qemu_irq_raise(s->irq);

        /* schedule next interrupt */
        if(!(s->status & TIMER_STATE_MANUALUPDATE)) {
            s5l8900_st_set_timer(s);
        }
    } else {
        s->next_planned_tick = 0;
        s->last_tick = 0;
        timer_del(s->st_timer);
    }
}

static void s5l8900_timer1_write(void *opaque, hwaddr addr, uint64_t value, unsigned size)
{
    //fprintf(stderr, "%s: writing 0x%08x to 0x%08x\n", __func__, value, addr);
    IPodTouchTimerState *s = (struct IPodTouchTimerState *) opaque;

    switch(addr){

        case TIMER_IRQSTAT:
            s->irqstat = value;
            return;
        case TIMER_IRQLATCH:
            //fprintf(stderr, "%s: lowering irq\n", __func__);
            qemu_irq_lower(s->irq);     
            return;
        case TIMER_4 + TIMER_CONFIG:
            s5l8900_st_update(s);
            s->config = value;
            break;
        case TIMER_4 + TIMER_STATE:
            if (value & TIMER_STATE_START) {
                s->base_time = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
                s5l8900_st_update(s);
                s5l8900_st_set_timer(s);
            } else if (value == TIMER_STATE_STOP) {
                timer_del(s->st_timer);
            }
            s->status = value;
            break;
        case TIMER_4 + TIMER_COUNT_BUFFER:
            s->bcount1 = s->bcreload = value;
            break;
        case TIMER_4 + TIMER_COUNT_BUFFER2:
            s->bcount2 = value;
            break;
      default:
        break;
    }
}

static uint64_t s5l8900_timer1_read(void *opaque, hwaddr addr, unsigned size)
{
    //fprintf(stderr, "%s: read from location 0x%08x\n", __func__, addr);
    IPodTouchTimerState *s = (struct IPodTouchTimerState *) opaque;
    uint64_t elapsed_ns, ticks;

    switch (addr) {
        case TIMER_TICKSHIGH:    // needs to be fixed so that read from low first works as well

            elapsed_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) / 2; // the timer ticks twice as slow as the CPU frequency in the kernel
            ticks = clock_ns_to_ticks(s->sysclk, elapsed_ns);
            //printf("TICKS: %lld\n", ticks);
            s->ticks_high = (ticks >> 32);
            s->ticks_low = (ticks & 0xFFFFFFFF);
            return s->ticks_high;
        case TIMER_TICKSLOW:
            return s->ticks_low;
        case TIMER_IRQSTAT:
            return ~0; // s->irqstat;
        case TIMER_IRQLATCH:
            return 0xffffffff;

      default:
        break;
    }
    return 0;
}

static const MemoryRegionOps timer1_ops = {
    .read = s5l8900_timer1_read,
    .write = s5l8900_timer1_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void s5l8900_timer_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    DeviceState *dev = DEVICE(sbd);
    IPodTouchTimerState *s = IPOD_TOUCH_TIMER(dev);

    memory_region_init_io(&s->iomem, obj, &timer1_ops, s, "timer1", 0x10001);
    sysbus_init_irq(sbd, &s->irq);

    s->base_time = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    s->st_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, s5l8900_st_tick, s);
}

static void s5l8900_timer_class_init(ObjectClass *klass, void *data)
{

}

static const TypeInfo ipod_touch_timer_info = {
    .name          = TYPE_IPOD_TOUCH_TIMER,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(IPodTouchTimerState),
    .instance_init = s5l8900_timer_init,
    .class_init    = s5l8900_timer_class_init,
};

static void ipod_touch_machine_types(void)
{
    type_register_static(&ipod_touch_timer_info);
}

type_init(ipod_touch_machine_types)