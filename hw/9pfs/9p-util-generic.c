/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "qemu/osdep.h"
#include "9p-util.h"
#include <glib/gstrfuncs.h>

char *qemu_open_flags_tostr(int flags)
{
    int acc = flags & O_ACCMODE;
    return g_strconcat(
        (acc == O_WRONLY) ? "WRONLY" : (acc == O_RDONLY) ? "RDONLY" : "RDWR",
        (flags & O_CREAT) ? "|CREAT" : "",
        (flags & O_EXCL) ? "|EXCL" : "",
        (flags & O_NOCTTY) ? "|NOCTTY" : "",
        (flags & O_TRUNC) ? "|TRUNC" : "",
        (flags & O_APPEND) ? "|APPEND" : "",
        (flags & O_NONBLOCK) ? "|NONBLOCK" : "",
        (flags & O_DSYNC) ? "|DSYNC" : "",
        #ifdef O_DIRECT
        (flags & O_DIRECT) ? "|DIRECT" : "",
        #endif
        (flags & O_LARGEFILE) ? "|LARGEFILE" : "",
        (flags & O_DIRECTORY) ? "|DIRECTORY" : "",
        (flags & O_NOFOLLOW) ? "|NOFOLLOW" : "",
        #ifdef O_NOATIME
        (flags & O_NOATIME) ? "|NOATIME" : "",
        #endif
        #ifdef O_CLOEXEC
        (flags & O_CLOEXEC) ? "|CLOEXEC" : "",
        #endif
        #ifdef __O_SYNC
        (flags & __O_SYNC) ? "|SYNC" : "",
        #else
        ((flags & O_SYNC) == O_SYNC) ? "|SYNC" : "",
        #endif
        #ifdef O_PATH
        (flags & O_PATH) ? "|PATH" : "",
        #endif
        #ifdef __O_TMPFILE
        (flags & __O_TMPFILE) ? "|TMPFILE" : "",
        #elif defined(O_TMPFILE)
        ((flags & O_TMPFILE) == O_TMPFILE) ? "|TMPFILE" : "",
        #endif
        /* O_NDELAY is usually just an alias of O_NONBLOCK */
        #if defined(O_NDELAY) && O_NDELAY != O_NONBLOCK
        (flags & O_NDELAY) ? "|NDELAY" : "",
        #endif
        NULL /* always last (required NULL termination) */
    );
}
