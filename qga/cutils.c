/*
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "cutils.h"
#include "qapi/error.h"

/**
 * qga_open_cloexec:
 * @name: the pathname to open
 * @flags: as in open()
 * @mode: as in open()
 *
 * A wrapper for open() function which sets O_CLOEXEC.
 *
 * On error, -1 is returned.
 */
int qga_open_cloexec(const char *name, int flags, mode_t mode)
{
    int ret;

#ifdef O_CLOEXEC
    ret = open(name, flags | O_CLOEXEC, mode);
#else
    ret = open(name, flags, mode);
    if (ret >= 0) {
        qemu_set_cloexec(ret);
    }
#endif

    return ret;
}
