/*
 *  Copyright (C) 2005  Anthony Liguori <anthony@codemonkey.ws>
 *
 *  Network Block Device Client Side
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

static int nbd_errno_to_system_errno(int err)
{
    switch (err) {
    case NBD_SUCCESS:
        return 0;
    case NBD_EPERM:
        return EPERM;
    case NBD_EIO:
        return EIO;
    case NBD_ENOMEM:
        return ENOMEM;
    case NBD_ENOSPC:
        return ENOSPC;
    case NBD_EINVAL:
    default:
        return EINVAL;
    }
}

/* Definitions for opaque data types */

static QTAILQ_HEAD(, NBDExport) exports = QTAILQ_HEAD_INITIALIZER(exports);

/* That's all folks */

/* Basic flow for negotiation

   Server         Client
   Negotiate

   or

   Server         Client
   Negotiate #1
                  Option
   Negotiate #2

   ----

   followed by

   Server         Client
                  Request
   Response
                  Request
   Response
                  ...
   ...
                  Request (type == 2)

*/

int nbd_receive_negotiate(int csock, const char *name, uint32_t *flags,
                          off_t *size, Error **errp)
{
    char buf[256];
    uint64_t magic, s;
    uint16_t tmp;
    int rc;

    TRACE("Receiving negotiation.");

    rc = -EINVAL;

    if (read_sync(csock, buf, 8) != 8) {
        error_setg(errp, "Failed to read data");
        goto fail;
    }

    buf[8] = '\0';
    if (strlen(buf) == 0) {
        error_setg(errp, "Server connection closed unexpectedly");
        goto fail;
    }

    TRACE("Magic is %c%c%c%c%c%c%c%c",
          qemu_isprint(buf[0]) ? buf[0] : '.',
          qemu_isprint(buf[1]) ? buf[1] : '.',
          qemu_isprint(buf[2]) ? buf[2] : '.',
          qemu_isprint(buf[3]) ? buf[3] : '.',
          qemu_isprint(buf[4]) ? buf[4] : '.',
          qemu_isprint(buf[5]) ? buf[5] : '.',
          qemu_isprint(buf[6]) ? buf[6] : '.',
          qemu_isprint(buf[7]) ? buf[7] : '.');

    if (memcmp(buf, "NBDMAGIC", 8) != 0) {
        error_setg(errp, "Invalid magic received");
        goto fail;
    }

    if (read_sync(csock, &magic, sizeof(magic)) != sizeof(magic)) {
        error_setg(errp, "Failed to read magic");
        goto fail;
    }
    magic = be64_to_cpu(magic);
    TRACE("Magic is 0x%" PRIx64, magic);

    if (name) {
        uint32_t reserved = 0;
        uint32_t opt;
        uint32_t namesize;

        TRACE("Checking magic (opts_magic)");
        if (magic != NBD_OPTS_MAGIC) {
            if (magic == NBD_CLIENT_MAGIC) {
                error_setg(errp, "Server does not support export names");
            } else {
                error_setg(errp, "Bad magic received");
            }
            goto fail;
        }
        if (read_sync(csock, &tmp, sizeof(tmp)) != sizeof(tmp)) {
            error_setg(errp, "Failed to read server flags");
            goto fail;
        }
        *flags = be16_to_cpu(tmp) << 16;
        /* reserved for future use */
        if (write_sync(csock, &reserved, sizeof(reserved)) !=
            sizeof(reserved)) {
            error_setg(errp, "Failed to read reserved field");
            goto fail;
        }
        /* write the export name */
        magic = cpu_to_be64(magic);
        if (write_sync(csock, &magic, sizeof(magic)) != sizeof(magic)) {
            error_setg(errp, "Failed to send export name magic");
            goto fail;
        }
        opt = cpu_to_be32(NBD_OPT_EXPORT_NAME);
        if (write_sync(csock, &opt, sizeof(opt)) != sizeof(opt)) {
            error_setg(errp, "Failed to send export name option number");
            goto fail;
        }
        namesize = cpu_to_be32(strlen(name));
        if (write_sync(csock, &namesize, sizeof(namesize)) !=
            sizeof(namesize)) {
            error_setg(errp, "Failed to send export name length");
            goto fail;
        }
        if (write_sync(csock, (char*)name, strlen(name)) != strlen(name)) {
            error_setg(errp, "Failed to send export name");
            goto fail;
        }
    } else {
        TRACE("Checking magic (cli_magic)");

        if (magic != NBD_CLIENT_MAGIC) {
            if (magic == NBD_OPTS_MAGIC) {
                error_setg(errp, "Server requires an export name");
            } else {
                error_setg(errp, "Bad magic received");
            }
            goto fail;
        }
    }

    if (read_sync(csock, &s, sizeof(s)) != sizeof(s)) {
        error_setg(errp, "Failed to read export length");
        goto fail;
    }
    *size = be64_to_cpu(s);
    TRACE("Size is %" PRIu64, *size);

    if (!name) {
        if (read_sync(csock, flags, sizeof(*flags)) != sizeof(*flags)) {
            error_setg(errp, "Failed to read export flags");
            goto fail;
        }
        *flags = be32_to_cpup(flags);
    } else {
        if (read_sync(csock, &tmp, sizeof(tmp)) != sizeof(tmp)) {
            error_setg(errp, "Failed to read export flags");
            goto fail;
        }
        *flags |= be16_to_cpu(tmp);
    }
    if (read_sync(csock, &buf, 124) != 124) {
        error_setg(errp, "Failed to read reserved block");
        goto fail;
    }
    rc = 0;

fail:
    return rc;
}

#ifdef __linux__
int nbd_init(int fd, int csock, uint32_t flags, off_t size)
{
    TRACE("Setting NBD socket");

    if (ioctl(fd, NBD_SET_SOCK, csock) < 0) {
        int serrno = errno;
        LOG("Failed to set NBD socket");
        return -serrno;
    }

    TRACE("Setting block size to %lu", (unsigned long)BDRV_SECTOR_SIZE);

    if (ioctl(fd, NBD_SET_BLKSIZE, (size_t)BDRV_SECTOR_SIZE) < 0) {
        int serrno = errno;
        LOG("Failed setting NBD block size");
        return -serrno;
    }

    TRACE("Setting size to %zd block(s)", (size_t)(size / BDRV_SECTOR_SIZE));

    if (ioctl(fd, NBD_SET_SIZE_BLOCKS, (size_t)(size / BDRV_SECTOR_SIZE)) < 0) {
        int serrno = errno;
        LOG("Failed setting size (in blocks)");
        return -serrno;
    }

    if (ioctl(fd, NBD_SET_FLAGS, flags) < 0) {
        if (errno == ENOTTY) {
            int read_only = (flags & NBD_FLAG_READ_ONLY) != 0;
            TRACE("Setting readonly attribute");

            if (ioctl(fd, BLKROSET, (unsigned long) &read_only) < 0) {
                int serrno = errno;
                LOG("Failed setting read-only attribute");
                return -serrno;
            }
        } else {
            int serrno = errno;
            LOG("Failed setting flags");
            return -serrno;
        }
    }

    TRACE("Negotiation ended");

    return 0;
}

int nbd_client(int fd)
{
    int ret;
    int serrno;

    TRACE("Doing NBD loop");

    ret = ioctl(fd, NBD_DO_IT);
    if (ret < 0 && errno == EPIPE) {
        /* NBD_DO_IT normally returns EPIPE when someone has disconnected
         * the socket via NBD_DISCONNECT.  We do not want to return 1 in
         * that case.
         */
        ret = 0;
    }
    serrno = errno;

    TRACE("NBD loop returned %d: %s", ret, strerror(serrno));

    TRACE("Clearing NBD queue");
    ioctl(fd, NBD_CLEAR_QUE);

    TRACE("Clearing NBD socket");
    ioctl(fd, NBD_CLEAR_SOCK);

    errno = serrno;
    return ret;
}
#else
int nbd_init(int fd, int csock, uint32_t flags, off_t size)
{
    return -ENOTSUP;
}

int nbd_client(int fd)
{
    return -ENOTSUP;
}
#endif

ssize_t nbd_send_request(int csock, struct nbd_request *request)
{
    uint8_t buf[NBD_REQUEST_SIZE];
    ssize_t ret;

    cpu_to_be32w((uint32_t*)buf, NBD_REQUEST_MAGIC);
    cpu_to_be32w((uint32_t*)(buf + 4), request->type);
    cpu_to_be64w((uint64_t*)(buf + 8), request->handle);
    cpu_to_be64w((uint64_t*)(buf + 16), request->from);
    cpu_to_be32w((uint32_t*)(buf + 24), request->len);

    TRACE("Sending request to client: "
          "{ .from = %" PRIu64", .len = %u, .handle = %" PRIu64", .type=%i}",
          request->from, request->len, request->handle, request->type);

    ret = write_sync(csock, buf, sizeof(buf));
    if (ret < 0) {
        return ret;
    }

    if (ret != sizeof(buf)) {
        LOG("writing to socket failed");
        return -EINVAL;
    }
    return 0;
}

ssize_t nbd_receive_reply(int csock, struct nbd_reply *reply)
{
    uint8_t buf[NBD_REPLY_SIZE];
    uint32_t magic;
    ssize_t ret;

    ret = read_sync(csock, buf, sizeof(buf));
    if (ret < 0) {
        return ret;
    }

    if (ret != sizeof(buf)) {
        LOG("read failed");
        return -EINVAL;
    }

    /* Reply
       [ 0 ..  3]    magic   (NBD_REPLY_MAGIC)
       [ 4 ..  7]    error   (0 == no error)
       [ 7 .. 15]    handle
     */

    magic = be32_to_cpup((uint32_t*)buf);
    reply->error  = be32_to_cpup((uint32_t*)(buf + 4));
    reply->handle = be64_to_cpup((uint64_t*)(buf + 8));

    reply->error = nbd_errno_to_system_errno(reply->error);

    TRACE("Got reply: "
          "{ magic = 0x%x, .error = %d, handle = %" PRIu64" }",
          magic, reply->error, reply->handle);

    if (magic != NBD_REPLY_MAGIC) {
        LOG("invalid magic (got 0x%x)", magic);
        return -EINVAL;
    }
    return 0;
}

