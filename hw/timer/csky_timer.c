/*
 * CSKY timer emulation.
 *
 * Written by lyc
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "qemu/timer.h"
#include "sysemu/sysemu.h"
#include "qemu/cutils.h"
#include "qemu/log.h"
#include "hw/ptimer.h"
#include "cpu.h"
#include "hw/csky/cskydev.h"

#define TIMER_CTRL_ENABLE         (1 << 0)
#define TIMER_CTRL_MODE           (1 << 1)
#define TIMER_CTRL_IE             (1 << 2)
#define TIMER_CTRL_CLOCK          (1 << 3)

#define TYPE_CSKY_TIMER     "csky_timer"
#define CSKY_TIMER(obj)     OBJECT_CHECK(csky_timer_state, (obj), \
                                         TYPE_CSKY_TIMER)

uint32_t csky_timer_freq = 50000000ll;

typedef struct csky_timer_state {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    ptimer_state * timer[4];
    uint32_t control[4];
    uint32_t limit[4];
    int freq[4];
    int int_level[4];
    qemu_irq irq[4];
} csky_timer_state;

static void csky_timer_update(csky_timer_state *s, int index)
{
    /* Update interrupts.  */
    if (s->int_level[index] && !(s->control[index] & TIMER_CTRL_IE)) {
        qemu_irq_raise(s->irq[index]);
    } else {
        qemu_irq_lower(s->irq[index]);
    }
}

static uint32_t csky_timer_read(csky_timer_state *s, hwaddr offset, int index)
{
    switch (offset >> 2) {
    case 0: /* TimerN LoadCount */
        return s->limit[index];
    case 1: /* TimerN CurrentValue */
        return ptimer_get_count(s->timer[index]);
    case 2: /* TimerN ControlReg */
        return s->control[index];
    case 3: /* TimerN EOI */
        s->int_level[index] = 0;
        csky_timer_update(s, index);
        return 0;
    case 4: /* TimerN IntStatus */
        return s->int_level[index] && !(s->control[index] & TIMER_CTRL_IE);
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "csky_timer_read: Bad offset %x\n", (int)offset);
        return 0;
    }
}

static void csky_timer_reload(csky_timer_state *s, int reload, int index)
{
    uint32_t limit;
    if (s->control[index] & TIMER_CTRL_MODE) {
        limit = s->limit[index];
    } else {
        limit = s->limit[index];
    }
    ptimer_set_limit(s->timer[index], limit, reload);
}

static void csky_timer_write(csky_timer_state *s, hwaddr offset,
                             uint64_t value, int index)
{
    switch (offset >> 2) {
    case 0: /*TimerN LoadCount*/
        s->limit[index] = value;
        if (s->control[index] & TIMER_CTRL_ENABLE) {
            csky_timer_reload(s, 0, index);
            ptimer_run(s->timer[index], 0);
        }
        break;
    case 2: /*TimerN ControlReg*/
        if (s->control[index] & TIMER_CTRL_ENABLE) {
            /* Pause the timer if it is running. */
            ptimer_stop(s->timer[index]);
        }
        s->control[index] = value;
        csky_timer_reload(s, s->control[index] & TIMER_CTRL_ENABLE, index);
        ptimer_set_freq(s->timer[index], s->freq[index]);
        if (s->control[index] & TIMER_CTRL_ENABLE) {
            /* Restart the timer if still enabled. */
            ptimer_run(s->timer[index], 0);
        }
        break;

    case 1: /*TimerN CurrentValue*/
    case 3: /*TimerN EOI*/
    case 4: /*TimerN IntStatus*/
        return;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "csky_timer_write: Bad offset %x\n", (int)offset);
    }
    csky_timer_update(s, index);
}

static void csky_timer_tick0(void *opaque)
{
    csky_timer_state *s = (csky_timer_state *)opaque;
    csky_timer_reload(s, 1, 0);
    s->int_level[0] = 1;
    csky_timer_update(s, 0);
}

static void csky_timer_tick1(void *opaque)
{
    csky_timer_state *s = (csky_timer_state *)opaque;
    csky_timer_reload(s, 1, 1);
    s->int_level[1] = 1;
    csky_timer_update(s, 1);
}

static void csky_timer_tick2(void *opaque)
{
    csky_timer_state *s = (csky_timer_state *)opaque;
    csky_timer_reload(s, 1, 2);
    s->int_level[2] = 1;
    csky_timer_update(s, 2);
}

static void csky_timer_tick3(void *opaque)
{
    csky_timer_state *s = (csky_timer_state *)opaque;
    csky_timer_reload(s, 1, 3);
    s->int_level[3] = 1;
    csky_timer_update(s, 3);
}

static uint64_t csky_timers_read(void *opaque, hwaddr offset, unsigned size)
{
    csky_timer_state *s = (csky_timer_state *)opaque;
    int n;
    int i;
    uint32_t ret;

    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "csky_timers_read: Bad read size\n");
    }

    n = offset / 0x14;
    switch (n) {
    case 0: /*TimerN*/
    case 1:
    case 2:
    case 3:
        return csky_timer_read(s, offset % 0x14, n);
    case 8: /*Timer System Register*/
        switch ((offset % 0x14) >> 2) {
        case 0: /*TimersIntStatus*/
            ret = ((s->int_level[0] && !(s->control[0] & TIMER_CTRL_IE)) |
                   ((s->int_level[1] &&
                     !(s->control[1] & TIMER_CTRL_IE)) << 1) |
                   ((s->int_level[2] &&
                     !(s->control[2] & TIMER_CTRL_IE)) << 2) |
                   ((s->int_level[3] &&
                     !(s->control[3] & TIMER_CTRL_IE)) << 3));
            return ret;
        case 1: /*TimersEOI*/
            for (i = 0; i <= 3; i++) {
                s->int_level[i] = 0;
                csky_timer_update(s, i);
            }
            return 0;
        case 2: /*TimersRawIntStatus*/
            return (s->int_level[0] | (s->int_level[1] << 1) |
                    (s->int_level[2] << 2) | (s->int_level[3] << 3));

        default:
            qemu_log_mask(LOG_GUEST_ERROR,
                          "csky_timers_read: Bad offset %x\n", (int)offset);
            return 0;
        }

    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "csky_timers_read: Bad timer %d\n", n);
        return 0;
    }
}

static void csky_timers_write(void *opaque, hwaddr offset, uint64_t value,
                              unsigned size)
{
    csky_timer_state *s = (csky_timer_state *)opaque;
    int n;

    n = offset / 0x14;
    if (n > 3) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "csky_timers_write: Bad timer %d\n", n);
    }

    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "csky_timers_write: Bad write size\n");
    }

    csky_timer_write(s, offset % 0x14, value, n);
}

static const MemoryRegionOps csky_timer_ops = {
    .read = csky_timers_read,
    .write = csky_timers_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

void csky_timer_set_freq(uint32_t freq)
{
    csky_timer_freq = freq;
}

static void csky_timer_init(Object *obj)
{
    QEMUBH *bh;
    csky_timer_state *s = CSKY_TIMER(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    s->freq[0] = csky_timer_freq;
    bh = qemu_bh_new(csky_timer_tick0, s);
    s->timer[0] = ptimer_init(bh, PTIMER_POLICY_DEFAULT);
    sysbus_init_irq(sbd, &s->irq[0]);

    s->freq[1] = csky_timer_freq;
    bh = qemu_bh_new(csky_timer_tick1, s);
    s->timer[1] = ptimer_init(bh, PTIMER_POLICY_DEFAULT);
    sysbus_init_irq(sbd, &s->irq[1]);

    s->freq[2] = csky_timer_freq;
    bh = qemu_bh_new(csky_timer_tick2, s);
    s->timer[2] = ptimer_init(bh, PTIMER_POLICY_DEFAULT);
    sysbus_init_irq(sbd, &s->irq[2]);

    s->freq[3] = csky_timer_freq;
    bh = qemu_bh_new(csky_timer_tick3, s);
    s->timer[3] = ptimer_init(bh, PTIMER_POLICY_DEFAULT);
    sysbus_init_irq(sbd, &s->irq[3]);

    memory_region_init_io(&s->iomem, obj, &csky_timer_ops, s,
                          TYPE_CSKY_TIMER, 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);
}

static const VMStateDescription vmstate_csky_timer = {
    .name = TYPE_CSKY_TIMER,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PTIMER_ARRAY(timer, csky_timer_state, 4),
        VMSTATE_UINT32_ARRAY(control, csky_timer_state, 4),
        VMSTATE_UINT32_ARRAY(limit, csky_timer_state, 4),
        VMSTATE_INT32_ARRAY(freq, csky_timer_state, 4),
        VMSTATE_INT32_ARRAY(int_level, csky_timer_state, 4),
        VMSTATE_END_OF_LIST()
    }
};

static void csky_timer_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->vmsd = &vmstate_csky_timer;
}

static const TypeInfo csky_timer_info = {
    .name          = TYPE_CSKY_TIMER,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(csky_timer_state),
    .instance_init = csky_timer_init,
    .class_init    = csky_timer_class_init,
};

static void csky_timer_register_types(void)
{
    type_register_static(&csky_timer_info);
}

type_init(csky_timer_register_types)
