/*
 * QEMU 8253/8254 interval timer emulation
 * 
 * Copyright (c) 2003-2004 Fabrice Bellard
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
#include "vl.h"

//#define DEBUG_PIT

#define RW_STATE_LSB 0
#define RW_STATE_MSB 1
#define RW_STATE_WORD0 2
#define RW_STATE_WORD1 3
#define RW_STATE_LATCHED_WORD0 4
#define RW_STATE_LATCHED_WORD1 5

PITChannelState pit_channels[3];

static void pit_irq_timer_update(PITChannelState *s, int64_t current_time);

static int pit_get_count(PITChannelState *s)
{
    uint64_t d;
    int counter;

    d = muldiv64(qemu_get_clock(vm_clock) - s->count_load_time, PIT_FREQ, ticks_per_sec);
    switch(s->mode) {
    case 0:
    case 1:
    case 4:
    case 5:
        counter = (s->count - d) & 0xffff;
        break;
    case 3:
        /* XXX: may be incorrect for odd counts */
        counter = s->count - ((2 * d) % s->count);
        break;
    default:
        counter = s->count - (d % s->count);
        break;
    }
    return counter;
}

/* get pit output bit */
int pit_get_out(PITChannelState *s, int64_t current_time)
{
    uint64_t d;
    int out;

    d = muldiv64(current_time - s->count_load_time, PIT_FREQ, ticks_per_sec);
    switch(s->mode) {
    default:
    case 0:
        out = (d >= s->count);
        break;
    case 1:
        out = (d < s->count);
        break;
    case 2:
        if ((d % s->count) == 0 && d != 0)
            out = 1;
        else
            out = 0;
        break;
    case 3:
        out = (d % s->count) < ((s->count + 1) >> 1);
        break;
    case 4:
    case 5:
        out = (d == s->count);
        break;
    }
    return out;
}

/* return -1 if no transition will occur.  */
static int64_t pit_get_next_transition_time(PITChannelState *s, 
                                            int64_t current_time)
{
    uint64_t d, next_time, base;
    int period2;

    d = muldiv64(current_time - s->count_load_time, PIT_FREQ, ticks_per_sec);
    switch(s->mode) {
    default:
    case 0:
    case 1:
        if (d < s->count)
            next_time = s->count;
        else
            return -1;
        break;
    case 2:
        base = (d / s->count) * s->count;
        if ((d - base) == 0 && d != 0)
            next_time = base + s->count;
        else
            next_time = base + s->count + 1;
        break;
    case 3:
        base = (d / s->count) * s->count;
        period2 = ((s->count + 1) >> 1);
        if ((d - base) < period2) 
            next_time = base + period2;
        else
            next_time = base + s->count;
        break;
    case 4:
    case 5:
        if (d < s->count)
            next_time = s->count;
        else if (d == s->count)
            next_time = s->count + 1;
        else
            return -1;
        break;
    }
    /* convert to timer units */
    next_time = s->count_load_time + muldiv64(next_time, ticks_per_sec, PIT_FREQ);
    return next_time;
}

/* val must be 0 or 1 */
void pit_set_gate(PITChannelState *s, int val)
{
    switch(s->mode) {
    default:
    case 0:
    case 4:
        /* XXX: just disable/enable counting */
        break;
    case 1:
    case 5:
        if (s->gate < val) {
            /* restart counting on rising edge */
            s->count_load_time = qemu_get_clock(vm_clock);
            pit_irq_timer_update(s, s->count_load_time);
        }
        break;
    case 2:
    case 3:
        if (s->gate < val) {
            /* restart counting on rising edge */
            s->count_load_time = qemu_get_clock(vm_clock);
            pit_irq_timer_update(s, s->count_load_time);
        }
        /* XXX: disable/enable counting */
        break;
    }
    s->gate = val;
}

static inline void pit_load_count(PITChannelState *s, int val)
{
    if (val == 0)
        val = 0x10000;
    s->count_load_time = qemu_get_clock(vm_clock);
    s->count = val;
    pit_irq_timer_update(s, s->count_load_time);
}

static void pit_ioport_write(void *opaque, uint32_t addr, uint32_t val)
{
    int channel, access;
    PITChannelState *s;

    addr &= 3;
    if (addr == 3) {
        channel = val >> 6;
        if (channel == 3)
            return;
        s = &pit_channels[channel];
        access = (val >> 4) & 3;
        switch(access) {
        case 0:
            s->latched_count = pit_get_count(s);
            s->rw_state = RW_STATE_LATCHED_WORD0;
            break;
        default:
            s->mode = (val >> 1) & 7;
            s->bcd = val & 1;
            s->rw_state = access - 1 +  RW_STATE_LSB;
            /* XXX: update irq timer ? */
            break;
        }
    } else {
        s = &pit_channels[addr];
        switch(s->rw_state) {
        case RW_STATE_LSB:
            pit_load_count(s, val);
            break;
        case RW_STATE_MSB:
            pit_load_count(s, val << 8);
            break;
        case RW_STATE_WORD0:
        case RW_STATE_WORD1:
            if (s->rw_state & 1) {
                pit_load_count(s, (s->latched_count & 0xff) | (val << 8));
            } else {
                s->latched_count = val;
            }
            s->rw_state ^= 1;
            break;
        }
    }
}

static uint32_t pit_ioport_read(void *opaque, uint32_t addr)
{
    int ret, count;
    PITChannelState *s;
    
    addr &= 3;
    s = &pit_channels[addr];
    switch(s->rw_state) {
    case RW_STATE_LSB:
    case RW_STATE_MSB:
    case RW_STATE_WORD0:
    case RW_STATE_WORD1:
        count = pit_get_count(s);
        if (s->rw_state & 1)
            ret = (count >> 8) & 0xff;
        else
            ret = count & 0xff;
        if (s->rw_state & 2)
            s->rw_state ^= 1;
        break;
    default:
    case RW_STATE_LATCHED_WORD0:
    case RW_STATE_LATCHED_WORD1:
        if (s->rw_state & 1)
            ret = s->latched_count >> 8;
        else
            ret = s->latched_count & 0xff;
        s->rw_state ^= 1;
        break;
    }
    return ret;
}

static void pit_irq_timer_update(PITChannelState *s, int64_t current_time)
{
    int64_t expire_time;
    int irq_level;

    if (!s->irq_timer)
        return;
    expire_time = pit_get_next_transition_time(s, current_time);
    irq_level = pit_get_out(s, current_time);
    pic_set_irq(s->irq, irq_level);
#ifdef DEBUG_PIT
    printf("irq_level=%d next_delay=%f\n",
           irq_level, 
           (double)(expire_time - current_time) / ticks_per_sec);
#endif
    s->next_transition_time = expire_time;
    if (expire_time != -1)
        qemu_mod_timer(s->irq_timer, expire_time);
    else
        qemu_del_timer(s->irq_timer);
}

static void pit_irq_timer(void *opaque)
{
    PITChannelState *s = opaque;

    pit_irq_timer_update(s, s->next_transition_time);
}

static void pit_save(QEMUFile *f, void *opaque)
{
    PITChannelState *s;
    int i;
    
    for(i = 0; i < 3; i++) {
        s = &pit_channels[i];
        qemu_put_be32s(f, &s->count);
        qemu_put_be16s(f, &s->latched_count);
        qemu_put_8s(f, &s->rw_state);
        qemu_put_8s(f, &s->mode);
        qemu_put_8s(f, &s->bcd);
        qemu_put_8s(f, &s->gate);
        qemu_put_be64s(f, &s->count_load_time);
        if (s->irq_timer) {
            qemu_put_be64s(f, &s->next_transition_time);
            qemu_put_timer(f, s->irq_timer);
        }
    }
}

static int pit_load(QEMUFile *f, void *opaque, int version_id)
{
    PITChannelState *s;
    int i;
    
    if (version_id != 1)
        return -EINVAL;

    for(i = 0; i < 3; i++) {
        s = &pit_channels[i];
        qemu_get_be32s(f, &s->count);
        qemu_get_be16s(f, &s->latched_count);
        qemu_get_8s(f, &s->rw_state);
        qemu_get_8s(f, &s->mode);
        qemu_get_8s(f, &s->bcd);
        qemu_get_8s(f, &s->gate);
        qemu_get_be64s(f, &s->count_load_time);
        if (s->irq_timer) {
            qemu_get_be64s(f, &s->next_transition_time);
            qemu_get_timer(f, s->irq_timer);
        }
    }
    return 0;
}

void pit_init(int base, int irq)
{
    PITChannelState *s;
    int i;

    for(i = 0;i < 3; i++) {
        s = &pit_channels[i];
        if (i == 0) {
            /* the timer 0 is connected to an IRQ */
            s->irq_timer = qemu_new_timer(vm_clock, pit_irq_timer, s);
            s->irq = irq;
        }
        s->mode = 3;
        s->gate = (i != 2);
        pit_load_count(s, 0);
    }

    register_savevm("i8254", base, 1, pit_save, pit_load, NULL);

    register_ioport_write(base, 4, 1, pit_ioport_write, NULL);
    register_ioport_read(base, 3, 1, pit_ioport_read, NULL);
}

