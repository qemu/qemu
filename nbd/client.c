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


static int nbd_handle_reply_err(uint32_t opt, uint32_t type, Error **errp)
{
    if (!(type & (1 << 31))) {
        return 0;
    }

    switch (type) {
    case NBD_REP_ERR_UNSUP:
        error_setg(errp, "Unsupported option type %x", opt);
        break;

    case NBD_REP_ERR_POLICY:
        error_setg(errp, "Denied by server for option %x", opt);
        break;

    case NBD_REP_ERR_INVALID:
        error_setg(errp, "Invalid data length for option %x", opt);
        break;

    case NBD_REP_ERR_TLS_REQD:
        error_setg(errp, "TLS negotiation required before option %x", opt);
        break;

    default:
        error_setg(errp, "Unknown error code when asking for option %x", opt);
        break;
    }

    return -1;
}

static int nbd_receive_list(QIOChannel *ioc, char **name, Error **errp)
{
    uint64_t magic;
    uint32_t opt;
    uint32_t type;
    uint32_t len;
    uint32_t namelen;

    *name = NULL;
    if (read_sync(ioc, &magic, sizeof(magic)) != sizeof(magic)) {
        error_setg(errp, "failed to read list option magic");
        return -1;
    }
    magic = be64_to_cpu(magic);
    if (magic != NBD_REP_MAGIC) {
        error_setg(errp, "Unexpected option list magic");
        return -1;
    }
    if (read_sync(ioc, &opt, sizeof(opt)) != sizeof(opt)) {
        error_setg(errp, "failed to read list option");
        return -1;
    }
    opt = be32_to_cpu(opt);
    if (opt != NBD_OPT_LIST) {
        error_setg(errp, "Unexpected option type %x expected %x",
                   opt, NBD_OPT_LIST);
        return -1;
    }

    if (read_sync(ioc, &type, sizeof(type)) != sizeof(type)) {
        error_setg(errp, "failed to read list option type");
        return -1;
    }
    type = be32_to_cpu(type);
    if (type == NBD_REP_ERR_UNSUP) {
        return 0;
    }
    if (nbd_handle_reply_err(opt, type, errp) < 0) {
        return -1;
    }

    if (read_sync(ioc, &len, sizeof(len)) != sizeof(len)) {
        error_setg(errp, "failed to read option length");
        return -1;
    }
    len = be32_to_cpu(len);

    if (type == NBD_REP_ACK) {
        if (len != 0) {
            error_setg(errp, "length too long for option end");
            return -1;
        }
    } else if (type == NBD_REP_SERVER) {
        if (read_sync(ioc, &namelen, sizeof(namelen)) != sizeof(namelen)) {
            error_setg(errp, "failed to read option name length");
            return -1;
        }
        namelen = be32_to_cpu(namelen);
        if (len != (namelen + sizeof(namelen))) {
            error_setg(errp, "incorrect option mame length");
            return -1;
        }
        if (namelen > 255) {
            error_setg(errp, "export name length too long %d", namelen);
            return -1;
        }

        *name = g_new0(char, namelen + 1);
        if (read_sync(ioc, *name, namelen) != namelen) {
            error_setg(errp, "failed to read export name");
            g_free(*name);
            *name = NULL;
            return -1;
        }
        (*name)[namelen] = '\0';
    } else {
        error_setg(errp, "Unexpected reply type %x expected %x",
                   type, NBD_REP_SERVER);
        return -1;
    }
    return 1;
}


static int nbd_receive_query_exports(QIOChannel *ioc,
                                     const char *wantname,
                                     Error **errp)
{
    uint64_t magic = cpu_to_be64(NBD_OPTS_MAGIC);
    uint32_t opt = cpu_to_be32(NBD_OPT_LIST);
    uint32_t length = 0;
    bool foundExport = false;

    TRACE("Querying export list");
    if (write_sync(ioc, &magic, sizeof(magic)) != sizeof(magic)) {
        error_setg(errp, "Failed to send list option magic");
        return -1;
    }

    if (write_sync(ioc, &opt, sizeof(opt)) != sizeof(opt)) {
        error_setg(errp, "Failed to send list option number");
        return -1;
    }

    if (write_sync(ioc, &length, sizeof(length)) != sizeof(length)) {
        error_setg(errp, "Failed to send list option length");
        return -1;
    }

    TRACE("Reading available export names");
    while (1) {
        char *name = NULL;
        int ret = nbd_receive_list(ioc, &name, errp);

        if (ret < 0) {
            g_free(name);
            name = NULL;
            return -1;
        }
        if (ret == 0) {
            /* Server doesn't support export listing, so
             * we will just assume an export with our
             * wanted name exists */
            foundExport = true;
            break;
        }
        if (name == NULL) {
            TRACE("End of export name list");
            break;
        }
        if (g_str_equal(name, wantname)) {
            foundExport = true;
            TRACE("Found desired export name '%s'", name);
        } else {
            TRACE("Ignored export name '%s'", name);
        }
        g_free(name);
    }

    if (!foundExport) {
        error_setg(errp, "No export with name '%s' available", wantname);
        return -1;
    }

    return 0;
}

static QIOChannel *nbd_receive_starttls(QIOChannel *ioc,
                                        QCryptoTLSCreds *tlscreds,
                                        const char *hostname, Error **errp)
{
    uint64_t magic = cpu_to_be64(NBD_OPTS_MAGIC);
    uint32_t opt = cpu_to_be32(NBD_OPT_STARTTLS);
    uint32_t length = 0;
    uint32_t type;
    QIOChannelTLS *tioc;
    struct NBDTLSHandshakeData data = { 0 };

    TRACE("Requesting TLS from server");
    if (write_sync(ioc, &magic, sizeof(magic)) != sizeof(magic)) {
        error_setg(errp, "Failed to send option magic");
        return NULL;
    }

    if (write_sync(ioc, &opt, sizeof(opt)) != sizeof(opt)) {
        error_setg(errp, "Failed to send option number");
        return NULL;
    }

    if (write_sync(ioc, &length, sizeof(length)) != sizeof(length)) {
        error_setg(errp, "Failed to send option length");
        return NULL;
    }

    TRACE("Getting TLS reply from server1");
    if (read_sync(ioc, &magic, sizeof(magic)) != sizeof(magic)) {
        error_setg(errp, "failed to read option magic");
        return NULL;
    }
    magic = be64_to_cpu(magic);
    if (magic != NBD_REP_MAGIC) {
        error_setg(errp, "Unexpected option magic");
        return NULL;
    }
    TRACE("Getting TLS reply from server2");
    if (read_sync(ioc, &opt, sizeof(opt)) != sizeof(opt)) {
        error_setg(errp, "failed to read option");
        return NULL;
    }
    opt = be32_to_cpu(opt);
    if (opt != NBD_OPT_STARTTLS) {
        error_setg(errp, "Unexpected option type %x expected %x",
                   opt, NBD_OPT_STARTTLS);
        return NULL;
    }

    TRACE("Getting TLS reply from server");
    if (read_sync(ioc, &type, sizeof(type)) != sizeof(type)) {
        error_setg(errp, "failed to read option type");
        return NULL;
    }
    type = be32_to_cpu(type);
    if (type != NBD_REP_ACK) {
        error_setg(errp, "Server rejected request to start TLS %x",
                   type);
        return NULL;
    }

    TRACE("Getting TLS reply from server");
    if (read_sync(ioc, &length, sizeof(length)) != sizeof(length)) {
        error_setg(errp, "failed to read option length");
        return NULL;
    }
    length = be32_to_cpu(length);
    if (length != 0) {
        error_setg(errp, "Start TLS reponse was not zero %x",
                   length);
        return NULL;
    }

    TRACE("TLS request approved, setting up TLS");
    tioc = qio_channel_tls_new_client(ioc, tlscreds, hostname, errp);
    if (!tioc) {
        return NULL;
    }
    data.loop = g_main_loop_new(g_main_context_default(), FALSE);
    TRACE("Starting TLS hanshake");
    qio_channel_tls_handshake(tioc,
                              nbd_tls_handshake,
                              &data,
                              NULL);

    if (!data.complete) {
        g_main_loop_run(data.loop);
    }
    g_main_loop_unref(data.loop);
    if (data.error) {
        error_propagate(errp, data.error);
        object_unref(OBJECT(tioc));
        return NULL;
    }

    return QIO_CHANNEL(tioc);
}


int nbd_receive_negotiate(QIOChannel *ioc, const char *name, uint32_t *flags,
                          QCryptoTLSCreds *tlscreds, const char *hostname,
                          QIOChannel **outioc,
                          off_t *size, Error **errp)
{
    char buf[256];
    uint64_t magic, s;
    int rc;

    TRACE("Receiving negotiation tlscreds=%p hostname=%s.",
          tlscreds, hostname ? hostname : "<null>");

    rc = -EINVAL;

    if (outioc) {
        *outioc = NULL;
    }
    if (tlscreds && !outioc) {
        error_setg(errp, "Output I/O channel required for TLS");
        goto fail;
    }

    if (read_sync(ioc, buf, 8) != 8) {
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

    if (read_sync(ioc, &magic, sizeof(magic)) != sizeof(magic)) {
        error_setg(errp, "Failed to read magic");
        goto fail;
    }
    magic = be64_to_cpu(magic);
    TRACE("Magic is 0x%" PRIx64, magic);

    if (magic == NBD_OPTS_MAGIC) {
        uint32_t clientflags = 0;
        uint32_t opt;
        uint32_t namesize;
        uint16_t globalflags;
        uint16_t exportflags;
        bool fixedNewStyle = false;

        if (read_sync(ioc, &globalflags, sizeof(globalflags)) !=
            sizeof(globalflags)) {
            error_setg(errp, "Failed to read server flags");
            goto fail;
        }
        globalflags = be16_to_cpu(globalflags);
        *flags = globalflags << 16;
        TRACE("Global flags are %x", globalflags);
        if (globalflags & NBD_FLAG_FIXED_NEWSTYLE) {
            fixedNewStyle = true;
            TRACE("Server supports fixed new style");
            clientflags |= NBD_FLAG_C_FIXED_NEWSTYLE;
        }
        /* client requested flags */
        clientflags = cpu_to_be32(clientflags);
        if (write_sync(ioc, &clientflags, sizeof(clientflags)) !=
            sizeof(clientflags)) {
            error_setg(errp, "Failed to send clientflags field");
            goto fail;
        }
        if (tlscreds) {
            if (fixedNewStyle) {
                *outioc = nbd_receive_starttls(ioc, tlscreds, hostname, errp);
                if (!*outioc) {
                    goto fail;
                }
                ioc = *outioc;
            } else {
                error_setg(errp, "Server does not support STARTTLS");
                goto fail;
            }
        }
        if (!name) {
            TRACE("Using default NBD export name \"\"");
            name = "";
        }
        if (fixedNewStyle) {
            /* Check our desired export is present in the
             * server export list. Since NBD_OPT_EXPORT_NAME
             * cannot return an error message, running this
             * query gives us good error reporting if the
             * server required TLS
             */
            if (nbd_receive_query_exports(ioc, name, errp) < 0) {
                goto fail;
            }
        }
        /* write the export name */
        magic = cpu_to_be64(magic);
        if (write_sync(ioc, &magic, sizeof(magic)) != sizeof(magic)) {
            error_setg(errp, "Failed to send export name magic");
            goto fail;
        }
        opt = cpu_to_be32(NBD_OPT_EXPORT_NAME);
        if (write_sync(ioc, &opt, sizeof(opt)) != sizeof(opt)) {
            error_setg(errp, "Failed to send export name option number");
            goto fail;
        }
        namesize = cpu_to_be32(strlen(name));
        if (write_sync(ioc, &namesize, sizeof(namesize)) !=
            sizeof(namesize)) {
            error_setg(errp, "Failed to send export name length");
            goto fail;
        }
        if (write_sync(ioc, (char *)name, strlen(name)) != strlen(name)) {
            error_setg(errp, "Failed to send export name");
            goto fail;
        }

        if (read_sync(ioc, &s, sizeof(s)) != sizeof(s)) {
            error_setg(errp, "Failed to read export length");
            goto fail;
        }
        *size = be64_to_cpu(s);
        TRACE("Size is %" PRIu64, *size);

        if (read_sync(ioc, &exportflags, sizeof(exportflags)) !=
            sizeof(exportflags)) {
            error_setg(errp, "Failed to read export flags");
            goto fail;
        }
        exportflags = be16_to_cpu(exportflags);
        *flags |= exportflags;
        TRACE("Export flags are %x", exportflags);
    } else if (magic == NBD_CLIENT_MAGIC) {
        if (name) {
            error_setg(errp, "Server does not support export names");
            goto fail;
        }
        if (tlscreds) {
            error_setg(errp, "Server does not support STARTTLS");
            goto fail;
        }

        if (read_sync(ioc, &s, sizeof(s)) != sizeof(s)) {
            error_setg(errp, "Failed to read export length");
            goto fail;
        }
        *size = be64_to_cpu(s);
        TRACE("Size is %" PRIu64, *size);

        if (read_sync(ioc, flags, sizeof(*flags)) != sizeof(*flags)) {
            error_setg(errp, "Failed to read export flags");
            goto fail;
        }
        *flags = be32_to_cpup(flags);
    } else {
        error_setg(errp, "Bad magic received");
        goto fail;
    }

    if (read_sync(ioc, &buf, 124) != 124) {
        error_setg(errp, "Failed to read reserved block");
        goto fail;
    }
    rc = 0;

fail:
    return rc;
}

#ifdef __linux__
int nbd_init(int fd, QIOChannelSocket *sioc, uint32_t flags, off_t size)
{
    TRACE("Setting NBD socket");

    if (ioctl(fd, NBD_SET_SOCK, sioc->fd) < 0) {
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
int nbd_init(int fd, QIOChannelSocket *ioc, uint32_t flags, off_t size)
{
    return -ENOTSUP;
}

int nbd_client(int fd)
{
    return -ENOTSUP;
}
#endif

ssize_t nbd_send_request(QIOChannel *ioc, struct nbd_request *request)
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

    ret = write_sync(ioc, buf, sizeof(buf));
    if (ret < 0) {
        return ret;
    }

    if (ret != sizeof(buf)) {
        LOG("writing to socket failed");
        return -EINVAL;
    }
    return 0;
}

ssize_t nbd_receive_reply(QIOChannel *ioc, struct nbd_reply *reply)
{
    uint8_t buf[NBD_REPLY_SIZE];
    uint32_t magic;
    ssize_t ret;

    ret = read_sync(ioc, buf, sizeof(buf));
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

