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
#include "qapi/error.h"
#include "nbd-internal.h"

ssize_t nbd_wr_syncv(QIOChannel *ioc,
                     struct iovec *iov,
                     size_t niov,
                     size_t length,
                     bool do_read)
{
    ssize_t done = 0;
    Error *local_err = NULL;
    struct iovec *local_iov = g_new(struct iovec, niov);
    struct iovec *local_iov_head = local_iov;
    unsigned int nlocal_iov = niov;

    nlocal_iov = iov_copy(local_iov, nlocal_iov, iov, niov, 0, length);

    while (nlocal_iov > 0) {
        ssize_t len;
        if (do_read) {
            len = qio_channel_readv(ioc, local_iov, nlocal_iov, &local_err);
        } else {
            len = qio_channel_writev(ioc, local_iov, nlocal_iov, &local_err);
        }
        if (len == QIO_CHANNEL_ERR_BLOCK) {
            if (qemu_in_coroutine()) {
                qio_channel_yield(ioc, do_read ? G_IO_IN : G_IO_OUT);
            } else {
                return -EAGAIN;
            }
            continue;
        }
        if (len < 0) {
            TRACE("I/O error: %s", error_get_pretty(local_err));
            error_free(local_err);
            /* XXX handle Error objects */
            done = -EIO;
            goto cleanup;
        }

        if (do_read && len == 0) {
            break;
        }

        iov_discard_front(&local_iov, &nlocal_iov, len);
        done += len;
    }

 cleanup:
    g_free(local_iov_head);
    return done;
}


void nbd_tls_handshake(QIOTask *task,
                       void *opaque)
{
    struct NBDTLSHandshakeData *data = opaque;

    if (qio_task_propagate_error(task, &data->error)) {
        TRACE("TLS failed %s", error_get_pretty(data->error));
    }
    data->complete = true;
    g_main_loop_quit(data->loop);
}
