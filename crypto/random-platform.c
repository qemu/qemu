/*
 * QEMU Crypto random number provider
 *
 * Copyright (c) 2015-2016 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "qemu/osdep.h"

#include "crypto/random.h"

int qcrypto_random_bytes(uint8_t *buf G_GNUC_UNUSED,
                         size_t buflen G_GNUC_UNUSED,
                         Error **errp)
{
    int fd;
    int ret = -1;
    int got;

    /* TBD perhaps also add support for BSD getentropy / Linux
     * getrandom syscalls directly */
    fd = open("/dev/urandom", O_RDONLY);
    if (fd == -1 && errno == ENOENT) {
        fd = open("/dev/random", O_RDONLY);
    }

    if (fd < 0) {
        error_setg(errp, "No /dev/urandom or /dev/random found");
        return -1;
    }

    while (buflen > 0) {
        got = read(fd, buf, buflen);
        if (got < 0) {
            error_setg_errno(errp, errno,
                             "Unable to read random bytes");
            goto cleanup;
        } else if (!got) {
            error_setg(errp,
                       "Unexpected EOF reading random bytes");
            goto cleanup;
        }
        buflen -= got;
        buf += got;
    }

    ret = 0;
 cleanup:
    close(fd);
    return ret;
}
