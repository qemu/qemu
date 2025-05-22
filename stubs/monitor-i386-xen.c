/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-misc-i386.h"

EvtchnInfoList *qmp_xen_event_list(Error **errp)
{
    error_setg(errp, "Xen event channel emulation not enabled");
    return NULL;
}

void qmp_xen_event_inject(uint32_t port, Error **errp)
{
    error_setg(errp, "Xen event channel emulation not enabled");
}
