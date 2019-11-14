/*
 *  Copyright (C) 2016-2018 Red Hat, Inc.
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
#include "qemu/queue.h"
#include "trace.h"
#include "nbd-internal.h"
#include "qemu/units.h"

#define NBD_META_ID_BASE_ALLOCATION 0
#define NBD_META_ID_DIRTY_BITMAP 1

/*
 * NBD_MAX_BLOCK_STATUS_EXTENTS: 1 MiB of extents data. An empirical
 * constant. If an increase is needed, note that the NBD protocol
 * recommends no larger than 32 mb, so that the client won't consider
 * the reply as a denial of service attack.
 */
#define NBD_MAX_BLOCK_STATUS_EXTENTS (1 * MiB / 8)

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
    case EOVERFLOW:
        return NBD_EOVERFLOW;
    case ENOTSUP:
#if ENOTSUP != EOPNOTSUPP
    case EOPNOTSUPP:
#endif
        return NBD_ENOTSUP;
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
    uint64_t dev_offset;
    uint64_t size;
    uint16_t nbdflags;
    QTAILQ_HEAD(, NBDClient) clients;
    QTAILQ_ENTRY(NBDExport) next;

    AioContext *ctx;

    BlockBackend *eject_notifier_blk;
    Notifier eject_notifier;

    BdrvDirtyBitmap *export_bitmap;
    char *export_bitmap_context;
};

static QTAILQ_HEAD(, NBDExport) exports = QTAILQ_HEAD_INITIALIZER(exports);

/* NBDExportMetaContexts represents a list of contexts to be exported,
 * as selected by NBD_OPT_SET_META_CONTEXT. Also used for
 * NBD_OPT_LIST_META_CONTEXT. */
typedef struct NBDExportMetaContexts {
    NBDExport *exp;
    bool valid; /* means that negotiation of the option finished without
                   errors */
    bool base_allocation; /* export base:allocation context (block status) */
    bool bitmap; /* export qemu:dirty-bitmap:<export bitmap name> */
} NBDExportMetaContexts;

struct NBDClient {
    int refcount;
    void (*close_fn)(NBDClient *client, bool negotiated);

    NBDExport *exp;
    QCryptoTLSCreds *tlscreds;
    char *tlsauthz;
    QIOChannelSocket *sioc; /* The underlying data channel */
    QIOChannel *ioc; /* The current I/O channel which may differ (eg TLS) */

    Coroutine *recv_coroutine;

    CoMutex send_lock;
    Coroutine *send_coroutine;

    QTAILQ_ENTRY(NBDClient) next;
    int nb_requests;
    bool closing;

    uint32_t check_align; /* If non-zero, check for aligned client requests */

    bool structured_reply;
    NBDExportMetaContexts export_meta;

    uint32_t opt; /* Current option being negotiated */
    uint32_t optlen; /* remaining length of data in ioc for the option being
                        negotiated now */
};

static void nbd_client_receive_next_request(NBDClient *client);

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

static inline void set_be_option_rep(NBDOptionReply *rep, uint32_t option,
                                     uint32_t type, uint32_t length)
{
    stq_be_p(&rep->magic, NBD_REP_MAGIC);
    stl_be_p(&rep->option, option);
    stl_be_p(&rep->type, type);
    stl_be_p(&rep->length, length);
}

/* Send a reply header, including length, but no payload.
 * Return -errno on error, 0 on success. */
static int nbd_negotiate_send_rep_len(NBDClient *client, uint32_t type,
                                      uint32_t len, Error **errp)
{
    NBDOptionReply rep;

    trace_nbd_negotiate_send_rep_len(client->opt, nbd_opt_lookup(client->opt),
                                     type, nbd_rep_lookup(type), len);

    assert(len < NBD_MAX_BUFFER_SIZE);

    set_be_option_rep(&rep, client->opt, type, len);
    return nbd_write(client->ioc, &rep, sizeof(rep), errp);
}

/* Send a reply header with default 0 length.
 * Return -errno on error, 0 on success. */
static int nbd_negotiate_send_rep(NBDClient *client, uint32_t type,
                                  Error **errp)
{
    return nbd_negotiate_send_rep_len(client, type, 0, errp);
}

/* Send an error reply.
 * Return -errno on error, 0 on success. */
static int GCC_FMT_ATTR(4, 0)
nbd_negotiate_send_rep_verr(NBDClient *client, uint32_t type,
                            Error **errp, const char *fmt, va_list va)
{
    g_autofree char *msg = NULL;
    int ret;
    size_t len;

    msg = g_strdup_vprintf(fmt, va);
    len = strlen(msg);
    assert(len < 4096);
    trace_nbd_negotiate_send_rep_err(msg);
    ret = nbd_negotiate_send_rep_len(client, type, len, errp);
    if (ret < 0) {
        return ret;
    }
    if (nbd_write(client->ioc, msg, len, errp) < 0) {
        error_prepend(errp, "write failed (error message): ");
        return -EIO;
    }

    return 0;
}

/* Send an error reply.
 * Return -errno on error, 0 on success. */
static int GCC_FMT_ATTR(4, 5)
nbd_negotiate_send_rep_err(NBDClient *client, uint32_t type,
                           Error **errp, const char *fmt, ...)
{
    va_list va;
    int ret;

    va_start(va, fmt);
    ret = nbd_negotiate_send_rep_verr(client, type, errp, fmt, va);
    va_end(va);
    return ret;
}

/* Drop remainder of the current option, and send a reply with the
 * given error type and message. Return -errno on read or write
 * failure; or 0 if connection is still live. */
static int GCC_FMT_ATTR(4, 0)
nbd_opt_vdrop(NBDClient *client, uint32_t type, Error **errp,
              const char *fmt, va_list va)
{
    int ret = nbd_drop(client->ioc, client->optlen, errp);

    client->optlen = 0;
    if (!ret) {
        ret = nbd_negotiate_send_rep_verr(client, type, errp, fmt, va);
    }
    return ret;
}

static int GCC_FMT_ATTR(4, 5)
nbd_opt_drop(NBDClient *client, uint32_t type, Error **errp,
             const char *fmt, ...)
{
    int ret;
    va_list va;

    va_start(va, fmt);
    ret = nbd_opt_vdrop(client, type, errp, fmt, va);
    va_end(va);

    return ret;
}

static int GCC_FMT_ATTR(3, 4)
nbd_opt_invalid(NBDClient *client, Error **errp, const char *fmt, ...)
{
    int ret;
    va_list va;

    va_start(va, fmt);
    ret = nbd_opt_vdrop(client, NBD_REP_ERR_INVALID, errp, fmt, va);
    va_end(va);

    return ret;
}

/* Read size bytes from the unparsed payload of the current option.
 * Return -errno on I/O error, 0 if option was completely handled by
 * sending a reply about inconsistent lengths, or 1 on success. */
static int nbd_opt_read(NBDClient *client, void *buffer, size_t size,
                        Error **errp)
{
    if (size > client->optlen) {
        return nbd_opt_invalid(client, errp,
                               "Inconsistent lengths in option %s",
                               nbd_opt_lookup(client->opt));
    }
    client->optlen -= size;
    return qio_channel_read_all(client->ioc, buffer, size, errp) < 0 ? -EIO : 1;
}

/* Drop size bytes from the unparsed payload of the current option.
 * Return -errno on I/O error, 0 if option was completely handled by
 * sending a reply about inconsistent lengths, or 1 on success. */
static int nbd_opt_skip(NBDClient *client, size_t size, Error **errp)
{
    if (size > client->optlen) {
        return nbd_opt_invalid(client, errp,
                               "Inconsistent lengths in option %s",
                               nbd_opt_lookup(client->opt));
    }
    client->optlen -= size;
    return nbd_drop(client->ioc, size, errp) < 0 ? -EIO : 1;
}

/* nbd_opt_read_name
 *
 * Read a string with the format:
 *   uint32_t len     (<= NBD_MAX_STRING_SIZE)
 *   len bytes string (not 0-terminated)
 *
 * On success, @name will be allocated.
 * If @length is non-null, it will be set to the actual string length.
 *
 * Return -errno on I/O error, 0 if option was completely handled by
 * sending a reply about inconsistent lengths, or 1 on success.
 */
static int nbd_opt_read_name(NBDClient *client, char **name, uint32_t *length,
                             Error **errp)
{
    int ret;
    uint32_t len;
    g_autofree char *local_name = NULL;

    *name = NULL;
    ret = nbd_opt_read(client, &len, sizeof(len), errp);
    if (ret <= 0) {
        return ret;
    }
    len = cpu_to_be32(len);

    if (len > NBD_MAX_STRING_SIZE) {
        return nbd_opt_invalid(client, errp,
                               "Invalid name length: %" PRIu32, len);
    }

    local_name = g_malloc(len + 1);
    ret = nbd_opt_read(client, local_name, len, errp);
    if (ret <= 0) {
        return ret;
    }
    local_name[len] = '\0';

    if (length) {
        *length = len;
    }
    *name = g_steal_pointer(&local_name);

    return 1;
}

/* Send a single NBD_REP_SERVER reply to NBD_OPT_LIST, including payload.
 * Return -errno on error, 0 on success. */
static int nbd_negotiate_send_rep_list(NBDClient *client, NBDExport *exp,
                                       Error **errp)
{
    size_t name_len, desc_len;
    uint32_t len;
    const char *name = exp->name ? exp->name : "";
    const char *desc = exp->description ? exp->description : "";
    QIOChannel *ioc = client->ioc;
    int ret;

    trace_nbd_negotiate_send_rep_list(name, desc);
    name_len = strlen(name);
    desc_len = strlen(desc);
    assert(name_len <= NBD_MAX_STRING_SIZE && desc_len <= NBD_MAX_STRING_SIZE);
    len = name_len + desc_len + sizeof(len);
    ret = nbd_negotiate_send_rep_len(client, NBD_REP_SERVER, len, errp);
    if (ret < 0) {
        return ret;
    }

    len = cpu_to_be32(name_len);
    if (nbd_write(ioc, &len, sizeof(len), errp) < 0) {
        error_prepend(errp, "write failed (name length): ");
        return -EINVAL;
    }

    if (nbd_write(ioc, name, name_len, errp) < 0) {
        error_prepend(errp, "write failed (name buffer): ");
        return -EINVAL;
    }

    if (nbd_write(ioc, desc, desc_len, errp) < 0) {
        error_prepend(errp, "write failed (description buffer): ");
        return -EINVAL;
    }

    return 0;
}

/* Process the NBD_OPT_LIST command, with a potential series of replies.
 * Return -errno on error, 0 on success. */
static int nbd_negotiate_handle_list(NBDClient *client, Error **errp)
{
    NBDExport *exp;
    assert(client->opt == NBD_OPT_LIST);

    /* For each export, send a NBD_REP_SERVER reply. */
    QTAILQ_FOREACH(exp, &exports, next) {
        if (nbd_negotiate_send_rep_list(client, exp, errp)) {
            return -EINVAL;
        }
    }
    /* Finish with a NBD_REP_ACK. */
    return nbd_negotiate_send_rep(client, NBD_REP_ACK, errp);
}

static void nbd_check_meta_export(NBDClient *client)
{
    client->export_meta.valid &= client->exp == client->export_meta.exp;
}

/* Send a reply to NBD_OPT_EXPORT_NAME.
 * Return -errno on error, 0 on success. */
static int nbd_negotiate_handle_export_name(NBDClient *client, bool no_zeroes,
                                            Error **errp)
{
    g_autofree char *name = NULL;
    char buf[NBD_REPLY_EXPORT_NAME_SIZE] = "";
    size_t len;
    int ret;
    uint16_t myflags;

    /* Client sends:
        [20 ..  xx]   export name (length bytes)
       Server replies:
        [ 0 ..   7]   size
        [ 8 ..   9]   export flags
        [10 .. 133]   reserved     (0) [unless no_zeroes]
     */
    trace_nbd_negotiate_handle_export_name();
    if (client->optlen > NBD_MAX_STRING_SIZE) {
        error_setg(errp, "Bad length received");
        return -EINVAL;
    }
    name = g_malloc(client->optlen + 1);
    if (nbd_read(client->ioc, name, client->optlen, "export name", errp) < 0) {
        return -EIO;
    }
    name[client->optlen] = '\0';
    client->optlen = 0;

    trace_nbd_negotiate_handle_export_name_request(name);

    client->exp = nbd_export_find(name);
    if (!client->exp) {
        error_setg(errp, "export not found");
        return -EINVAL;
    }

    myflags = client->exp->nbdflags;
    if (client->structured_reply) {
        myflags |= NBD_FLAG_SEND_DF;
    }
    trace_nbd_negotiate_new_style_size_flags(client->exp->size, myflags);
    stq_be_p(buf, client->exp->size);
    stw_be_p(buf + 8, myflags);
    len = no_zeroes ? 10 : sizeof(buf);
    ret = nbd_write(client->ioc, buf, len, errp);
    if (ret < 0) {
        error_prepend(errp, "write failed: ");
        return ret;
    }

    QTAILQ_INSERT_TAIL(&client->exp->clients, client, next);
    nbd_export_get(client->exp);
    nbd_check_meta_export(client);

    return 0;
}

/* Send a single NBD_REP_INFO, with a buffer @buf of @length bytes.
 * The buffer does NOT include the info type prefix.
 * Return -errno on error, 0 if ready to send more. */
static int nbd_negotiate_send_info(NBDClient *client,
                                   uint16_t info, uint32_t length, void *buf,
                                   Error **errp)
{
    int rc;

    trace_nbd_negotiate_send_info(info, nbd_info_lookup(info), length);
    rc = nbd_negotiate_send_rep_len(client, NBD_REP_INFO,
                                    sizeof(info) + length, errp);
    if (rc < 0) {
        return rc;
    }
    info = cpu_to_be16(info);
    if (nbd_write(client->ioc, &info, sizeof(info), errp) < 0) {
        return -EIO;
    }
    if (nbd_write(client->ioc, buf, length, errp) < 0) {
        return -EIO;
    }
    return 0;
}

/* nbd_reject_length: Handle any unexpected payload.
 * @fatal requests that we quit talking to the client, even if we are able
 * to successfully send an error reply.
 * Return:
 * -errno  transmission error occurred or @fatal was requested, errp is set
 * 0       error message successfully sent to client, errp is not set
 */
static int nbd_reject_length(NBDClient *client, bool fatal, Error **errp)
{
    int ret;

    assert(client->optlen);
    ret = nbd_opt_invalid(client, errp, "option '%s' has unexpected length",
                          nbd_opt_lookup(client->opt));
    if (fatal && !ret) {
        error_setg(errp, "option '%s' has unexpected length",
                   nbd_opt_lookup(client->opt));
        return -EINVAL;
    }
    return ret;
}

/* Handle NBD_OPT_INFO and NBD_OPT_GO.
 * Return -errno on error, 0 if ready for next option, and 1 to move
 * into transmission phase.  */
static int nbd_negotiate_handle_info(NBDClient *client, Error **errp)
{
    int rc;
    g_autofree char *name = NULL;
    NBDExport *exp;
    uint16_t requests;
    uint16_t request;
    uint32_t namelen;
    bool sendname = false;
    bool blocksize = false;
    uint32_t sizes[3];
    char buf[sizeof(uint64_t) + sizeof(uint16_t)];
    uint32_t check_align = 0;
    uint16_t myflags;

    /* Client sends:
        4 bytes: L, name length (can be 0)
        L bytes: export name
        2 bytes: N, number of requests (can be 0)
        N * 2 bytes: N requests
    */
    rc = nbd_opt_read_name(client, &name, &namelen, errp);
    if (rc <= 0) {
        return rc;
    }
    trace_nbd_negotiate_handle_export_name_request(name);

    rc = nbd_opt_read(client, &requests, sizeof(requests), errp);
    if (rc <= 0) {
        return rc;
    }
    requests = be16_to_cpu(requests);
    trace_nbd_negotiate_handle_info_requests(requests);
    while (requests--) {
        rc = nbd_opt_read(client, &request, sizeof(request), errp);
        if (rc <= 0) {
            return rc;
        }
        request = be16_to_cpu(request);
        trace_nbd_negotiate_handle_info_request(request,
                                                nbd_info_lookup(request));
        /* We care about NBD_INFO_NAME and NBD_INFO_BLOCK_SIZE;
         * everything else is either a request we don't know or
         * something we send regardless of request */
        switch (request) {
        case NBD_INFO_NAME:
            sendname = true;
            break;
        case NBD_INFO_BLOCK_SIZE:
            blocksize = true;
            break;
        }
    }
    if (client->optlen) {
        return nbd_reject_length(client, false, errp);
    }

    exp = nbd_export_find(name);
    if (!exp) {
        return nbd_negotiate_send_rep_err(client, NBD_REP_ERR_UNKNOWN,
                                          errp, "export '%s' not present",
                                          name);
    }

    /* Don't bother sending NBD_INFO_NAME unless client requested it */
    if (sendname) {
        rc = nbd_negotiate_send_info(client, NBD_INFO_NAME, namelen, name,
                                     errp);
        if (rc < 0) {
            return rc;
        }
    }

    /* Send NBD_INFO_DESCRIPTION only if available, regardless of
     * client request */
    if (exp->description) {
        size_t len = strlen(exp->description);

        assert(len <= NBD_MAX_STRING_SIZE);
        rc = nbd_negotiate_send_info(client, NBD_INFO_DESCRIPTION,
                                     len, exp->description, errp);
        if (rc < 0) {
            return rc;
        }
    }

    /* Send NBD_INFO_BLOCK_SIZE always, but tweak the minimum size
     * according to whether the client requested it, and according to
     * whether this is OPT_INFO or OPT_GO. */
    /* minimum - 1 for back-compat, or actual if client will obey it. */
    if (client->opt == NBD_OPT_INFO || blocksize) {
        check_align = sizes[0] = blk_get_request_alignment(exp->blk);
    } else {
        sizes[0] = 1;
    }
    assert(sizes[0] <= NBD_MAX_BUFFER_SIZE);
    /* preferred - Hard-code to 4096 for now.
     * TODO: is blk_bs(blk)->bl.opt_transfer appropriate? */
    sizes[1] = MAX(4096, sizes[0]);
    /* maximum - At most 32M, but smaller as appropriate. */
    sizes[2] = MIN(blk_get_max_transfer(exp->blk), NBD_MAX_BUFFER_SIZE);
    trace_nbd_negotiate_handle_info_block_size(sizes[0], sizes[1], sizes[2]);
    sizes[0] = cpu_to_be32(sizes[0]);
    sizes[1] = cpu_to_be32(sizes[1]);
    sizes[2] = cpu_to_be32(sizes[2]);
    rc = nbd_negotiate_send_info(client, NBD_INFO_BLOCK_SIZE,
                                 sizeof(sizes), sizes, errp);
    if (rc < 0) {
        return rc;
    }

    /* Send NBD_INFO_EXPORT always */
    myflags = exp->nbdflags;
    if (client->structured_reply) {
        myflags |= NBD_FLAG_SEND_DF;
    }
    trace_nbd_negotiate_new_style_size_flags(exp->size, myflags);
    stq_be_p(buf, exp->size);
    stw_be_p(buf + 8, myflags);
    rc = nbd_negotiate_send_info(client, NBD_INFO_EXPORT,
                                 sizeof(buf), buf, errp);
    if (rc < 0) {
        return rc;
    }

    /*
     * If the client is just asking for NBD_OPT_INFO, but forgot to
     * request block sizes in a situation that would impact
     * performance, then return an error. But for NBD_OPT_GO, we
     * tolerate all clients, regardless of alignments.
     */
    if (client->opt == NBD_OPT_INFO && !blocksize &&
        blk_get_request_alignment(exp->blk) > 1) {
        return nbd_negotiate_send_rep_err(client,
                                          NBD_REP_ERR_BLOCK_SIZE_REQD,
                                          errp,
                                          "request NBD_INFO_BLOCK_SIZE to "
                                          "use this export");
    }

    /* Final reply */
    rc = nbd_negotiate_send_rep(client, NBD_REP_ACK, errp);
    if (rc < 0) {
        return rc;
    }

    if (client->opt == NBD_OPT_GO) {
        client->exp = exp;
        client->check_align = check_align;
        QTAILQ_INSERT_TAIL(&client->exp->clients, client, next);
        nbd_export_get(client->exp);
        nbd_check_meta_export(client);
        rc = 1;
    }
    return rc;
}


/* Handle NBD_OPT_STARTTLS. Return NULL to drop connection, or else the
 * new channel for all further (now-encrypted) communication. */
static QIOChannel *nbd_negotiate_handle_starttls(NBDClient *client,
                                                 Error **errp)
{
    QIOChannel *ioc;
    QIOChannelTLS *tioc;
    struct NBDTLSHandshakeData data = { 0 };

    assert(client->opt == NBD_OPT_STARTTLS);

    trace_nbd_negotiate_handle_starttls();
    ioc = client->ioc;

    if (nbd_negotiate_send_rep(client, NBD_REP_ACK, errp) < 0) {
        return NULL;
    }

    tioc = qio_channel_tls_new_server(ioc,
                                      client->tlscreds,
                                      client->tlsauthz,
                                      errp);
    if (!tioc) {
        return NULL;
    }

    qio_channel_set_name(QIO_CHANNEL(tioc), "nbd-server-tls");
    trace_nbd_negotiate_handle_starttls_handshake();
    data.loop = g_main_loop_new(g_main_context_default(), FALSE);
    qio_channel_tls_handshake(tioc,
                              nbd_tls_handshake,
                              &data,
                              NULL,
                              NULL);

    if (!data.complete) {
        g_main_loop_run(data.loop);
    }
    g_main_loop_unref(data.loop);
    if (data.error) {
        object_unref(OBJECT(tioc));
        error_propagate(errp, data.error);
        return NULL;
    }

    return QIO_CHANNEL(tioc);
}

/* nbd_negotiate_send_meta_context
 *
 * Send one chunk of reply to NBD_OPT_{LIST,SET}_META_CONTEXT
 *
 * For NBD_OPT_LIST_META_CONTEXT @context_id is ignored, 0 is used instead.
 */
static int nbd_negotiate_send_meta_context(NBDClient *client,
                                           const char *context,
                                           uint32_t context_id,
                                           Error **errp)
{
    NBDOptionReplyMetaContext opt;
    struct iovec iov[] = {
        {.iov_base = &opt, .iov_len = sizeof(opt)},
        {.iov_base = (void *)context, .iov_len = strlen(context)}
    };

    assert(iov[1].iov_len <= NBD_MAX_STRING_SIZE);
    if (client->opt == NBD_OPT_LIST_META_CONTEXT) {
        context_id = 0;
    }

    trace_nbd_negotiate_meta_query_reply(context, context_id);
    set_be_option_rep(&opt.h, client->opt, NBD_REP_META_CONTEXT,
                      sizeof(opt) - sizeof(opt.h) + iov[1].iov_len);
    stl_be_p(&opt.context_id, context_id);

    return qio_channel_writev_all(client->ioc, iov, 2, errp) < 0 ? -EIO : 0;
}

/* Read strlen(@pattern) bytes, and set @match to true if they match @pattern.
 * @match is never set to false.
 *
 * Return -errno on I/O error, 0 if option was completely handled by
 * sending a reply about inconsistent lengths, or 1 on success.
 *
 * Note: return code = 1 doesn't mean that we've read exactly @pattern.
 * It only means that there are no errors.
 */
static int nbd_meta_pattern(NBDClient *client, const char *pattern, bool *match,
                            Error **errp)
{
    int ret;
    char *query;
    size_t len = strlen(pattern);

    assert(len);

    query = g_malloc(len);
    ret = nbd_opt_read(client, query, len, errp);
    if (ret <= 0) {
        g_free(query);
        return ret;
    }

    if (strncmp(query, pattern, len) == 0) {
        trace_nbd_negotiate_meta_query_parse(pattern);
        *match = true;
    } else {
        trace_nbd_negotiate_meta_query_skip("pattern not matched");
    }
    g_free(query);

    return 1;
}

/*
 * Read @len bytes, and set @match to true if they match @pattern, or if @len
 * is 0 and the client is performing _LIST_. @match is never set to false.
 *
 * Return -errno on I/O error, 0 if option was completely handled by
 * sending a reply about inconsistent lengths, or 1 on success.
 *
 * Note: return code = 1 doesn't mean that we've read exactly @pattern.
 * It only means that there are no errors.
 */
static int nbd_meta_empty_or_pattern(NBDClient *client, const char *pattern,
                                     uint32_t len, bool *match, Error **errp)
{
    if (len == 0) {
        if (client->opt == NBD_OPT_LIST_META_CONTEXT) {
            *match = true;
        }
        trace_nbd_negotiate_meta_query_parse("empty");
        return 1;
    }

    if (len != strlen(pattern)) {
        trace_nbd_negotiate_meta_query_skip("different lengths");
        return nbd_opt_skip(client, len, errp);
    }

    return nbd_meta_pattern(client, pattern, match, errp);
}

/* nbd_meta_base_query
 *
 * Handle queries to 'base' namespace. For now, only the base:allocation
 * context is available.  'len' is the amount of text remaining to be read from
 * the current name, after the 'base:' portion has been stripped.
 *
 * Return -errno on I/O error, 0 if option was completely handled by
 * sending a reply about inconsistent lengths, or 1 on success.
 */
static int nbd_meta_base_query(NBDClient *client, NBDExportMetaContexts *meta,
                               uint32_t len, Error **errp)
{
    return nbd_meta_empty_or_pattern(client, "allocation", len,
                                     &meta->base_allocation, errp);
}

/* nbd_meta_bitmap_query
 *
 * Handle query to 'qemu:' namespace.
 * @len is the amount of text remaining to be read from the current name, after
 * the 'qemu:' portion has been stripped.
 *
 * Return -errno on I/O error, 0 if option was completely handled by
 * sending a reply about inconsistent lengths, or 1 on success. */
static int nbd_meta_qemu_query(NBDClient *client, NBDExportMetaContexts *meta,
                               uint32_t len, Error **errp)
{
    bool dirty_bitmap = false;
    size_t dirty_bitmap_len = strlen("dirty-bitmap:");
    int ret;

    if (!meta->exp->export_bitmap) {
        trace_nbd_negotiate_meta_query_skip("no dirty-bitmap exported");
        return nbd_opt_skip(client, len, errp);
    }

    if (len == 0) {
        if (client->opt == NBD_OPT_LIST_META_CONTEXT) {
            meta->bitmap = true;
        }
        trace_nbd_negotiate_meta_query_parse("empty");
        return 1;
    }

    if (len < dirty_bitmap_len) {
        trace_nbd_negotiate_meta_query_skip("not dirty-bitmap:");
        return nbd_opt_skip(client, len, errp);
    }

    len -= dirty_bitmap_len;
    ret = nbd_meta_pattern(client, "dirty-bitmap:", &dirty_bitmap, errp);
    if (ret <= 0) {
        return ret;
    }
    if (!dirty_bitmap) {
        trace_nbd_negotiate_meta_query_skip("not dirty-bitmap:");
        return nbd_opt_skip(client, len, errp);
    }

    trace_nbd_negotiate_meta_query_parse("dirty-bitmap:");

    return nbd_meta_empty_or_pattern(
            client, meta->exp->export_bitmap_context +
            strlen("qemu:dirty_bitmap:"), len, &meta->bitmap, errp);
}

/* nbd_negotiate_meta_query
 *
 * Parse namespace name and call corresponding function to parse body of the
 * query.
 *
 * The only supported namespaces are 'base' and 'qemu'.
 *
 * The function aims not wasting time and memory to read long unknown namespace
 * names.
 *
 * Return -errno on I/O error, 0 if option was completely handled by
 * sending a reply about inconsistent lengths, or 1 on success. */
static int nbd_negotiate_meta_query(NBDClient *client,
                                    NBDExportMetaContexts *meta, Error **errp)
{
    /*
     * Both 'qemu' and 'base' namespaces have length = 5 including a
     * colon. If another length namespace is later introduced, this
     * should certainly be refactored.
     */
    int ret;
    size_t ns_len = 5;
    char ns[5];
    uint32_t len;

    ret = nbd_opt_read(client, &len, sizeof(len), errp);
    if (ret <= 0) {
        return ret;
    }
    len = cpu_to_be32(len);

    if (len > NBD_MAX_STRING_SIZE) {
        trace_nbd_negotiate_meta_query_skip("length too long");
        return nbd_opt_skip(client, len, errp);
    }
    if (len < ns_len) {
        trace_nbd_negotiate_meta_query_skip("length too short");
        return nbd_opt_skip(client, len, errp);
    }

    len -= ns_len;
    ret = nbd_opt_read(client, ns, ns_len, errp);
    if (ret <= 0) {
        return ret;
    }

    if (!strncmp(ns, "base:", ns_len)) {
        trace_nbd_negotiate_meta_query_parse("base:");
        return nbd_meta_base_query(client, meta, len, errp);
    } else if (!strncmp(ns, "qemu:", ns_len)) {
        trace_nbd_negotiate_meta_query_parse("qemu:");
        return nbd_meta_qemu_query(client, meta, len, errp);
    }

    trace_nbd_negotiate_meta_query_skip("unknown namespace");
    return nbd_opt_skip(client, len, errp);
}

/* nbd_negotiate_meta_queries
 * Handle NBD_OPT_LIST_META_CONTEXT and NBD_OPT_SET_META_CONTEXT
 *
 * Return -errno on I/O error, or 0 if option was completely handled. */
static int nbd_negotiate_meta_queries(NBDClient *client,
                                      NBDExportMetaContexts *meta, Error **errp)
{
    int ret;
    g_autofree char *export_name = NULL;
    NBDExportMetaContexts local_meta;
    uint32_t nb_queries;
    int i;

    if (!client->structured_reply) {
        return nbd_opt_invalid(client, errp,
                               "request option '%s' when structured reply "
                               "is not negotiated",
                               nbd_opt_lookup(client->opt));
    }

    if (client->opt == NBD_OPT_LIST_META_CONTEXT) {
        /* Only change the caller's meta on SET. */
        meta = &local_meta;
    }

    memset(meta, 0, sizeof(*meta));

    ret = nbd_opt_read_name(client, &export_name, NULL, errp);
    if (ret <= 0) {
        return ret;
    }

    meta->exp = nbd_export_find(export_name);
    if (meta->exp == NULL) {
        return nbd_opt_drop(client, NBD_REP_ERR_UNKNOWN, errp,
                            "export '%s' not present", export_name);
    }

    ret = nbd_opt_read(client, &nb_queries, sizeof(nb_queries), errp);
    if (ret <= 0) {
        return ret;
    }
    nb_queries = cpu_to_be32(nb_queries);
    trace_nbd_negotiate_meta_context(nbd_opt_lookup(client->opt),
                                     export_name, nb_queries);

    if (client->opt == NBD_OPT_LIST_META_CONTEXT && !nb_queries) {
        /* enable all known contexts */
        meta->base_allocation = true;
        meta->bitmap = !!meta->exp->export_bitmap;
    } else {
        for (i = 0; i < nb_queries; ++i) {
            ret = nbd_negotiate_meta_query(client, meta, errp);
            if (ret <= 0) {
                return ret;
            }
        }
    }

    if (meta->base_allocation) {
        ret = nbd_negotiate_send_meta_context(client, "base:allocation",
                                              NBD_META_ID_BASE_ALLOCATION,
                                              errp);
        if (ret < 0) {
            return ret;
        }
    }

    if (meta->bitmap) {
        ret = nbd_negotiate_send_meta_context(client,
                                              meta->exp->export_bitmap_context,
                                              NBD_META_ID_DIRTY_BITMAP,
                                              errp);
        if (ret < 0) {
            return ret;
        }
    }

    ret = nbd_negotiate_send_rep(client, NBD_REP_ACK, errp);
    if (ret == 0) {
        meta->valid = true;
    }

    return ret;
}

/* nbd_negotiate_options
 * Process all NBD_OPT_* client option commands, during fixed newstyle
 * negotiation.
 * Return:
 * -errno  on error, errp is set
 * 0       on successful negotiation, errp is not set
 * 1       if client sent NBD_OPT_ABORT, i.e. on valid disconnect,
 *         errp is not set
 */
static int nbd_negotiate_options(NBDClient *client, Error **errp)
{
    uint32_t flags;
    bool fixedNewstyle = false;
    bool no_zeroes = false;

    /* Client sends:
        [ 0 ..   3]   client flags

       Then we loop until NBD_OPT_EXPORT_NAME or NBD_OPT_GO:
        [ 0 ..   7]   NBD_OPTS_MAGIC
        [ 8 ..  11]   NBD option
        [12 ..  15]   Data length
        ...           Rest of request

        [ 0 ..   7]   NBD_OPTS_MAGIC
        [ 8 ..  11]   Second NBD option
        [12 ..  15]   Data length
        ...           Rest of request
    */

    if (nbd_read32(client->ioc, &flags, "flags", errp) < 0) {
        return -EIO;
    }
    trace_nbd_negotiate_options_flags(flags);
    if (flags & NBD_FLAG_C_FIXED_NEWSTYLE) {
        fixedNewstyle = true;
        flags &= ~NBD_FLAG_C_FIXED_NEWSTYLE;
    }
    if (flags & NBD_FLAG_C_NO_ZEROES) {
        no_zeroes = true;
        flags &= ~NBD_FLAG_C_NO_ZEROES;
    }
    if (flags != 0) {
        error_setg(errp, "Unknown client flags 0x%" PRIx32 " received", flags);
        return -EINVAL;
    }

    while (1) {
        int ret;
        uint32_t option, length;
        uint64_t magic;

        if (nbd_read64(client->ioc, &magic, "opts magic", errp) < 0) {
            return -EINVAL;
        }
        trace_nbd_negotiate_options_check_magic(magic);
        if (magic != NBD_OPTS_MAGIC) {
            error_setg(errp, "Bad magic received");
            return -EINVAL;
        }

        if (nbd_read32(client->ioc, &option, "option", errp) < 0) {
            return -EINVAL;
        }
        client->opt = option;

        if (nbd_read32(client->ioc, &length, "option length", errp) < 0) {
            return -EINVAL;
        }
        assert(!client->optlen);
        client->optlen = length;

        if (length > NBD_MAX_BUFFER_SIZE) {
            error_setg(errp, "len (%" PRIu32" ) is larger than max len (%u)",
                       length, NBD_MAX_BUFFER_SIZE);
            return -EINVAL;
        }

        trace_nbd_negotiate_options_check_option(option,
                                                 nbd_opt_lookup(option));
        if (client->tlscreds &&
            client->ioc == (QIOChannel *)client->sioc) {
            QIOChannel *tioc;
            if (!fixedNewstyle) {
                error_setg(errp, "Unsupported option 0x%" PRIx32, option);
                return -EINVAL;
            }
            switch (option) {
            case NBD_OPT_STARTTLS:
                if (length) {
                    /* Unconditionally drop the connection if the client
                     * can't start a TLS negotiation correctly */
                    return nbd_reject_length(client, true, errp);
                }
                tioc = nbd_negotiate_handle_starttls(client, errp);
                if (!tioc) {
                    return -EIO;
                }
                ret = 0;
                object_unref(OBJECT(client->ioc));
                client->ioc = QIO_CHANNEL(tioc);
                break;

            case NBD_OPT_EXPORT_NAME:
                /* No way to return an error to client, so drop connection */
                error_setg(errp, "Option 0x%x not permitted before TLS",
                           option);
                return -EINVAL;

            default:
                /* Let the client keep trying, unless they asked to
                 * quit. Always try to give an error back to the
                 * client; but when replying to OPT_ABORT, be aware
                 * that the client may hang up before receiving the
                 * error, in which case we are fine ignoring the
                 * resulting EPIPE. */
                ret = nbd_opt_drop(client, NBD_REP_ERR_TLS_REQD,
                                   option == NBD_OPT_ABORT ? NULL : errp,
                                   "Option 0x%" PRIx32
                                   " not permitted before TLS", option);
                if (option == NBD_OPT_ABORT) {
                    return 1;
                }
                break;
            }
        } else if (fixedNewstyle) {
            switch (option) {
            case NBD_OPT_LIST:
                if (length) {
                    ret = nbd_reject_length(client, false, errp);
                } else {
                    ret = nbd_negotiate_handle_list(client, errp);
                }
                break;

            case NBD_OPT_ABORT:
                /* NBD spec says we must try to reply before
                 * disconnecting, but that we must also tolerate
                 * guests that don't wait for our reply. */
                nbd_negotiate_send_rep(client, NBD_REP_ACK, NULL);
                return 1;

            case NBD_OPT_EXPORT_NAME:
                return nbd_negotiate_handle_export_name(client, no_zeroes,
                                                        errp);

            case NBD_OPT_INFO:
            case NBD_OPT_GO:
                ret = nbd_negotiate_handle_info(client, errp);
                if (ret == 1) {
                    assert(option == NBD_OPT_GO);
                    return 0;
                }
                break;

            case NBD_OPT_STARTTLS:
                if (length) {
                    ret = nbd_reject_length(client, false, errp);
                } else if (client->tlscreds) {
                    ret = nbd_negotiate_send_rep_err(client,
                                                     NBD_REP_ERR_INVALID, errp,
                                                     "TLS already enabled");
                } else {
                    ret = nbd_negotiate_send_rep_err(client,
                                                     NBD_REP_ERR_POLICY, errp,
                                                     "TLS not configured");
                }
                break;

            case NBD_OPT_STRUCTURED_REPLY:
                if (length) {
                    ret = nbd_reject_length(client, false, errp);
                } else if (client->structured_reply) {
                    ret = nbd_negotiate_send_rep_err(
                        client, NBD_REP_ERR_INVALID, errp,
                        "structured reply already negotiated");
                } else {
                    ret = nbd_negotiate_send_rep(client, NBD_REP_ACK, errp);
                    client->structured_reply = true;
                }
                break;

            case NBD_OPT_LIST_META_CONTEXT:
            case NBD_OPT_SET_META_CONTEXT:
                ret = nbd_negotiate_meta_queries(client, &client->export_meta,
                                                 errp);
                break;

            default:
                ret = nbd_opt_drop(client, NBD_REP_ERR_UNSUP, errp,
                                   "Unsupported option %" PRIu32 " (%s)",
                                   option, nbd_opt_lookup(option));
                break;
            }
        } else {
            /*
             * If broken new-style we should drop the connection
             * for anything except NBD_OPT_EXPORT_NAME
             */
            switch (option) {
            case NBD_OPT_EXPORT_NAME:
                return nbd_negotiate_handle_export_name(client, no_zeroes,
                                                        errp);

            default:
                error_setg(errp, "Unsupported option %" PRIu32 " (%s)",
                           option, nbd_opt_lookup(option));
                return -EINVAL;
            }
        }
        if (ret < 0) {
            return ret;
        }
    }
}

/* nbd_negotiate
 * Return:
 * -errno  on error, errp is set
 * 0       on successful negotiation, errp is not set
 * 1       if client sent NBD_OPT_ABORT, i.e. on valid disconnect,
 *         errp is not set
 */
static coroutine_fn int nbd_negotiate(NBDClient *client, Error **errp)
{
    char buf[NBD_OLDSTYLE_NEGOTIATE_SIZE] = "";
    int ret;

    /* Old style negotiation header, no room for options
        [ 0 ..   7]   passwd       ("NBDMAGIC")
        [ 8 ..  15]   magic        (NBD_CLIENT_MAGIC)
        [16 ..  23]   size
        [24 ..  27]   export flags (zero-extended)
        [28 .. 151]   reserved     (0)

       New style negotiation header, client can send options
        [ 0 ..   7]   passwd       ("NBDMAGIC")
        [ 8 ..  15]   magic        (NBD_OPTS_MAGIC)
        [16 ..  17]   server flags (0)
        ....options sent, ending in NBD_OPT_EXPORT_NAME or NBD_OPT_GO....
     */

    qio_channel_set_blocking(client->ioc, false, NULL);

    trace_nbd_negotiate_begin();
    memcpy(buf, "NBDMAGIC", 8);

    stq_be_p(buf + 8, NBD_OPTS_MAGIC);
    stw_be_p(buf + 16, NBD_FLAG_FIXED_NEWSTYLE | NBD_FLAG_NO_ZEROES);

    if (nbd_write(client->ioc, buf, 18, errp) < 0) {
        error_prepend(errp, "write failed: ");
        return -EINVAL;
    }
    ret = nbd_negotiate_options(client, errp);
    if (ret != 0) {
        if (ret < 0) {
            error_prepend(errp, "option negotiation failed: ");
        }
        return ret;
    }

    /* Attach the channel to the same AioContext as the export */
    if (client->exp && client->exp->ctx) {
        qio_channel_attach_aio_context(client->ioc, client->exp->ctx);
    }

    assert(!client->optlen);
    trace_nbd_negotiate_success();

    return 0;
}

static int nbd_receive_request(QIOChannel *ioc, NBDRequest *request,
                               Error **errp)
{
    uint8_t buf[NBD_REQUEST_SIZE];
    uint32_t magic;
    int ret;

    ret = nbd_read(ioc, buf, sizeof(buf), "request", errp);
    if (ret < 0) {
        return ret;
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

    trace_nbd_receive_request(magic, request->flags, request->type,
                              request->from, request->len);

    if (magic != NBD_REQUEST_MAGIC) {
        error_setg(errp, "invalid magic (got 0x%" PRIx32 ")", magic);
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
        g_free(client->tlsauthz);
        if (client->exp) {
            QTAILQ_REMOVE(&client->exp->clients, client, next);
            nbd_export_put(client->exp);
        }
        g_free(client);
    }
}

static void client_close(NBDClient *client, bool negotiated)
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
    if (client->close_fn) {
        client->close_fn(client, negotiated);
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

    trace_nbd_blk_aio_attached(exp->name, ctx);

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

    trace_nbd_blk_aio_detach(exp->name, exp->ctx);

    QTAILQ_FOREACH(client, &exp->clients, next) {
        qio_channel_detach_aio_context(client->ioc);
    }

    exp->ctx = NULL;
}

static void nbd_eject_notifier(Notifier *n, void *data)
{
    NBDExport *exp = container_of(n, NBDExport, eject_notifier);
    AioContext *aio_context;

    aio_context = exp->ctx;
    aio_context_acquire(aio_context);
    nbd_export_close(exp);
    aio_context_release(aio_context);
}

NBDExport *nbd_export_new(BlockDriverState *bs, uint64_t dev_offset,
                          uint64_t size, const char *name, const char *desc,
                          const char *bitmap, bool readonly, bool shared,
                          void (*close)(NBDExport *), bool writethrough,
                          BlockBackend *on_eject_blk, Error **errp)
{
    AioContext *ctx;
    BlockBackend *blk;
    NBDExport *exp = g_new0(NBDExport, 1);
    uint64_t perm;
    int ret;

    /*
     * NBD exports are used for non-shared storage migration.  Make sure
     * that BDRV_O_INACTIVE is cleared and the image is ready for write
     * access since the export could be available before migration handover.
     * ctx was acquired in the caller.
     */
    assert(name && strlen(name) <= NBD_MAX_STRING_SIZE);
    ctx = bdrv_get_aio_context(bs);
    bdrv_invalidate_cache(bs, NULL);

    /* Don't allow resize while the NBD server is running, otherwise we don't
     * care what happens with the node. */
    perm = BLK_PERM_CONSISTENT_READ;
    if (!readonly) {
        perm |= BLK_PERM_WRITE;
    }
    blk = blk_new(ctx, perm,
                  BLK_PERM_CONSISTENT_READ | BLK_PERM_WRITE_UNCHANGED |
                  BLK_PERM_WRITE | BLK_PERM_GRAPH_MOD);
    ret = blk_insert_bs(blk, bs, errp);
    if (ret < 0) {
        goto fail;
    }
    blk_set_enable_write_cache(blk, !writethrough);
    blk_set_allow_aio_context_change(blk, true);

    exp->refcount = 1;
    QTAILQ_INIT(&exp->clients);
    exp->blk = blk;
    assert(dev_offset <= INT64_MAX);
    exp->dev_offset = dev_offset;
    exp->name = g_strdup(name);
    assert(!desc || strlen(desc) <= NBD_MAX_STRING_SIZE);
    exp->description = g_strdup(desc);
    exp->nbdflags = (NBD_FLAG_HAS_FLAGS | NBD_FLAG_SEND_FLUSH |
                     NBD_FLAG_SEND_FUA | NBD_FLAG_SEND_CACHE);
    if (readonly) {
        exp->nbdflags |= NBD_FLAG_READ_ONLY;
        if (shared) {
            exp->nbdflags |= NBD_FLAG_CAN_MULTI_CONN;
        }
    } else {
        exp->nbdflags |= (NBD_FLAG_SEND_TRIM | NBD_FLAG_SEND_WRITE_ZEROES |
                          NBD_FLAG_SEND_FAST_ZERO);
    }
    assert(size <= INT64_MAX - dev_offset);
    exp->size = QEMU_ALIGN_DOWN(size, BDRV_SECTOR_SIZE);

    if (bitmap) {
        BdrvDirtyBitmap *bm = NULL;

        while (true) {
            bm = bdrv_find_dirty_bitmap(bs, bitmap);
            if (bm != NULL || bs->backing == NULL) {
                break;
            }

            bs = bs->backing->bs;
        }

        if (bm == NULL) {
            error_setg(errp, "Bitmap '%s' is not found", bitmap);
            goto fail;
        }

        if (bdrv_dirty_bitmap_check(bm, BDRV_BITMAP_ALLOW_RO, errp)) {
            goto fail;
        }

        if (readonly && bdrv_is_writable(bs) &&
            bdrv_dirty_bitmap_enabled(bm)) {
            error_setg(errp,
                       "Enabled bitmap '%s' incompatible with readonly export",
                       bitmap);
            goto fail;
        }

        bdrv_dirty_bitmap_set_busy(bm, true);
        exp->export_bitmap = bm;
        assert(strlen(bitmap) <= BDRV_BITMAP_MAX_NAME_SIZE);
        exp->export_bitmap_context = g_strdup_printf("qemu:dirty-bitmap:%s",
                                                     bitmap);
        assert(strlen(exp->export_bitmap_context) < NBD_MAX_STRING_SIZE);
    }

    exp->close = close;
    exp->ctx = ctx;
    blk_add_aio_context_notifier(blk, blk_aio_attached, blk_aio_detach, exp);

    if (on_eject_blk) {
        blk_ref(on_eject_blk);
        exp->eject_notifier_blk = on_eject_blk;
        exp->eject_notifier.notify = nbd_eject_notifier;
        blk_add_remove_bs_notifier(on_eject_blk, &exp->eject_notifier);
    }
    QTAILQ_INSERT_TAIL(&exports, exp, next);
    nbd_export_get(exp);
    return exp;

fail:
    blk_unref(blk);
    g_free(exp->name);
    g_free(exp->description);
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

AioContext *
nbd_export_aio_context(NBDExport *exp)
{
    return exp->ctx;
}

void nbd_export_close(NBDExport *exp)
{
    NBDClient *client, *next;

    nbd_export_get(exp);
    /*
     * TODO: Should we expand QMP NbdServerRemoveNode enum to allow a
     * close mode that stops advertising the export to new clients but
     * still permits existing clients to run to completion? Because of
     * that possibility, nbd_export_close() can be called more than
     * once on an export.
     */
    QTAILQ_FOREACH_SAFE(client, &exp->clients, next, next) {
        client_close(client, true);
    }
    if (exp->name) {
        nbd_export_put(exp);
        g_free(exp->name);
        exp->name = NULL;
        QTAILQ_REMOVE(&exports, exp, next);
    }
    g_free(exp->description);
    exp->description = NULL;
    nbd_export_put(exp);
}

void nbd_export_remove(NBDExport *exp, NbdServerRemoveMode mode, Error **errp)
{
    if (mode == NBD_SERVER_REMOVE_MODE_HARD || QTAILQ_EMPTY(&exp->clients)) {
        nbd_export_close(exp);
        return;
    }

    assert(mode == NBD_SERVER_REMOVE_MODE_SAFE);

    error_setg(errp, "export '%s' still in use", exp->name);
    error_append_hint(errp, "Use mode='hard' to force client disconnect\n");
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

    /* nbd_export_close() may theoretically reduce refcount to 0. It may happen
     * if someone calls nbd_export_put() on named export not through
     * nbd_export_set_name() when refcount is 1. So, let's assert that
     * it is > 0.
     */
    assert(exp->refcount > 0);
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

        if (exp->export_bitmap) {
            bdrv_dirty_bitmap_set_busy(exp->export_bitmap, false);
            g_free(exp->export_bitmap_context);
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
    AioContext *aio_context;

    QTAILQ_FOREACH_SAFE(exp, &exports, next, next) {
        aio_context = exp->ctx;
        aio_context_acquire(aio_context);
        nbd_export_close(exp);
        aio_context_release(aio_context);
    }
}

static int coroutine_fn nbd_co_send_iov(NBDClient *client, struct iovec *iov,
                                        unsigned niov, Error **errp)
{
    int ret;

    g_assert(qemu_in_coroutine());
    qemu_co_mutex_lock(&client->send_lock);
    client->send_coroutine = qemu_coroutine_self();

    ret = qio_channel_writev_all(client->ioc, iov, niov, errp) < 0 ? -EIO : 0;

    client->send_coroutine = NULL;
    qemu_co_mutex_unlock(&client->send_lock);

    return ret;
}

static inline void set_be_simple_reply(NBDSimpleReply *reply, uint64_t error,
                                       uint64_t handle)
{
    stl_be_p(&reply->magic, NBD_SIMPLE_REPLY_MAGIC);
    stl_be_p(&reply->error, error);
    stq_be_p(&reply->handle, handle);
}

static int nbd_co_send_simple_reply(NBDClient *client,
                                    uint64_t handle,
                                    uint32_t error,
                                    void *data,
                                    size_t len,
                                    Error **errp)
{
    NBDSimpleReply reply;
    int nbd_err = system_errno_to_nbd_errno(error);
    struct iovec iov[] = {
        {.iov_base = &reply, .iov_len = sizeof(reply)},
        {.iov_base = data, .iov_len = len}
    };

    trace_nbd_co_send_simple_reply(handle, nbd_err, nbd_err_lookup(nbd_err),
                                   len);
    set_be_simple_reply(&reply, nbd_err, handle);

    return nbd_co_send_iov(client, iov, len ? 2 : 1, errp);
}

static inline void set_be_chunk(NBDStructuredReplyChunk *chunk, uint16_t flags,
                                uint16_t type, uint64_t handle, uint32_t length)
{
    stl_be_p(&chunk->magic, NBD_STRUCTURED_REPLY_MAGIC);
    stw_be_p(&chunk->flags, flags);
    stw_be_p(&chunk->type, type);
    stq_be_p(&chunk->handle, handle);
    stl_be_p(&chunk->length, length);
}

static int coroutine_fn nbd_co_send_structured_done(NBDClient *client,
                                                    uint64_t handle,
                                                    Error **errp)
{
    NBDStructuredReplyChunk chunk;
    struct iovec iov[] = {
        {.iov_base = &chunk, .iov_len = sizeof(chunk)},
    };

    trace_nbd_co_send_structured_done(handle);
    set_be_chunk(&chunk, NBD_REPLY_FLAG_DONE, NBD_REPLY_TYPE_NONE, handle, 0);

    return nbd_co_send_iov(client, iov, 1, errp);
}

static int coroutine_fn nbd_co_send_structured_read(NBDClient *client,
                                                    uint64_t handle,
                                                    uint64_t offset,
                                                    void *data,
                                                    size_t size,
                                                    bool final,
                                                    Error **errp)
{
    NBDStructuredReadData chunk;
    struct iovec iov[] = {
        {.iov_base = &chunk, .iov_len = sizeof(chunk)},
        {.iov_base = data, .iov_len = size}
    };

    assert(size);
    trace_nbd_co_send_structured_read(handle, offset, data, size);
    set_be_chunk(&chunk.h, final ? NBD_REPLY_FLAG_DONE : 0,
                 NBD_REPLY_TYPE_OFFSET_DATA, handle,
                 sizeof(chunk) - sizeof(chunk.h) + size);
    stq_be_p(&chunk.offset, offset);

    return nbd_co_send_iov(client, iov, 2, errp);
}

static int coroutine_fn nbd_co_send_structured_error(NBDClient *client,
                                                     uint64_t handle,
                                                     uint32_t error,
                                                     const char *msg,
                                                     Error **errp)
{
    NBDStructuredError chunk;
    int nbd_err = system_errno_to_nbd_errno(error);
    struct iovec iov[] = {
        {.iov_base = &chunk, .iov_len = sizeof(chunk)},
        {.iov_base = (char *)msg, .iov_len = msg ? strlen(msg) : 0},
    };

    assert(nbd_err);
    trace_nbd_co_send_structured_error(handle, nbd_err,
                                       nbd_err_lookup(nbd_err), msg ? msg : "");
    set_be_chunk(&chunk.h, NBD_REPLY_FLAG_DONE, NBD_REPLY_TYPE_ERROR, handle,
                 sizeof(chunk) - sizeof(chunk.h) + iov[1].iov_len);
    stl_be_p(&chunk.error, nbd_err);
    stw_be_p(&chunk.message_length, iov[1].iov_len);

    return nbd_co_send_iov(client, iov, 1 + !!iov[1].iov_len, errp);
}

/* Do a sparse read and send the structured reply to the client.
 * Returns -errno if sending fails. bdrv_block_status_above() failure is
 * reported to the client, at which point this function succeeds.
 */
static int coroutine_fn nbd_co_send_sparse_read(NBDClient *client,
                                                uint64_t handle,
                                                uint64_t offset,
                                                uint8_t *data,
                                                size_t size,
                                                Error **errp)
{
    int ret = 0;
    NBDExport *exp = client->exp;
    size_t progress = 0;

    while (progress < size) {
        int64_t pnum;
        int status = bdrv_block_status_above(blk_bs(exp->blk), NULL,
                                             offset + progress,
                                             size - progress, &pnum, NULL,
                                             NULL);
        bool final;

        if (status < 0) {
            char *msg = g_strdup_printf("unable to check for holes: %s",
                                        strerror(-status));

            ret = nbd_co_send_structured_error(client, handle, -status, msg,
                                               errp);
            g_free(msg);
            return ret;
        }
        assert(pnum && pnum <= size - progress);
        final = progress + pnum == size;
        if (status & BDRV_BLOCK_ZERO) {
            NBDStructuredReadHole chunk;
            struct iovec iov[] = {
                {.iov_base = &chunk, .iov_len = sizeof(chunk)},
            };

            trace_nbd_co_send_structured_read_hole(handle, offset + progress,
                                                   pnum);
            set_be_chunk(&chunk.h, final ? NBD_REPLY_FLAG_DONE : 0,
                         NBD_REPLY_TYPE_OFFSET_HOLE,
                         handle, sizeof(chunk) - sizeof(chunk.h));
            stq_be_p(&chunk.offset, offset + progress);
            stl_be_p(&chunk.length, pnum);
            ret = nbd_co_send_iov(client, iov, 1, errp);
        } else {
            ret = blk_pread(exp->blk, offset + progress + exp->dev_offset,
                            data + progress, pnum);
            if (ret < 0) {
                error_setg_errno(errp, -ret, "reading from file failed");
                break;
            }
            ret = nbd_co_send_structured_read(client, handle, offset + progress,
                                              data + progress, pnum, final,
                                              errp);
        }

        if (ret < 0) {
            break;
        }
        progress += pnum;
    }
    return ret;
}

/*
 * Populate @extents from block status. Update @bytes to be the actual
 * length encoded (which may be smaller than the original), and update
 * @nb_extents to the number of extents used.
 *
 * Returns zero on success and -errno on bdrv_block_status_above failure.
 */
static int blockstatus_to_extents(BlockDriverState *bs, uint64_t offset,
                                  uint64_t *bytes, NBDExtent *extents,
                                  unsigned int *nb_extents)
{
    uint64_t remaining_bytes = *bytes;
    NBDExtent *extent = extents, *extents_end = extents + *nb_extents;
    bool first_extent = true;

    assert(*nb_extents);
    while (remaining_bytes) {
        uint32_t flags;
        int64_t num;
        int ret = bdrv_block_status_above(bs, NULL, offset, remaining_bytes,
                                          &num, NULL, NULL);

        if (ret < 0) {
            return ret;
        }

        flags = (ret & BDRV_BLOCK_ALLOCATED ? 0 : NBD_STATE_HOLE) |
                (ret & BDRV_BLOCK_ZERO      ? NBD_STATE_ZERO : 0);

        if (first_extent) {
            extent->flags = flags;
            extent->length = num;
            first_extent = false;
        } else if (flags == extent->flags) {
            /* extend current extent */
            extent->length += num;
        } else {
            if (extent + 1 == extents_end) {
                break;
            }

            /* start new extent */
            extent++;
            extent->flags = flags;
            extent->length = num;
        }
        offset += num;
        remaining_bytes -= num;
    }

    extents_end = extent + 1;

    for (extent = extents; extent < extents_end; extent++) {
        extent->flags = cpu_to_be32(extent->flags);
        extent->length = cpu_to_be32(extent->length);
    }

    *bytes -= remaining_bytes;
    *nb_extents = extents_end - extents;

    return 0;
}

/* nbd_co_send_extents
 *
 * @length is only for tracing purposes (and may be smaller or larger
 * than the client's original request). @last controls whether
 * NBD_REPLY_FLAG_DONE is sent. @extents should already be in
 * big-endian format.
 */
static int nbd_co_send_extents(NBDClient *client, uint64_t handle,
                               NBDExtent *extents, unsigned int nb_extents,
                               uint64_t length, bool last,
                               uint32_t context_id, Error **errp)
{
    NBDStructuredMeta chunk;

    struct iovec iov[] = {
        {.iov_base = &chunk, .iov_len = sizeof(chunk)},
        {.iov_base = extents, .iov_len = nb_extents * sizeof(extents[0])}
    };

    trace_nbd_co_send_extents(handle, nb_extents, context_id, length, last);
    set_be_chunk(&chunk.h, last ? NBD_REPLY_FLAG_DONE : 0,
                 NBD_REPLY_TYPE_BLOCK_STATUS,
                 handle, sizeof(chunk) - sizeof(chunk.h) + iov[1].iov_len);
    stl_be_p(&chunk.context_id, context_id);

    return nbd_co_send_iov(client, iov, 2, errp);
}

/* Get block status from the exported device and send it to the client */
static int nbd_co_send_block_status(NBDClient *client, uint64_t handle,
                                    BlockDriverState *bs, uint64_t offset,
                                    uint32_t length, bool dont_fragment,
                                    bool last, uint32_t context_id,
                                    Error **errp)
{
    int ret;
    unsigned int nb_extents = dont_fragment ? 1 : NBD_MAX_BLOCK_STATUS_EXTENTS;
    NBDExtent *extents = g_new(NBDExtent, nb_extents);
    uint64_t final_length = length;

    ret = blockstatus_to_extents(bs, offset, &final_length, extents,
                                 &nb_extents);
    if (ret < 0) {
        g_free(extents);
        return nbd_co_send_structured_error(
                client, handle, -ret, "can't get block status", errp);
    }

    ret = nbd_co_send_extents(client, handle, extents, nb_extents,
                              final_length, last, context_id, errp);

    g_free(extents);

    return ret;
}

/*
 * Populate @extents from a dirty bitmap. Unless @dont_fragment, the
 * final extent may exceed the original @length. Store in @length the
 * byte length encoded (which may be smaller or larger than the
 * original), and return the number of extents used.
 */
static unsigned int bitmap_to_extents(BdrvDirtyBitmap *bitmap, uint64_t offset,
                                      uint64_t *length, NBDExtent *extents,
                                      unsigned int nb_extents,
                                      bool dont_fragment)
{
    uint64_t begin = offset, end = offset;
    uint64_t overall_end = offset + *length;
    unsigned int i = 0;
    BdrvDirtyBitmapIter *it;
    bool dirty;

    bdrv_dirty_bitmap_lock(bitmap);

    it = bdrv_dirty_iter_new(bitmap);
    dirty = bdrv_dirty_bitmap_get_locked(bitmap, offset);

    assert(begin < overall_end && nb_extents);
    while (begin < overall_end && i < nb_extents) {
        bool next_dirty = !dirty;

        if (dirty) {
            end = bdrv_dirty_bitmap_next_zero(bitmap, begin, UINT64_MAX);
        } else {
            bdrv_set_dirty_iter(it, begin);
            end = bdrv_dirty_iter_next(it);
        }
        if (end == -1 || end - begin > UINT32_MAX) {
            /* Cap to an aligned value < 4G beyond begin. */
            end = MIN(bdrv_dirty_bitmap_size(bitmap),
                      begin + UINT32_MAX + 1 -
                      bdrv_dirty_bitmap_granularity(bitmap));
            next_dirty = dirty;
        }
        if (dont_fragment && end > overall_end) {
            end = overall_end;
        }

        extents[i].length = cpu_to_be32(end - begin);
        extents[i].flags = cpu_to_be32(dirty ? NBD_STATE_DIRTY : 0);
        i++;
        begin = end;
        dirty = next_dirty;
    }

    bdrv_dirty_iter_free(it);

    bdrv_dirty_bitmap_unlock(bitmap);

    assert(offset < end);
    *length = end - offset;
    return i;
}

static int nbd_co_send_bitmap(NBDClient *client, uint64_t handle,
                              BdrvDirtyBitmap *bitmap, uint64_t offset,
                              uint32_t length, bool dont_fragment, bool last,
                              uint32_t context_id, Error **errp)
{
    int ret;
    unsigned int nb_extents = dont_fragment ? 1 : NBD_MAX_BLOCK_STATUS_EXTENTS;
    NBDExtent *extents = g_new(NBDExtent, nb_extents);
    uint64_t final_length = length;

    nb_extents = bitmap_to_extents(bitmap, offset, &final_length, extents,
                                   nb_extents, dont_fragment);

    ret = nbd_co_send_extents(client, handle, extents, nb_extents,
                              final_length, last, context_id, errp);

    g_free(extents);

    return ret;
}

/* nbd_co_receive_request
 * Collect a client request. Return 0 if request looks valid, -EIO to drop
 * connection right away, and any other negative value to report an error to
 * the client (although the caller may still need to disconnect after reporting
 * the error).
 */
static int nbd_co_receive_request(NBDRequestData *req, NBDRequest *request,
                                  Error **errp)
{
    NBDClient *client = req->client;
    int valid_flags;

    g_assert(qemu_in_coroutine());
    assert(client->recv_coroutine == qemu_coroutine_self());
    if (nbd_receive_request(client->ioc, request, errp) < 0) {
        return -EIO;
    }

    trace_nbd_co_receive_request_decode_type(request->handle, request->type,
                                             nbd_cmd_lookup(request->type));

    if (request->type != NBD_CMD_WRITE) {
        /* No payload, we are ready to read the next request.  */
        req->complete = true;
    }

    if (request->type == NBD_CMD_DISC) {
        /* Special case: we're going to disconnect without a reply,
         * whether or not flags, from, or len are bogus */
        return -EIO;
    }

    if (request->type == NBD_CMD_READ || request->type == NBD_CMD_WRITE ||
        request->type == NBD_CMD_CACHE)
    {
        if (request->len > NBD_MAX_BUFFER_SIZE) {
            error_setg(errp, "len (%" PRIu32" ) is larger than max len (%u)",
                       request->len, NBD_MAX_BUFFER_SIZE);
            return -EINVAL;
        }

        if (request->type != NBD_CMD_CACHE) {
            req->data = blk_try_blockalign(client->exp->blk, request->len);
            if (req->data == NULL) {
                error_setg(errp, "No memory");
                return -ENOMEM;
            }
        }
    }

    if (request->type == NBD_CMD_WRITE) {
        if (nbd_read(client->ioc, req->data, request->len, "CMD_WRITE data",
                     errp) < 0)
        {
            return -EIO;
        }
        req->complete = true;

        trace_nbd_co_receive_request_payload_received(request->handle,
                                                      request->len);
    }

    /* Sanity checks. */
    if (client->exp->nbdflags & NBD_FLAG_READ_ONLY &&
        (request->type == NBD_CMD_WRITE ||
         request->type == NBD_CMD_WRITE_ZEROES ||
         request->type == NBD_CMD_TRIM)) {
        error_setg(errp, "Export is read-only");
        return -EROFS;
    }
    if (request->from > client->exp->size ||
        request->len > client->exp->size - request->from) {
        error_setg(errp, "operation past EOF; From: %" PRIu64 ", Len: %" PRIu32
                   ", Size: %" PRIu64, request->from, request->len,
                   client->exp->size);
        return (request->type == NBD_CMD_WRITE ||
                request->type == NBD_CMD_WRITE_ZEROES) ? -ENOSPC : -EINVAL;
    }
    if (client->check_align && !QEMU_IS_ALIGNED(request->from | request->len,
                                                client->check_align)) {
        /*
         * The block layer gracefully handles unaligned requests, but
         * it's still worth tracing client non-compliance
         */
        trace_nbd_co_receive_align_compliance(nbd_cmd_lookup(request->type),
                                              request->from,
                                              request->len,
                                              client->check_align);
    }
    valid_flags = NBD_CMD_FLAG_FUA;
    if (request->type == NBD_CMD_READ && client->structured_reply) {
        valid_flags |= NBD_CMD_FLAG_DF;
    } else if (request->type == NBD_CMD_WRITE_ZEROES) {
        valid_flags |= NBD_CMD_FLAG_NO_HOLE | NBD_CMD_FLAG_FAST_ZERO;
    } else if (request->type == NBD_CMD_BLOCK_STATUS) {
        valid_flags |= NBD_CMD_FLAG_REQ_ONE;
    }
    if (request->flags & ~valid_flags) {
        error_setg(errp, "unsupported flags for command %s (got 0x%x)",
                   nbd_cmd_lookup(request->type), request->flags);
        return -EINVAL;
    }

    return 0;
}

/* Send simple reply without a payload, or a structured error
 * @error_msg is ignored if @ret >= 0
 * Returns 0 if connection is still live, -errno on failure to talk to client
 */
static coroutine_fn int nbd_send_generic_reply(NBDClient *client,
                                               uint64_t handle,
                                               int ret,
                                               const char *error_msg,
                                               Error **errp)
{
    if (client->structured_reply && ret < 0) {
        return nbd_co_send_structured_error(client, handle, -ret, error_msg,
                                            errp);
    } else {
        return nbd_co_send_simple_reply(client, handle, ret < 0 ? -ret : 0,
                                        NULL, 0, errp);
    }
}

/* Handle NBD_CMD_READ request.
 * Return -errno if sending fails. Other errors are reported directly to the
 * client as an error reply. */
static coroutine_fn int nbd_do_cmd_read(NBDClient *client, NBDRequest *request,
                                        uint8_t *data, Error **errp)
{
    int ret;
    NBDExport *exp = client->exp;

    assert(request->type == NBD_CMD_READ);

    /* XXX: NBD Protocol only documents use of FUA with WRITE */
    if (request->flags & NBD_CMD_FLAG_FUA) {
        ret = blk_co_flush(exp->blk);
        if (ret < 0) {
            return nbd_send_generic_reply(client, request->handle, ret,
                                          "flush failed", errp);
        }
    }

    if (client->structured_reply && !(request->flags & NBD_CMD_FLAG_DF) &&
        request->len)
    {
        return nbd_co_send_sparse_read(client, request->handle, request->from,
                                       data, request->len, errp);
    }

    ret = blk_pread(exp->blk, request->from + exp->dev_offset, data,
                    request->len);
    if (ret < 0) {
        return nbd_send_generic_reply(client, request->handle, ret,
                                      "reading from file failed", errp);
    }

    if (client->structured_reply) {
        if (request->len) {
            return nbd_co_send_structured_read(client, request->handle,
                                               request->from, data,
                                               request->len, true, errp);
        } else {
            return nbd_co_send_structured_done(client, request->handle, errp);
        }
    } else {
        return nbd_co_send_simple_reply(client, request->handle, 0,
                                        data, request->len, errp);
    }
}

/*
 * nbd_do_cmd_cache
 *
 * Handle NBD_CMD_CACHE request.
 * Return -errno if sending fails. Other errors are reported directly to the
 * client as an error reply.
 */
static coroutine_fn int nbd_do_cmd_cache(NBDClient *client, NBDRequest *request,
                                         Error **errp)
{
    int ret;
    NBDExport *exp = client->exp;

    assert(request->type == NBD_CMD_CACHE);

    ret = blk_co_preadv(exp->blk, request->from + exp->dev_offset, request->len,
                        NULL, BDRV_REQ_COPY_ON_READ | BDRV_REQ_PREFETCH);

    return nbd_send_generic_reply(client, request->handle, ret,
                                  "caching data failed", errp);
}

/* Handle NBD request.
 * Return -errno if sending fails. Other errors are reported directly to the
 * client as an error reply. */
static coroutine_fn int nbd_handle_request(NBDClient *client,
                                           NBDRequest *request,
                                           uint8_t *data, Error **errp)
{
    int ret;
    int flags;
    NBDExport *exp = client->exp;
    char *msg;

    switch (request->type) {
    case NBD_CMD_CACHE:
        return nbd_do_cmd_cache(client, request, errp);

    case NBD_CMD_READ:
        return nbd_do_cmd_read(client, request, data, errp);

    case NBD_CMD_WRITE:
        flags = 0;
        if (request->flags & NBD_CMD_FLAG_FUA) {
            flags |= BDRV_REQ_FUA;
        }
        ret = blk_pwrite(exp->blk, request->from + exp->dev_offset,
                         data, request->len, flags);
        return nbd_send_generic_reply(client, request->handle, ret,
                                      "writing to file failed", errp);

    case NBD_CMD_WRITE_ZEROES:
        flags = 0;
        if (request->flags & NBD_CMD_FLAG_FUA) {
            flags |= BDRV_REQ_FUA;
        }
        if (!(request->flags & NBD_CMD_FLAG_NO_HOLE)) {
            flags |= BDRV_REQ_MAY_UNMAP;
        }
        if (request->flags & NBD_CMD_FLAG_FAST_ZERO) {
            flags |= BDRV_REQ_NO_FALLBACK;
        }
        ret = blk_pwrite_zeroes(exp->blk, request->from + exp->dev_offset,
                                request->len, flags);
        return nbd_send_generic_reply(client, request->handle, ret,
                                      "writing to file failed", errp);

    case NBD_CMD_DISC:
        /* unreachable, thanks to special case in nbd_co_receive_request() */
        abort();

    case NBD_CMD_FLUSH:
        ret = blk_co_flush(exp->blk);
        return nbd_send_generic_reply(client, request->handle, ret,
                                      "flush failed", errp);

    case NBD_CMD_TRIM:
        ret = blk_co_pdiscard(exp->blk, request->from + exp->dev_offset,
                              request->len);
        if (ret == 0 && request->flags & NBD_CMD_FLAG_FUA) {
            ret = blk_co_flush(exp->blk);
        }
        return nbd_send_generic_reply(client, request->handle, ret,
                                      "discard failed", errp);

    case NBD_CMD_BLOCK_STATUS:
        if (!request->len) {
            return nbd_send_generic_reply(client, request->handle, -EINVAL,
                                          "need non-zero length", errp);
        }
        if (client->export_meta.valid &&
            (client->export_meta.base_allocation ||
             client->export_meta.bitmap))
        {
            bool dont_fragment = request->flags & NBD_CMD_FLAG_REQ_ONE;

            if (client->export_meta.base_allocation) {
                ret = nbd_co_send_block_status(client, request->handle,
                                               blk_bs(exp->blk), request->from,
                                               request->len, dont_fragment,
                                               !client->export_meta.bitmap,
                                               NBD_META_ID_BASE_ALLOCATION,
                                               errp);
                if (ret < 0) {
                    return ret;
                }
            }

            if (client->export_meta.bitmap) {
                ret = nbd_co_send_bitmap(client, request->handle,
                                         client->exp->export_bitmap,
                                         request->from, request->len,
                                         dont_fragment,
                                         true, NBD_META_ID_DIRTY_BITMAP, errp);
                if (ret < 0) {
                    return ret;
                }
            }

            return ret;
        } else {
            return nbd_send_generic_reply(client, request->handle, -EINVAL,
                                          "CMD_BLOCK_STATUS not negotiated",
                                          errp);
        }

    default:
        msg = g_strdup_printf("invalid request type (%" PRIu32 ") received",
                              request->type);
        ret = nbd_send_generic_reply(client, request->handle, -EINVAL, msg,
                                     errp);
        g_free(msg);
        return ret;
    }
}

/* Owns a reference to the NBDClient passed as opaque.  */
static coroutine_fn void nbd_trip(void *opaque)
{
    NBDClient *client = opaque;
    NBDRequestData *req;
    NBDRequest request = { 0 };    /* GCC thinks it can be used uninitialized */
    int ret;
    Error *local_err = NULL;

    trace_nbd_trip();
    if (client->closing) {
        nbd_client_put(client);
        return;
    }

    req = nbd_request_get(client);
    ret = nbd_co_receive_request(req, &request, &local_err);
    client->recv_coroutine = NULL;

    if (client->closing) {
        /*
         * The client may be closed when we are blocked in
         * nbd_co_receive_request()
         */
        goto done;
    }

    nbd_client_receive_next_request(client);
    if (ret == -EIO) {
        goto disconnect;
    }

    if (ret < 0) {
        /* It wans't -EIO, so, according to nbd_co_receive_request()
         * semantics, we should return the error to the client. */
        Error *export_err = local_err;

        local_err = NULL;
        ret = nbd_send_generic_reply(client, request.handle, -EINVAL,
                                     error_get_pretty(export_err), &local_err);
        error_free(export_err);
    } else {
        ret = nbd_handle_request(client, &request, req->data, &local_err);
    }
    if (ret < 0) {
        error_prepend(&local_err, "Failed to send reply: ");
        goto disconnect;
    }

    /* We must disconnect after NBD_CMD_WRITE if we did not
     * read the payload.
     */
    if (!req->complete) {
        error_setg(&local_err, "Request handling failed in intermediate state");
        goto disconnect;
    }

done:
    nbd_request_put(req);
    nbd_client_put(client);
    return;

disconnect:
    if (local_err) {
        error_reportf_err(local_err, "Disconnect client, due to: ");
    }
    nbd_request_put(req);
    client_close(client, true);
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
    NBDClient *client = opaque;
    Error *local_err = NULL;

    qemu_co_mutex_init(&client->send_lock);

    if (nbd_negotiate(client, &local_err)) {
        if (local_err) {
            error_report_err(local_err);
        }
        client_close(client, false);
        return;
    }

    nbd_client_receive_next_request(client);
}

/*
 * Create a new client listener using the given channel @sioc.
 * Begin servicing it in a coroutine.  When the connection closes, call
 * @close_fn with an indication of whether the client completed negotiation.
 */
void nbd_client_new(QIOChannelSocket *sioc,
                    QCryptoTLSCreds *tlscreds,
                    const char *tlsauthz,
                    void (*close_fn)(NBDClient *, bool))
{
    NBDClient *client;
    Coroutine *co;

    client = g_new0(NBDClient, 1);
    client->refcount = 1;
    client->tlscreds = tlscreds;
    if (tlscreds) {
        object_ref(OBJECT(client->tlscreds));
    }
    client->tlsauthz = g_strdup(tlsauthz);
    client->sioc = sioc;
    object_ref(OBJECT(client->sioc));
    client->ioc = QIO_CHANNEL(sioc);
    object_ref(OBJECT(client->ioc));
    client->close_fn = close_fn;

    co = qemu_coroutine_create(nbd_co_client_start, client);
    qemu_coroutine_enter(co);
}
