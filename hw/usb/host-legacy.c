/*
 * Linux host USB redirector
 *
 * Copyright (c) 2005 Fabrice Bellard
 *
 * Copyright (c) 2008 Max Krasnyansky
 *      Support for host device auto connect & disconnect
 *      Major rewrite to support fully async operation
 *
 * Copyright 2008 TJ <linux@tjworld.net>
 *      Added flexible support for /dev/bus/usb /sys/bus/usb/devices in addition
 *      to the legacy /proc/bus/usb USB device discovery and handling
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
#include "qemu-common.h"
#include "hw/usb.h"
#include "hw/usb/host.h"

/*
 * Autoconnect filter
 * Format:
 *    auto:bus:dev[:vid:pid]
 *    auto:bus.dev[:vid:pid]
 *
 *    bus  - bus number    (dec, * means any)
 *    dev  - device number (dec, * means any)
 *    vid  - vendor id     (hex, * means any)
 *    pid  - product id    (hex, * means any)
 *
 *    See 'lsusb' output.
 */
static int parse_filter(const char *spec, struct USBAutoFilter *f)
{
    enum { BUS, DEV, VID, PID, DONE };
    const char *p = spec;
    int i;

    f->bus_num    = 0;
    f->addr       = 0;
    f->vendor_id  = 0;
    f->product_id = 0;

    for (i = BUS; i < DONE; i++) {
        p = strpbrk(p, ":.");
        if (!p) {
            break;
        }
        p++;

        if (*p == '*') {
            continue;
        }
        switch (i) {
        case BUS:
            f->bus_num = strtol(p, NULL, 10);
            break;
        case DEV:
            f->addr    = strtol(p, NULL, 10);
            break;
        case VID:
            f->vendor_id  = strtol(p, NULL, 16);
            break;
        case PID:
            f->product_id = strtol(p, NULL, 16);
            break;
        }
    }

    if (i < DEV) {
        fprintf(stderr, "husb: invalid auto filter spec %s\n", spec);
        return -1;
    }

    return 0;
}

USBDevice *usb_host_device_open(USBBus *bus, const char *devname)
{
    struct USBAutoFilter filter;
    USBDevice *dev;
    char *p;

    dev = usb_create(bus, "usb-host");

    if (strstr(devname, "auto:")) {
        if (parse_filter(devname, &filter) < 0) {
            goto fail;
        }
    } else {
        p = strchr(devname, '.');
        if (p) {
            filter.bus_num    = strtoul(devname, NULL, 0);
            filter.addr       = strtoul(p + 1, NULL, 0);
            filter.vendor_id  = 0;
            filter.product_id = 0;
        } else {
            p = strchr(devname, ':');
            if (p) {
                filter.bus_num    = 0;
                filter.addr       = 0;
                filter.vendor_id  = strtoul(devname, NULL, 16);
                filter.product_id = strtoul(p + 1, NULL, 16);
            } else {
                goto fail;
            }
        }
    }

    qdev_prop_set_uint32(&dev->qdev, "hostbus",   filter.bus_num);
    qdev_prop_set_uint32(&dev->qdev, "hostaddr",  filter.addr);
    qdev_prop_set_uint32(&dev->qdev, "vendorid",  filter.vendor_id);
    qdev_prop_set_uint32(&dev->qdev, "productid", filter.product_id);
    return dev;

fail:
    object_unparent(OBJECT(dev));
    return NULL;
}

static void usb_host_register_types(void)
{
    usb_legacy_register("usb-host", "host", usb_host_device_open);
}

type_init(usb_host_register_types)
