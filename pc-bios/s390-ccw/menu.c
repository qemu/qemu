/*
 * QEMU S390 Interactive Boot Menu
 *
 * Copyright 2018 IBM Corp.
 * Author: Collin L. Walling <walling@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */

#include "libc.h"
#include "s390-ccw.h"

static uint8_t flag;
static uint64_t timeout;

int menu_get_zipl_boot_index(const char *menu_data)
{
    return 0; /* implemented next patch */
}

void menu_set_parms(uint8_t boot_menu_flag, uint32_t boot_menu_timeout)
{
    flag = boot_menu_flag;
    timeout = boot_menu_timeout;
}

bool menu_is_enabled_zipl(void)
{
    return flag & QIPL_FLAG_BM_OPTS_CMD;
}
