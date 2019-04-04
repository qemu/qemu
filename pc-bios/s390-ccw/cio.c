/*
 * S390 Channel I/O
 *
 * Copyright (c) 2013 Alexander Graf <agraf@suse.de>
 * Copyright (c) 2019 IBM Corp.
 *
 * Author(s): Jason J. Herne <jjherne@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */

#include "libc.h"
#include "s390-ccw.h"
#include "cio.h"

static char chsc_page[PAGE_SIZE] __attribute__((__aligned__(PAGE_SIZE)));

int enable_mss_facility(void)
{
    int ret;
    ChscAreaSda *sda_area = (ChscAreaSda *) chsc_page;

    memset(sda_area, 0, PAGE_SIZE);
    sda_area->request.length = 0x0400;
    sda_area->request.code = 0x0031;
    sda_area->operation_code = 0x2;

    ret = chsc(sda_area);
    if ((ret == 0) && (sda_area->response.code == 0x0001)) {
        return 0;
    }
    return -EIO;
}

void enable_subchannel(SubChannelId schid)
{
    Schib schib;

    stsch_err(schid, &schib);
    schib.pmcw.ena = 1;
    msch(schid, &schib);
}
