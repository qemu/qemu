/*
 * Stubs for qemu-vnc standalone binary.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "system/runstate.h"
#include "hw/core/qdev.h"
#include "monitor/monitor.h"
#include "migration/vmstate.h"

bool runstate_is_running(void)
{
    return true;
}

bool phase_check(MachineInitPhase phase)
{
    return true;
}

DeviceState *qdev_find_recursive(BusState *bus, const char *id)
{
    return NULL;
}

/*
 * Provide the monitor stubs locally so that the linker does not
 * pull stubs/monitor-core.c.o from libqemuutil.a (which would
 * bring a conflicting qapi_event_emit definition).
 */
Monitor *monitor_cur(void)
{
    return NULL;
}

Monitor *monitor_set_cur(Coroutine *co, Monitor *mon)
{
    return NULL;
}

/*
 * Link-time stubs for VMState symbols referenced by VNC code.
 * The standalone binary never performs migration, so these are
 * never actually used at runtime.
 */
const VMStateInfo vmstate_info_bool = {};
const VMStateInfo vmstate_info_int32 = {};
const VMStateInfo vmstate_info_uint32 = {};
const VMStateInfo vmstate_info_buffer = {};
