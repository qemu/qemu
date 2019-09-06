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
#include "trace.h"
#include "nbd-internal.h"

/* Discard length bytes from channel.  Return -errno on failure and 0 on
 * success */
int nbd_drop(QIOChannel *ioc, size_t size, Error **errp)
{
    ssize_t ret = 0;
    char small[1024];
    char *buffer;

    buffer = sizeof(small) >= size ? small : g_malloc(MIN(65536, size));
    while (size > 0) {
        ssize_t count = MIN(65536, size);
        ret = nbd_read(ioc, buffer, MIN(65536, size), NULL, errp);

        if (ret < 0) {
            goto cleanup;
        }
        size -= count;
    }

 cleanup:
    if (buffer != small) {
        g_free(buffer);
    }
    return ret;
}


void nbd_tls_handshake(QIOTask *task,
                       void *opaque)
{
    struct NBDTLSHandshakeData *data = opaque;

    qio_task_propagate_error(task, &data->error);
    data->complete = true;
    g_main_loop_quit(data->loop);
}


const char *nbd_opt_lookup(uint32_t opt)
{
    switch (opt) {
    case NBD_OPT_EXPORT_NAME:
        return "export name";
    case NBD_OPT_ABORT:
        return "abort";
    case NBD_OPT_LIST:
        return "list";
    case NBD_OPT_STARTTLS:
        return "starttls";
    case NBD_OPT_INFO:
        return "info";
    case NBD_OPT_GO:
        return "go";
    case NBD_OPT_STRUCTURED_REPLY:
        return "structured reply";
    case NBD_OPT_LIST_META_CONTEXT:
        return "list meta context";
    case NBD_OPT_SET_META_CONTEXT:
        return "set meta context";
    default:
        return "<unknown>";
    }
}


const char *nbd_rep_lookup(uint32_t rep)
{
    switch (rep) {
    case NBD_REP_ACK:
        return "ack";
    case NBD_REP_SERVER:
        return "server";
    case NBD_REP_INFO:
        return "info";
    case NBD_REP_META_CONTEXT:
        return "meta context";
    case NBD_REP_ERR_UNSUP:
        return "unsupported";
    case NBD_REP_ERR_POLICY:
        return "denied by policy";
    case NBD_REP_ERR_INVALID:
        return "invalid";
    case NBD_REP_ERR_PLATFORM:
        return "platform lacks support";
    case NBD_REP_ERR_TLS_REQD:
        return "TLS required";
    case NBD_REP_ERR_UNKNOWN:
        return "export unknown";
    case NBD_REP_ERR_SHUTDOWN:
        return "server shutting down";
    case NBD_REP_ERR_BLOCK_SIZE_REQD:
        return "block size required";
    default:
        return "<unknown>";
    }
}


const char *nbd_info_lookup(uint16_t info)
{
    switch (info) {
    case NBD_INFO_EXPORT:
        return "export";
    case NBD_INFO_NAME:
        return "name";
    case NBD_INFO_DESCRIPTION:
        return "description";
    case NBD_INFO_BLOCK_SIZE:
        return "block size";
    default:
        return "<unknown>";
    }
}


const char *nbd_cmd_lookup(uint16_t cmd)
{
    switch (cmd) {
    case NBD_CMD_READ:
        return "read";
    case NBD_CMD_WRITE:
        return "write";
    case NBD_CMD_DISC:
        return "disconnect";
    case NBD_CMD_FLUSH:
        return "flush";
    case NBD_CMD_TRIM:
        return "trim";
    case NBD_CMD_CACHE:
        return "cache";
    case NBD_CMD_WRITE_ZEROES:
        return "write zeroes";
    case NBD_CMD_BLOCK_STATUS:
        return "block status";
    default:
        return "<unknown>";
    }
}


const char *nbd_reply_type_lookup(uint16_t type)
{
    switch (type) {
    case NBD_REPLY_TYPE_NONE:
        return "none";
    case NBD_REPLY_TYPE_OFFSET_DATA:
        return "data";
    case NBD_REPLY_TYPE_OFFSET_HOLE:
        return "hole";
    case NBD_REPLY_TYPE_BLOCK_STATUS:
        return "block status";
    case NBD_REPLY_TYPE_ERROR:
        return "generic error";
    case NBD_REPLY_TYPE_ERROR_OFFSET:
        return "error at offset";
    default:
        if (type & (1 << 15)) {
            return "<unknown error>";
        }
        return "<unknown>";
    }
}


const char *nbd_err_lookup(int err)
{
    switch (err) {
    case NBD_SUCCESS:
        return "success";
    case NBD_EPERM:
        return "EPERM";
    case NBD_EIO:
        return "EIO";
    case NBD_ENOMEM:
        return "ENOMEM";
    case NBD_EINVAL:
        return "EINVAL";
    case NBD_ENOSPC:
        return "ENOSPC";
    case NBD_EOVERFLOW:
        return "EOVERFLOW";
    case NBD_ENOTSUP:
        return "ENOTSUP";
    case NBD_ESHUTDOWN:
        return "ESHUTDOWN";
    default:
        return "<unknown>";
    }
}


int nbd_errno_to_system_errno(int err)
{
    int ret;
    switch (err) {
    case NBD_SUCCESS:
        ret = 0;
        break;
    case NBD_EPERM:
        ret = EPERM;
        break;
    case NBD_EIO:
        ret = EIO;
        break;
    case NBD_ENOMEM:
        ret = ENOMEM;
        break;
    case NBD_ENOSPC:
        ret = ENOSPC;
        break;
    case NBD_EOVERFLOW:
        ret = EOVERFLOW;
        break;
    case NBD_ENOTSUP:
        ret = ENOTSUP;
        break;
    case NBD_ESHUTDOWN:
        ret = ESHUTDOWN;
        break;
    default:
        trace_nbd_unknown_error(err);
        /* fallthrough */
    case NBD_EINVAL:
        ret = EINVAL;
        break;
    }
    return ret;
}
