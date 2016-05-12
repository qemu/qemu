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
#include "hw/hw.h"
#include "hw/boards.h"
#include "hw/sysbus.h"
#include "hw/xen/xen_backend.h"
#include "xen_domainbuild.h"
#include "sysemu/block-backend.h"

#define TYPE_XENSYSDEV "xensysdev"

DeviceState *xen_sysdev;

static void xen_init_pv(MachineState *machine)
{
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
#ifdef CONFIG_XEN_PV_DOMAIN_BUILD
    case XEN_CREATE: {
        const char *kernel_filename = machine->kernel_filename;
        const char *kernel_cmdline = machine->kernel_cmdline;
        const char *initrd_filename = machine->initrd_filename;
        if (xen_domain_build_pv(kernel_filename, initrd_filename,
                                kernel_cmdline) < 0) {
            fprintf(stderr, "xen pv domain creation failed\n");
            exit(1);
        }
        break;
    }
#endif
    case XEN_EMULATE:
        fprintf(stderr, "xen emulation not implemented (yet)\n");
        exit(1);
        break;
    default:
        fprintf(stderr, "unhandled xen_mode %d\n", xen_mode);
        exit(1);
        break;
    }

    xen_sysdev = qdev_create(NULL, TYPE_XENSYSDEV);
    qdev_init_nofail(xen_sysdev);

    xen_be_register("console", &xen_console_ops);
    xen_be_register("vkbd", &xen_kbdmouse_ops);
    xen_be_register("vfb", &xen_framebuffer_ops);
    xen_be_register("qdisk", &xen_blkdev_ops);
    xen_be_register("qnic", &xen_netdev_ops);
#ifdef CONFIG_USB_LIBUSB
    xen_be_register("qusb", &xen_usb_ops);
#endif

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

static int xen_sysdev_init(SysBusDevice *dev)
{
    return 0;
}

static Property xen_sysdev_properties[] = {
    {/* end of property list */},
};

static void xen_sysdev_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SysBusDeviceClass *k = SYS_BUS_DEVICE_CLASS(klass);

    k->init = xen_sysdev_init;
    dc->props = xen_sysdev_properties;
}

static const TypeInfo xensysdev_info = {
    .name          = TYPE_XENSYSDEV,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(SysBusDevice),
    .class_init    = xen_sysdev_class_init,
};

static void xenpv_register_types(void)
{
    type_register_static(&xensysdev_info);
}

type_init(xenpv_register_types);

static void xenpv_machine_init(MachineClass *mc)
{
    mc->desc = "Xen Para-virtualized PC";
    mc->init = xen_init_pv;
    mc->max_cpus = 1;
    mc->default_machine_opts = "accel=xen";
}

DEFINE_MACHINE("xenpv", xenpv_machine_init)
