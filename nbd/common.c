/*
 *  Copyright (C) 2005  Anthony Liguori <anthony@codemonkey.ws>
 *
 *  Network Block Device Common Code
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; under version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "nbd-internal.h"

ssize_t nbd_wr_sync(int fd, void *buffer, size_t size, bool do_read)
{
    size_t offset = 0;
    int err;

    if (qemu_in_coroutine()) {
        if (do_read) {
            return qemu_co_recv(fd, buffer, size);
        } else {
            return qemu_co_send(fd, buffer, size);
        }
    }

    while (offset < size) {
        ssize_t len;

        if (do_read) {
            len = qemu_recv(fd, buffer + offset, size - offset, 0);
        } else {
            len = send(fd, buffer + offset, size - offset, 0);
        }

        if (len < 0) {
            err = socket_error();

            /* recoverable error */
            if (err == EINTR || (offset > 0 && (err == EAGAIN || err == EWOULDBLOCK))) {
                continue;
            }

            /* unrecoverable error */
            return -err;
        }

        /* eof */
        if (len == 0) {
            break;
        }

        offset += len;
    }

    return offset;
}
