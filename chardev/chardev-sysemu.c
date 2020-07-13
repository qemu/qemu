/*
 * QEMU System Emulator
 *
 * Copyright (c) 2003-2008 Fabrice Bellard
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
#include "sysemu/sysemu.h"
#include "chardev/char.h"
#include "qemu/error-report.h"
#include "chardev-internal.h"

static int chardev_machine_done_notify_one(Object *child, void *opaque)
{
    Chardev *chr = (Chardev *)child;
    ChardevClass *class = CHARDEV_GET_CLASS(chr);

    if (class->chr_machine_done) {
        return class->chr_machine_done(chr);
    }

    return 0;
}

static void chardev_machine_done_hook(Notifier *notifier, void *unused)
{
    int ret = object_child_foreach(get_chardevs_root(),
                                   chardev_machine_done_notify_one, NULL);

    if (ret) {
        error_report("Failed to call chardev machine_done hooks");
        exit(1);
    }
}


static Notifier chardev_machine_done_notify = {
    .notify = chardev_machine_done_hook,
};

static void register_types(void)
{
    /*
     * This must be done after machine init, since we register FEs with muxes
     * as part of realize functions like serial_isa_realizefn when -nographic
     * is specified.
     */
    qemu_add_machine_init_done_notifier(&chardev_machine_done_notify);
}

type_init(register_types);
