/*
 * Copyright (C) 2010       Citrix Ltd.
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#include "hw/xen_common.h"
#include "hw/xen_backend.h"

/* Initialise Xen */

int xen_init(void)
{
    xen_xc = xen_xc_interface_open(0, 0, 0);
    if (xen_xc == XC_HANDLER_INITIAL_VALUE) {
        xen_be_printf(NULL, 0, "can't open xen interface\n");
        return -1;
    }

    return 0;
}
