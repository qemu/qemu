/*
 * QEMU Xen emulation: QMP stubs
 *
 * Copyright © 2023 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * Authors: David Woodhouse <dwmw2@infradead.org>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"

#include "qapi/error.h"
#include "qapi/qapi-commands-misc-target.h"

#ifdef TARGET_I386
EvtchnInfoList *qmp_xen_event_list(Error **errp)
{
    error_setg(errp, "Xen event channel emulation not enabled");
    return NULL;
}

void qmp_xen_event_inject(uint32_t port, Error **errp)
{
    error_setg(errp, "Xen event channel emulation not enabled");
}
#endif
