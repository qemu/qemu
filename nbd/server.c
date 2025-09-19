/*
 *  Copyright Red Hat
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

#include "block/block_int.h"
#include "block/export.h"
#include "block/dirty-bitmap.h"
#include "qapi/error.h"
#include "qemu/queue.h"
#include "trace.h"
#include "nbd-internal.h"
#include "qemu/units.h"
#include "qemu/memalign.h"

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

/*
 * NBDMetaContexts represents a list of meta contexts in use,
 * as selected by NBD_OPT_SET_META_CONTEXT. Also used for
 * NBD_OPT_LIST_META_CONTEXT.
 */
struct NBDMetaContexts {
    const NBDExport *exp; /* associated export */
    size_t count; /* number of negotiated contexts */
    bool base_allocation; /* export base:allocation context (block status) */
    bool allocation_depth; /* export qemu:allocation-depth */
    bool *bitmaps; /*
                    * export qemu:dirty-bitmap:<export bitmap name>,
                    * sized by exp->nr_export_bitmaps
                    */
};

struct NBDClient {
    int refcount; /* atomic */
    void (*close_fn)(NBDClient *client, bool negotiated);
    void *owner;

    QemuMutex lock;

    NBDExport *exp;
    QCryptoTLSCreds *tlscreds;
    char *tlsauthz;
    uint32_t handshake_max_secs;
    QIOChannelSocket *sioc; /* The underlying data channel */
    QIOChannel *ioc; /* The current I/O channel which may differ (eg TLS) */

    Coroutine *recv_coroutine; /* protected by lock */

    CoMutex send_lock;
    Coroutine *send_coroutine;

    bool read_yielding; /* protected by lock */
    bool quiescing; /* protected by lock */

    QTAILQ_ENTRY(NBDClient) next;
    int nb_requests; /* protected by lock */
    bool closing; /* protected by lock */

    uint32_t check_align; /* If non-zero, check for aligned client requests */

    NBDMode mode;
    NBDMetaContexts contexts; /* Negotiated meta contexts */

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
static coroutine_fn int
nbd_negotiate_send_rep_len(NBDClient *client, uint32_t type,
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
static coroutine_fn int
nbd_negotiate_send_rep(NBDClient *client, uint32_t type, Error **errp)
{
    return nbd_negotiate_send_rep_len(client, type, 0, errp);
}

/* Send an error reply.
 * Return -errno on error, 0 on success. */
static coroutine_fn int G_GNUC_PRINTF(4, 0)
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
static coroutine_fn int G_GNUC_PRINTF(4, 5)
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
static coroutine_fn int G_GNUC_PRINTF(4, 0)
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

static coroutine_fn int G_GNUC_PRINTF(4, 5)
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

static coroutine_fn int G_GNUC_PRINTF(3, 4)
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
static coroutine_fn int
nbd_opt_read(NBDClient *client, void *buffer, size_t size,
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
static coroutine_fn int
nbd_opt_skip(NBDClient *client, size_t size, Error **errp)
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
static coroutine_fn int
nbd_opt_read_name(NBDClient *client, char **name, uint32_t *length,
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
static coroutine_fn int
nbd_negotiate_send_rep_list(NBDClient *client, NBDExport *exp, Error **errp)
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
static coroutine_fn int
nbd_negotiate_handle_list(NBDClient *client, Error **errp)
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

static coroutine_fn void
nbd_check_meta_export(NBDClient *client, NBDExport *exp)
{
    if (exp != client->contexts.exp) {
        client->contexts.count = 0;
    }
}

/* Send a reply to NBD_OPT_EXPORT_NAME.
 * Return -errno on error, 0 on success. */
static coroutine_fn int
nbd_negotiate_handle_export_name(NBDClient *client, bool no_zeroes,
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
    if (client->mode >= NBD_MODE_EXTENDED) {
        error_setg(errp, "Extended headers already negotiated");
        return -EINVAL;
    }
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
    nbd_check_meta_export(client, client->exp);

    myflags = client->exp->nbdflags;
    if (client->mode >= NBD_MODE_STRUCTURED) {
        myflags |= NBD_FLAG_SEND_DF;
    }
    if (client->mode >= NBD_MODE_EXTENDED && client->contexts.count) {
        myflags |= NBD_FLAG_BLOCK_STAT_PAYLOAD;
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

    return 0;
}

/* Send a single NBD_REP_INFO, with a buffer @buf of @length bytes.
 * The buffer does NOT include the info type prefix.
 * Return -errno on error, 0 if ready to send more. */
static coroutine_fn int
nbd_negotiate_send_info(NBDClient *client, uint16_t info, uint32_t length,
                        void *buf, Error **errp)
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
static coroutine_fn int
nbd_reject_length(NBDClient *client, bool fatal, Error **errp)
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
static coroutine_fn int
nbd_negotiate_handle_info(NBDClient *client, Error **errp)
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
    if (client->opt == NBD_OPT_GO) {
        nbd_check_meta_export(client, exp);
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
    if (client->mode >= NBD_MODE_STRUCTURED) {
        myflags |= NBD_FLAG_SEND_DF;
    }
    if (client->mode >= NBD_MODE_EXTENDED &&
        (client->contexts.count || client->opt == NBD_OPT_INFO)) {
        myflags |= NBD_FLAG_BLOCK_STAT_PAYLOAD;
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
        rc = 1;
    }
    return rc;
}

/* Callback to learn when QIO TLS upgrade is complete */
struct NBDTLSServerHandshakeData {
    bool complete;
    Error *error;
    Coroutine *co;
};

static void
nbd_server_tls_handshake(QIOTask *task, void *opaque)
{
    struct NBDTLSServerHandshakeData *data = opaque;

    qio_task_propagate_error(task, &data->error);
    data->complete = true;
    if (!qemu_coroutine_entered(data->co)) {
        aio_co_wake(data->co);
    }
}

/* Handle NBD_OPT_STARTTLS. Return NULL to drop connection, or else the
 * new channel for all further (now-encrypted) communication. */
static coroutine_fn QIOChannel *
nbd_negotiate_handle_starttls(NBDClient *client, Error **errp)
{
    QIOChannel *ioc;
    QIOChannelTLS *tioc;
    struct NBDTLSServerHandshakeData data = { 0 };

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
    data.co = qemu_coroutine_self();
    qio_channel_tls_handshake(tioc,
                              nbd_server_tls_handshake,
                              &data,
                              NULL,
                              NULL);

    if (!data.complete) {
        qemu_coroutine_yield();
        assert(data.complete);
    }

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
static coroutine_fn int
nbd_negotiate_send_meta_context(NBDClient *client, const char *context,
                                uint32_t context_id, Error **errp)
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
static coroutine_fn bool
nbd_meta_empty_or_pattern(NBDClient *client, const char *pattern,
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
static coroutine_fn bool
nbd_strshift(const char **str, const char *prefix)
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
static coroutine_fn bool
nbd_meta_base_query(NBDClient *client, NBDMetaContexts *meta,
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
static coroutine_fn bool
nbd_meta_qemu_query(NBDClient *client, NBDMetaContexts *meta,
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
            if (meta->exp->nr_export_bitmaps) {
                memset(meta->bitmaps, 1, meta->exp->nr_export_bitmaps);
            }
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
            if (client->opt == NBD_OPT_LIST_META_CONTEXT &&
                meta->exp->nr_export_bitmaps) {
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
static coroutine_fn int
nbd_negotiate_meta_query(NBDClient *client,
                         NBDMetaContexts *meta, Error **errp)
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
static coroutine_fn int
nbd_negotiate_meta_queries(NBDClient *client, Error **errp)
{
    int ret;
    g_autofree char *export_name = NULL;
    /* Mark unused to work around https://bugs.llvm.org/show_bug.cgi?id=3888 */
    g_autofree G_GNUC_UNUSED bool *bitmaps = NULL;
    NBDMetaContexts local_meta = {0};
    NBDMetaContexts *meta;
    uint32_t nb_queries;
    size_t i;
    size_t count = 0;

    if (client->opt == NBD_OPT_SET_META_CONTEXT &&
        client->mode < NBD_MODE_STRUCTURED) {
        return nbd_opt_invalid(client, errp,
                               "request option '%s' when structured reply "
                               "is not negotiated",
                               nbd_opt_lookup(client->opt));
    }

    if (client->opt == NBD_OPT_LIST_META_CONTEXT) {
        /* Only change the caller's meta on SET. */
        meta = &local_meta;
    } else {
        meta = &client->contexts;
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
        if (meta->exp->nr_export_bitmaps) {
            memset(meta->bitmaps, 1, meta->exp->nr_export_bitmaps);
        }
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
 * 1       if client sent NBD_OPT_ABORT (i.e. on valid disconnect) or never
 *         wrote anything (i.e. port probe); errp is not set
 */
static coroutine_fn int
nbd_negotiate_options(NBDClient *client, Error **errp)
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

    /*
     * Intentionally ignore errors on this first read - we do not want
     * to be noisy about a mere port probe, but only for clients that
     * start talking the protocol and then quit abruptly.
     */
    if (nbd_read32(client->ioc, &flags, "flags", NULL) < 0) {
        return 1;
    }
    client->mode = NBD_MODE_EXPORT_NAME;
    trace_nbd_negotiate_options_flags(flags);
    if (flags & NBD_FLAG_C_FIXED_NEWSTYLE) {
        fixedNewstyle = true;
        flags &= ~NBD_FLAG_C_FIXED_NEWSTYLE;
        client->mode = NBD_MODE_SIMPLE;
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
            error_setg(errp, "len (%" PRIu32 ") is larger than max len (%u)",
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
                client->ioc = tioc;
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
                } else if (client->mode >= NBD_MODE_EXTENDED) {
                    ret = nbd_negotiate_send_rep_err(
                        client, NBD_REP_ERR_EXT_HEADER_REQD, errp,
                        "extended headers already negotiated");
                } else if (client->mode >= NBD_MODE_STRUCTURED) {
                    ret = nbd_negotiate_send_rep_err(
                        client, NBD_REP_ERR_INVALID, errp,
                        "structured reply already negotiated");
                } else {
                    ret = nbd_negotiate_send_rep(client, NBD_REP_ACK, errp);
                    client->mode = NBD_MODE_STRUCTURED;
                }
                break;

            case NBD_OPT_LIST_META_CONTEXT:
            case NBD_OPT_SET_META_CONTEXT:
                ret = nbd_negotiate_meta_queries(client, errp);
                break;

            case NBD_OPT_EXTENDED_HEADERS:
                if (length) {
                    ret = nbd_reject_length(client, false, errp);
                } else if (client->mode >= NBD_MODE_EXTENDED) {
                    ret = nbd_negotiate_send_rep_err(
                        client, NBD_REP_ERR_INVALID, errp,
                        "extended headers already negotiated");
                } else {
                    ret = nbd_negotiate_send_rep(client, NBD_REP_ACK, errp);
                    client->mode = NBD_MODE_EXTENDED;
                }
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
 * 1       if client sent NBD_OPT_ABORT (i.e. on valid disconnect) or never
 *         wrote anything (i.e. port probe); errp is not set
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

    if (!qio_channel_set_blocking(client->ioc, false, errp)) {
        return -EINVAL;
    }
    qio_channel_set_follow_coroutine_ctx(client->ioc, true);

    trace_nbd_negotiate_begin();
    memcpy(buf, "NBDMAGIC", 8);

    stq_be_p(buf + 8, NBD_OPTS_MAGIC);
    stw_be_p(buf + 16, NBD_FLAG_FIXED_NEWSTYLE | NBD_FLAG_NO_ZEROES);

    /*
     * Be silent about failure to write our greeting: there is nothing
     * wrong with a client testing if our port is alive.
     */
    if (nbd_write(client->ioc, buf, 18, NULL) < 0) {
        return 1;
    }
    ret = nbd_negotiate_options(client, errp);
    if (ret != 0) {
        if (ret < 0) {
            error_prepend(errp, "option negotiation failed: ");
        }
        return ret;
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
            WITH_QEMU_LOCK_GUARD(&client->lock) {
                client->read_yielding = true;

                /* Prompt main loop thread to re-run nbd_drained_poll() */
                aio_wait_kick();
            }
            qio_channel_yield(client->ioc, G_IO_IN);
            WITH_QEMU_LOCK_GUARD(&client->lock) {
                client->read_yielding = false;
                if (client->quiescing) {
                    return -EAGAIN;
                }
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

static int coroutine_fn nbd_receive_request(NBDClient *client, NBDRequest *request,
                                            Error **errp)
{
    uint8_t buf[NBD_EXTENDED_REQUEST_SIZE];
    uint32_t magic, expect;
    int ret;
    size_t size = client->mode >= NBD_MODE_EXTENDED ?
        NBD_EXTENDED_REQUEST_SIZE : NBD_REQUEST_SIZE;

    ret = nbd_read_eof(client, buf, size, errp);
    if (ret < 0) {
        return ret;
    }
    if (ret == 0) {
        return -EIO;
    }

    /*
     * Compact request
     *  [ 0 ..  3]   magic   (NBD_REQUEST_MAGIC)
     *  [ 4 ..  5]   flags   (NBD_CMD_FLAG_FUA, ...)
     *  [ 6 ..  7]   type    (NBD_CMD_READ, ...)
     *  [ 8 .. 15]   cookie
     *  [16 .. 23]   from
     *  [24 .. 27]   len
     * Extended request
     *  [ 0 ..  3]   magic   (NBD_EXTENDED_REQUEST_MAGIC)
     *  [ 4 ..  5]   flags   (NBD_CMD_FLAG_FUA, NBD_CMD_FLAG_PAYLOAD_LEN, ...)
     *  [ 6 ..  7]   type    (NBD_CMD_READ, ...)
     *  [ 8 .. 15]   cookie
     *  [16 .. 23]   from
     *  [24 .. 31]   len
     */

    magic = ldl_be_p(buf);
    request->flags  = lduw_be_p(buf + 4);
    request->type   = lduw_be_p(buf + 6);
    request->cookie = ldq_be_p(buf + 8);
    request->from   = ldq_be_p(buf + 16);
    if (client->mode >= NBD_MODE_EXTENDED) {
        request->len = ldq_be_p(buf + 24);
        expect = NBD_EXTENDED_REQUEST_MAGIC;
    } else {
        request->len = (uint32_t)ldl_be_p(buf + 24); /* widen 32 to 64 bits */
        expect = NBD_REQUEST_MAGIC;
    }

    trace_nbd_receive_request(magic, request->flags, request->type,
                              request->from, request->len);

    if (magic != expect) {
        error_setg(errp, "invalid magic (got 0x%" PRIx32 ", expected 0x%"
                   PRIx32 ")", magic, expect);
        return -EINVAL;
    }
    return 0;
}

#define MAX_NBD_REQUESTS 16

/* Runs in export AioContext and main loop thread */
void nbd_client_get(NBDClient *client)
{
    qatomic_inc(&client->refcount);
}

void nbd_client_put(NBDClient *client)
{
    assert(qemu_in_main_thread());

    if (qatomic_fetch_dec(&client->refcount) == 1) {
        /* The last reference should be dropped by client->close,
         * which is called by client_close.
         */
        assert(client->closing);

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
        g_free(client->contexts.bitmaps);
        qemu_mutex_destroy(&client->lock);
        g_free(client);
    }
}

/*
 * Tries to release the reference to @client, but only if other references
 * remain. This is an optimization for the common case where we want to avoid
 * the expense of scheduling nbd_client_put() in the main loop thread.
 *
 * Returns true upon success or false if the reference was not released because
 * it is the last reference.
 */
static bool nbd_client_put_nonzero(NBDClient *client)
{
    int old = qatomic_read(&client->refcount);
    int expected;

    do {
        if (old == 1) {
            return false;
        }

        expected = old;
        old = qatomic_cmpxchg(&client->refcount, expected, expected - 1);
    } while (old != expected);

    return true;
}

static void client_close(NBDClient *client, bool negotiated)
{
    assert(qemu_in_main_thread());

    WITH_QEMU_LOCK_GUARD(&client->lock) {
        if (client->closing) {
            return;
        }

        client->closing = true;
    }

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

/* Runs in export AioContext with client->lock held */
static NBDRequestData *nbd_request_get(NBDClient *client)
{
    NBDRequestData *req;

    assert(client->nb_requests <= MAX_NBD_REQUESTS - 1);
    client->nb_requests++;

    req = g_new0(NBDRequestData, 1);
    req->client = client;
    return req;
}

/* Runs in export AioContext with client->lock held */
static void nbd_request_put(NBDRequestData *req)
{
    NBDClient *client = req->client;

    if (req->data) {
        qemu_vfree(req->data);
    }
    g_free(req);

    client->nb_requests--;

    if (client->quiescing && client->nb_requests == 0) {
        aio_wait_kick();
    }

    nbd_client_receive_next_request(client);
}

static void blk_aio_attached(AioContext *ctx, void *opaque)
{
    NBDExport *exp = opaque;
    NBDClient *client;

    assert(qemu_in_main_thread());

    trace_nbd_blk_aio_attached(exp->name, ctx);

    exp->common.ctx = ctx;

    QTAILQ_FOREACH(client, &exp->clients, next) {
        WITH_QEMU_LOCK_GUARD(&client->lock) {
            assert(client->nb_requests == 0);
            assert(client->recv_coroutine == NULL);
            assert(client->send_coroutine == NULL);
        }
    }
}

static void blk_aio_detach(void *opaque)
{
    NBDExport *exp = opaque;

    assert(qemu_in_main_thread());

    trace_nbd_blk_aio_detach(exp->name, exp->common.ctx);

    exp->common.ctx = NULL;
}

static void nbd_drained_begin(void *opaque)
{
    NBDExport *exp = opaque;
    NBDClient *client;

    assert(qemu_in_main_thread());

    QTAILQ_FOREACH(client, &exp->clients, next) {
        WITH_QEMU_LOCK_GUARD(&client->lock) {
            client->quiescing = true;
        }
    }
}

static void nbd_drained_end(void *opaque)
{
    NBDExport *exp = opaque;
    NBDClient *client;

    assert(qemu_in_main_thread());

    QTAILQ_FOREACH(client, &exp->clients, next) {
        WITH_QEMU_LOCK_GUARD(&client->lock) {
            client->quiescing = false;
            nbd_client_receive_next_request(client);
        }
    }
}

/* Runs in export AioContext */
static void nbd_wake_read_bh(void *opaque)
{
    NBDClient *client = opaque;
    qio_channel_wake_read(client->ioc);
}

static bool nbd_drained_poll(void *opaque)
{
    NBDExport *exp = opaque;
    NBDClient *client;

    assert(qemu_in_main_thread());

    QTAILQ_FOREACH(client, &exp->clients, next) {
        WITH_QEMU_LOCK_GUARD(&client->lock) {
            if (client->nb_requests != 0) {
                /*
                 * If there's a coroutine waiting for a request on nbd_read_eof()
                 * enter it here so we don't depend on the client to wake it up.
                 *
                 * Schedule a BH in the export AioContext to avoid missing the
                 * wake up due to the race between qio_channel_wake_read() and
                 * qio_channel_yield().
                 */
                if (client->recv_coroutine != NULL && client->read_yielding) {
                    aio_bh_schedule_oneshot(nbd_export_aio_context(client->exp),
                                            nbd_wake_read_bh, client);
                }

                return true;
            }
        }
    }

    return false;
}

static void nbd_eject_notifier(Notifier *n, void *data)
{
    NBDExport *exp = container_of(n, NBDExport, eject_notifier);

    assert(qemu_in_main_thread());

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

static const BlockDevOps nbd_block_ops = {
    .drained_begin = nbd_drained_begin,
    .drained_end = nbd_drained_end,
    .drained_poll = nbd_drained_poll,
};

static int nbd_export_create(BlockExport *blk_exp, BlockExportOptions *exp_args,
                             Error **errp)
{
    NBDExport *exp = container_of(blk_exp, NBDExport, common);
    BlockExportOptionsNbd *arg = &exp_args->u.nbd;
    const char *name = arg->name ?: exp_args->node_name;
    BlockBackend *blk = blk_exp->blk;
    int64_t size;
    uint64_t perm, shared_perm;
    bool readonly = !exp_args->writable;
    BlockDirtyBitmapOrStrList *bitmaps;
    size_t i;
    int ret;

    GLOBAL_STATE_CODE();
    assert(exp_args->type == BLOCK_EXPORT_TYPE_NBD);

    if (!nbd_server_is_running()) {
        error_setg(errp, "NBD server not running");
        return -EINVAL;
    }

    if (strlen(name) > NBD_MAX_STRING_SIZE) {
        error_setg(errp, "export name '%s' too long", name);
        return -EINVAL;
    }

    if (arg->description && strlen(arg->description) > NBD_MAX_STRING_SIZE) {
        error_setg(errp, "description '%s' too long", arg->description);
        return -EINVAL;
    }

    if (nbd_export_find(name)) {
        error_setg(errp, "NBD server already has export named '%s'", name);
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
    exp->name = g_strdup(name);
    exp->description = g_strdup(arg->description);
    exp->nbdflags = (NBD_FLAG_HAS_FLAGS | NBD_FLAG_SEND_FLUSH |
                     NBD_FLAG_SEND_FUA | NBD_FLAG_SEND_CACHE);

    if (nbd_server_max_connections() != 1) {
        exp->nbdflags |= NBD_FLAG_CAN_MULTI_CONN;
    }
    if (readonly) {
        exp->nbdflags |= NBD_FLAG_READ_ONLY;
    } else {
        exp->nbdflags |= (NBD_FLAG_SEND_TRIM | NBD_FLAG_SEND_WRITE_ZEROES |
                          NBD_FLAG_SEND_FAST_ZERO);
    }
    exp->size = QEMU_ALIGN_DOWN(size, BDRV_SECTOR_SIZE);

    bdrv_graph_rdlock_main_loop();

    for (bitmaps = arg->bitmaps; bitmaps; bitmaps = bitmaps->next) {
        exp->nr_export_bitmaps++;
    }
    exp->export_bitmaps = g_new0(BdrvDirtyBitmap *, exp->nr_export_bitmaps);
    for (i = 0, bitmaps = arg->bitmaps; bitmaps;
         i++, bitmaps = bitmaps->next)
    {
        const char *bitmap;
        BlockDriverState *bs = blk_bs(blk);
        BdrvDirtyBitmap *bm = NULL;

        switch (bitmaps->value->type) {
        case QTYPE_QSTRING:
            bitmap = bitmaps->value->u.local;
            while (bs) {
                bm = bdrv_find_dirty_bitmap(bs, bitmap);
                if (bm != NULL) {
                    break;
                }

                bs = bdrv_filter_or_cow_bs(bs);
            }

            if (bm == NULL) {
                ret = -ENOENT;
                error_setg(errp, "Bitmap '%s' is not found",
                           bitmaps->value->u.local);
                goto fail;
            }

            if (readonly && bdrv_is_writable(bs) &&
                bdrv_dirty_bitmap_enabled(bm)) {
                ret = -EINVAL;
                error_setg(errp, "Enabled bitmap '%s' incompatible with "
                           "readonly export", bitmap);
                goto fail;
            }
            break;
        case QTYPE_QDICT:
            bitmap = bitmaps->value->u.external.name;
            bm = block_dirty_bitmap_lookup(bitmaps->value->u.external.node,
                                           bitmap, NULL, errp);
            if (!bm) {
                ret = -ENOENT;
                goto fail;
            }
            break;
        default:
            abort();
        }

        assert(bm);

        if (bdrv_dirty_bitmap_check(bm, BDRV_BITMAP_ALLOW_RO, errp)) {
            ret = -EINVAL;
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

    /*
     * We need to inhibit request queuing in the block layer to ensure we can
     * be properly quiesced when entering a drained section, as our coroutines
     * servicing pending requests might enter blk_pread().
     */
    blk_set_disable_request_queuing(blk, true);

    blk_add_aio_context_notifier(blk, blk_aio_attached, blk_aio_detach, exp);

    blk_set_dev_ops(blk, &nbd_block_ops, exp);

    QTAILQ_INSERT_TAIL(&exports, exp, next);

    bdrv_graph_rdunlock_main_loop();

    return 0;

fail:
    bdrv_graph_rdunlock_main_loop();
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
     * TODO: Should we expand QMP BlockExportRemoveMode enum to allow a
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

    if (exp->eject_notifier_blk) {
        notifier_remove(&exp->eject_notifier);
        blk_unref(exp->eject_notifier_blk);
    }
    blk_remove_aio_context_notifier(exp->common.blk, blk_aio_attached,
                                    blk_aio_detach, exp);
    blk_set_disable_request_queuing(exp->common.blk, false);

    for (i = 0; i < exp->nr_export_bitmaps; i++) {
        bdrv_dirty_bitmap_set_busy(exp->export_bitmaps[i], false);
    }
}

const BlockExportDriver blk_exp_nbd = {
    .type               = BLOCK_EXPORT_TYPE_NBD,
    .instance_size      = sizeof(NBDExport),
    .supports_inactive  = true,
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
                                       uint64_t cookie)
{
    stl_be_p(&reply->magic, NBD_SIMPLE_REPLY_MAGIC);
    stl_be_p(&reply->error, error);
    stq_be_p(&reply->cookie, cookie);
}

static int coroutine_fn nbd_co_send_simple_reply(NBDClient *client,
                                                 NBDRequest *request,
                                                 uint32_t error,
                                                 void *data,
                                                 uint64_t len,
                                                 Error **errp)
{
    NBDSimpleReply reply;
    int nbd_err = system_errno_to_nbd_errno(error);
    struct iovec iov[] = {
        {.iov_base = &reply, .iov_len = sizeof(reply)},
        {.iov_base = data, .iov_len = len}
    };

    assert(!len || !nbd_err);
    assert(len <= NBD_MAX_BUFFER_SIZE);
    assert(client->mode < NBD_MODE_STRUCTURED ||
           (client->mode == NBD_MODE_STRUCTURED &&
            request->type != NBD_CMD_READ));
    trace_nbd_co_send_simple_reply(request->cookie, nbd_err,
                                   nbd_err_lookup(nbd_err), len);
    set_be_simple_reply(&reply, nbd_err, request->cookie);

    return nbd_co_send_iov(client, iov, 2, errp);
}

/*
 * Prepare the header of a reply chunk for network transmission.
 *
 * On input, @iov is partially initialized: iov[0].iov_base must point
 * to an uninitialized NBDReply, while the remaining @niov elements
 * (if any) must be ready for transmission.  This function then
 * populates iov[0] for transmission.
 */
static inline void set_be_chunk(NBDClient *client, struct iovec *iov,
                                size_t niov, uint16_t flags, uint16_t type,
                                NBDRequest *request)
{
    size_t i, length = 0;

    for (i = 1; i < niov; i++) {
        length += iov[i].iov_len;
    }
    assert(length <= NBD_MAX_BUFFER_SIZE + sizeof(NBDStructuredReadData));

    if (client->mode >= NBD_MODE_EXTENDED) {
        NBDExtendedReplyChunk *chunk = iov->iov_base;

        iov[0].iov_len = sizeof(*chunk);
        stl_be_p(&chunk->magic, NBD_EXTENDED_REPLY_MAGIC);
        stw_be_p(&chunk->flags, flags);
        stw_be_p(&chunk->type, type);
        stq_be_p(&chunk->cookie, request->cookie);
        stq_be_p(&chunk->offset, request->from);
        stq_be_p(&chunk->length, length);
    } else {
        NBDStructuredReplyChunk *chunk = iov->iov_base;

        iov[0].iov_len = sizeof(*chunk);
        stl_be_p(&chunk->magic, NBD_STRUCTURED_REPLY_MAGIC);
        stw_be_p(&chunk->flags, flags);
        stw_be_p(&chunk->type, type);
        stq_be_p(&chunk->cookie, request->cookie);
        stl_be_p(&chunk->length, length);
    }
}

static int coroutine_fn nbd_co_send_chunk_done(NBDClient *client,
                                               NBDRequest *request,
                                               Error **errp)
{
    NBDReply hdr;
    struct iovec iov[] = {
        {.iov_base = &hdr},
    };

    trace_nbd_co_send_chunk_done(request->cookie);
    set_be_chunk(client, iov, 1, NBD_REPLY_FLAG_DONE,
                 NBD_REPLY_TYPE_NONE, request);
    return nbd_co_send_iov(client, iov, 1, errp);
}

static int coroutine_fn nbd_co_send_chunk_read(NBDClient *client,
                                               NBDRequest *request,
                                               uint64_t offset,
                                               void *data,
                                               uint64_t size,
                                               bool final,
                                               Error **errp)
{
    NBDReply hdr;
    NBDStructuredReadData chunk;
    struct iovec iov[] = {
        {.iov_base = &hdr},
        {.iov_base = &chunk, .iov_len = sizeof(chunk)},
        {.iov_base = data, .iov_len = size}
    };

    assert(size && size <= NBD_MAX_BUFFER_SIZE);
    trace_nbd_co_send_chunk_read(request->cookie, offset, data, size);
    set_be_chunk(client, iov, 3, final ? NBD_REPLY_FLAG_DONE : 0,
                 NBD_REPLY_TYPE_OFFSET_DATA, request);
    stq_be_p(&chunk.offset, offset);

    return nbd_co_send_iov(client, iov, 3, errp);
}

static int coroutine_fn nbd_co_send_chunk_error(NBDClient *client,
                                                NBDRequest *request,
                                                uint32_t error,
                                                const char *msg,
                                                Error **errp)
{
    NBDReply hdr;
    NBDStructuredError chunk;
    int nbd_err = system_errno_to_nbd_errno(error);
    struct iovec iov[] = {
        {.iov_base = &hdr},
        {.iov_base = &chunk, .iov_len = sizeof(chunk)},
        {.iov_base = (char *)msg, .iov_len = msg ? strlen(msg) : 0},
    };

    assert(nbd_err);
    trace_nbd_co_send_chunk_error(request->cookie, nbd_err,
                                  nbd_err_lookup(nbd_err), msg ? msg : "");
    set_be_chunk(client, iov, 3, NBD_REPLY_FLAG_DONE,
                 NBD_REPLY_TYPE_ERROR, request);
    stl_be_p(&chunk.error, nbd_err);
    stw_be_p(&chunk.message_length, iov[2].iov_len);

    return nbd_co_send_iov(client, iov, 3, errp);
}

/* Do a sparse read and send the structured reply to the client.
 * Returns -errno if sending fails. blk_co_block_status_above() failure is
 * reported to the client, at which point this function succeeds.
 */
static int coroutine_fn nbd_co_send_sparse_read(NBDClient *client,
                                                NBDRequest *request,
                                                uint64_t offset,
                                                uint8_t *data,
                                                uint64_t size,
                                                Error **errp)
{
    int ret = 0;
    NBDExport *exp = client->exp;
    size_t progress = 0;

    assert(size <= NBD_MAX_BUFFER_SIZE);
    while (progress < size) {
        int64_t pnum;
        int status = blk_co_block_status_above(exp->common.blk, NULL,
                                               offset + progress,
                                               size - progress, &pnum, NULL,
                                               NULL);
        bool final;

        if (status < 0) {
            char *msg = g_strdup_printf("unable to check for holes: %s",
                                        strerror(-status));

            ret = nbd_co_send_chunk_error(client, request, -status, msg, errp);
            g_free(msg);
            return ret;
        }
        assert(pnum && pnum <= size - progress);
        final = progress + pnum == size;
        if (status & BDRV_BLOCK_ZERO) {
            NBDReply hdr;
            NBDStructuredReadHole chunk;
            struct iovec iov[] = {
                {.iov_base = &hdr},
                {.iov_base = &chunk, .iov_len = sizeof(chunk)},
            };

            trace_nbd_co_send_chunk_read_hole(request->cookie,
                                              offset + progress, pnum);
            set_be_chunk(client, iov, 2,
                         final ? NBD_REPLY_FLAG_DONE : 0,
                         NBD_REPLY_TYPE_OFFSET_HOLE, request);
            stq_be_p(&chunk.offset, offset + progress);
            stl_be_p(&chunk.length, pnum);
            ret = nbd_co_send_iov(client, iov, 2, errp);
        } else {
            ret = blk_co_pread(exp->common.blk, offset + progress, pnum,
                               data + progress, 0);
            if (ret < 0) {
                error_setg_errno(errp, -ret, "reading from file failed");
                break;
            }
            ret = nbd_co_send_chunk_read(client, request, offset + progress,
                                         data + progress, pnum, final, errp);
        }

        if (ret < 0) {
            break;
        }
        progress += pnum;
    }
    return ret;
}

typedef struct NBDExtentArray {
    NBDExtent64 *extents;
    unsigned int nb_alloc;
    unsigned int count;
    uint64_t total_length;
    bool extended;
    bool can_add;
    bool converted_to_be;
} NBDExtentArray;

static NBDExtentArray *nbd_extent_array_new(unsigned int nb_alloc,
                                            NBDMode mode)
{
    NBDExtentArray *ea = g_new0(NBDExtentArray, 1);

    assert(mode >= NBD_MODE_STRUCTURED);
    ea->nb_alloc = nb_alloc;
    ea->extents = g_new(NBDExtent64, nb_alloc);
    ea->extended = mode >= NBD_MODE_EXTENDED;
    ea->can_add = true;

    return ea;
}

static void nbd_extent_array_free(NBDExtentArray *ea)
{
    g_free(ea->extents);
    g_free(ea);
}
G_DEFINE_AUTOPTR_CLEANUP_FUNC(NBDExtentArray, nbd_extent_array_free)

/* Further modifications of the array after conversion are abandoned */
static void nbd_extent_array_convert_to_be(NBDExtentArray *ea)
{
    int i;

    assert(!ea->converted_to_be);
    assert(ea->extended);
    ea->can_add = false;
    ea->converted_to_be = true;

    for (i = 0; i < ea->count; i++) {
        ea->extents[i].length = cpu_to_be64(ea->extents[i].length);
        ea->extents[i].flags = cpu_to_be64(ea->extents[i].flags);
    }
}

/* Further modifications of the array after conversion are abandoned */
static NBDExtent32 *nbd_extent_array_convert_to_narrow(NBDExtentArray *ea)
{
    int i;
    NBDExtent32 *extents = g_new(NBDExtent32, ea->count);

    assert(!ea->converted_to_be);
    assert(!ea->extended);
    ea->can_add = false;
    ea->converted_to_be = true;

    for (i = 0; i < ea->count; i++) {
        assert((ea->extents[i].length | ea->extents[i].flags) <= UINT32_MAX);
        extents[i].length = cpu_to_be32(ea->extents[i].length);
        extents[i].flags = cpu_to_be32(ea->extents[i].flags);
    }

    return extents;
}

/*
 * Add extent to NBDExtentArray. If extent can't be added (no available space),
 * return -1.
 * For safety, when returning -1 for the first time, .can_add is set to false,
 * and further calls to nbd_extent_array_add() will crash.
 * (this avoids the situation where a caller ignores failure to add one extent,
 * where adding another extent that would squash into the last array entry
 * would result in an incorrect range reported to the client)
 */
static int nbd_extent_array_add(NBDExtentArray *ea,
                                uint64_t length, uint32_t flags)
{
    assert(ea->can_add);

    if (!length) {
        return 0;
    }
    if (!ea->extended) {
        assert(length <= UINT32_MAX);
    }

    /* Extend previous extent if flags are the same */
    if (ea->count > 0 && flags == ea->extents[ea->count - 1].flags) {
        uint64_t sum = length + ea->extents[ea->count - 1].length;

        /*
         * sum cannot overflow: the block layer bounds image size at
         * 2^63, and ea->extents[].length comes from the block layer.
         */
        assert(sum >= length);
        if (sum <= UINT32_MAX || ea->extended) {
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
    ea->extents[ea->count] = (NBDExtent64) {.length = length, .flags = flags};
    ea->count++;

    return 0;
}

static int coroutine_fn blockstatus_to_extents(BlockBackend *blk,
                                               uint64_t offset, uint64_t bytes,
                                               NBDExtentArray *ea)
{
    while (bytes) {
        uint32_t flags;
        int64_t num;
        int ret = blk_co_block_status_above(blk, NULL, offset, bytes, &num,
                                            NULL, NULL);

        if (ret < 0) {
            return ret;
        }

        flags = (ret & BDRV_BLOCK_DATA ? 0 : NBD_STATE_HOLE) |
                (ret & BDRV_BLOCK_ZERO ? NBD_STATE_ZERO : 0);

        if (nbd_extent_array_add(ea, num, flags) < 0) {
            return 0;
        }

        offset += num;
        bytes -= num;
    }

    return 0;
}

static int coroutine_fn blockalloc_to_extents(BlockBackend *blk,
                                              uint64_t offset, uint64_t bytes,
                                              NBDExtentArray *ea)
{
    while (bytes) {
        int64_t num;
        int ret = blk_co_is_allocated_above(blk, NULL, false, offset, bytes,
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
static int coroutine_fn
nbd_co_send_extents(NBDClient *client, NBDRequest *request, NBDExtentArray *ea,
                    bool last, uint32_t context_id, Error **errp)
{
    NBDReply hdr;
    NBDStructuredMeta meta;
    NBDExtendedMeta meta_ext;
    g_autofree NBDExtent32 *extents = NULL;
    uint16_t type;
    struct iovec iov[] = { {.iov_base = &hdr}, {0}, {0} };

    if (client->mode >= NBD_MODE_EXTENDED) {
        type = NBD_REPLY_TYPE_BLOCK_STATUS_EXT;

        iov[1].iov_base = &meta_ext;
        iov[1].iov_len = sizeof(meta_ext);
        stl_be_p(&meta_ext.context_id, context_id);
        stl_be_p(&meta_ext.count, ea->count);

        nbd_extent_array_convert_to_be(ea);
        iov[2].iov_base = ea->extents;
        iov[2].iov_len = ea->count * sizeof(ea->extents[0]);
    } else {
        type = NBD_REPLY_TYPE_BLOCK_STATUS;

        iov[1].iov_base = &meta;
        iov[1].iov_len = sizeof(meta);
        stl_be_p(&meta.context_id, context_id);

        extents = nbd_extent_array_convert_to_narrow(ea);
        iov[2].iov_base = extents;
        iov[2].iov_len = ea->count * sizeof(extents[0]);
    }

    trace_nbd_co_send_extents(request->cookie, ea->count, context_id,
                              ea->total_length, last);
    set_be_chunk(client, iov, 3, last ? NBD_REPLY_FLAG_DONE : 0, type,
                 request);

    return nbd_co_send_iov(client, iov, 3, errp);
}

/* Get block status from the exported device and send it to the client */
static int
coroutine_fn nbd_co_send_block_status(NBDClient *client, NBDRequest *request,
                                      BlockBackend *blk, uint64_t offset,
                                      uint64_t length, bool dont_fragment,
                                      bool last, uint32_t context_id,
                                      Error **errp)
{
    int ret;
    unsigned int nb_extents = dont_fragment ? 1 : NBD_MAX_BLOCK_STATUS_EXTENTS;
    g_autoptr(NBDExtentArray) ea =
        nbd_extent_array_new(nb_extents, client->mode);

    if (context_id == NBD_META_ID_BASE_ALLOCATION) {
        ret = blockstatus_to_extents(blk, offset, length, ea);
    } else {
        ret = blockalloc_to_extents(blk, offset, length, ea);
    }
    if (ret < 0) {
        return nbd_co_send_chunk_error(client, request, -ret,
                                       "can't get block status", errp);
    }

    return nbd_co_send_extents(client, request, ea, last, context_id, errp);
}

/* Populate @ea from a dirty bitmap. */
static void bitmap_to_extents(BdrvDirtyBitmap *bitmap,
                              uint64_t offset, uint64_t length,
                              NBDExtentArray *es)
{
    int64_t start, dirty_start, dirty_count;
    int64_t end = offset + length;
    bool full = false;
    int64_t bound = es->extended ? INT64_MAX : INT32_MAX;

    bdrv_dirty_bitmap_lock(bitmap);

    for (start = offset;
         bdrv_dirty_bitmap_next_dirty_area(bitmap, start, end, bound,
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

static int coroutine_fn nbd_co_send_bitmap(NBDClient *client,
                                           NBDRequest *request,
                                           BdrvDirtyBitmap *bitmap,
                                           uint64_t offset,
                                           uint64_t length, bool dont_fragment,
                                           bool last, uint32_t context_id,
                                           Error **errp)
{
    unsigned int nb_extents = dont_fragment ? 1 : NBD_MAX_BLOCK_STATUS_EXTENTS;
    g_autoptr(NBDExtentArray) ea =
        nbd_extent_array_new(nb_extents, client->mode);

    bitmap_to_extents(bitmap, offset, length, ea);

    return nbd_co_send_extents(client, request, ea, last, context_id, errp);
}

/*
 * nbd_co_block_status_payload_read
 * Called when a client wants a subset of negotiated contexts via a
 * BLOCK_STATUS payload.  Check the payload for valid length and
 * contents.  On success, return 0 with request updated to effective
 * length.  If request was invalid but all payload consumed, return 0
 * with request->len and request->contexts->count set to 0 (which will
 * trigger an appropriate NBD_EINVAL response later on).  Return
 * negative errno if the payload was not fully consumed.
 */
static int
nbd_co_block_status_payload_read(NBDClient *client, NBDRequest *request,
                                 Error **errp)
{
    uint64_t payload_len = request->len;
    g_autofree char *buf = NULL;
    size_t count, i, nr_bitmaps;
    uint32_t id;

    if (payload_len > NBD_MAX_BUFFER_SIZE) {
        error_setg(errp, "len (%" PRIu64 ") is larger than max len (%u)",
                   request->len, NBD_MAX_BUFFER_SIZE);
        return -EINVAL;
    }

    assert(client->contexts.exp == client->exp);
    nr_bitmaps = client->exp->nr_export_bitmaps;
    request->contexts = g_new0(NBDMetaContexts, 1);
    request->contexts->exp = client->exp;

    if (payload_len % sizeof(uint32_t) ||
        payload_len < sizeof(NBDBlockStatusPayload) ||
        payload_len > (sizeof(NBDBlockStatusPayload) +
                       sizeof(id) * client->contexts.count)) {
        goto skip;
    }

    buf = g_malloc(payload_len);
    if (nbd_read(client->ioc, buf, payload_len,
                 "CMD_BLOCK_STATUS data", errp) < 0) {
        return -EIO;
    }
    trace_nbd_co_receive_request_payload_received(request->cookie,
                                                  payload_len);
    request->contexts->bitmaps = g_new0(bool, nr_bitmaps);
    count = (payload_len - sizeof(NBDBlockStatusPayload)) / sizeof(id);
    payload_len = 0;

    for (i = 0; i < count; i++) {
        id = ldl_be_p(buf + sizeof(NBDBlockStatusPayload) + sizeof(id) * i);
        if (id == NBD_META_ID_BASE_ALLOCATION) {
            if (!client->contexts.base_allocation ||
                request->contexts->base_allocation) {
                goto skip;
            }
            request->contexts->base_allocation = true;
        } else if (id == NBD_META_ID_ALLOCATION_DEPTH) {
            if (!client->contexts.allocation_depth ||
                request->contexts->allocation_depth) {
                goto skip;
            }
            request->contexts->allocation_depth = true;
        } else {
            unsigned idx = id - NBD_META_ID_DIRTY_BITMAP;

            if (idx >= nr_bitmaps || !client->contexts.bitmaps[idx] ||
                request->contexts->bitmaps[idx]) {
                goto skip;
            }
            request->contexts->bitmaps[idx] = true;
        }
    }

    request->len = ldq_be_p(buf);
    request->contexts->count = count;
    return 0;

 skip:
    trace_nbd_co_receive_block_status_payload_compliance(request->from,
                                                         request->len);
    request->len = request->contexts->count = 0;
    return nbd_drop(client->ioc, payload_len, errp);
}

/* nbd_co_receive_request
 * Collect a client request. Return 0 if request looks valid, -EIO to drop
 * connection right away, -EAGAIN to indicate we were interrupted and the
 * channel should be quiesced, and any other negative value to report an error
 * to the client (although the caller may still need to disconnect after
 * reporting the error).
 */
static int coroutine_fn nbd_co_receive_request(NBDRequestData *req,
                                               NBDRequest *request,
                                               Error **errp)
{
    NBDClient *client = req->client;
    bool extended_with_payload;
    bool check_length = false;
    bool check_rofs = false;
    bool allocate_buffer = false;
    bool payload_okay = false;
    uint64_t payload_len = 0;
    int valid_flags = NBD_CMD_FLAG_FUA;
    int ret;

    g_assert(qemu_in_coroutine());
    ret = nbd_receive_request(client, request, errp);
    if (ret < 0) {
        return ret;
    }

    trace_nbd_co_receive_request_decode_type(request->cookie, request->type,
                                             nbd_cmd_lookup(request->type));
    extended_with_payload = client->mode >= NBD_MODE_EXTENDED &&
        request->flags & NBD_CMD_FLAG_PAYLOAD_LEN;
    if (extended_with_payload) {
        payload_len = request->len;
        check_length = true;
    }

    switch (request->type) {
    case NBD_CMD_DISC:
        /* Special case: we're going to disconnect without a reply,
         * whether or not flags, from, or len are bogus */
        req->complete = true;
        return -EIO;

    case NBD_CMD_READ:
        if (client->mode >= NBD_MODE_STRUCTURED) {
            valid_flags |= NBD_CMD_FLAG_DF;
        }
        check_length = true;
        allocate_buffer = true;
        break;

    case NBD_CMD_WRITE:
        if (client->mode >= NBD_MODE_EXTENDED) {
            if (!extended_with_payload) {
                /* The client is noncompliant. Trace it, but proceed. */
                trace_nbd_co_receive_ext_payload_compliance(request->from,
                                                            request->len);
            }
            valid_flags |= NBD_CMD_FLAG_PAYLOAD_LEN;
        }
        payload_okay = true;
        payload_len = request->len;
        check_length = true;
        allocate_buffer = true;
        check_rofs = true;
        break;

    case NBD_CMD_FLUSH:
        break;

    case NBD_CMD_TRIM:
        check_rofs = true;
        break;

    case NBD_CMD_CACHE:
        check_length = true;
        break;

    case NBD_CMD_WRITE_ZEROES:
        valid_flags |= NBD_CMD_FLAG_NO_HOLE | NBD_CMD_FLAG_FAST_ZERO;
        check_rofs = true;
        break;

    case NBD_CMD_BLOCK_STATUS:
        if (extended_with_payload) {
            ret = nbd_co_block_status_payload_read(client, request, errp);
            if (ret < 0) {
                return ret;
            }
            /* payload now consumed */
            check_length = false;
            payload_len = 0;
            valid_flags |= NBD_CMD_FLAG_PAYLOAD_LEN;
        } else {
            request->contexts = &client->contexts;
        }
        valid_flags |= NBD_CMD_FLAG_REQ_ONE;
        break;

    default:
        /* Unrecognized, will fail later */
        ;
    }

    /* Payload and buffer handling. */
    if (!payload_len) {
        req->complete = true;
    }
    if (check_length && request->len > NBD_MAX_BUFFER_SIZE) {
        /* READ, WRITE, CACHE */
        error_setg(errp, "len (%" PRIu64 ") is larger than max len (%u)",
                   request->len, NBD_MAX_BUFFER_SIZE);
        return -EINVAL;
    }
    if (payload_len && !payload_okay) {
        /*
         * For now, we don't support payloads on other commands; but
         * we can keep the connection alive by ignoring the payload.
         * We will fail the command later with NBD_EINVAL for the use
         * of an unsupported flag (and not for access beyond bounds).
         */
        assert(request->type != NBD_CMD_WRITE);
        request->len = 0;
    }
    if (allocate_buffer) {
        /* READ, WRITE */
        req->data = blk_try_blockalign(client->exp->common.blk,
                                       request->len);
        if (req->data == NULL) {
            error_setg(errp, "No memory");
            return -ENOMEM;
        }
    }
    if (payload_len) {
        if (payload_okay) {
            /* WRITE */
            assert(req->data);
            ret = nbd_read(client->ioc, req->data, payload_len,
                           "CMD_WRITE data", errp);
        } else {
            ret = nbd_drop(client->ioc, payload_len, errp);
        }
        if (ret < 0) {
            return -EIO;
        }
        req->complete = true;
        trace_nbd_co_receive_request_payload_received(request->cookie,
                                                      payload_len);
    }

    /* Sanity checks. */
    if (client->exp->nbdflags & NBD_FLAG_READ_ONLY && check_rofs) {
        /* WRITE, TRIM, WRITE_ZEROES */
        error_setg(errp, "Export is read-only");
        return -EROFS;
    }
    if (request->from > client->exp->size ||
        request->len > client->exp->size - request->from) {
        error_setg(errp, "operation past EOF; From: %" PRIu64 ", Len: %" PRIu64
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
                                               NBDRequest *request,
                                               int ret,
                                               const char *error_msg,
                                               Error **errp)
{
    if (client->mode >= NBD_MODE_STRUCTURED && ret < 0) {
        return nbd_co_send_chunk_error(client, request, -ret, error_msg, errp);
    } else if (client->mode >= NBD_MODE_EXTENDED) {
        return nbd_co_send_chunk_done(client, request, errp);
    } else {
        return nbd_co_send_simple_reply(client, request, ret < 0 ? -ret : 0,
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
    assert(request->len <= NBD_MAX_BUFFER_SIZE);

    /* XXX: NBD Protocol only documents use of FUA with WRITE */
    if (request->flags & NBD_CMD_FLAG_FUA) {
        ret = blk_co_flush(exp->common.blk);
        if (ret < 0) {
            return nbd_send_generic_reply(client, request, ret,
                                          "flush failed", errp);
        }
    }

    if (client->mode >= NBD_MODE_STRUCTURED &&
        !(request->flags & NBD_CMD_FLAG_DF) && request->len)
    {
        return nbd_co_send_sparse_read(client, request, request->from,
                                       data, request->len, errp);
    }

    ret = blk_co_pread(exp->common.blk, request->from, request->len, data, 0);
    if (ret < 0) {
        return nbd_send_generic_reply(client, request, ret,
                                      "reading from file failed", errp);
    }

    if (client->mode >= NBD_MODE_STRUCTURED) {
        if (request->len) {
            return nbd_co_send_chunk_read(client, request, request->from, data,
                                          request->len, true, errp);
        } else {
            return nbd_co_send_chunk_done(client, request, errp);
        }
    } else {
        return nbd_co_send_simple_reply(client, request, 0,
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
    assert(request->len <= NBD_MAX_BUFFER_SIZE);

    ret = blk_co_preadv(exp->common.blk, request->from, request->len,
                        NULL, BDRV_REQ_COPY_ON_READ | BDRV_REQ_PREFETCH);

    return nbd_send_generic_reply(client, request, ret,
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
    bool inactive;

    WITH_GRAPH_RDLOCK_GUARD() {
        inactive = bdrv_is_inactive(blk_bs(exp->common.blk));
        if (inactive) {
            switch (request->type) {
            case NBD_CMD_READ:
                /* These commands are allowed on inactive nodes */
                break;
            default:
                /* Return an error for the rest */
                return nbd_send_generic_reply(client, request, -EPERM,
                                              "export is inactive", errp);
            }
        }
    }

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
        assert(request->len <= NBD_MAX_BUFFER_SIZE);
        ret = blk_co_pwrite(exp->common.blk, request->from, request->len, data,
                            flags);
        return nbd_send_generic_reply(client, request, ret,
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
        ret = blk_co_pwrite_zeroes(exp->common.blk, request->from, request->len,
                                   flags);
        return nbd_send_generic_reply(client, request, ret,
                                      "writing to file failed", errp);

    case NBD_CMD_DISC:
        /* unreachable, thanks to special case in nbd_co_receive_request() */
        abort();

    case NBD_CMD_FLUSH:
        ret = blk_co_flush(exp->common.blk);
        return nbd_send_generic_reply(client, request, ret,
                                      "flush failed", errp);

    case NBD_CMD_TRIM:
        ret = blk_co_pdiscard(exp->common.blk, request->from, request->len);
        if (ret >= 0 && request->flags & NBD_CMD_FLAG_FUA) {
            ret = blk_co_flush(exp->common.blk);
        }
        return nbd_send_generic_reply(client, request, ret,
                                      "discard failed", errp);

    case NBD_CMD_BLOCK_STATUS:
        assert(request->contexts);
        assert(client->mode >= NBD_MODE_EXTENDED ||
               request->len <= UINT32_MAX);
        if (request->contexts->count) {
            bool dont_fragment = request->flags & NBD_CMD_FLAG_REQ_ONE;
            int contexts_remaining = request->contexts->count;

            if (!request->len) {
                return nbd_send_generic_reply(client, request, -EINVAL,
                                              "need non-zero length", errp);
            }
            if (request->contexts->base_allocation) {
                ret = nbd_co_send_block_status(client, request,
                                               exp->common.blk,
                                               request->from,
                                               request->len, dont_fragment,
                                               !--contexts_remaining,
                                               NBD_META_ID_BASE_ALLOCATION,
                                               errp);
                if (ret < 0) {
                    return ret;
                }
            }

            if (request->contexts->allocation_depth) {
                ret = nbd_co_send_block_status(client, request,
                                               exp->common.blk,
                                               request->from, request->len,
                                               dont_fragment,
                                               !--contexts_remaining,
                                               NBD_META_ID_ALLOCATION_DEPTH,
                                               errp);
                if (ret < 0) {
                    return ret;
                }
            }

            assert(request->contexts->exp == client->exp);
            for (i = 0; i < client->exp->nr_export_bitmaps; i++) {
                if (!request->contexts->bitmaps[i]) {
                    continue;
                }
                ret = nbd_co_send_bitmap(client, request,
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
        } else if (client->contexts.count) {
            return nbd_send_generic_reply(client, request, -EINVAL,
                                          "CMD_BLOCK_STATUS payload not valid",
                                          errp);
        } else {
            return nbd_send_generic_reply(client, request, -EINVAL,
                                          "CMD_BLOCK_STATUS not negotiated",
                                          errp);
        }

    default:
        msg = g_strdup_printf("invalid request type (%" PRIu32 ") received",
                              request->type);
        ret = nbd_send_generic_reply(client, request, -EINVAL, msg,
                                     errp);
        g_free(msg);
        return ret;
    }
}

/* Owns a reference to the NBDClient passed as opaque.  */
static coroutine_fn void nbd_trip(void *opaque)
{
    NBDRequestData *req = opaque;
    NBDClient *client = req->client;
    NBDRequest request = { 0 };    /* GCC thinks it can be used uninitialized */
    int ret;
    Error *local_err = NULL;

    /*
     * Note that nbd_client_put() and client_close() must be called from the
     * main loop thread. Use aio_co_reschedule_self() to switch AioContext
     * before calling these functions.
     */

    trace_nbd_trip();

    qemu_mutex_lock(&client->lock);

    if (client->closing) {
        goto done;
    }

    if (client->quiescing) {
        /*
         * We're switching between AIO contexts. Don't attempt to receive a new
         * request and kick the main context which may be waiting for us.
         */
        client->recv_coroutine = NULL;
        aio_wait_kick();
        goto done;
    }

    /*
     * nbd_co_receive_request() returns -EAGAIN when nbd_drained_begin() has
     * set client->quiescing but by the time we get back nbd_drained_end() may
     * have already cleared client->quiescing. In that case we try again
     * because nothing else will spawn an nbd_trip() coroutine until we set
     * client->recv_coroutine = NULL further down.
     */
    do {
        assert(client->recv_coroutine == qemu_coroutine_self());
        qemu_mutex_unlock(&client->lock);
        ret = nbd_co_receive_request(req, &request, &local_err);
        qemu_mutex_lock(&client->lock);
    } while (ret == -EAGAIN && !client->quiescing);

    client->recv_coroutine = NULL;

    if (client->closing) {
        /*
         * The client may be closed when we are blocked in
         * nbd_co_receive_request()
         */
        goto done;
    }

    if (ret == -EAGAIN) {
        goto done;
    }

    nbd_client_receive_next_request(client);

    if (ret == -EIO) {
        goto disconnect;
    }

    qemu_mutex_unlock(&client->lock);
    qio_channel_set_cork(client->ioc, true);

    if (ret < 0) {
        /* It wasn't -EIO, so, according to nbd_co_receive_request()
         * semantics, we should return the error to the client. */
        Error *export_err = local_err;

        local_err = NULL;
        ret = nbd_send_generic_reply(client, &request, -EINVAL,
                                     error_get_pretty(export_err), &local_err);
        error_free(export_err);
    } else {
        ret = nbd_handle_request(client, &request, req->data, &local_err);
    }
    if (request.contexts && request.contexts != &client->contexts) {
        assert(request.type == NBD_CMD_BLOCK_STATUS);
        g_free(request.contexts->bitmaps);
        g_free(request.contexts);
    }

    qio_channel_set_cork(client->ioc, false);
    qemu_mutex_lock(&client->lock);

    if (ret < 0) {
        error_prepend(&local_err, "Failed to send reply: ");
        goto disconnect;
    }

    /*
     * We must disconnect after NBD_CMD_WRITE or BLOCK_STATUS with
     * payload if we did not read the payload.
     */
    if (!req->complete) {
        error_setg(&local_err, "Request handling failed in intermediate state");
        goto disconnect;
    }

done:
    nbd_request_put(req);

    qemu_mutex_unlock(&client->lock);

    if (!nbd_client_put_nonzero(client)) {
        aio_co_reschedule_self(qemu_get_aio_context());
        nbd_client_put(client);
    }
    return;

disconnect:
    if (local_err) {
        error_reportf_err(local_err, "Disconnect client, due to: ");
    }

    nbd_request_put(req);
    qemu_mutex_unlock(&client->lock);

    aio_co_reschedule_self(qemu_get_aio_context());
    client_close(client, true);
    nbd_client_put(client);
}

/*
 * Runs in export AioContext and main loop thread. Caller must hold
 * client->lock.
 */
static void nbd_client_receive_next_request(NBDClient *client)
{
    NBDRequestData *req;

    if (!client->recv_coroutine && client->nb_requests < MAX_NBD_REQUESTS &&
        !client->quiescing) {
        nbd_client_get(client);
        req = nbd_request_get(client);
        client->recv_coroutine = qemu_coroutine_create(nbd_trip, req);
        aio_co_schedule(client->exp->common.ctx, client->recv_coroutine);
    }
}

static void nbd_handshake_timer_cb(void *opaque)
{
    QIOChannel *ioc = opaque;

    trace_nbd_handshake_timer_cb();
    qio_channel_shutdown(ioc, QIO_CHANNEL_SHUTDOWN_BOTH, NULL);
}

static coroutine_fn void nbd_co_client_start(void *opaque)
{
    NBDClient *client = opaque;
    Error *local_err = NULL;
    QEMUTimer *handshake_timer = NULL;

    qemu_co_mutex_init(&client->send_lock);

    /*
     * Create a timer to bound the time spent in negotiation. If the
     * timer expires, it is likely nbd_negotiate will fail because the
     * socket was shutdown.
     */
    if (client->handshake_max_secs > 0) {
        handshake_timer = aio_timer_new(qemu_get_aio_context(),
                                        QEMU_CLOCK_REALTIME,
                                        SCALE_NS,
                                        nbd_handshake_timer_cb,
                                        client->sioc);
        timer_mod(handshake_timer,
                  qemu_clock_get_ns(QEMU_CLOCK_REALTIME) +
                  client->handshake_max_secs * NANOSECONDS_PER_SECOND);
    }

    if (nbd_negotiate(client, &local_err)) {
        if (local_err) {
            error_report_err(local_err);
        }
        timer_free(handshake_timer);
        client_close(client, false);
        return;
    }

    timer_free(handshake_timer);
    WITH_QEMU_LOCK_GUARD(&client->lock) {
        nbd_client_receive_next_request(client);
    }
}

/*
 * Create a new client listener using the given channel @sioc and @owner.
 * Begin servicing it in a coroutine.  When the connection closes, call
 * @close_fn with an indication of whether the client completed negotiation
 * within @handshake_max_secs seconds (0 for unbounded).
 */
void nbd_client_new(QIOChannelSocket *sioc,
                    uint32_t handshake_max_secs,
                    QCryptoTLSCreds *tlscreds,
                    const char *tlsauthz,
                    void (*close_fn)(NBDClient *, bool),
                    void *owner)
{
    NBDClient *client;
    Coroutine *co;

    client = g_new0(NBDClient, 1);
    qemu_mutex_init(&client->lock);
    client->refcount = 1;
    client->tlscreds = tlscreds;
    if (tlscreds) {
        object_ref(OBJECT(client->tlscreds));
    }
    client->tlsauthz = g_strdup(tlsauthz);
    client->handshake_max_secs = handshake_max_secs;
    client->sioc = sioc;
    qio_channel_set_delay(QIO_CHANNEL(sioc), false);
    object_ref(OBJECT(client->sioc));
    client->ioc = QIO_CHANNEL(sioc);
    object_ref(OBJECT(client->ioc));
    client->close_fn = close_fn;
    client->owner = owner;

    nbd_set_socket_send_buffer(sioc);

    co = qemu_coroutine_create(nbd_co_client_start, client);
    qemu_coroutine_enter(co);
}

void *
nbd_client_owner(NBDClient *client)
{
    return client->owner;
}
