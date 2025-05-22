/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-misc-arm.h"


GICCapabilityList *qmp_query_gic_capabilities(Error **errp)
{
    error_setg(errp, "GIC hardware is not available on this target");
    return NULL;
}
