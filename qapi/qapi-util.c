/*
 * QAPI util functions
 *
 * Authors:
 *  Hu Tao       <hutao@cn.fujitsu.com>
 *  Peter Lieven <pl@kamp.de>
 * 
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */

#include "qemu-common.h"
#include "qapi/error.h"
#include "qapi/util.h"

int qapi_enum_parse(const char *lookup[], const char *buf,
                    int max, int def, Error **errp)
{
    int i;

    if (!buf) {
        return def;
    }

    for (i = 0; i < max; i++) {
        if (!strcmp(buf, lookup[i])) {
            return i;
        }
    }

    error_setg(errp, "invalid parameter value: %s", buf);
    return def;
}
