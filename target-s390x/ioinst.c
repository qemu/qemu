/*
 * I/O instructions for S/390
 *
 * Copyright 2012 IBM Corp.
 * Author(s): Cornelia Huck <cornelia.huck@de.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */

#include <sys/types.h>

#include "cpu.h"
#include "ioinst.h"

int ioinst_disassemble_sch_ident(uint32_t value, int *m, int *cssid, int *ssid,
                                 int *schid)
{
    if (!IOINST_SCHID_ONE(value)) {
        return -EINVAL;
    }
    if (!IOINST_SCHID_M(value)) {
        if (IOINST_SCHID_CSSID(value)) {
            return -EINVAL;
        }
        *cssid = 0;
        *m = 0;
    } else {
        *cssid = IOINST_SCHID_CSSID(value);
        *m = 1;
    }
    *ssid = IOINST_SCHID_SSID(value);
    *schid = IOINST_SCHID_NR(value);
    return 0;
}
