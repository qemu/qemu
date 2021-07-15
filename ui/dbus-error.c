/*
 * QEMU DBus display errors
 *
 * Copyright (c) 2021 Marc-André Lureau <marcandre.lureau@redhat.com>
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
#include "dbus.h"

static const GDBusErrorEntry dbus_display_error_entries[] = {
    { DBUS_DISPLAY_ERROR_FAILED, "org.qemu.Display1.Error.Failed" },
    { DBUS_DISPLAY_ERROR_INVALID, "org.qemu.Display1.Error.Invalid" },
    { DBUS_DISPLAY_ERROR_UNSUPPORTED, "org.qemu.Display1.Error.Unsupported" },
};

G_STATIC_ASSERT(G_N_ELEMENTS(dbus_display_error_entries) ==
                DBUS_DISPLAY_N_ERRORS);

GQuark
dbus_display_error_quark(void)
{
    static gsize quark;

    g_dbus_error_register_error_domain(
        "dbus-display-error-quark",
        &quark,
        dbus_display_error_entries,
        G_N_ELEMENTS(dbus_display_error_entries));

    return (GQuark)quark;
}
