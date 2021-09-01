/*
 * udmabuf helper functions.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "ui/console.h"

#include <fcntl.h>
#include <sys/ioctl.h>

int udmabuf_fd(void)
{
    static bool first = true;
    static int udmabuf;

    if (!first) {
        return udmabuf;
    }
    first = false;

    udmabuf = open("/dev/udmabuf", O_RDWR);
    if (udmabuf < 0) {
        warn_report("open /dev/udmabuf: %s", strerror(errno));
    }
    return udmabuf;
}
