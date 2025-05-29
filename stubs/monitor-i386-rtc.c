/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-misc-i386.h"

void qmp_rtc_reset_reinjection(Error **errp)
{
    error_setg(errp,
               "RTC interrupt reinjection backlog reset is not available for"
               "this machine");
}
