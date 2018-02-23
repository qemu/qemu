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

static int get_boot_index(int entries)
{
    return 0; /* implemented next patch */
}

static void zipl_println(const char *data, size_t len)
{
    char buf[len + 2];

    ebcdic_to_ascii(data, buf, len);
    buf[len] = '\n';
    buf[len + 1] = '\0';

    sclp_print(buf);
}

int menu_get_zipl_boot_index(const char *menu_data)
{
    size_t len;
    int entries;

    /* Print and count all menu items, including the banner */
    for (entries = 0; *menu_data; entries++) {
        len = strlen(menu_data);
        zipl_println(menu_data, len);
        menu_data += len + 1;

        if (entries < 2) {
            sclp_print("\n");
        }
    }

    sclp_print("\n");
    return get_boot_index(entries - 1); /* subtract 1 to exclude banner */
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
