/* SPDX-License-Identifier: GPL-2.0-or-later */

/*
 * Stubs for audio test - provides missing functions for standalone audio test
 */

#include "qemu/osdep.h"
#include "qemu/dbus.h"
#include "ui/qemu-spice-module.h"
#include "ui/dbus-module.h"
#include "system/replay.h"
#include "system/runstate.h"

int using_spice;
int using_dbus_display;

struct QemuSpiceOps qemu_spice;

GQuark dbus_display_error_quark(void)
{
    return g_quark_from_static_string("dbus-display-error-quark");
}

#ifdef WIN32
/* from ui/dbus.h */
bool
dbus_win32_import_socket(GDBusMethodInvocation *invocation,
                         GVariant *arg_listener, int *socket);

bool
dbus_win32_import_socket(GDBusMethodInvocation *invocation,
                         GVariant *arg_listener, int *socket)
{
    return true;
}
#endif

void replay_audio_in(size_t *recorded, st_sample *samples,
                     size_t *wpos, size_t size)
{
}

void replay_audio_out(size_t *played)
{
}

static int dummy_vmse;

VMChangeStateEntry *qemu_add_vm_change_state_handler(VMChangeStateHandler *cb,
                                                     void *opaque)
{
    return (VMChangeStateEntry *)&dummy_vmse;
}

void qemu_del_vm_change_state_handler(VMChangeStateEntry *e)
{
}

bool runstate_is_running(void)
{
    return true;
}
