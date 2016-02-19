/*
 * Terminal 3270 implementation
 *
 * Copyright 2017 IBM Corp.
 *
 * Authors: Yang Chen <bjcyang@linux.vnet.ibm.com>
 *          Jing Liu <liujbjl@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */

#include "qemu/osdep.h"
#include "hw/s390x/3270-ccw.h"

typedef struct Terminal3270 {
    EmulatedCcw3270Device cdev;
} Terminal3270;

#define TYPE_TERMINAL_3270 "x-terminal3270"

static void terminal_init(EmulatedCcw3270Device *dev, Error **errp)
{
    static bool terminal_available;

    if (terminal_available) {
        error_setg(errp, "Multiple 3270 terminals are not supported.");
        return;
    }
    terminal_available = true;
}

static void terminal_class_init(ObjectClass *klass, void *data)
{
    EmulatedCcw3270Class *ck = EMULATED_CCW_3270_CLASS(klass);

    ck->init = terminal_init;
}

static const TypeInfo ccw_terminal_info = {
    .name = TYPE_TERMINAL_3270,
    .parent = TYPE_EMULATED_CCW_3270,
    .instance_size = sizeof(Terminal3270),
    .class_init = terminal_class_init,
    .class_size = sizeof(EmulatedCcw3270Class),
};

static void register_types(void)
{
    type_register_static(&ccw_terminal_info);
}

type_init(register_types)
