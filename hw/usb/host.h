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

#ifndef QEMU_USB_HOST_H
#define QEMU_USB_HOST_H

struct USBAutoFilter {
    uint32_t bus_num;
    uint32_t addr;
    char     *port;
    uint32_t vendor_id;
    uint32_t product_id;
};

#endif /* QEMU_USB_HOST_H */
