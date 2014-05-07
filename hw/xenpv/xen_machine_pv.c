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

#include "hw/hw.h"
#include "hw/boards.h"
#include "hw/xen/xen_backend.h"
#include "xen_domainbuild.h"
#include "sysemu/blockdev.h"

static void xen_init_pv(QEMUMachineInitArgs *args)
{
    const char *kernel_filename = args->kernel_filename;
    const char *kernel_cmdline = args->kernel_cmdline;
    const char *initrd_filename = args->initrd_filename;
    DriveInfo *dinfo;
    int i;

    /* Initialize backend core & drivers */
    if (xen_be_init() != 0) {
        fprintf(stderr, "%s: xen backend core setup failed\n", __FUNCTION__);
        exit(1);
    }

    switch (xen_mode) {
    case XEN_ATTACH:
        /* nothing to do, xend handles everything */
        break;
    case XEN_CREATE:
        if (xen_domain_build_pv(kernel_filename, initrd_filename,
                                kernel_cmdline) < 0) {
            fprintf(stderr, "xen pv domain creation failed\n");
            exit(1);
        }
        break;
    case XEN_EMULATE:
        fprintf(stderr, "xen emulation not implemented (yet)\n");
        exit(1);
        break;
    }

    xen_be_register("console", &xen_console_ops);
    xen_be_register("vkbd", &xen_kbdmouse_ops);
    xen_be_register("vfb", &xen_framebuffer_ops);
    xen_be_register("qdisk", &xen_blkdev_ops);
    xen_be_register("qnic", &xen_netdev_ops);

    /* configure framebuffer */
    if (xenfb_enabled) {
        xen_config_dev_vfb(0, "vnc");
        xen_config_dev_vkbd(0);
    }

    /* configure disks */
    for (i = 0; i < 16; i++) {
        dinfo = drive_get(IF_XEN, 0, i);
        if (!dinfo)
            continue;
        xen_config_dev_blk(dinfo);
    }

    /* configure nics */
    for (i = 0; i < nb_nics; i++) {
        if (!nd_table[i].model || 0 != strcmp(nd_table[i].model, "xen"))
            continue;
        xen_config_dev_nic(nd_table + i);
    }

    /* config cleanup hook */
    atexit(xen_config_cleanup);

    /* setup framebuffer */
    xen_init_display(xen_domid);
}

static QEMUMachine xenpv_machine = {
    .name = "xenpv",
    .desc = "Xen Para-virtualized PC",
    .init = xen_init_pv,
    .max_cpus = 1,
    .default_machine_opts = "accel=xen",
};

static void xenpv_machine_init(void)
{
    qemu_register_machine(&xenpv_machine);
}

machine_init(xenpv_machine_init);
