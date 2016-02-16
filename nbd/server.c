/*
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
#include "nbd-internal.h"

static int system_errno_to_nbd_errno(int err)
{
    switch (err) {
    case 0:
        return NBD_SUCCESS;
    case EPERM:
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
    case EINVAL:
    default:
        return NBD_EINVAL;
    }
}

/* Definitions for opaque data types */

typedef struct NBDRequest NBDRequest;

struct NBDRequest {
    QSIMPLEQ_ENTRY(NBDRequest) entry;
    NBDClient *client;
    uint8_t *data;
};

struct NBDExport {
    int refcount;
    void (*close)(NBDExport *exp);

    BlockBackend *blk;
    char *name;
    off_t dev_offset;
    off_t size;
    uint32_t nbdflags;
    QTAILQ_HEAD(, NBDClient) clients;
    QTAILQ_ENTRY(NBDExport) next;

    AioContext *ctx;

    Notifier eject_notifier;
};

static QTAILQ_HEAD(, NBDExport) exports = QTAILQ_HEAD_INITIALIZER(exports);

struct NBDClient {
    int refcount;
    void (*close)(NBDClient *client);

    NBDExport *exp;
    QCryptoTLSCreds *tlscreds;
    char *tlsaclname;
    QIOChannelSocket *sioc; /* The underlying data channel */
    QIOChannel *ioc; /* The current I/O channel which may differ (eg TLS) */

    Coroutine *recv_coroutine;

    CoMutex send_lock;
    Coroutine *send_coroutine;

    bool can_read;

    QTAILQ_ENTRY(NBDClient) next;
    int nb_requests;
    bool closing;
};

/* That's all folks */

static void nbd_set_handlers(NBDClient *client);
static void nbd_unset_handlers(NBDClient *client);
static void nbd_update_can_read(NBDClient *client);

static gboolean nbd_negotiate_continue(QIOChannel *ioc,
                                       GIOCondition condition,
                                       void *opaque)
{
    qemu_coroutine_enter(opaque, NULL);
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

static ssize_t nbd_negotiate_write(QIOChannel *ioc, void *buffer, size_t size)
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

static int nbd_negotiate_send_rep(QIOChannel *ioc, uint32_t type, uint32_t opt)
{
    uint64_t magic;
    uint32_t len;

    TRACE("Reply opt=%x type=%x", type, opt);

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
    len = cpu_to_be32(0);
    if (nbd_negotiate_write(ioc, &len, sizeof(len)) != sizeof(len)) {
        LOG("write failed (rep data length)");
        return -EINVAL;
    }
    return 0;
}

static int nbd_negotiate_send_rep_list(QIOChannel *ioc, NBDExport *exp)
{
    uint64_t magic, name_len;
    uint32_t opt, type, len;

    TRACE("Advertizing export name '%s'", exp->name ? exp->name : "");
    name_len = strlen(exp->name);
    magic = cpu_to_be64(NBD_REP_MAGIC);
    if (nbd_negotiate_write(ioc, &magic, sizeof(magic)) != sizeof(magic)) {
        LOG("write failed (magic)");
        return -EINVAL;
     }
    opt = cpu_to_be32(NBD_OPT_LIST);
    if (nbd_negotiate_write(ioc, &opt, sizeof(opt)) != sizeof(opt)) {
        LOG("write failed (opt)");
        return -EINVAL;
    }
    type = cpu_to_be32(NBD_REP_SERVER);
    if (nbd_negotiate_write(ioc, &type, sizeof(type)) != sizeof(type)) {
        LOG("write failed (reply type)");
        return -EINVAL;
    }
    len = cpu_to_be32(name_len + sizeof(len));
    if (nbd_negotiate_write(ioc, &len, sizeof(len)) != sizeof(len)) {
        LOG("write failed (length)");
        return -EINVAL;
    }
    len = cpu_to_be32(name_len);
    if (nbd_negotiate_write(ioc, &len, sizeof(len)) != sizeof(len)) {
        LOG("write failed (length)");
        return -EINVAL;
    }
    if (nbd_negotiate_write(ioc, exp->name, name_len) != name_len) {
        LOG("write failed (buffer)");
        return -EINVAL;
    }
    return 0;
}

static int nbd_negotiate_handle_list(NBDClient *client, uint32_t length)
{
    NBDExport *exp;

    if (length) {
        if (nbd_negotiate_drop_sync(client->ioc, length) != length) {
            return -EIO;
        }
        return nbd_negotiate_send_rep(client->ioc,
                                      NBD_REP_ERR_INVALID, NBD_OPT_LIST);
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
    char name[256];

    /* Client sends:
        [20 ..  xx]   export name (length bytes)
     */
    TRACE("Checking length");
    if (length > 255) {
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
        nbd_negotiate_send_rep(ioc, NBD_REP_ERR_INVALID, NBD_OPT_STARTTLS);
        return NULL;
    }

    nbd_negotiate_send_rep(client->ioc, NBD_REP_ACK, NBD_OPT_STARTTLS);

    tioc = qio_channel_tls_new_server(ioc,
                                      client->tlscreds,
                                      client->tlsaclname,
                                      NULL);
    if (!tioc) {
        return NULL;
    }

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
        TRACE("Support supports fixed newstyle handshake");
        fixedNewstyle = true;
        flags &= ~NBD_FLAG_C_FIXED_NEWSTYLE;
    }
    if (flags != 0) {
        TRACE("Unknown client flags 0x%x received", flags);
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

        TRACE("Checking option 0x%x", clientflags);
        if (client->tlscreds &&
            client->ioc == (QIOChannel *)client->sioc) {
            QIOChannel *tioc;
            if (!fixedNewstyle) {
                TRACE("Unsupported option 0x%x", clientflags);
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

            default:
                TRACE("Option 0x%x not permitted before TLS", clientflags);
                nbd_negotiate_send_rep(client->ioc, NBD_REP_ERR_TLS_REQD,
                                       clientflags);
                return -EINVAL;
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
                return -EINVAL;

            case NBD_OPT_EXPORT_NAME:
                return nbd_negotiate_handle_export_name(client, length);

            case NBD_OPT_STARTTLS:
                if (client->tlscreds) {
                    TRACE("TLS already enabled");
                    nbd_negotiate_send_rep(client->ioc, NBD_REP_ERR_INVALID,
                                           clientflags);
                } else {
                    TRACE("TLS not configured");
                    nbd_negotiate_send_rep(client->ioc, NBD_REP_ERR_POLICY,
                                           clientflags);
                }
                return -EINVAL;
            default:
                TRACE("Unsupported option 0x%x", clientflags);
                nbd_negotiate_send_rep(client->ioc, NBD_REP_ERR_UNSUP,
                                       clientflags);
                return -EINVAL;
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
                TRACE("Unsupported option 0x%x", clientflags);
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
    const int myflags = (NBD_FLAG_HAS_FLAGS | NBD_FLAG_SEND_TRIM |
                         NBD_FLAG_SEND_FLUSH | NBD_FLAG_SEND_FUA);
    bool oldStyle;

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
        [28 .. 151]   reserved     (0)
     */

    qio_channel_set_blocking(client->ioc, false, NULL);
    rc = -EINVAL;

    TRACE("Beginning negotiation.");
    memset(buf, 0, sizeof(buf));
    memcpy(buf, "NBDMAGIC", 8);

    oldStyle = client->exp != NULL && !client->tlscreds;
    if (oldStyle) {
        assert ((client->exp->nbdflags & ~65535) == 0);
        stq_be_p(buf + 8, NBD_CLIENT_MAGIC);
        stq_be_p(buf + 16, client->exp->size);
        stw_be_p(buf + 26, client->exp->nbdflags | myflags);
    } else {
        stq_be_p(buf + 8, NBD_OPTS_MAGIC);
        stw_be_p(buf + 16, NBD_FLAG_FIXED_NEWSTYLE);
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

        assert ((client->exp->nbdflags & ~65535) == 0);
        stq_be_p(buf + 18, client->exp->size);
        stw_be_p(buf + 26, client->exp->nbdflags | myflags);
        if (nbd_negotiate_write(client->ioc, buf + 18, sizeof(buf) - 18) !=
            sizeof(buf) - 18) {
            LOG("write failed");
            goto fail;
        }
    }

    TRACE("Negotiation succeeded.");
    rc = 0;
fail:
    return rc;
}

#ifdef __linux__

int nbd_disconnect(int fd)
{
    ioctl(fd, NBD_CLEAR_QUE);
    ioctl(fd, NBD_DISCONNECT);
    ioctl(fd, NBD_CLEAR_SOCK);
    return 0;
}

#else

int nbd_disconnect(int fd)
{
    return -ENOTSUP;
}
#endif

static ssize_t nbd_receive_request(QIOChannel *ioc, struct nbd_request *request)
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
       [ 4 ..  7]   type    (0 == READ, 1 == WRITE)
       [ 8 .. 15]   handle
       [16 .. 23]   from
       [24 .. 27]   len
     */

    magic = be32_to_cpup((uint32_t*)buf);
    request->type  = be32_to_cpup((uint32_t*)(buf + 4));
    request->handle = be64_to_cpup((uint64_t*)(buf + 8));
    request->from  = be64_to_cpup((uint64_t*)(buf + 16));
    request->len   = be32_to_cpup((uint32_t*)(buf + 24));

    TRACE("Got request: "
          "{ magic = 0x%x, .type = %d, from = %" PRIu64" , len = %u }",
          magic, request->type, request->from, request->len);

    if (magic != NBD_REQUEST_MAGIC) {
        LOG("invalid magic (got 0x%x)", magic);
        return -EINVAL;
    }
    return 0;
}

static ssize_t nbd_send_reply(QIOChannel *ioc, struct nbd_reply *reply)
{
    uint8_t buf[NBD_REPLY_SIZE];
    ssize_t ret;

    reply->error = system_errno_to_nbd_errno(reply->error);

    /* Reply
       [ 0 ..  3]    magic   (NBD_REPLY_MAGIC)
       [ 4 ..  7]    error   (0 == no error)
       [ 7 .. 15]    handle
     */
    stl_be_p(buf, NBD_REPLY_MAGIC);
    stl_be_p(buf + 4, reply->error);
    stq_be_p(buf + 8, reply->handle);

    TRACE("Sending response to client");

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

        nbd_unset_handlers(client);
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

static NBDRequest *nbd_request_get(NBDClient *client)
{
    NBDRequest *req;

    assert(client->nb_requests <= MAX_NBD_REQUESTS - 1);
    client->nb_requests++;
    nbd_update_can_read(client);

    req = g_new0(NBDRequest, 1);
    nbd_client_get(client);
    req->client = client;
    return req;
}

static void nbd_request_put(NBDRequest *req)
{
    NBDClient *client = req->client;

    if (req->data) {
        qemu_vfree(req->data);
    }
    g_free(req);

    client->nb_requests--;
    nbd_update_can_read(client);
    nbd_client_put(client);
}

static void blk_aio_attached(AioContext *ctx, void *opaque)
{
    NBDExport *exp = opaque;
    NBDClient *client;

    TRACE("Export %s: Attaching clients to AIO context %p\n", exp->name, ctx);

    exp->ctx = ctx;

    QTAILQ_FOREACH(client, &exp->clients, next) {
        nbd_set_handlers(client);
    }
}

static void blk_aio_detach(void *opaque)
{
    NBDExport *exp = opaque;
    NBDClient *client;

    TRACE("Export %s: Detaching clients from AIO context %p\n", exp->name, exp->ctx);

    QTAILQ_FOREACH(client, &exp->clients, next) {
        nbd_unset_handlers(client);
    }

    exp->ctx = NULL;
}

static void nbd_eject_notifier(Notifier *n, void *data)
{
    NBDExport *exp = container_of(n, NBDExport, eject_notifier);
    nbd_export_close(exp);
}

NBDExport *nbd_export_new(BlockBackend *blk, off_t dev_offset, off_t size,
                          uint32_t nbdflags, void (*close)(NBDExport *),
                          Error **errp)
{
    NBDExport *exp = g_malloc0(sizeof(NBDExport));
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
    blk_ref(blk);
    blk_add_aio_context_notifier(blk, blk_aio_attached, blk_aio_detach, exp);

    exp->eject_notifier.notify = nbd_eject_notifier;
    blk_add_remove_bs_notifier(blk, &exp->eject_notifier);

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

void nbd_export_close(NBDExport *exp)
{
    NBDClient *client, *next;

    nbd_export_get(exp);
    QTAILQ_FOREACH_SAFE(client, &exp->clients, next, next) {
        client_close(client);
    }
    nbd_export_set_name(exp, NULL);
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

        if (exp->close) {
            exp->close(exp);
        }

        if (exp->blk) {
            notifier_remove(&exp->eject_notifier);
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

static ssize_t nbd_co_send_reply(NBDRequest *req, struct nbd_reply *reply,
                                 int len)
{
    NBDClient *client = req->client;
    ssize_t rc, ret;

    g_assert(qemu_in_coroutine());
    qemu_co_mutex_lock(&client->send_lock);
    client->send_coroutine = qemu_coroutine_self();
    nbd_set_handlers(client);

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
    nbd_set_handlers(client);
    qemu_co_mutex_unlock(&client->send_lock);
    return rc;
}

static ssize_t nbd_co_receive_request(NBDRequest *req, struct nbd_request *request)
{
    NBDClient *client = req->client;
    uint32_t command;
    ssize_t rc;

    g_assert(qemu_in_coroutine());
    client->recv_coroutine = qemu_coroutine_self();
    nbd_update_can_read(client);

    rc = nbd_receive_request(client->ioc, request);
    if (rc < 0) {
        if (rc != -EAGAIN) {
            rc = -EIO;
        }
        goto out;
    }

    if ((request->from + request->len) < request->from) {
        LOG("integer overflow detected! "
            "you're probably being attacked");
        rc = -EINVAL;
        goto out;
    }

    TRACE("Decoding type");

    command = request->type & NBD_CMD_MASK_COMMAND;
    if (command == NBD_CMD_READ || command == NBD_CMD_WRITE) {
        if (request->len > NBD_MAX_BUFFER_SIZE) {
            LOG("len (%u) is larger than max len (%u)",
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
    if (command == NBD_CMD_WRITE) {
        TRACE("Reading %u byte(s)", request->len);

        if (read_sync(client->ioc, req->data, request->len) != request->len) {
            LOG("reading from socket failed");
            rc = -EIO;
            goto out;
        }
    }
    rc = 0;

out:
    client->recv_coroutine = NULL;
    nbd_update_can_read(client);

    return rc;
}

static void nbd_trip(void *opaque)
{
    NBDClient *client = opaque;
    NBDExport *exp = client->exp;
    NBDRequest *req;
    struct nbd_request request;
    struct nbd_reply reply;
    ssize_t ret;
    uint32_t command;

    TRACE("Reading request.");
    if (client->closing) {
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
    command = request.type & NBD_CMD_MASK_COMMAND;
    if (command != NBD_CMD_DISC && (request.from + request.len) > exp->size) {
            LOG("From: %" PRIu64 ", Len: %u, Size: %" PRIu64
            ", Offset: %" PRIu64 "\n",
                    request.from, request.len,
                    (uint64_t)exp->size, (uint64_t)exp->dev_offset);
        LOG("requested operation past EOF--bad client?");
        goto invalid_request;
    }

    if (client->closing) {
        /*
         * The client may be closed when we are blocked in
         * nbd_co_receive_request()
         */
        goto done;
    }

    switch (command) {
    case NBD_CMD_READ:
        TRACE("Request type is READ");

        if (request.type & NBD_CMD_FLAG_FUA) {
            ret = blk_co_flush(exp->blk);
            if (ret < 0) {
                LOG("flush failed");
                reply.error = -ret;
                goto error_reply;
            }
        }

        ret = blk_read(exp->blk,
                       (request.from + exp->dev_offset) / BDRV_SECTOR_SIZE,
                       req->data, request.len / BDRV_SECTOR_SIZE);
        if (ret < 0) {
            LOG("reading from file failed");
            reply.error = -ret;
            goto error_reply;
        }

        TRACE("Read %u byte(s)", request.len);
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

        ret = blk_write(exp->blk,
                        (request.from + exp->dev_offset) / BDRV_SECTOR_SIZE,
                        req->data, request.len / BDRV_SECTOR_SIZE);
        if (ret < 0) {
            LOG("writing to file failed");
            reply.error = -ret;
            goto error_reply;
        }

        if (request.type & NBD_CMD_FLAG_FUA) {
            ret = blk_co_flush(exp->blk);
            if (ret < 0) {
                LOG("flush failed");
                reply.error = -ret;
                goto error_reply;
            }
        }

        if (nbd_co_send_reply(req, &reply, 0) < 0) {
            goto out;
        }
        break;
    case NBD_CMD_DISC:
        TRACE("Request type is DISCONNECT");
        errno = 0;
        goto out;
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
        ret = blk_co_discard(exp->blk, (request.from + exp->dev_offset)
                                       / BDRV_SECTOR_SIZE,
                             request.len / BDRV_SECTOR_SIZE);
        if (ret < 0) {
            LOG("discard failed");
            reply.error = -ret;
        }
        if (nbd_co_send_reply(req, &reply, 0) < 0) {
            goto out;
        }
        break;
    default:
        LOG("invalid request type (%u) received", request.type);
    invalid_request:
        reply.error = EINVAL;
    error_reply:
        if (nbd_co_send_reply(req, &reply, 0) < 0) {
            goto out;
        }
        break;
    }

    TRACE("Request/Reply complete");

done:
    nbd_request_put(req);
    return;

out:
    nbd_request_put(req);
    client_close(client);
}

static void nbd_read(void *opaque)
{
    NBDClient *client = opaque;

    if (client->recv_coroutine) {
        qemu_coroutine_enter(client->recv_coroutine, NULL);
    } else {
        qemu_coroutine_enter(qemu_coroutine_create(nbd_trip), client);
    }
}

static void nbd_restart_write(void *opaque)
{
    NBDClient *client = opaque;

    qemu_coroutine_enter(client->send_coroutine, NULL);
}

static void nbd_set_handlers(NBDClient *client)
{
    if (client->exp && client->exp->ctx) {
        aio_set_fd_handler(client->exp->ctx, client->sioc->fd,
                           true,
                           client->can_read ? nbd_read : NULL,
                           client->send_coroutine ? nbd_restart_write : NULL,
                           client);
    }
}

static void nbd_unset_handlers(NBDClient *client)
{
    if (client->exp && client->exp->ctx) {
        aio_set_fd_handler(client->exp->ctx, client->sioc->fd,
                           true, NULL, NULL, NULL);
    }
}

static void nbd_update_can_read(NBDClient *client)
{
    bool can_read = client->recv_coroutine ||
                    client->nb_requests < MAX_NBD_REQUESTS;

    if (can_read != client->can_read) {
        client->can_read = can_read;
        nbd_set_handlers(client);

        /* There is no need to invoke aio_notify(), since aio_set_fd_handler()
         * in nbd_set_handlers() will have taken care of that */
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
    nbd_set_handlers(client);

    if (exp) {
        QTAILQ_INSERT_TAIL(&exp->clients, client, next);
    }
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
    client->can_read = true;
    client->close = close_fn;

    data->client = client;
    data->co = qemu_coroutine_create(nbd_co_client_start);
    qemu_coroutine_enter(data->co, data);
}
