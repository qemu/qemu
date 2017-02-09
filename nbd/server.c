/*
 *  Copyright (C) 2016 Red Hat, Inc.
 *  Copyright (C) 2005  Anthony Liguori <anthony@codemonkey.ws>
 *
 *  Network Block Device Server Side
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

static int system_errno_to_nbd_errno(int err)
{
    switch (err) {
    case 0:
        return NBD_SUCCESS;
    case EPERM:
    case EROFS:
        return NBD_EPERM;
    case EIO:
        return NBD_EIO;
    case ENOMEM:
        return NBD_ENOMEM;
#ifdef EDQUOT
    case EDQUOT:
#endif
    case EFBIG:
    case ENOSPC:
        return NBD_ENOSPC;
    case ESHUTDOWN:
        return NBD_ESHUTDOWN;
    case EINVAL:
    default:
        return NBD_EINVAL;
    }
}

/* Definitions for opaque data types */

typedef struct NBDRequestData NBDRequestData;

struct NBDRequestData {
    QSIMPLEQ_ENTRY(NBDRequestData) entry;
    NBDClient *client;
    uint8_t *data;
    bool complete;
};

struct NBDExport {
    int refcount;
    void (*close)(NBDExport *exp);

    BlockBackend *blk;
    char *name;
    char *description;
    off_t dev_offset;
    off_t size;
    uint16_t nbdflags;
    QTAILQ_HEAD(, NBDClient) clients;
    QTAILQ_ENTRY(NBDExport) next;

    AioContext *ctx;

    BlockBackend *eject_notifier_blk;
    Notifier eject_notifier;
};

static QTAILQ_HEAD(, NBDExport) exports = QTAILQ_HEAD_INITIALIZER(exports);

struct NBDClient {
    int refcount;
    void (*close)(NBDClient *client);

    bool no_zeroes;
    NBDExport *exp;
    QCryptoTLSCreds *tlscreds;
    char *tlsaclname;
    QIOChannelSocket *sioc; /* The underlying data channel */
    QIOChannel *ioc; /* The current I/O channel which may differ (eg TLS) */

    Coroutine *recv_coroutine;

    CoMutex send_lock;
    Coroutine *send_coroutine;

    QTAILQ_ENTRY(NBDClient) next;
    int nb_requests;
    bool closing;
};

/* That's all folks */

static void nbd_client_receive_next_request(NBDClient *client);

static gboolean nbd_negotiate_continue(QIOChannel *ioc,
                                       GIOCondition condition,
                                       void *opaque)
{
    qemu_coroutine_enter(opaque);
    return TRUE;
}

static ssize_t nbd_negotiate_read(QIOChannel *ioc, void *buffer, size_t size)
{
    ssize_t ret;
    guint watch;

    assert(qemu_in_coroutine());
    /* Negotiation are always in main loop. */
    watch = qio_channel_add_watch(ioc,
                                  G_IO_IN,
                                  nbd_negotiate_continue,
                                  qemu_coroutine_self(),
                                  NULL);
    ret = read_sync(ioc, buffer, size);
    g_source_remove(watch);
    return ret;

}

static ssize_t nbd_negotiate_write(QIOChannel *ioc, const void *buffer,
                                   size_t size)
{
    ssize_t ret;
    guint watch;

    assert(qemu_in_coroutine());
    /* Negotiation are always in main loop. */
    watch = qio_channel_add_watch(ioc,
                                  G_IO_OUT,
                                  nbd_negotiate_continue,
                                  qemu_coroutine_self(),
                                  NULL);
    ret = write_sync(ioc, buffer, size);
    g_source_remove(watch);
    return ret;
}

static ssize_t nbd_negotiate_drop_sync(QIOChannel *ioc, size_t size)
{
    ssize_t ret, dropped = size;
    uint8_t *buffer = g_malloc(MIN(65536, size));

    while (size > 0) {
        ret = nbd_negotiate_read(ioc, buffer, MIN(65536, size));
        if (ret < 0) {
            g_free(buffer);
            return ret;
        }

        assert(ret <= size);
        size -= ret;
    }

    g_free(buffer);
    return dropped;
}

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

/* Send a reply header, including length, but no payload.
 * Return -errno on error, 0 on success. */
static int nbd_negotiate_send_rep_len(QIOChannel *ioc, uint32_t type,
                                      uint32_t opt, uint32_t len)
{
    uint64_t magic;

    TRACE("Reply opt=%" PRIx32 " type=%" PRIx32 " len=%" PRIu32,
          type, opt, len);

    magic = cpu_to_be64(NBD_REP_MAGIC);
    if (nbd_negotiate_write(ioc, &magic, sizeof(magic)) != sizeof(magic)) {
        LOG("write failed (rep magic)");
        return -EINVAL;
    }
    opt = cpu_to_be32(opt);
    if (nbd_negotiate_write(ioc, &opt, sizeof(opt)) != sizeof(opt)) {
        LOG("write failed (rep opt)");
        return -EINVAL;
    }
    type = cpu_to_be32(type);
    if (nbd_negotiate_write(ioc, &type, sizeof(type)) != sizeof(type)) {
        LOG("write failed (rep type)");
        return -EINVAL;
    }
    len = cpu_to_be32(len);
    if (nbd_negotiate_write(ioc, &len, sizeof(len)) != sizeof(len)) {
        LOG("write failed (rep data length)");
        return -EINVAL;
    }
    return 0;
}

/* Send a reply header with default 0 length.
 * Return -errno on error, 0 on success. */
static int nbd_negotiate_send_rep(QIOChannel *ioc, uint32_t type, uint32_t opt)
{
    return nbd_negotiate_send_rep_len(ioc, type, opt, 0);
}

/* Send an error reply.
 * Return -errno on error, 0 on success. */
static int GCC_FMT_ATTR(4, 5)
nbd_negotiate_send_rep_err(QIOChannel *ioc, uint32_t type,
                           uint32_t opt, const char *fmt, ...)
{
    va_list va;
    char *msg;
    int ret;
    size_t len;

    va_start(va, fmt);
    msg = g_strdup_vprintf(fmt, va);
    va_end(va);
    len = strlen(msg);
    assert(len < 4096);
    TRACE("sending error message \"%s\"", msg);
    ret = nbd_negotiate_send_rep_len(ioc, type, opt, len);
    if (ret < 0) {
        goto out;
    }
    if (nbd_negotiate_write(ioc, msg, len) != len) {
        LOG("write failed (error message)");
        ret = -EIO;
    } else {
        ret = 0;
    }
out:
    g_free(msg);
    return ret;
}

/* Send a single NBD_REP_SERVER reply to NBD_OPT_LIST, including payload.
 * Return -errno on error, 0 on success. */
static int nbd_negotiate_send_rep_list(QIOChannel *ioc, NBDExport *exp)
{
    size_t name_len, desc_len;
    uint32_t len;
    const char *name = exp->name ? exp->name : "";
    const char *desc = exp->description ? exp->description : "";
    int rc;

    TRACE("Advertising export name '%s' description '%s'", name, desc);
    name_len = strlen(name);
    desc_len = strlen(desc);
    len = name_len + desc_len + sizeof(len);
    rc = nbd_negotiate_send_rep_len(ioc, NBD_REP_SERVER, NBD_OPT_LIST, len);
    if (rc < 0) {
        return rc;
    }

    len = cpu_to_be32(name_len);
    if (nbd_negotiate_write(ioc, &len, sizeof(len)) != sizeof(len)) {
        LOG("write failed (name length)");
        return -EINVAL;
    }
    if (nbd_negotiate_write(ioc, name, name_len) != name_len) {
        LOG("write failed (name buffer)");
        return -EINVAL;
    }
    if (nbd_negotiate_write(ioc, desc, desc_len) != desc_len) {
        LOG("write failed (description buffer)");
        return -EINVAL;
    }
    return 0;
}

/* Process the NBD_OPT_LIST command, with a potential series of replies.
 * Return -errno on error, 0 on success. */
static int nbd_negotiate_handle_list(NBDClient *client, uint32_t length)
{
    NBDExport *exp;

    if (length) {
        if (nbd_negotiate_drop_sync(client->ioc, length) != length) {
            return -EIO;
        }
        return nbd_negotiate_send_rep_err(client->ioc,
                                          NBD_REP_ERR_INVALID, NBD_OPT_LIST,
                                          "OPT_LIST should not have length");
    }

    /* For each export, send a NBD_REP_SERVER reply. */
    QTAILQ_FOREACH(exp, &exports, next) {
        if (nbd_negotiate_send_rep_list(client->ioc, exp)) {
            return -EINVAL;
        }
    }
    /* Finish with a NBD_REP_ACK. */
    return nbd_negotiate_send_rep(client->ioc, NBD_REP_ACK, NBD_OPT_LIST);
}

static int nbd_negotiate_handle_export_name(NBDClient *client, uint32_t length)
{
    int rc = -EINVAL;
    char name[NBD_MAX_NAME_SIZE + 1];

    /* Client sends:
        [20 ..  xx]   export name (length bytes)
     */
    TRACE("Checking length");
    if (length >= sizeof(name)) {
        LOG("Bad length received");
        goto fail;
    }
    if (nbd_negotiate_read(client->ioc, name, length) != length) {
        LOG("read failed");
        goto fail;
    }
    name[length] = '\0';

    TRACE("Client requested export '%s'", name);

    client->exp = nbd_export_find(name);
    if (!client->exp) {
        LOG("export not found");
        goto fail;
    }

    QTAILQ_INSERT_TAIL(&client->exp->clients, client, next);
    nbd_export_get(client->exp);
    rc = 0;
fail:
    return rc;
}

/* Handle NBD_OPT_STARTTLS. Return NULL to drop connection, or else the
 * new channel for all further (now-encrypted) communication. */
static QIOChannel *nbd_negotiate_handle_starttls(NBDClient *client,
                                                 uint32_t length)
{
    QIOChannel *ioc;
    QIOChannelTLS *tioc;
    struct NBDTLSHandshakeData data = { 0 };

    TRACE("Setting up TLS");
    ioc = client->ioc;
    if (length) {
        if (nbd_negotiate_drop_sync(ioc, length) != length) {
            return NULL;
        }
        nbd_negotiate_send_rep_err(ioc, NBD_REP_ERR_INVALID, NBD_OPT_STARTTLS,
                                   "OPT_STARTTLS should not have length");
        return NULL;
    }

    if (nbd_negotiate_send_rep(client->ioc, NBD_REP_ACK,
                               NBD_OPT_STARTTLS) < 0) {
        return NULL;
    }

    tioc = qio_channel_tls_new_server(ioc,
                                      client->tlscreds,
                                      client->tlsaclname,
                                      NULL);
    if (!tioc) {
        return NULL;
    }

    qio_channel_set_name(QIO_CHANNEL(tioc), "nbd-server-tls");
    TRACE("Starting TLS handshake");
    data.loop = g_main_loop_new(g_main_context_default(), FALSE);
    qio_channel_tls_handshake(tioc,
                              nbd_tls_handshake,
                              &data,
                              NULL);

    if (!data.complete) {
        g_main_loop_run(data.loop);
    }
    g_main_loop_unref(data.loop);
    if (data.error) {
        object_unref(OBJECT(tioc));
        error_free(data.error);
        return NULL;
    }

    return QIO_CHANNEL(tioc);
}


/* Process all NBD_OPT_* client option commands.
 * Return -errno on error, 0 on success. */
static int nbd_negotiate_options(NBDClient *client)
{
    uint32_t flags;
    bool fixedNewstyle = false;

    /* Client sends:
        [ 0 ..   3]   client flags

        [ 0 ..   7]   NBD_OPTS_MAGIC
        [ 8 ..  11]   NBD option
        [12 ..  15]   Data length
        ...           Rest of request

        [ 0 ..   7]   NBD_OPTS_MAGIC
        [ 8 ..  11]   Second NBD option
        [12 ..  15]   Data length
        ...           Rest of request
    */

    if (nbd_negotiate_read(client->ioc, &flags, sizeof(flags)) !=
        sizeof(flags)) {
        LOG("read failed");
        return -EIO;
    }
    TRACE("Checking client flags");
    be32_to_cpus(&flags);
    if (flags & NBD_FLAG_C_FIXED_NEWSTYLE) {
        TRACE("Client supports fixed newstyle handshake");
        fixedNewstyle = true;
        flags &= ~NBD_FLAG_C_FIXED_NEWSTYLE;
    }
    if (flags & NBD_FLAG_C_NO_ZEROES) {
        TRACE("Client supports no zeroes at handshake end");
        client->no_zeroes = true;
        flags &= ~NBD_FLAG_C_NO_ZEROES;
    }
    if (flags != 0) {
        TRACE("Unknown client flags 0x%" PRIx32 " received", flags);
        return -EIO;
    }

    while (1) {
        int ret;
        uint32_t clientflags, length;
        uint64_t magic;

        if (nbd_negotiate_read(client->ioc, &magic, sizeof(magic)) !=
            sizeof(magic)) {
            LOG("read failed");
            return -EINVAL;
        }
        TRACE("Checking opts magic");
        if (magic != be64_to_cpu(NBD_OPTS_MAGIC)) {
            LOG("Bad magic received");
            return -EINVAL;
        }

        if (nbd_negotiate_read(client->ioc, &clientflags,
                               sizeof(clientflags)) != sizeof(clientflags)) {
            LOG("read failed");
            return -EINVAL;
        }
        clientflags = be32_to_cpu(clientflags);

        if (nbd_negotiate_read(client->ioc, &length, sizeof(length)) !=
            sizeof(length)) {
            LOG("read failed");
            return -EINVAL;
        }
        length = be32_to_cpu(length);

        TRACE("Checking option 0x%" PRIx32, clientflags);
        if (client->tlscreds &&
            client->ioc == (QIOChannel *)client->sioc) {
            QIOChannel *tioc;
            if (!fixedNewstyle) {
                TRACE("Unsupported option 0x%" PRIx32, clientflags);
                return -EINVAL;
            }
            switch (clientflags) {
            case NBD_OPT_STARTTLS:
                tioc = nbd_negotiate_handle_starttls(client, length);
                if (!tioc) {
                    return -EIO;
                }
                object_unref(OBJECT(client->ioc));
                client->ioc = QIO_CHANNEL(tioc);
                break;

            case NBD_OPT_EXPORT_NAME:
                /* No way to return an error to client, so drop connection */
                TRACE("Option 0x%x not permitted before TLS", clientflags);
                return -EINVAL;

            default:
                if (nbd_negotiate_drop_sync(client->ioc, length) != length) {
                    return -EIO;
                }
                ret = nbd_negotiate_send_rep_err(client->ioc,
                                                 NBD_REP_ERR_TLS_REQD,
                                                 clientflags,
                                                 "Option 0x%" PRIx32
                                                 "not permitted before TLS",
                                                 clientflags);
                if (ret < 0) {
                    return ret;
                }
                /* Let the client keep trying, unless they asked to quit */
                if (clientflags == NBD_OPT_ABORT) {
                    return -EINVAL;
                }
                break;
            }
        } else if (fixedNewstyle) {
            switch (clientflags) {
            case NBD_OPT_LIST:
                ret = nbd_negotiate_handle_list(client, length);
                if (ret < 0) {
                    return ret;
                }
                break;

            case NBD_OPT_ABORT:
                /* NBD spec says we must try to reply before
                 * disconnecting, but that we must also tolerate
                 * guests that don't wait for our reply. */
                nbd_negotiate_send_rep(client->ioc, NBD_REP_ACK, clientflags);
                return -EINVAL;

            case NBD_OPT_EXPORT_NAME:
                return nbd_negotiate_handle_export_name(client, length);

            case NBD_OPT_STARTTLS:
                if (nbd_negotiate_drop_sync(client->ioc, length) != length) {
                    return -EIO;
                }
                if (client->tlscreds) {
                    ret = nbd_negotiate_send_rep_err(client->ioc,
                                                     NBD_REP_ERR_INVALID,
                                                     clientflags,
                                                     "TLS already enabled");
                } else {
                    ret = nbd_negotiate_send_rep_err(client->ioc,
                                                     NBD_REP_ERR_POLICY,
                                                     clientflags,
                                                     "TLS not configured");
                }
                if (ret < 0) {
                    return ret;
                }
                break;
            default:
                if (nbd_negotiate_drop_sync(client->ioc, length) != length) {
                    return -EIO;
                }
                ret = nbd_negotiate_send_rep_err(client->ioc,
                                                 NBD_REP_ERR_UNSUP,
                                                 clientflags,
                                                 "Unsupported option 0x%"
                                                 PRIx32,
                                                 clientflags);
                if (ret < 0) {
                    return ret;
                }
                break;
            }
        } else {
            /*
             * If broken new-style we should drop the connection
             * for anything except NBD_OPT_EXPORT_NAME
             */
            switch (clientflags) {
            case NBD_OPT_EXPORT_NAME:
                return nbd_negotiate_handle_export_name(client, length);

            default:
                TRACE("Unsupported option 0x%" PRIx32, clientflags);
                return -EINVAL;
            }
        }
    }
}

typedef struct {
    NBDClient *client;
    Coroutine *co;
} NBDClientNewData;

static coroutine_fn int nbd_negotiate(NBDClientNewData *data)
{
    NBDClient *client = data->client;
    char buf[8 + 8 + 8 + 128];
    int rc;
    const uint16_t myflags = (NBD_FLAG_HAS_FLAGS | NBD_FLAG_SEND_TRIM |
                              NBD_FLAG_SEND_FLUSH | NBD_FLAG_SEND_FUA |
                              NBD_FLAG_SEND_WRITE_ZEROES);
    bool oldStyle;
    size_t len;

    /* Old style negotiation header without options
        [ 0 ..   7]   passwd       ("NBDMAGIC")
        [ 8 ..  15]   magic        (NBD_CLIENT_MAGIC)
        [16 ..  23]   size
        [24 ..  25]   server flags (0)
        [26 ..  27]   export flags
        [28 .. 151]   reserved     (0)

       New style negotiation header with options
        [ 0 ..   7]   passwd       ("NBDMAGIC")
        [ 8 ..  15]   magic        (NBD_OPTS_MAGIC)
        [16 ..  17]   server flags (0)
        ....options sent....
        [18 ..  25]   size
        [26 ..  27]   export flags
        [28 .. 151]   reserved     (0, omit if no_zeroes)
     */

    qio_channel_set_blocking(client->ioc, false, NULL);
    rc = -EINVAL;

    TRACE("Beginning negotiation.");
    memset(buf, 0, sizeof(buf));
    memcpy(buf, "NBDMAGIC", 8);

    oldStyle = client->exp != NULL && !client->tlscreds;
    if (oldStyle) {
        TRACE("advertising size %" PRIu64 " and flags %x",
              client->exp->size, client->exp->nbdflags | myflags);
        stq_be_p(buf + 8, NBD_CLIENT_MAGIC);
        stq_be_p(buf + 16, client->exp->size);
        stw_be_p(buf + 26, client->exp->nbdflags | myflags);
    } else {
        stq_be_p(buf + 8, NBD_OPTS_MAGIC);
        stw_be_p(buf + 16, NBD_FLAG_FIXED_NEWSTYLE | NBD_FLAG_NO_ZEROES);
    }

    if (oldStyle) {
        if (client->tlscreds) {
            TRACE("TLS cannot be enabled with oldstyle protocol");
            goto fail;
        }
        if (nbd_negotiate_write(client->ioc, buf, sizeof(buf)) != sizeof(buf)) {
            LOG("write failed");
            goto fail;
        }
    } else {
        if (nbd_negotiate_write(client->ioc, buf, 18) != 18) {
            LOG("write failed");
            goto fail;
        }
        rc = nbd_negotiate_options(client);
        if (rc != 0) {
            LOG("option negotiation failed");
            goto fail;
        }

        TRACE("advertising size %" PRIu64 " and flags %x",
              client->exp->size, client->exp->nbdflags | myflags);
        stq_be_p(buf + 18, client->exp->size);
        stw_be_p(buf + 26, client->exp->nbdflags | myflags);
        len = client->no_zeroes ? 10 : sizeof(buf) - 18;
        if (nbd_negotiate_write(client->ioc, buf + 18, len) != len) {
            LOG("write failed");
            goto fail;
        }
    }

    TRACE("Negotiation succeeded.");
    rc = 0;
fail:
    return rc;
}

static ssize_t nbd_receive_request(QIOChannel *ioc, NBDRequest *request)
{
    uint8_t buf[NBD_REQUEST_SIZE];
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

    /* Request
       [ 0 ..  3]   magic   (NBD_REQUEST_MAGIC)
       [ 4 ..  5]   flags   (NBD_CMD_FLAG_FUA, ...)
       [ 6 ..  7]   type    (NBD_CMD_READ, ...)
       [ 8 .. 15]   handle
       [16 .. 23]   from
       [24 .. 27]   len
     */

    magic = ldl_be_p(buf);
    request->flags  = lduw_be_p(buf + 4);
    request->type   = lduw_be_p(buf + 6);
    request->handle = ldq_be_p(buf + 8);
    request->from   = ldq_be_p(buf + 16);
    request->len    = ldl_be_p(buf + 24);

    TRACE("Got request: { magic = 0x%" PRIx32 ", .flags = %" PRIx16
          ", .type = %" PRIx16 ", from = %" PRIu64 ", len = %" PRIu32 " }",
          magic, request->flags, request->type, request->from, request->len);

    if (magic != NBD_REQUEST_MAGIC) {
        LOG("invalid magic (got 0x%" PRIx32 ")", magic);
        return -EINVAL;
    }
    return 0;
}

static ssize_t nbd_send_reply(QIOChannel *ioc, NBDReply *reply)
{
    uint8_t buf[NBD_REPLY_SIZE];
    ssize_t ret;

    reply->error = system_errno_to_nbd_errno(reply->error);

    TRACE("Sending response to client: { .error = %" PRId32
          ", handle = %" PRIu64 " }",
          reply->error, reply->handle);

    /* Reply
       [ 0 ..  3]    magic   (NBD_REPLY_MAGIC)
       [ 4 ..  7]    error   (0 == no error)
       [ 7 .. 15]    handle
     */
    stl_be_p(buf, NBD_REPLY_MAGIC);
    stl_be_p(buf + 4, reply->error);
    stq_be_p(buf + 8, reply->handle);

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

#define MAX_NBD_REQUESTS 16

void nbd_client_get(NBDClient *client)
{
    client->refcount++;
}

void nbd_client_put(NBDClient *client)
{
    if (--client->refcount == 0) {
        /* The last reference should be dropped by client->close,
         * which is called by client_close.
         */
        assert(client->closing);

        qio_channel_detach_aio_context(client->ioc);
        object_unref(OBJECT(client->sioc));
        object_unref(OBJECT(client->ioc));
        if (client->tlscreds) {
            object_unref(OBJECT(client->tlscreds));
        }
        g_free(client->tlsaclname);
        if (client->exp) {
            QTAILQ_REMOVE(&client->exp->clients, client, next);
            nbd_export_put(client->exp);
        }
        g_free(client);
    }
}

static void client_close(NBDClient *client)
{
    if (client->closing) {
        return;
    }

    client->closing = true;

    /* Force requests to finish.  They will drop their own references,
     * then we'll close the socket and free the NBDClient.
     */
    qio_channel_shutdown(client->ioc, QIO_CHANNEL_SHUTDOWN_BOTH,
                         NULL);

    /* Also tell the client, so that they release their reference.  */
    if (client->close) {
        client->close(client);
    }
}

static NBDRequestData *nbd_request_get(NBDClient *client)
{
    NBDRequestData *req;

    assert(client->nb_requests <= MAX_NBD_REQUESTS - 1);
    client->nb_requests++;

    req = g_new0(NBDRequestData, 1);
    nbd_client_get(client);
    req->client = client;
    return req;
}

static void nbd_request_put(NBDRequestData *req)
{
    NBDClient *client = req->client;

    if (req->data) {
        qemu_vfree(req->data);
    }
    g_free(req);

    client->nb_requests--;
    nbd_client_receive_next_request(client);

    nbd_client_put(client);
}

static void blk_aio_attached(AioContext *ctx, void *opaque)
{
    NBDExport *exp = opaque;
    NBDClient *client;

    TRACE("Export %s: Attaching clients to AIO context %p\n", exp->name, ctx);

    exp->ctx = ctx;

    QTAILQ_FOREACH(client, &exp->clients, next) {
        qio_channel_attach_aio_context(client->ioc, ctx);
        if (client->recv_coroutine) {
            aio_co_schedule(ctx, client->recv_coroutine);
        }
        if (client->send_coroutine) {
            aio_co_schedule(ctx, client->send_coroutine);
        }
    }
}

static void blk_aio_detach(void *opaque)
{
    NBDExport *exp = opaque;
    NBDClient *client;

    TRACE("Export %s: Detaching clients from AIO context %p\n", exp->name, exp->ctx);

    QTAILQ_FOREACH(client, &exp->clients, next) {
        qio_channel_detach_aio_context(client->ioc);
    }

    exp->ctx = NULL;
}

static void nbd_eject_notifier(Notifier *n, void *data)
{
    NBDExport *exp = container_of(n, NBDExport, eject_notifier);
    nbd_export_close(exp);
}

NBDExport *nbd_export_new(BlockDriverState *bs, off_t dev_offset, off_t size,
                          uint16_t nbdflags, void (*close)(NBDExport *),
                          bool writethrough, BlockBackend *on_eject_blk,
                          Error **errp)
{
    BlockBackend *blk;
    NBDExport *exp = g_malloc0(sizeof(NBDExport));
    uint64_t perm;
    int ret;

    /* Don't allow resize while the NBD server is running, otherwise we don't
     * care what happens with the node. */
    perm = BLK_PERM_CONSISTENT_READ;
    if ((nbdflags & NBD_FLAG_READ_ONLY) == 0) {
        perm |= BLK_PERM_WRITE;
    }
    blk = blk_new(perm, BLK_PERM_CONSISTENT_READ | BLK_PERM_WRITE_UNCHANGED |
                        BLK_PERM_WRITE | BLK_PERM_GRAPH_MOD);
    ret = blk_insert_bs(blk, bs, errp);
    if (ret < 0) {
        goto fail;
    }
    blk_set_enable_write_cache(blk, !writethrough);

    exp->refcount = 1;
    QTAILQ_INIT(&exp->clients);
    exp->blk = blk;
    exp->dev_offset = dev_offset;
    exp->nbdflags = nbdflags;
    exp->size = size < 0 ? blk_getlength(blk) : size;
    if (exp->size < 0) {
        error_setg_errno(errp, -exp->size,
                         "Failed to determine the NBD export's length");
        goto fail;
    }
    exp->size -= exp->size % BDRV_SECTOR_SIZE;

    exp->close = close;
    exp->ctx = blk_get_aio_context(blk);
    blk_add_aio_context_notifier(blk, blk_aio_attached, blk_aio_detach, exp);

    if (on_eject_blk) {
        blk_ref(on_eject_blk);
        exp->eject_notifier_blk = on_eject_blk;
        exp->eject_notifier.notify = nbd_eject_notifier;
        blk_add_remove_bs_notifier(on_eject_blk, &exp->eject_notifier);
    }

    /*
     * NBD exports are used for non-shared storage migration.  Make sure
     * that BDRV_O_INACTIVE is cleared and the image is ready for write
     * access since the export could be available before migration handover.
     */
    aio_context_acquire(exp->ctx);
    blk_invalidate_cache(blk, NULL);
    aio_context_release(exp->ctx);
    return exp;

fail:
    blk_unref(blk);
    g_free(exp);
    return NULL;
}

NBDExport *nbd_export_find(const char *name)
{
    NBDExport *exp;
    QTAILQ_FOREACH(exp, &exports, next) {
        if (strcmp(name, exp->name) == 0) {
            return exp;
        }
    }

    return NULL;
}

void nbd_export_set_name(NBDExport *exp, const char *name)
{
    if (exp->name == name) {
        return;
    }

    nbd_export_get(exp);
    if (exp->name != NULL) {
        g_free(exp->name);
        exp->name = NULL;
        QTAILQ_REMOVE(&exports, exp, next);
        nbd_export_put(exp);
    }
    if (name != NULL) {
        nbd_export_get(exp);
        exp->name = g_strdup(name);
        QTAILQ_INSERT_TAIL(&exports, exp, next);
    }
    nbd_export_put(exp);
}

void nbd_export_set_description(NBDExport *exp, const char *description)
{
    g_free(exp->description);
    exp->description = g_strdup(description);
}

void nbd_export_close(NBDExport *exp)
{
    NBDClient *client, *next;

    nbd_export_get(exp);
    QTAILQ_FOREACH_SAFE(client, &exp->clients, next, next) {
        client_close(client);
    }
    nbd_export_set_name(exp, NULL);
    nbd_export_set_description(exp, NULL);
    nbd_export_put(exp);
}

void nbd_export_get(NBDExport *exp)
{
    assert(exp->refcount > 0);
    exp->refcount++;
}

void nbd_export_put(NBDExport *exp)
{
    assert(exp->refcount > 0);
    if (exp->refcount == 1) {
        nbd_export_close(exp);
    }

    if (--exp->refcount == 0) {
        assert(exp->name == NULL);
        assert(exp->description == NULL);

        if (exp->close) {
            exp->close(exp);
        }

        if (exp->blk) {
            if (exp->eject_notifier_blk) {
                notifier_remove(&exp->eject_notifier);
                blk_unref(exp->eject_notifier_blk);
            }
            blk_remove_aio_context_notifier(exp->blk, blk_aio_attached,
                                            blk_aio_detach, exp);
            blk_unref(exp->blk);
            exp->blk = NULL;
        }

        g_free(exp);
    }
}

BlockBackend *nbd_export_get_blockdev(NBDExport *exp)
{
    return exp->blk;
}

void nbd_export_close_all(void)
{
    NBDExport *exp, *next;

    QTAILQ_FOREACH_SAFE(exp, &exports, next, next) {
        nbd_export_close(exp);
    }
}

static ssize_t nbd_co_send_reply(NBDRequestData *req, NBDReply *reply,
                                 int len)
{
    NBDClient *client = req->client;
    ssize_t rc, ret;

    g_assert(qemu_in_coroutine());
    qemu_co_mutex_lock(&client->send_lock);
    client->send_coroutine = qemu_coroutine_self();

    if (!len) {
        rc = nbd_send_reply(client->ioc, reply);
    } else {
        qio_channel_set_cork(client->ioc, true);
        rc = nbd_send_reply(client->ioc, reply);
        if (rc >= 0) {
            ret = write_sync(client->ioc, req->data, len);
            if (ret != len) {
                rc = -EIO;
            }
        }
        qio_channel_set_cork(client->ioc, false);
    }

    client->send_coroutine = NULL;
    qemu_co_mutex_unlock(&client->send_lock);
    return rc;
}

/* Collect a client request.  Return 0 if request looks valid, -EAGAIN
 * to keep trying the collection, -EIO to drop connection right away,
 * and any other negative value to report an error to the client
 * (although the caller may still need to disconnect after reporting
 * the error).  */
static ssize_t nbd_co_receive_request(NBDRequestData *req,
                                      NBDRequest *request)
{
    NBDClient *client = req->client;
    ssize_t rc;

    g_assert(qemu_in_coroutine());
    assert(client->recv_coroutine == qemu_coroutine_self());
    rc = nbd_receive_request(client->ioc, request);
    if (rc < 0) {
        if (rc != -EAGAIN) {
            rc = -EIO;
        }
        goto out;
    }

    TRACE("Decoding type");

    if (request->type != NBD_CMD_WRITE) {
        /* No payload, we are ready to read the next request.  */
        req->complete = true;
    }

    if (request->type == NBD_CMD_DISC) {
        /* Special case: we're going to disconnect without a reply,
         * whether or not flags, from, or len are bogus */
        TRACE("Request type is DISCONNECT");
        rc = -EIO;
        goto out;
    }

    /* Check for sanity in the parameters, part 1.  Defer as many
     * checks as possible until after reading any NBD_CMD_WRITE
     * payload, so we can try and keep the connection alive.  */
    if ((request->from + request->len) < request->from) {
        LOG("integer overflow detected, you're probably being attacked");
        rc = -EINVAL;
        goto out;
    }

    if (request->type == NBD_CMD_READ || request->type == NBD_CMD_WRITE) {
        if (request->len > NBD_MAX_BUFFER_SIZE) {
            LOG("len (%" PRIu32" ) is larger than max len (%u)",
                request->len, NBD_MAX_BUFFER_SIZE);
            rc = -EINVAL;
            goto out;
        }

        req->data = blk_try_blockalign(client->exp->blk, request->len);
        if (req->data == NULL) {
            rc = -ENOMEM;
            goto out;
        }
    }
    if (request->type == NBD_CMD_WRITE) {
        TRACE("Reading %" PRIu32 " byte(s)", request->len);

        if (read_sync(client->ioc, req->data, request->len) != request->len) {
            LOG("reading from socket failed");
            rc = -EIO;
            goto out;
        }
        req->complete = true;
    }

    /* Sanity checks, part 2. */
    if (request->from + request->len > client->exp->size) {
        LOG("operation past EOF; From: %" PRIu64 ", Len: %" PRIu32
            ", Size: %" PRIu64, request->from, request->len,
            (uint64_t)client->exp->size);
        rc = request->type == NBD_CMD_WRITE ? -ENOSPC : -EINVAL;
        goto out;
    }
    if (request->flags & ~(NBD_CMD_FLAG_FUA | NBD_CMD_FLAG_NO_HOLE)) {
        LOG("unsupported flags (got 0x%x)", request->flags);
        rc = -EINVAL;
        goto out;
    }
    if (request->type != NBD_CMD_WRITE_ZEROES &&
        (request->flags & NBD_CMD_FLAG_NO_HOLE)) {
        LOG("unexpected flags (got 0x%x)", request->flags);
        rc = -EINVAL;
        goto out;
    }

    rc = 0;

out:
    client->recv_coroutine = NULL;
    nbd_client_receive_next_request(client);

    return rc;
}

/* Owns a reference to the NBDClient passed as opaque.  */
static coroutine_fn void nbd_trip(void *opaque)
{
    NBDClient *client = opaque;
    NBDExport *exp = client->exp;
    NBDRequestData *req;
    NBDRequest request = { 0 };    /* GCC thinks it can be used uninitialized */
    NBDReply reply;
    ssize_t ret;
    int flags;

    TRACE("Reading request.");
    if (client->closing) {
        nbd_client_put(client);
        return;
    }

    req = nbd_request_get(client);
    ret = nbd_co_receive_request(req, &request);
    if (ret == -EAGAIN) {
        goto done;
    }
    if (ret == -EIO) {
        goto out;
    }

    reply.handle = request.handle;
    reply.error = 0;

    if (ret < 0) {
        reply.error = -ret;
        goto error_reply;
    }

    if (client->closing) {
        /*
         * The client may be closed when we are blocked in
         * nbd_co_receive_request()
         */
        goto done;
    }

    switch (request.type) {
    case NBD_CMD_READ:
        TRACE("Request type is READ");

        /* XXX: NBD Protocol only documents use of FUA with WRITE */
        if (request.flags & NBD_CMD_FLAG_FUA) {
            ret = blk_co_flush(exp->blk);
            if (ret < 0) {
                LOG("flush failed");
                reply.error = -ret;
                goto error_reply;
            }
        }

        ret = blk_pread(exp->blk, request.from + exp->dev_offset,
                        req->data, request.len);
        if (ret < 0) {
            LOG("reading from file failed");
            reply.error = -ret;
            goto error_reply;
        }

        TRACE("Read %" PRIu32" byte(s)", request.len);
        if (nbd_co_send_reply(req, &reply, request.len) < 0)
            goto out;
        break;
    case NBD_CMD_WRITE:
        TRACE("Request type is WRITE");

        if (exp->nbdflags & NBD_FLAG_READ_ONLY) {
            TRACE("Server is read-only, return error");
            reply.error = EROFS;
            goto error_reply;
        }

        TRACE("Writing to device");

        flags = 0;
        if (request.flags & NBD_CMD_FLAG_FUA) {
            flags |= BDRV_REQ_FUA;
        }
        ret = blk_pwrite(exp->blk, request.from + exp->dev_offset,
                         req->data, request.len, flags);
        if (ret < 0) {
            LOG("writing to file failed");
            reply.error = -ret;
            goto error_reply;
        }

        if (nbd_co_send_reply(req, &reply, 0) < 0) {
            goto out;
        }
        break;

    case NBD_CMD_WRITE_ZEROES:
        TRACE("Request type is WRITE_ZEROES");

        if (exp->nbdflags & NBD_FLAG_READ_ONLY) {
            TRACE("Server is read-only, return error");
            reply.error = EROFS;
            goto error_reply;
        }

        TRACE("Writing to device");

        flags = 0;
        if (request.flags & NBD_CMD_FLAG_FUA) {
            flags |= BDRV_REQ_FUA;
        }
        if (!(request.flags & NBD_CMD_FLAG_NO_HOLE)) {
            flags |= BDRV_REQ_MAY_UNMAP;
        }
        ret = blk_pwrite_zeroes(exp->blk, request.from + exp->dev_offset,
                                request.len, flags);
        if (ret < 0) {
            LOG("writing to file failed");
            reply.error = -ret;
            goto error_reply;
        }

        if (nbd_co_send_reply(req, &reply, 0) < 0) {
            goto out;
        }
        break;

    case NBD_CMD_DISC:
        /* unreachable, thanks to special case in nbd_co_receive_request() */
        abort();

    case NBD_CMD_FLUSH:
        TRACE("Request type is FLUSH");

        ret = blk_co_flush(exp->blk);
        if (ret < 0) {
            LOG("flush failed");
            reply.error = -ret;
        }
        if (nbd_co_send_reply(req, &reply, 0) < 0) {
            goto out;
        }
        break;
    case NBD_CMD_TRIM:
        TRACE("Request type is TRIM");
        ret = blk_co_pdiscard(exp->blk, request.from + exp->dev_offset,
                              request.len);
        if (ret < 0) {
            LOG("discard failed");
            reply.error = -ret;
        }
        if (nbd_co_send_reply(req, &reply, 0) < 0) {
            goto out;
        }
        break;
    default:
        LOG("invalid request type (%" PRIu32 ") received", request.type);
        reply.error = EINVAL;
    error_reply:
        /* We must disconnect after NBD_CMD_WRITE if we did not
         * read the payload.
         */
        if (nbd_co_send_reply(req, &reply, 0) < 0 || !req->complete) {
            goto out;
        }
        break;
    }

    TRACE("Request/Reply complete");

done:
    nbd_request_put(req);
    nbd_client_put(client);
    return;

out:
    nbd_request_put(req);
    client_close(client);
    nbd_client_put(client);
}

static void nbd_client_receive_next_request(NBDClient *client)
{
    if (!client->recv_coroutine && client->nb_requests < MAX_NBD_REQUESTS) {
        nbd_client_get(client);
        client->recv_coroutine = qemu_coroutine_create(nbd_trip, client);
        aio_co_schedule(client->exp->ctx, client->recv_coroutine);
    }
}

static coroutine_fn void nbd_co_client_start(void *opaque)
{
    NBDClientNewData *data = opaque;
    NBDClient *client = data->client;
    NBDExport *exp = client->exp;

    if (exp) {
        nbd_export_get(exp);
    }
    if (nbd_negotiate(data)) {
        client_close(client);
        goto out;
    }
    qemu_co_mutex_init(&client->send_lock);

    if (exp) {
        QTAILQ_INSERT_TAIL(&exp->clients, client, next);
    }

    nbd_client_receive_next_request(client);

out:
    g_free(data);
}

void nbd_client_new(NBDExport *exp,
                    QIOChannelSocket *sioc,
                    QCryptoTLSCreds *tlscreds,
                    const char *tlsaclname,
                    void (*close_fn)(NBDClient *))
{
    NBDClient *client;
    NBDClientNewData *data = g_new(NBDClientNewData, 1);

    client = g_malloc0(sizeof(NBDClient));
    client->refcount = 1;
    client->exp = exp;
    client->tlscreds = tlscreds;
    if (tlscreds) {
        object_ref(OBJECT(client->tlscreds));
    }
    client->tlsaclname = g_strdup(tlsaclname);
    client->sioc = sioc;
    object_ref(OBJECT(client->sioc));
    client->ioc = QIO_CHANNEL(sioc);
    object_ref(OBJECT(client->ioc));
    client->close = close_fn;

    data->client = client;
    data->co = qemu_coroutine_create(nbd_co_client_start, data);
    qemu_coroutine_enter(data->co);
}
