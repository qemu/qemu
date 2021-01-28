/*
 *  Copyright (C) 2016-2020 Red Hat, Inc.
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

#include "block/export.h"
#include "qapi/error.h"
#include "qemu/queue.h"
#include "trace.h"
#include "nbd-internal.h"
#include "qemu/units.h"

#define NBD_META_ID_BASE_ALLOCATION 0
#define NBD_META_ID_ALLOCATION_DEPTH 1
/* Dirty bitmaps use 'NBD_META_ID_DIRTY_BITMAP + i', so keep this id last. */
#define NBD_META_ID_DIRTY_BITMAP 2

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
    BlockExport common;

    char *name;
    char *description;
    uint64_t size;
    uint16_t nbdflags;
    QTAILQ_HEAD(, NBDClient) clients;
    QTAILQ_ENTRY(NBDExport) next;

    BlockBackend *eject_notifier_blk;
    Notifier eject_notifier;

    bool allocation_depth;
    BdrvDirtyBitmap **export_bitmaps;
    size_t nr_export_bitmaps;
};

static QTAILQ_HEAD(, NBDExport) exports = QTAILQ_HEAD_INITIALIZER(exports);

/* NBDExportMetaContexts represents a list of contexts to be exported,
 * as selected by NBD_OPT_SET_META_CONTEXT. Also used for
 * NBD_OPT_LIST_META_CONTEXT. */
typedef struct NBDExportMetaContexts {
    NBDExport *exp;
    size_t count; /* number of negotiated contexts */
    bool base_allocation; /* export base:allocation context (block status) */
    bool allocation_depth; /* export qemu:allocation-depth */
    bool *bitmaps; /*
                    * export qemu:dirty-bitmap:<export bitmap name>,
                    * sized by exp->nr_export_bitmaps
                    */
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

    bool read_yielding;
    bool quiescing;

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
    ERRP_GUARD();
    g_autofree char *msg = NULL;
    int ret;
    size_t len;

    msg = g_strdup_vprintf(fmt, va);
    len = strlen(msg);
    assert(len < NBD_MAX_STRING_SIZE);
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

/*
 * Return a malloc'd copy of @name suitable for use in an error reply.
 */
static char *
nbd_sanitize_name(const char *name)
{
    if (strnlen(name, 80) < 80) {
        return g_strdup(name);
    }
    /* XXX Should we also try to sanitize any control characters? */
    return g_strdup_printf("%.80s...", name);
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
 * If @check_nul, require that no NUL bytes appear in buffer.
 * Return -errno on I/O error, 0 if option was completely handled by
 * sending a reply about inconsistent lengths, or 1 on success. */
static int nbd_opt_read(NBDClient *client, void *buffer, size_t size,
                        bool check_nul, Error **errp)
{
    if (size > client->optlen) {
        return nbd_opt_invalid(client, errp,
                               "Inconsistent lengths in option %s",
                               nbd_opt_lookup(client->opt));
    }
    client->optlen -= size;
    if (qio_channel_read_all(client->ioc, buffer, size, errp) < 0) {
        return -EIO;
    }

    if (check_nul && strnlen(buffer, size) != size) {
        return nbd_opt_invalid(client, errp,
                               "Unexpected embedded NUL in option %s",
                               nbd_opt_lookup(client->opt));
    }
    return 1;
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
    ret = nbd_opt_read(client, &len, sizeof(len), false, errp);
    if (ret <= 0) {
        return ret;
    }
    len = cpu_to_be32(len);

    if (len > NBD_MAX_STRING_SIZE) {
        return nbd_opt_invalid(client, errp,
                               "Invalid name length: %" PRIu32, len);
    }

    local_name = g_malloc(len + 1);
    ret = nbd_opt_read(client, local_name, len, true, errp);
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
    ERRP_GUARD();
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
    if (client->exp != client->export_meta.exp) {
        client->export_meta.count = 0;
    }
}

/* Send a reply to NBD_OPT_EXPORT_NAME.
 * Return -errno on error, 0 on success. */
static int nbd_negotiate_handle_export_name(NBDClient *client, bool no_zeroes,
                                            Error **errp)
{
    ERRP_GUARD();
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
    blk_exp_ref(&client->exp->common);
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
    uint32_t namelen = 0;
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

    rc = nbd_opt_read(client, &requests, sizeof(requests), false, errp);
    if (rc <= 0) {
        return rc;
    }
    requests = be16_to_cpu(requests);
    trace_nbd_negotiate_handle_info_requests(requests);
    while (requests--) {
        rc = nbd_opt_read(client, &request, sizeof(request), false, errp);
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
        g_autofree char *sane_name = nbd_sanitize_name(name);

        return nbd_negotiate_send_rep_err(client, NBD_REP_ERR_UNKNOWN,
                                          errp, "export '%s' not present",
                                          sane_name);
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
        check_align = sizes[0] = blk_get_request_alignment(exp->common.blk);
    } else {
        sizes[0] = 1;
    }
    assert(sizes[0] <= NBD_MAX_BUFFER_SIZE);
    /* preferred - Hard-code to 4096 for now.
     * TODO: is blk_bs(blk)->bl.opt_transfer appropriate? */
    sizes[1] = MAX(4096, sizes[0]);
    /* maximum - At most 32M, but smaller as appropriate. */
    sizes[2] = MIN(blk_get_max_transfer(exp->common.blk), NBD_MAX_BUFFER_SIZE);
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
        blk_get_request_alignment(exp->common.blk) > 1) {
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
        blk_exp_ref(&client->exp->common);
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

/*
 * Return true if @query matches @pattern, or if @query is empty when
 * the @client is performing _LIST_.
 */
static bool nbd_meta_empty_or_pattern(NBDClient *client, const char *pattern,
                                      const char *query)
{
    if (!*query) {
        trace_nbd_negotiate_meta_query_parse("empty");
        return client->opt == NBD_OPT_LIST_META_CONTEXT;
    }
    if (strcmp(query, pattern) == 0) {
        trace_nbd_negotiate_meta_query_parse(pattern);
        return true;
    }
    trace_nbd_negotiate_meta_query_skip("pattern not matched");
    return false;
}

/*
 * Return true and adjust @str in place if it begins with @prefix.
 */
static bool nbd_strshift(const char **str, const char *prefix)
{
    size_t len = strlen(prefix);

    if (strncmp(*str, prefix, len) == 0) {
        *str += len;
        return true;
    }
    return false;
}

/* nbd_meta_base_query
 *
 * Handle queries to 'base' namespace. For now, only the base:allocation
 * context is available.  Return true if @query has been handled.
 */
static bool nbd_meta_base_query(NBDClient *client, NBDExportMetaContexts *meta,
                                const char *query)
{
    if (!nbd_strshift(&query, "base:")) {
        return false;
    }
    trace_nbd_negotiate_meta_query_parse("base:");

    if (nbd_meta_empty_or_pattern(client, "allocation", query)) {
        meta->base_allocation = true;
    }
    return true;
}

/* nbd_meta_qemu_query
 *
 * Handle queries to 'qemu' namespace. For now, only the qemu:dirty-bitmap:
 * and qemu:allocation-depth contexts are available.  Return true if @query
 * has been handled.
 */
static bool nbd_meta_qemu_query(NBDClient *client, NBDExportMetaContexts *meta,
                                const char *query)
{
    size_t i;

    if (!nbd_strshift(&query, "qemu:")) {
        return false;
    }
    trace_nbd_negotiate_meta_query_parse("qemu:");

    if (!*query) {
        if (client->opt == NBD_OPT_LIST_META_CONTEXT) {
            meta->allocation_depth = meta->exp->allocation_depth;
            memset(meta->bitmaps, 1, meta->exp->nr_export_bitmaps);
        }
        trace_nbd_negotiate_meta_query_parse("empty");
        return true;
    }

    if (strcmp(query, "allocation-depth") == 0) {
        trace_nbd_negotiate_meta_query_parse("allocation-depth");
        meta->allocation_depth = meta->exp->allocation_depth;
        return true;
    }

    if (nbd_strshift(&query, "dirty-bitmap:")) {
        trace_nbd_negotiate_meta_query_parse("dirty-bitmap:");
        if (!*query) {
            if (client->opt == NBD_OPT_LIST_META_CONTEXT) {
                memset(meta->bitmaps, 1, meta->exp->nr_export_bitmaps);
            }
            trace_nbd_negotiate_meta_query_parse("empty");
            return true;
        }

        for (i = 0; i < meta->exp->nr_export_bitmaps; i++) {
            const char *bm_name;

            bm_name = bdrv_dirty_bitmap_name(meta->exp->export_bitmaps[i]);
            if (strcmp(bm_name, query) == 0) {
                meta->bitmaps[i] = true;
                trace_nbd_negotiate_meta_query_parse(query);
                return true;
            }
        }
        trace_nbd_negotiate_meta_query_skip("no dirty-bitmap match");
        return true;
    }

    trace_nbd_negotiate_meta_query_skip("unknown qemu context");
    return true;
}

/* nbd_negotiate_meta_query
 *
 * Parse namespace name and call corresponding function to parse body of the
 * query.
 *
 * The only supported namespaces are 'base' and 'qemu'.
 *
 * Return -errno on I/O error, 0 if option was completely handled by
 * sending a reply about inconsistent lengths, or 1 on success. */
static int nbd_negotiate_meta_query(NBDClient *client,
                                    NBDExportMetaContexts *meta, Error **errp)
{
    int ret;
    g_autofree char *query = NULL;
    uint32_t len;

    ret = nbd_opt_read(client, &len, sizeof(len), false, errp);
    if (ret <= 0) {
        return ret;
    }
    len = cpu_to_be32(len);

    if (len > NBD_MAX_STRING_SIZE) {
        trace_nbd_negotiate_meta_query_skip("length too long");
        return nbd_opt_skip(client, len, errp);
    }

    query = g_malloc(len + 1);
    ret = nbd_opt_read(client, query, len, true, errp);
    if (ret <= 0) {
        return ret;
    }
    query[len] = '\0';

    if (nbd_meta_base_query(client, meta, query)) {
        return 1;
    }
    if (nbd_meta_qemu_query(client, meta, query)) {
        return 1;
    }

    trace_nbd_negotiate_meta_query_skip("unknown namespace");
    return 1;
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
    g_autofree bool *bitmaps = NULL;
    NBDExportMetaContexts local_meta = {0};
    uint32_t nb_queries;
    size_t i;
    size_t count = 0;

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

    g_free(meta->bitmaps);
    memset(meta, 0, sizeof(*meta));

    ret = nbd_opt_read_name(client, &export_name, NULL, errp);
    if (ret <= 0) {
        return ret;
    }

    meta->exp = nbd_export_find(export_name);
    if (meta->exp == NULL) {
        g_autofree char *sane_name = nbd_sanitize_name(export_name);

        return nbd_opt_drop(client, NBD_REP_ERR_UNKNOWN, errp,
                            "export '%s' not present", sane_name);
    }
    meta->bitmaps = g_new0(bool, meta->exp->nr_export_bitmaps);
    if (client->opt == NBD_OPT_LIST_META_CONTEXT) {
        bitmaps = meta->bitmaps;
    }

    ret = nbd_opt_read(client, &nb_queries, sizeof(nb_queries), false, errp);
    if (ret <= 0) {
        return ret;
    }
    nb_queries = cpu_to_be32(nb_queries);
    trace_nbd_negotiate_meta_context(nbd_opt_lookup(client->opt),
                                     export_name, nb_queries);

    if (client->opt == NBD_OPT_LIST_META_CONTEXT && !nb_queries) {
        /* enable all known contexts */
        meta->base_allocation = true;
        meta->allocation_depth = meta->exp->allocation_depth;
        memset(meta->bitmaps, 1, meta->exp->nr_export_bitmaps);
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
        count++;
    }

    if (meta->allocation_depth) {
        ret = nbd_negotiate_send_meta_context(client, "qemu:allocation-depth",
                                              NBD_META_ID_ALLOCATION_DEPTH,
                                              errp);
        if (ret < 0) {
            return ret;
        }
        count++;
    }

    for (i = 0; i < meta->exp->nr_export_bitmaps; i++) {
        const char *bm_name;
        g_autofree char *context = NULL;

        if (!meta->bitmaps[i]) {
            continue;
        }

        bm_name = bdrv_dirty_bitmap_name(meta->exp->export_bitmaps[i]);
        context = g_strdup_printf("qemu:dirty-bitmap:%s", bm_name);

        ret = nbd_negotiate_send_meta_context(client, context,
                                              NBD_META_ID_DIRTY_BITMAP + i,
                                              errp);
        if (ret < 0) {
            return ret;
        }
        count++;
    }

    ret = nbd_negotiate_send_rep(client, NBD_REP_ACK, errp);
    if (ret == 0) {
        meta->count = count;
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
    ERRP_GUARD();
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
    if (client->exp && client->exp->common.ctx) {
        qio_channel_attach_aio_context(client->ioc, client->exp->common.ctx);
    }

    assert(!client->optlen);
    trace_nbd_negotiate_success();

    return 0;
}

/* nbd_read_eof
 * Tries to read @size bytes from @ioc. This is a local implementation of
 * qio_channel_readv_all_eof. We have it here because we need it to be
 * interruptible and to know when the coroutine is yielding.
 * Returns 1 on success
 *         0 on eof, when no data was read (errp is not set)
 *         negative errno on failure (errp is set)
 */
static inline int coroutine_fn
nbd_read_eof(NBDClient *client, void *buffer, size_t size, Error **errp)
{
    bool partial = false;

    assert(size);
    while (size > 0) {
        struct iovec iov = { .iov_base = buffer, .iov_len = size };
        ssize_t len;

        len = qio_channel_readv(client->ioc, &iov, 1, errp);
        if (len == QIO_CHANNEL_ERR_BLOCK) {
            client->read_yielding = true;
            qio_channel_yield(client->ioc, G_IO_IN);
            client->read_yielding = false;
            if (client->quiescing) {
                return -EAGAIN;
            }
            continue;
        } else if (len < 0) {
            return -EIO;
        } else if (len == 0) {
            if (partial) {
                error_setg(errp,
                           "Unexpected end-of-file before all bytes were read");
                return -EIO;
            } else {
                return 0;
            }
        }

        partial = true;
        size -= len;
        buffer = (uint8_t *) buffer + len;
    }
    return 1;
}

static int nbd_receive_request(NBDClient *client, NBDRequest *request,
                               Error **errp)
{
    uint8_t buf[NBD_REQUEST_SIZE];
    uint32_t magic;
    int ret;

    ret = nbd_read_eof(client, buf, sizeof(buf), errp);
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
            blk_exp_unref(&client->exp->common);
        }
        g_free(client->export_meta.bitmaps);
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

    exp->common.ctx = ctx;

    QTAILQ_FOREACH(client, &exp->clients, next) {
        qio_channel_attach_aio_context(client->ioc, ctx);

        assert(client->recv_coroutine == NULL);
        assert(client->send_coroutine == NULL);

        if (client->quiescing) {
            client->quiescing = false;
            nbd_client_receive_next_request(client);
        }
    }
}

static void nbd_aio_detach_bh(void *opaque)
{
    NBDExport *exp = opaque;
    NBDClient *client;

    QTAILQ_FOREACH(client, &exp->clients, next) {
        qio_channel_detach_aio_context(client->ioc);
        client->quiescing = true;

        if (client->recv_coroutine) {
            if (client->read_yielding) {
                qemu_aio_coroutine_enter(exp->common.ctx,
                                         client->recv_coroutine);
            } else {
                AIO_WAIT_WHILE(exp->common.ctx, client->recv_coroutine != NULL);
            }
        }

        if (client->send_coroutine) {
            AIO_WAIT_WHILE(exp->common.ctx, client->send_coroutine != NULL);
        }
    }
}

static void blk_aio_detach(void *opaque)
{
    NBDExport *exp = opaque;

    trace_nbd_blk_aio_detach(exp->name, exp->common.ctx);

    aio_wait_bh_oneshot(exp->common.ctx, nbd_aio_detach_bh, exp);

    exp->common.ctx = NULL;
}

static void nbd_eject_notifier(Notifier *n, void *data)
{
    NBDExport *exp = container_of(n, NBDExport, eject_notifier);

    blk_exp_request_shutdown(&exp->common);
}

void nbd_export_set_on_eject_blk(BlockExport *exp, BlockBackend *blk)
{
    NBDExport *nbd_exp = container_of(exp, NBDExport, common);
    assert(exp->drv == &blk_exp_nbd);
    assert(nbd_exp->eject_notifier_blk == NULL);

    blk_ref(blk);
    nbd_exp->eject_notifier_blk = blk;
    nbd_exp->eject_notifier.notify = nbd_eject_notifier;
    blk_add_remove_bs_notifier(blk, &nbd_exp->eject_notifier);
}

static int nbd_export_create(BlockExport *blk_exp, BlockExportOptions *exp_args,
                             Error **errp)
{
    NBDExport *exp = container_of(blk_exp, NBDExport, common);
    BlockExportOptionsNbd *arg = &exp_args->u.nbd;
    BlockBackend *blk = blk_exp->blk;
    int64_t size;
    uint64_t perm, shared_perm;
    bool readonly = !exp_args->writable;
    bool shared = !exp_args->writable;
    strList *bitmaps;
    size_t i;
    int ret;

    assert(exp_args->type == BLOCK_EXPORT_TYPE_NBD);

    if (!nbd_server_is_running()) {
        error_setg(errp, "NBD server not running");
        return -EINVAL;
    }

    if (!arg->has_name) {
        arg->name = exp_args->node_name;
    }

    if (strlen(arg->name) > NBD_MAX_STRING_SIZE) {
        error_setg(errp, "export name '%s' too long", arg->name);
        return -EINVAL;
    }

    if (arg->description && strlen(arg->description) > NBD_MAX_STRING_SIZE) {
        error_setg(errp, "description '%s' too long", arg->description);
        return -EINVAL;
    }

    if (nbd_export_find(arg->name)) {
        error_setg(errp, "NBD server already has export named '%s'", arg->name);
        return -EEXIST;
    }

    size = blk_getlength(blk);
    if (size < 0) {
        error_setg_errno(errp, -size,
                         "Failed to determine the NBD export's length");
        return size;
    }

    /* Don't allow resize while the NBD server is running, otherwise we don't
     * care what happens with the node. */
    blk_get_perm(blk, &perm, &shared_perm);
    ret = blk_set_perm(blk, perm, shared_perm & ~BLK_PERM_RESIZE, errp);
    if (ret < 0) {
        return ret;
    }

    QTAILQ_INIT(&exp->clients);
    exp->name = g_strdup(arg->name);
    exp->description = g_strdup(arg->description);
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
    exp->size = QEMU_ALIGN_DOWN(size, BDRV_SECTOR_SIZE);

    for (bitmaps = arg->bitmaps; bitmaps; bitmaps = bitmaps->next) {
        exp->nr_export_bitmaps++;
    }
    exp->export_bitmaps = g_new0(BdrvDirtyBitmap *, exp->nr_export_bitmaps);
    for (i = 0, bitmaps = arg->bitmaps; bitmaps;
         i++, bitmaps = bitmaps->next) {
        const char *bitmap = bitmaps->value;
        BlockDriverState *bs = blk_bs(blk);
        BdrvDirtyBitmap *bm = NULL;

        while (bs) {
            bm = bdrv_find_dirty_bitmap(bs, bitmap);
            if (bm != NULL) {
                break;
            }

            bs = bdrv_filter_or_cow_bs(bs);
        }

        if (bm == NULL) {
            ret = -ENOENT;
            error_setg(errp, "Bitmap '%s' is not found", bitmap);
            goto fail;
        }

        if (bdrv_dirty_bitmap_check(bm, BDRV_BITMAP_ALLOW_RO, errp)) {
            ret = -EINVAL;
            goto fail;
        }

        if (readonly && bdrv_is_writable(bs) &&
            bdrv_dirty_bitmap_enabled(bm)) {
            ret = -EINVAL;
            error_setg(errp,
                       "Enabled bitmap '%s' incompatible with readonly export",
                       bitmap);
            goto fail;
        }

        exp->export_bitmaps[i] = bm;
        assert(strlen(bitmap) <= BDRV_BITMAP_MAX_NAME_SIZE);
    }

    /* Mark bitmaps busy in a separate loop, to simplify roll-back concerns. */
    for (i = 0; i < exp->nr_export_bitmaps; i++) {
        bdrv_dirty_bitmap_set_busy(exp->export_bitmaps[i], true);
    }

    exp->allocation_depth = arg->allocation_depth;

    blk_add_aio_context_notifier(blk, blk_aio_attached, blk_aio_detach, exp);

    QTAILQ_INSERT_TAIL(&exports, exp, next);

    return 0;

fail:
    g_free(exp->export_bitmaps);
    g_free(exp->name);
    g_free(exp->description);
    return ret;
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
    return exp->common.ctx;
}

static void nbd_export_request_shutdown(BlockExport *blk_exp)
{
    NBDExport *exp = container_of(blk_exp, NBDExport, common);
    NBDClient *client, *next;

    blk_exp_ref(&exp->common);
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
        g_free(exp->name);
        exp->name = NULL;
        QTAILQ_REMOVE(&exports, exp, next);
    }
    blk_exp_unref(&exp->common);
}

static void nbd_export_delete(BlockExport *blk_exp)
{
    size_t i;
    NBDExport *exp = container_of(blk_exp, NBDExport, common);

    assert(exp->name == NULL);
    assert(QTAILQ_EMPTY(&exp->clients));

    g_free(exp->description);
    exp->description = NULL;

    if (exp->common.blk) {
        if (exp->eject_notifier_blk) {
            notifier_remove(&exp->eject_notifier);
            blk_unref(exp->eject_notifier_blk);
        }
        blk_remove_aio_context_notifier(exp->common.blk, blk_aio_attached,
                                        blk_aio_detach, exp);
    }

    for (i = 0; i < exp->nr_export_bitmaps; i++) {
        bdrv_dirty_bitmap_set_busy(exp->export_bitmaps[i], false);
    }
}

const BlockExportDriver blk_exp_nbd = {
    .type               = BLOCK_EXPORT_TYPE_NBD,
    .instance_size      = sizeof(NBDExport),
    .create             = nbd_export_create,
    .delete             = nbd_export_delete,
    .request_shutdown   = nbd_export_request_shutdown,
};

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
        int status = bdrv_block_status_above(blk_bs(exp->common.blk), NULL,
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
            ret = blk_pread(exp->common.blk, offset + progress,
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

typedef struct NBDExtentArray {
    NBDExtent *extents;
    unsigned int nb_alloc;
    unsigned int count;
    uint64_t total_length;
    bool can_add;
    bool converted_to_be;
} NBDExtentArray;

static NBDExtentArray *nbd_extent_array_new(unsigned int nb_alloc)
{
    NBDExtentArray *ea = g_new0(NBDExtentArray, 1);

    ea->nb_alloc = nb_alloc;
    ea->extents = g_new(NBDExtent, nb_alloc);
    ea->can_add = true;

    return ea;
}

static void nbd_extent_array_free(NBDExtentArray *ea)
{
    g_free(ea->extents);
    g_free(ea);
}
G_DEFINE_AUTOPTR_CLEANUP_FUNC(NBDExtentArray, nbd_extent_array_free);

/* Further modifications of the array after conversion are abandoned */
static void nbd_extent_array_convert_to_be(NBDExtentArray *ea)
{
    int i;

    assert(!ea->converted_to_be);
    ea->can_add = false;
    ea->converted_to_be = true;

    for (i = 0; i < ea->count; i++) {
        ea->extents[i].flags = cpu_to_be32(ea->extents[i].flags);
        ea->extents[i].length = cpu_to_be32(ea->extents[i].length);
    }
}

/*
 * Add extent to NBDExtentArray. If extent can't be added (no available space),
 * return -1.
 * For safety, when returning -1 for the first time, .can_add is set to false,
 * further call to nbd_extent_array_add() will crash.
 * (to avoid the situation, when after failing to add an extent (returned -1),
 * user miss this failure and add another extent, which is successfully added
 * (array is full, but new extent may be squashed into the last one), then we
 * have invalid array with skipped extent)
 */
static int nbd_extent_array_add(NBDExtentArray *ea,
                                uint32_t length, uint32_t flags)
{
    assert(ea->can_add);

    if (!length) {
        return 0;
    }

    /* Extend previous extent if flags are the same */
    if (ea->count > 0 && flags == ea->extents[ea->count - 1].flags) {
        uint64_t sum = (uint64_t)length + ea->extents[ea->count - 1].length;

        if (sum <= UINT32_MAX) {
            ea->extents[ea->count - 1].length = sum;
            ea->total_length += length;
            return 0;
        }
    }

    if (ea->count >= ea->nb_alloc) {
        ea->can_add = false;
        return -1;
    }

    ea->total_length += length;
    ea->extents[ea->count] = (NBDExtent) {.length = length, .flags = flags};
    ea->count++;

    return 0;
}

static int blockstatus_to_extents(BlockDriverState *bs, uint64_t offset,
                                  uint64_t bytes, NBDExtentArray *ea)
{
    while (bytes) {
        uint32_t flags;
        int64_t num;
        int ret = bdrv_block_status_above(bs, NULL, offset, bytes, &num,
                                          NULL, NULL);

        if (ret < 0) {
            return ret;
        }

        flags = (ret & BDRV_BLOCK_ALLOCATED ? 0 : NBD_STATE_HOLE) |
                (ret & BDRV_BLOCK_ZERO      ? NBD_STATE_ZERO : 0);

        if (nbd_extent_array_add(ea, num, flags) < 0) {
            return 0;
        }

        offset += num;
        bytes -= num;
    }

    return 0;
}

static int blockalloc_to_extents(BlockDriverState *bs, uint64_t offset,
                                 uint64_t bytes, NBDExtentArray *ea)
{
    while (bytes) {
        int64_t num;
        int ret = bdrv_is_allocated_above(bs, NULL, false, offset, bytes,
                                          &num);

        if (ret < 0) {
            return ret;
        }

        if (nbd_extent_array_add(ea, num, ret) < 0) {
            return 0;
        }

        offset += num;
        bytes -= num;
    }

    return 0;
}

/*
 * nbd_co_send_extents
 *
 * @ea is converted to BE by the function
 * @last controls whether NBD_REPLY_FLAG_DONE is sent.
 */
static int nbd_co_send_extents(NBDClient *client, uint64_t handle,
                               NBDExtentArray *ea,
                               bool last, uint32_t context_id, Error **errp)
{
    NBDStructuredMeta chunk;
    struct iovec iov[] = {
        {.iov_base = &chunk, .iov_len = sizeof(chunk)},
        {.iov_base = ea->extents, .iov_len = ea->count * sizeof(ea->extents[0])}
    };

    nbd_extent_array_convert_to_be(ea);

    trace_nbd_co_send_extents(handle, ea->count, context_id, ea->total_length,
                              last);
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
    g_autoptr(NBDExtentArray) ea = nbd_extent_array_new(nb_extents);

    if (context_id == NBD_META_ID_BASE_ALLOCATION) {
        ret = blockstatus_to_extents(bs, offset, length, ea);
    } else {
        ret = blockalloc_to_extents(bs, offset, length, ea);
    }
    if (ret < 0) {
        return nbd_co_send_structured_error(
                client, handle, -ret, "can't get block status", errp);
    }

    return nbd_co_send_extents(client, handle, ea, last, context_id, errp);
}

/* Populate @ea from a dirty bitmap. */
static void bitmap_to_extents(BdrvDirtyBitmap *bitmap,
                              uint64_t offset, uint64_t length,
                              NBDExtentArray *es)
{
    int64_t start, dirty_start, dirty_count;
    int64_t end = offset + length;
    bool full = false;

    bdrv_dirty_bitmap_lock(bitmap);

    for (start = offset;
         bdrv_dirty_bitmap_next_dirty_area(bitmap, start, end, INT32_MAX,
                                           &dirty_start, &dirty_count);
         start = dirty_start + dirty_count)
    {
        if ((nbd_extent_array_add(es, dirty_start - start, 0) < 0) ||
            (nbd_extent_array_add(es, dirty_count, NBD_STATE_DIRTY) < 0))
        {
            full = true;
            break;
        }
    }

    if (!full) {
        /* last non dirty extent, nothing to do if array is now full */
        (void) nbd_extent_array_add(es, end - start, 0);
    }

    bdrv_dirty_bitmap_unlock(bitmap);
}

static int nbd_co_send_bitmap(NBDClient *client, uint64_t handle,
                              BdrvDirtyBitmap *bitmap, uint64_t offset,
                              uint32_t length, bool dont_fragment, bool last,
                              uint32_t context_id, Error **errp)
{
    unsigned int nb_extents = dont_fragment ? 1 : NBD_MAX_BLOCK_STATUS_EXTENTS;
    g_autoptr(NBDExtentArray) ea = nbd_extent_array_new(nb_extents);

    bitmap_to_extents(bitmap, offset, length, ea);

    return nbd_co_send_extents(client, handle, ea, last, context_id, errp);
}

/* nbd_co_receive_request
 * Collect a client request. Return 0 if request looks valid, -EIO to drop
 * connection right away, -EAGAIN to indicate we were interrupted and the
 * channel should be quiesced, and any other negative value to report an error
 * to the client (although the caller may still need to disconnect after
 * reporting the error).
 */
static int nbd_co_receive_request(NBDRequestData *req, NBDRequest *request,
                                  Error **errp)
{
    NBDClient *client = req->client;
    int valid_flags;
    int ret;

    g_assert(qemu_in_coroutine());
    assert(client->recv_coroutine == qemu_coroutine_self());
    ret = nbd_receive_request(client, request, errp);
    if (ret < 0) {
        return  ret;
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
            req->data = blk_try_blockalign(client->exp->common.blk,
                                           request->len);
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
        ret = blk_co_flush(exp->common.blk);
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

    ret = blk_pread(exp->common.blk, request->from, data, request->len);
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

    ret = blk_co_preadv(exp->common.blk, request->from, request->len,
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
    size_t i;

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
        ret = blk_pwrite(exp->common.blk, request->from, data, request->len,
                         flags);
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
        ret = 0;
        /* FIXME simplify this when blk_pwrite_zeroes switches to 64-bit */
        while (ret >= 0 && request->len) {
            int align = client->check_align ?: 1;
            int len = MIN(request->len, QEMU_ALIGN_DOWN(BDRV_REQUEST_MAX_BYTES,
                                                        align));
            ret = blk_pwrite_zeroes(exp->common.blk, request->from, len, flags);
            request->len -= len;
            request->from += len;
        }
        return nbd_send_generic_reply(client, request->handle, ret,
                                      "writing to file failed", errp);

    case NBD_CMD_DISC:
        /* unreachable, thanks to special case in nbd_co_receive_request() */
        abort();

    case NBD_CMD_FLUSH:
        ret = blk_co_flush(exp->common.blk);
        return nbd_send_generic_reply(client, request->handle, ret,
                                      "flush failed", errp);

    case NBD_CMD_TRIM:
        ret = 0;
        /* FIXME simplify this when blk_co_pdiscard switches to 64-bit */
        while (ret >= 0 && request->len) {
            int align = client->check_align ?: 1;
            int len = MIN(request->len, QEMU_ALIGN_DOWN(BDRV_REQUEST_MAX_BYTES,
                                                        align));
            ret = blk_co_pdiscard(exp->common.blk, request->from, len);
            request->len -= len;
            request->from += len;
        }
        if (ret >= 0 && request->flags & NBD_CMD_FLAG_FUA) {
            ret = blk_co_flush(exp->common.blk);
        }
        return nbd_send_generic_reply(client, request->handle, ret,
                                      "discard failed", errp);

    case NBD_CMD_BLOCK_STATUS:
        if (!request->len) {
            return nbd_send_generic_reply(client, request->handle, -EINVAL,
                                          "need non-zero length", errp);
        }
        if (client->export_meta.count) {
            bool dont_fragment = request->flags & NBD_CMD_FLAG_REQ_ONE;
            int contexts_remaining = client->export_meta.count;

            if (client->export_meta.base_allocation) {
                ret = nbd_co_send_block_status(client, request->handle,
                                               blk_bs(exp->common.blk),
                                               request->from,
                                               request->len, dont_fragment,
                                               !--contexts_remaining,
                                               NBD_META_ID_BASE_ALLOCATION,
                                               errp);
                if (ret < 0) {
                    return ret;
                }
            }

            if (client->export_meta.allocation_depth) {
                ret = nbd_co_send_block_status(client, request->handle,
                                               blk_bs(exp->common.blk),
                                               request->from, request->len,
                                               dont_fragment,
                                               !--contexts_remaining,
                                               NBD_META_ID_ALLOCATION_DEPTH,
                                               errp);
                if (ret < 0) {
                    return ret;
                }
            }

            for (i = 0; i < client->exp->nr_export_bitmaps; i++) {
                if (!client->export_meta.bitmaps[i]) {
                    continue;
                }
                ret = nbd_co_send_bitmap(client, request->handle,
                                         client->exp->export_bitmaps[i],
                                         request->from, request->len,
                                         dont_fragment, !--contexts_remaining,
                                         NBD_META_ID_DIRTY_BITMAP + i, errp);
                if (ret < 0) {
                    return ret;
                }
            }

            assert(!contexts_remaining);

            return 0;
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

    if (client->quiescing) {
        /*
         * We're switching between AIO contexts. Don't attempt to receive a new
         * request and kick the main context which may be waiting for us.
         */
        nbd_client_put(client);
        client->recv_coroutine = NULL;
        aio_wait_kick();
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

    if (ret == -EAGAIN) {
        assert(client->quiescing);
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
    if (!client->recv_coroutine && client->nb_requests < MAX_NBD_REQUESTS &&
        !client->quiescing) {
        nbd_client_get(client);
        client->recv_coroutine = qemu_coroutine_create(nbd_trip, client);
        aio_co_schedule(client->exp->common.ctx, client->recv_coroutine);
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
