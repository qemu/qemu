/*
 * QEMU Xen PV Machine
 *
 * Copyright (c) 2007 Red Hat
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
#include "qemu/error-report.h"
#include "hw/boards.h"
#include "hw/xen/xen-legacy-backend.h"
#include "hw/xen/xen-bus.h"
#include "system/block-backend.h"
#include "system/system.h"

static void xen_init_pv(MachineState *machine)
{
    setup_xen_backend_ops();

    xen_bus_init();

    switch (xen_mode) {
    case XEN_ATTACH:
        /* nothing to do, libxl handles everything */
        break;
    case XEN_EMULATE:
        error_report("xen emulation not implemented (yet)");
        exit(1);
        break;
    default:
        error_report("unhandled xen_mode %d", xen_mode);
        exit(1);
        break;
    }

    /* configure framebuffer */
    if (vga_interface_type == VGA_XENFB) {
        xen_config_dev_vfb(0, "vnc");
        xen_config_dev_vkbd(0);
        vga_interface_created = true;
    }

    /* config cleanup hook */
    atexit(xen_config_cleanup);
}

static void xenpv_machine_init(MachineClass *mc)
{
    mc->desc = "Xen Para-virtualized PC";
    mc->init = xen_init_pv;
    mc->max_cpus = 1;
    mc->default_machine_opts = "accel=xen";
}

DEFINE_MACHINE("xenpv", xenpv_machine_init)
