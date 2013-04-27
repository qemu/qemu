/*
 * Virtual hardware watchdog.
 *
 * Copyright (C) 2009 Red Hat Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * By Richard W.M. Jones (rjones@redhat.com).
 */

#include "qemu-common.h"
#include "qemu/timer.h"
#include "sysemu/watchdog.h"
#include "hw/hw.h"
#include "hw/isa/isa.h"
#include "hw/i386/pc.h"

/*#define IB700_DEBUG 1*/

#ifdef IB700_DEBUG
#define ib700_debug(fs,...)					\
    fprintf(stderr,"ib700: %s: "fs,__func__,##__VA_ARGS__)
#else
#define ib700_debug(fs,...)
#endif

#define TYPE_IB700 "ib700"
#define IB700(obj) OBJECT_CHECK(IB700State, (obj), TYPE_IB700)

typedef struct IB700state {
    ISADevice parent_obj;

    QEMUTimer *timer;
} IB700State;

/* This is the timer.  We use a global here because the watchdog
 * code ensures there is only one watchdog (it is located at a fixed,
 * unchangeable IO port, so there could only ever be one anyway).
 */

/* A write to this register enables the timer. */
static void ib700_write_enable_reg(void *vp, uint32_t addr, uint32_t data)
{
    IB700State *s = vp;
    static int time_map[] = {
        30, 28, 26, 24, 22, 20, 18, 16,
        14, 12, 10,  8,  6,  4,  2,  0
    };
    int64_t timeout;

    ib700_debug("addr = %x, data = %x\n", addr, data);

    timeout = (int64_t) time_map[data & 0xF] * get_ticks_per_sec();
    qemu_mod_timer(s->timer, qemu_get_clock_ns (vm_clock) + timeout);
}

/* A write (of any value) to this register disables the timer. */
static void ib700_write_disable_reg(void *vp, uint32_t addr, uint32_t data)
{
    IB700State *s = vp;

    ib700_debug("addr = %x, data = %x\n", addr, data);

    qemu_del_timer(s->timer);
}

/* This is called when the watchdog expires. */
static void ib700_timer_expired(void *vp)
{
    IB700State *s = vp;

    ib700_debug("watchdog expired\n");

    watchdog_perform_action();
    qemu_del_timer(s->timer);
}

static const VMStateDescription vmstate_ib700 = {
    .name = "ib700_wdt",
    .version_id = 0,
    .minimum_version_id = 0,
    .minimum_version_id_old = 0,
    .fields      = (VMStateField []) {
        VMSTATE_TIMER(timer, IB700State),
        VMSTATE_END_OF_LIST()
    }
};

static int wdt_ib700_init(ISADevice *dev)
{
    IB700State *s = IB700(dev);

    ib700_debug("watchdog init\n");

    s->timer = qemu_new_timer_ns(vm_clock, ib700_timer_expired, s);
    register_ioport_write(0x441, 2, 1, ib700_write_disable_reg, s);
    register_ioport_write(0x443, 2, 1, ib700_write_enable_reg, s);

    return 0;
}

static void wdt_ib700_reset(DeviceState *dev)
{
    IB700State *s = IB700(dev);

    ib700_debug("watchdog reset\n");

    qemu_del_timer(s->timer);
}

static WatchdogTimerModel model = {
    .wdt_name = "ib700",
    .wdt_description = "iBASE 700",
};

static void wdt_ib700_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ISADeviceClass *ic = ISA_DEVICE_CLASS(klass);
    ic->init = wdt_ib700_init;
    dc->reset = wdt_ib700_reset;
    dc->vmsd = &vmstate_ib700;
}

static const TypeInfo wdt_ib700_info = {
    .name          = TYPE_IB700,
    .parent        = TYPE_ISA_DEVICE,
    .instance_size = sizeof(IB700State),
    .class_init    = wdt_ib700_class_init,
};

static void wdt_ib700_register_types(void)
{
    watchdog_add_model(&model);
    type_register_static(&wdt_ib700_info);
}

type_init(wdt_ib700_register_types)
