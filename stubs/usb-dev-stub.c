/*
 * QEMU USB device emulation stubs
 *
 * Copyright (C) 2021 Philippe Mathieu-Daudé <f4bug@amsat.org>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-machine.h"
#include "sysemu/sysemu.h"
#include "monitor/monitor.h"
#include "hw/usb.h"

USBDevice *usbdevice_create(const char *driver)
{
    error_report("Support for USB devices not built-in");

    return NULL;
}

HumanReadableText *qmp_x_query_usb(Error **errp)
{
    error_setg(errp, "Support for USB devices not built-in");
    return NULL;
}

void hmp_info_usb(Monitor *mon, const QDict *qdict)
{
    monitor_printf(mon, "Support for USB devices not built-in\n");
}
