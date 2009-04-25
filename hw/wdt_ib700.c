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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301,
 * USA.
 *
 * By Richard W.M. Jones (rjones@redhat.com).
 */

#include "qemu-common.h"
#include "qemu-timer.h"
#include "watchdog.h"
#include "hw.h"
#include "isa.h"
#include "pc.h"

/*#define IB700_DEBUG 1*/

#ifdef IB700_DEBUG
#define ib700_debug(fs,...)					\
    fprintf(stderr,"ib700: %s: "fs,__func__,##__VA_ARGS__)
#else
#define ib700_debug(fs,...)
#endif

/* This is the timer.  We use a global here because the watchdog
 * code ensures there is only one watchdog (it is located at a fixed,
 * unchangable IO port, so there could only ever be one anyway).
 */
static QEMUTimer *timer = NULL;

/* A write to this register enables the timer. */
static void ib700_write_enable_reg(void *vp, uint32_t addr, uint32_t data)
{
    static int time_map[] = {
        30, 28, 26, 24, 22, 20, 18, 16,
        14, 12, 10,  8,  6,  4,  2,  0
    };
    int64 timeout;

    ib700_debug("addr = %x, data = %x\n", addr, data);

    timeout = (int64_t) time_map[data & 0xF] * ticks_per_sec;
    qemu_mod_timer(timer, qemu_get_clock (vm_clock) + timeout);
}

/* A write (of any value) to this register disables the timer. */
static void ib700_write_disable_reg(void *vp, uint32_t addr, uint32_t data)
{
    ib700_debug("addr = %x, data = %x\n", addr, data);

    qemu_del_timer(timer);
}

/* This is called when the watchdog expires. */
static void ib700_timer_expired(void *vp)
{
    ib700_debug("watchdog expired\n");

    watchdog_perform_action();
    qemu_del_timer(timer);
}

static void ib700_save(QEMUFile *f, void *vp)
{
    qemu_put_timer(f, timer);
}

static int ib700_load(QEMUFile *f, void *vp, int version)
{
    if (version != 0)
        return -EINVAL;

    qemu_get_timer(f, timer);

    return 0;
}

/* Create and initialize a virtual IB700 during PC creation. */
static void ib700_pc_init(PCIBus *unused)
{
    register_savevm("ib700_wdt", -1, 0, ib700_save, ib700_load, NULL);

    register_ioport_write(0x441, 2, 1, ib700_write_disable_reg, NULL);
    register_ioport_write(0x443, 2, 1, ib700_write_enable_reg, NULL);
}

static WatchdogTimerModel model = {
    .wdt_name = "ib700",
    .wdt_description = "iBASE 700",
    .wdt_pc_init = ib700_pc_init,
};

void wdt_ib700_init(void)
{
    watchdog_add_model(&model);
    timer = qemu_new_timer(vm_clock, ib700_timer_expired, NULL);
}
