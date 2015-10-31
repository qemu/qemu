/*
 * QEMU Crypto random number provider
 *
 * Copyright (c) 2015 Red Hat, Inc.
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

#include <config-host.h>

#include "crypto/random.h"

int qcrypto_random_bytes(uint8_t *buf,
                         size_t buflen,
                         Error **errp)
{
    ssize_t ret;
    int fd = open("/dev/random", O_RDONLY);
    if (fd < 0) {
        error_setg_errno(errp, errno,
                         "Unable to open /dev/random");
        return -1;
    }

    while (buflen) {
        ret = read(fd, buf, buflen);
        if (ret < 0) {
            error_setg_errno(errp, errno,
                             "Unable to read random bytes");
            close(fd);
            return -1;
        }
        buflen -= ret;
    }

    close(fd);
    return 0;
}
