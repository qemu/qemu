/*
 * QEMU X11 keymaps
 *
 * Copyright (C) 2009-2010 Daniel P. Berrange <dan@berrange.com>
 * Copyright (C) 2017 Red Hat, Inc
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1 as
 * published by the Free Software Foundation.
 */

#include "qemu/osdep.h"

#include "x_keymap.h"
#include "trace.h"
#include "qemu/notify.h"
#include "ui/input.h"

#include <X11/XKBlib.h>
#include <X11/Xutil.h>

static gboolean check_for_xwin(Display *dpy)
{
    const char *vendor = ServerVendor(dpy);

    trace_xkeymap_vendor(vendor);

    if (strstr(vendor, "Cygwin/X")) {
        return TRUE;
    }

    return FALSE;
}

static gboolean check_for_xquartz(Display *dpy)
{
    int nextensions;
    int i;
    gboolean match = FALSE;
    char **extensions = XListExtensions(dpy, &nextensions);
    for (i = 0 ; extensions != NULL && i < nextensions ; i++) {
        trace_xkeymap_extension(extensions[i]);
        if (strcmp(extensions[i], "Apple-WM") == 0 ||
            strcmp(extensions[i], "Apple-DRI") == 0) {
            match = TRUE;
        }
    }
    if (extensions) {
        XFreeExtensionList(extensions);
    }

    return match;
}

const guint16 *qemu_xkeymap_mapping_table(Display *dpy, size_t *maplen)
{
    XkbDescPtr desc;
    const gchar *keycodes = NULL;
    const guint16 *map;

    /* There is no easy way to determine what X11 server
     * and platform & keyboard driver is in use. Thus we
     * do best guess heuristics.
     *
     * This will need more work for people with other
     * X servers..... patches welcomed.
     */

    desc = XkbGetMap(dpy,
                     XkbGBN_AllComponentsMask,
                     XkbUseCoreKbd);
    if (desc) {
        if (XkbGetNames(dpy, XkbKeycodesNameMask, desc) == Success) {
            keycodes = XGetAtomName (dpy, desc->names->keycodes);
            if (!keycodes) {
                g_warning("could not lookup keycode name");
            } else {
                trace_xkeymap_keycodes(keycodes);
            }
        }
        XkbFreeKeyboard(desc, XkbGBN_AllComponentsMask, True);
    }

    if (check_for_xwin(dpy)) {
        trace_xkeymap_keymap("xwin");
        *maplen = qemu_input_map_xorgxwin_to_qcode_len;
        map = qemu_input_map_xorgxwin_to_qcode;
    } else if (check_for_xquartz(dpy)) {
        trace_xkeymap_keymap("xquartz");
        *maplen = qemu_input_map_xorgxquartz_to_qcode_len;
        map = qemu_input_map_xorgxquartz_to_qcode;
    } else if ((keycodes && g_str_has_prefix(keycodes, "evdev")) ||
               (XKeysymToKeycode(dpy, XK_Page_Up) == 0x70)) {
        trace_xkeymap_keymap("evdev");
        *maplen = qemu_input_map_xorgevdev_to_qcode_len;
        map = qemu_input_map_xorgevdev_to_qcode;
    } else if ((keycodes && g_str_has_prefix(keycodes, "xfree86")) ||
               (XKeysymToKeycode(dpy, XK_Page_Up) == 0x63)) {
        trace_xkeymap_keymap("kbd");
        *maplen = qemu_input_map_xorgkbd_to_qcode_len;
        map = qemu_input_map_xorgkbd_to_qcode;
    } else {
        trace_xkeymap_keymap("NULL");
        g_warning("Unknown X11 keycode mapping '%s'.\n"
                  "Please report to qemu-devel@nongnu.org\n"
                  "including the following information:\n"
                  "\n"
                  "  - Operating system\n"
                  "  - X11 Server\n"
                  "  - xprop -root\n"
                  "  - xdpyinfo\n",
                  keycodes ? keycodes : "<null>");
        map = NULL;
    }
    if (keycodes) {
        XFree((void *)keycodes);
    }
    return map;
}
