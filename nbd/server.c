/*
 *  Copyright (C) 2016-2017 Red Hat, Inc.
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
#include "trace.h"
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
    void (*close_fn)(NBDClient *client, bool negotiated);

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
                                      uint32_t opt, uint32_t len, Error **errp)
{
    uint64_t magic;

    trace_nbd_negotiate_send_rep_len(opt, nbd_opt_lookup(opt),
                                     type, nbd_rep_lookup(type), len);

    assert(len < NBD_MAX_BUFFER_SIZE);
    magic = cpu_to_be64(NBD_REP_MAGIC);
    if (nbd_write(ioc, &magic, sizeof(magic), errp) < 0) {
        error_prepend(errp, "write failed (rep magic): ");
        return -EINVAL;
    }

    opt = cpu_to_be32(opt);
    if (nbd_write(ioc, &opt, sizeof(opt), errp) < 0) {
        error_prepend(errp, "write failed (rep opt): ");
        return -EINVAL;
    }

    type = cpu_to_be32(type);
    if (nbd_write(ioc, &type, sizeof(type), errp) < 0) {
        error_prepend(errp, "write failed (rep type): ");
        return -EINVAL;
    }

    len = cpu_to_be32(len);
    if (nbd_write(ioc, &len, sizeof(len), errp) < 0) {
        error_prepend(errp, "write failed (rep data length): ");
        return -EINVAL;
    }
    return 0;
}

/* Send a reply header with default 0 length.
 * Return -errno on error, 0 on success. */
static int nbd_negotiate_send_rep(QIOChannel *ioc, uint32_t type, uint32_t opt,
                                  Error **errp)
{
    return nbd_negotiate_send_rep_len(ioc, type, opt, 0, errp);
}

/* Send an error reply.
 * Return -errno on error, 0 on success. */
static int GCC_FMT_ATTR(5, 6)
nbd_negotiate_send_rep_err(QIOChannel *ioc, uint32_t type,
                           uint32_t opt, Error **errp, const char *fmt, ...)
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
    trace_nbd_negotiate_send_rep_err(msg);
    ret = nbd_negotiate_send_rep_len(ioc, type, opt, len, errp);
    if (ret < 0) {
        goto out;
    }
    if (nbd_write(ioc, msg, len, errp) < 0) {
        error_prepend(errp, "write failed (error message): ");
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
static int nbd_negotiate_send_rep_list(QIOChannel *ioc, NBDExport *exp,
                                       Error **errp)
{
    size_t name_len, desc_len;
    uint32_t len;
    const char *name = exp->name ? exp->name : "";
    const char *desc = exp->description ? exp->description : "";
    int ret;

    trace_nbd_negotiate_send_rep_list(name, desc);
    name_len = strlen(name);
    desc_len = strlen(desc);
    len = name_len + desc_len + sizeof(len);
    ret = nbd_negotiate_send_rep_len(ioc, NBD_REP_SERVER, NBD_OPT_LIST, len,
                                     errp);
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
static int nbd_negotiate_handle_list(NBDClient *client, uint32_t length,
                                     Error **errp)
{
    NBDExport *exp;

    if (length) {
        if (nbd_drop(client->ioc, length, errp) < 0) {
            return -EIO;
        }
        return nbd_negotiate_send_rep_err(client->ioc,
                                          NBD_REP_ERR_INVALID, NBD_OPT_LIST,
                                          errp,
                                          "OPT_LIST should not have length");
    }

    /* For each export, send a NBD_REP_SERVER reply. */
    QTAILQ_FOREACH(exp, &exports, next) {
        if (nbd_negotiate_send_rep_list(client->ioc, exp, errp)) {
            return -EINVAL;
        }
    }
    /* Finish with a NBD_REP_ACK. */
    return nbd_negotiate_send_rep(client->ioc, NBD_REP_ACK, NBD_OPT_LIST, errp);
}

/* Send a reply to NBD_OPT_EXPORT_NAME.
 * Return -errno on error, 0 on success. */
static int nbd_negotiate_handle_export_name(NBDClient *client, uint32_t length,
                                            uint16_t myflags, bool no_zeroes,
                                            Error **errp)
{
    char name[NBD_MAX_NAME_SIZE + 1];
    char buf[NBD_REPLY_EXPORT_NAME_SIZE] = "";
    size_t len;
    int ret;

    /* Client sends:
        [20 ..  xx]   export name (length bytes)
       Server replies:
        [ 0 ..   7]   size
        [ 8 ..   9]   export flags
        [10 .. 133]   reserved     (0) [unless no_zeroes]
     */
    trace_nbd_negotiate_handle_export_name();
    if (length >= sizeof(name)) {
        error_setg(errp, "Bad length received");
        return -EINVAL;
    }
    if (nbd_read(client->ioc, name, length, errp) < 0) {
        error_prepend(errp, "read failed: ");
        return -EINVAL;
    }
    name[length] = '\0';

    trace_nbd_negotiate_handle_export_name_request(name);

    client->exp = nbd_export_find(name);
    if (!client->exp) {
        error_setg(errp, "export not found");
        return -EINVAL;
    }

    trace_nbd_negotiate_new_style_size_flags(client->exp->size,
                                             client->exp->nbdflags | myflags);
    stq_be_p(buf, client->exp->size);
    stw_be_p(buf + 8, client->exp->nbdflags | myflags);
    len = no_zeroes ? 10 : sizeof(buf);
    ret = nbd_write(client->ioc, buf, len, errp);
    if (ret < 0) {
        error_prepend(errp, "write failed: ");
        return ret;
    }

    QTAILQ_INSERT_TAIL(&client->exp->clients, client, next);
    nbd_export_get(client->exp);

    return 0;
}

/* Send a single NBD_REP_INFO, with a buffer @buf of @length bytes.
 * The buffer does NOT include the info type prefix.
 * Return -errno on error, 0 if ready to send more. */
static int nbd_negotiate_send_info(NBDClient *client, uint32_t opt,
                                   uint16_t info, uint32_t length, void *buf,
                                   Error **errp)
{
    int rc;

    trace_nbd_negotiate_send_info(info, nbd_info_lookup(info), length);
    rc = nbd_negotiate_send_rep_len(client->ioc, NBD_REP_INFO, opt,
                                    sizeof(info) + length, errp);
    if (rc < 0) {
        return rc;
    }
    cpu_to_be16s(&info);
    if (nbd_write(client->ioc, &info, sizeof(info), errp) < 0) {
        return -EIO;
    }
    if (nbd_write(client->ioc, buf, length, errp) < 0) {
        return -EIO;
    }
    return 0;
}

/* Handle NBD_OPT_INFO and NBD_OPT_GO.
 * Return -errno on error, 0 if ready for next option, and 1 to move
 * into transmission phase.  */
static int nbd_negotiate_handle_info(NBDClient *client, uint32_t length,
                                     uint32_t opt, uint16_t myflags,
                                     Error **errp)
{
    int rc;
    char name[NBD_MAX_NAME_SIZE + 1];
    NBDExport *exp;
    uint16_t requests;
    uint16_t request;
    uint32_t namelen;
    bool sendname = false;
    bool blocksize = false;
    uint32_t sizes[3];
    char buf[sizeof(uint64_t) + sizeof(uint16_t)];
    const char *msg;

    /* Client sends:
        4 bytes: L, name length (can be 0)
        L bytes: export name
        2 bytes: N, number of requests (can be 0)
        N * 2 bytes: N requests
    */
    if (length < sizeof(namelen) + sizeof(requests)) {
        msg = "overall request too short";
        goto invalid;
    }
    if (nbd_read(client->ioc, &namelen, sizeof(namelen), errp) < 0) {
        return -EIO;
    }
    be32_to_cpus(&namelen);
    length -= sizeof(namelen);
    if (namelen > length - sizeof(requests) || (length - namelen) % 2) {
        msg = "name length is incorrect";
        goto invalid;
    }
    if (nbd_read(client->ioc, name, namelen, errp) < 0) {
        return -EIO;
    }
    name[namelen] = '\0';
    length -= namelen;
    trace_nbd_negotiate_handle_export_name_request(name);

    if (nbd_read(client->ioc, &requests, sizeof(requests), errp) < 0) {
        return -EIO;
    }
    be16_to_cpus(&requests);
    length -= sizeof(requests);
    trace_nbd_negotiate_handle_info_requests(requests);
    if (requests != length / sizeof(request)) {
        msg = "incorrect number of  requests for overall length";
        goto invalid;
    }
    while (requests--) {
        if (nbd_read(client->ioc, &request, sizeof(request), errp) < 0) {
            return -EIO;
        }
        be16_to_cpus(&request);
        length -= sizeof(request);
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

    exp = nbd_export_find(name);
    if (!exp) {
        return nbd_negotiate_send_rep_err(client->ioc, NBD_REP_ERR_UNKNOWN,
                                          opt, errp, "export '%s' not present",
                                          name);
    }

    /* Don't bother sending NBD_INFO_NAME unless client requested it */
    if (sendname) {
        rc = nbd_negotiate_send_info(client, opt, NBD_INFO_NAME, length, name,
                                     errp);
        if (rc < 0) {
            return rc;
        }
    }

    /* Send NBD_INFO_DESCRIPTION only if available, regardless of
     * client request */
    if (exp->description) {
        size_t len = strlen(exp->description);

        rc = nbd_negotiate_send_info(client, opt, NBD_INFO_DESCRIPTION,
                                     len, exp->description, errp);
        if (rc < 0) {
            return rc;
        }
    }

    /* Send NBD_INFO_BLOCK_SIZE always, but tweak the minimum size
     * according to whether the client requested it, and according to
     * whether this is OPT_INFO or OPT_GO. */
    /* minimum - 1 for back-compat, or 512 if client is new enough.
     * TODO: consult blk_bs(blk)->bl.request_alignment? */
    sizes[0] = (opt == NBD_OPT_INFO || blocksize) ? BDRV_SECTOR_SIZE : 1;
    /* preferred - Hard-code to 4096 for now.
     * TODO: is blk_bs(blk)->bl.opt_transfer appropriate? */
    sizes[1] = 4096;
    /* maximum - At most 32M, but smaller as appropriate. */
    sizes[2] = MIN(blk_get_max_transfer(exp->blk), NBD_MAX_BUFFER_SIZE);
    trace_nbd_negotiate_handle_info_block_size(sizes[0], sizes[1], sizes[2]);
    cpu_to_be32s(&sizes[0]);
    cpu_to_be32s(&sizes[1]);
    cpu_to_be32s(&sizes[2]);
    rc = nbd_negotiate_send_info(client, opt, NBD_INFO_BLOCK_SIZE,
                                 sizeof(sizes), sizes, errp);
    if (rc < 0) {
        return rc;
    }

    /* Send NBD_INFO_EXPORT always */
    trace_nbd_negotiate_new_style_size_flags(exp->size,
                                             exp->nbdflags | myflags);
    stq_be_p(buf, exp->size);
    stw_be_p(buf + 8, exp->nbdflags | myflags);
    rc = nbd_negotiate_send_info(client, opt, NBD_INFO_EXPORT,
                                 sizeof(buf), buf, errp);
    if (rc < 0) {
        return rc;
    }

    /* If the client is just asking for NBD_OPT_INFO, but forgot to
     * request block sizes, return an error.
     * TODO: consult blk_bs(blk)->request_align, and only error if it
     * is not 1? */
    if (opt == NBD_OPT_INFO && !blocksize) {
        return nbd_negotiate_send_rep_err(client->ioc,
                                          NBD_REP_ERR_BLOCK_SIZE_REQD, opt,
                                          errp,
                                          "request NBD_INFO_BLOCK_SIZE to "
                                          "use this export");
    }

    /* Final reply */
    rc = nbd_negotiate_send_rep(client->ioc, NBD_REP_ACK, opt, errp);
    if (rc < 0) {
        return rc;
    }

    if (opt == NBD_OPT_GO) {
        client->exp = exp;
        QTAILQ_INSERT_TAIL(&client->exp->clients, client, next);
        nbd_export_get(client->exp);
        rc = 1;
    }
    return rc;

 invalid:
    if (nbd_drop(client->ioc, length, errp) < 0) {
        return -EIO;
    }
    return nbd_negotiate_send_rep_err(client->ioc, NBD_REP_ERR_INVALID, opt,
                                      errp, "%s", msg);
}


/* Handle NBD_OPT_STARTTLS. Return NULL to drop connection, or else the
 * new channel for all further (now-encrypted) communication. */
static QIOChannel *nbd_negotiate_handle_starttls(NBDClient *client,
                                                 uint32_t length,
                                                 Error **errp)
{
    QIOChannel *ioc;
    QIOChannelTLS *tioc;
    struct NBDTLSHandshakeData data = { 0 };

    trace_nbd_negotiate_handle_starttls();
    ioc = client->ioc;
    if (length) {
        if (nbd_drop(ioc, length, errp) < 0) {
            return NULL;
        }
        nbd_negotiate_send_rep_err(ioc, NBD_REP_ERR_INVALID, NBD_OPT_STARTTLS,
                                   errp,
                                   "OPT_STARTTLS should not have length");
        return NULL;
    }

    if (nbd_negotiate_send_rep(client->ioc, NBD_REP_ACK,
                               NBD_OPT_STARTTLS, errp) < 0) {
        return NULL;
    }

    tioc = qio_channel_tls_new_server(ioc,
                                      client->tlscreds,
                                      client->tlsaclname,
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

/* nbd_negotiate_options
 * Process all NBD_OPT_* client option commands, during fixed newstyle
 * negotiation.
 * Return:
 * -errno  on error, errp is set
 * 0       on successful negotiation, errp is not set
 * 1       if client sent NBD_OPT_ABORT, i.e. on valid disconnect,
 *         errp is not set
 */
static int nbd_negotiate_options(NBDClient *client, uint16_t myflags,
                                 Error **errp)
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

    if (nbd_read(client->ioc, &flags, sizeof(flags), errp) < 0) {
        error_prepend(errp, "read failed: ");
        return -EIO;
    }
    be32_to_cpus(&flags);
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

        if (nbd_read(client->ioc, &magic, sizeof(magic), errp) < 0) {
            error_prepend(errp, "read failed: ");
            return -EINVAL;
        }
        magic = be64_to_cpu(magic);
        trace_nbd_negotiate_options_check_magic(magic);
        if (magic != NBD_OPTS_MAGIC) {
            error_setg(errp, "Bad magic received");
            return -EINVAL;
        }

        if (nbd_read(client->ioc, &option,
                     sizeof(option), errp) < 0) {
            error_prepend(errp, "read failed: ");
            return -EINVAL;
        }
        option = be32_to_cpu(option);

        if (nbd_read(client->ioc, &length, sizeof(length), errp) < 0) {
            error_prepend(errp, "read failed: ");
            return -EINVAL;
        }
        length = be32_to_cpu(length);

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
                tioc = nbd_negotiate_handle_starttls(client, length, errp);
                if (!tioc) {
                    return -EIO;
                }
                object_unref(OBJECT(client->ioc));
                client->ioc = QIO_CHANNEL(tioc);
                break;

            case NBD_OPT_EXPORT_NAME:
                /* No way to return an error to client, so drop connection */
                error_setg(errp, "Option 0x%x not permitted before TLS",
                           option);
                return -EINVAL;

            default:
                if (nbd_drop(client->ioc, length, errp) < 0) {
                    return -EIO;
                }
                ret = nbd_negotiate_send_rep_err(client->ioc,
                                                 NBD_REP_ERR_TLS_REQD,
                                                 option, errp,
                                                 "Option 0x%" PRIx32
                                                 "not permitted before TLS",
                                                 option);
                if (ret < 0) {
                    return ret;
                }
                /* Let the client keep trying, unless they asked to
                 * quit. In this mode, we've already sent an error, so
                 * we can't ack the abort.  */
                if (option == NBD_OPT_ABORT) {
                    return 1;
                }
                break;
            }
        } else if (fixedNewstyle) {
            switch (option) {
            case NBD_OPT_LIST:
                ret = nbd_negotiate_handle_list(client, length, errp);
                if (ret < 0) {
                    return ret;
                }
                break;

            case NBD_OPT_ABORT:
                /* NBD spec says we must try to reply before
                 * disconnecting, but that we must also tolerate
                 * guests that don't wait for our reply. */
                nbd_negotiate_send_rep(client->ioc, NBD_REP_ACK, option, NULL);
                return 1;

            case NBD_OPT_EXPORT_NAME:
                return nbd_negotiate_handle_export_name(client, length,
                                                        myflags, no_zeroes,
                                                        errp);

            case NBD_OPT_INFO:
            case NBD_OPT_GO:
                ret = nbd_negotiate_handle_info(client, length, option,
                                                myflags, errp);
                if (ret == 1) {
                    assert(option == NBD_OPT_GO);
                    return 0;
                }
                if (ret) {
                    return ret;
                }
                break;

            case NBD_OPT_STARTTLS:
                if (nbd_drop(client->ioc, length, errp) < 0) {
                    return -EIO;
                }
                if (client->tlscreds) {
                    ret = nbd_negotiate_send_rep_err(client->ioc,
                                                     NBD_REP_ERR_INVALID,
                                                     option, errp,
                                                     "TLS already enabled");
                } else {
                    ret = nbd_negotiate_send_rep_err(client->ioc,
                                                     NBD_REP_ERR_POLICY,
                                                     option, errp,
                                                     "TLS not configured");
                }
                if (ret < 0) {
                    return ret;
                }
                break;
            default:
                if (nbd_drop(client->ioc, length, errp) < 0) {
                    return -EIO;
                }
                ret = nbd_negotiate_send_rep_err(client->ioc,
                                                 NBD_REP_ERR_UNSUP,
                                                 option, errp,
                                                 "Unsupported option 0x%"
                                                 PRIx32 " (%s)", option,
                                                 nbd_opt_lookup(option));
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
            switch (option) {
            case NBD_OPT_EXPORT_NAME:
                return nbd_negotiate_handle_export_name(client, length,
                                                        myflags, no_zeroes,
                                                        errp);

            default:
                error_setg(errp, "Unsupported option 0x%" PRIx32 " (%s)",
                           option, nbd_opt_lookup(option));
                return -EINVAL;
            }
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
    const uint16_t myflags = (NBD_FLAG_HAS_FLAGS | NBD_FLAG_SEND_TRIM |
                              NBD_FLAG_SEND_FLUSH | NBD_FLAG_SEND_FUA |
                              NBD_FLAG_SEND_WRITE_ZEROES);
    bool oldStyle;

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

    oldStyle = client->exp != NULL && !client->tlscreds;
    if (oldStyle) {
        trace_nbd_negotiate_old_style(client->exp->size,
                                      client->exp->nbdflags | myflags);
        stq_be_p(buf + 8, NBD_CLIENT_MAGIC);
        stq_be_p(buf + 16, client->exp->size);
        stl_be_p(buf + 24, client->exp->nbdflags | myflags);

        if (nbd_write(client->ioc, buf, sizeof(buf), errp) < 0) {
            error_prepend(errp, "write failed: ");
            return -EINVAL;
        }
    } else {
        stq_be_p(buf + 8, NBD_OPTS_MAGIC);
        stw_be_p(buf + 16, NBD_FLAG_FIXED_NEWSTYLE | NBD_FLAG_NO_ZEROES);

        if (nbd_write(client->ioc, buf, 18, errp) < 0) {
            error_prepend(errp, "write failed: ");
            return -EINVAL;
        }
        ret = nbd_negotiate_options(client, myflags, errp);
        if (ret != 0) {
            if (ret < 0) {
                error_prepend(errp, "option negotiation failed: ");
            }
            return ret;
        }
    }

    trace_nbd_negotiate_success();

    return 0;
}

static int nbd_receive_request(QIOChannel *ioc, NBDRequest *request,
                               Error **errp)
{
    uint8_t buf[NBD_REQUEST_SIZE];
    uint32_t magic;
    int ret;

    ret = nbd_read(ioc, buf, sizeof(buf), errp);
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

static int nbd_send_reply(QIOChannel *ioc, NBDReply *reply, Error **errp)
{
    uint8_t buf[NBD_REPLY_SIZE];

    reply->error = system_errno_to_nbd_errno(reply->error);

    trace_nbd_send_reply(reply->error, reply->handle);

    /* Reply
       [ 0 ..  3]    magic   (NBD_REPLY_MAGIC)
       [ 4 ..  7]    error   (0 == no error)
       [ 7 .. 15]    handle
     */
    stl_be_p(buf, NBD_REPLY_MAGIC);
    stl_be_p(buf + 4, reply->error);
    stq_be_p(buf + 8, reply->handle);

    return nbd_write(ioc, buf, sizeof(buf), errp);
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
    nbd_export_close(exp);
}

NBDExport *nbd_export_new(BlockDriverState *bs, off_t dev_offset, off_t size,
                          uint16_t nbdflags, void (*close)(NBDExport *),
                          bool writethrough, BlockBackend *on_eject_blk,
                          Error **errp)
{
    AioContext *ctx;
    BlockBackend *blk;
    NBDExport *exp = g_malloc0(sizeof(NBDExport));
    uint64_t perm;
    int ret;

    /*
     * NBD exports are used for non-shared storage migration.  Make sure
     * that BDRV_O_INACTIVE is cleared and the image is ready for write
     * access since the export could be available before migration handover.
     */
    ctx = bdrv_get_aio_context(bs);
    aio_context_acquire(ctx);
    bdrv_invalidate_cache(bs, NULL);
    aio_context_release(ctx);

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
        client_close(client, true);
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

static int nbd_co_send_reply(NBDRequestData *req, NBDReply *reply, int len,
                             Error **errp)
{
    NBDClient *client = req->client;
    int ret;

    g_assert(qemu_in_coroutine());

    trace_nbd_co_send_reply(reply->handle, reply->error, len);

    qemu_co_mutex_lock(&client->send_lock);
    client->send_coroutine = qemu_coroutine_self();

    if (!len) {
        ret = nbd_send_reply(client->ioc, reply, errp);
    } else {
        qio_channel_set_cork(client->ioc, true);
        ret = nbd_send_reply(client->ioc, reply, errp);
        if (ret == 0) {
            ret = nbd_write(client->ioc, req->data, len, errp);
            if (ret < 0) {
                ret = -EIO;
            }
        }
        qio_channel_set_cork(client->ioc, false);
    }

    client->send_coroutine = NULL;
    qemu_co_mutex_unlock(&client->send_lock);
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

    /* Check for sanity in the parameters, part 1.  Defer as many
     * checks as possible until after reading any NBD_CMD_WRITE
     * payload, so we can try and keep the connection alive.  */
    if ((request->from + request->len) < request->from) {
        error_setg(errp,
                   "integer overflow detected, you're probably being attacked");
        return -EINVAL;
    }

    if (request->type == NBD_CMD_READ || request->type == NBD_CMD_WRITE) {
        if (request->len > NBD_MAX_BUFFER_SIZE) {
            error_setg(errp, "len (%" PRIu32" ) is larger than max len (%u)",
                       request->len, NBD_MAX_BUFFER_SIZE);
            return -EINVAL;
        }

        req->data = blk_try_blockalign(client->exp->blk, request->len);
        if (req->data == NULL) {
            error_setg(errp, "No memory");
            return -ENOMEM;
        }
    }
    if (request->type == NBD_CMD_WRITE) {
        if (nbd_read(client->ioc, req->data, request->len, errp) < 0) {
            error_prepend(errp, "reading from socket failed: ");
            return -EIO;
        }
        req->complete = true;

        trace_nbd_co_receive_request_payload_received(request->handle,
                                                      request->len);
    }

    /* Sanity checks, part 2. */
    if (request->from + request->len > client->exp->size) {
        error_setg(errp, "operation past EOF; From: %" PRIu64 ", Len: %" PRIu32
                   ", Size: %" PRIu64, request->from, request->len,
                   (uint64_t)client->exp->size);
        return request->type == NBD_CMD_WRITE ? -ENOSPC : -EINVAL;
    }
    if (request->flags & ~(NBD_CMD_FLAG_FUA | NBD_CMD_FLAG_NO_HOLE)) {
        error_setg(errp, "unsupported flags (got 0x%x)", request->flags);
        return -EINVAL;
    }
    if (request->type != NBD_CMD_WRITE_ZEROES &&
        (request->flags & NBD_CMD_FLAG_NO_HOLE)) {
        error_setg(errp, "unexpected flags (got 0x%x)", request->flags);
        return -EINVAL;
    }

    return 0;
}

/* Owns a reference to the NBDClient passed as opaque.  */
static coroutine_fn void nbd_trip(void *opaque)
{
    NBDClient *client = opaque;
    NBDExport *exp = client->exp;
    NBDRequestData *req;
    NBDRequest request = { 0 };    /* GCC thinks it can be used uninitialized */
    NBDReply reply;
    int ret;
    int flags;
    int reply_data_len = 0;
    Error *local_err = NULL;

    trace_nbd_trip();
    if (client->closing) {
        nbd_client_put(client);
        return;
    }

    req = nbd_request_get(client);
    ret = nbd_co_receive_request(req, &request, &local_err);
    client->recv_coroutine = NULL;
    nbd_client_receive_next_request(client);
    if (ret == -EIO) {
        goto disconnect;
    }

    reply.handle = request.handle;
    reply.error = 0;

    if (ret < 0) {
        reply.error = -ret;
        goto reply;
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
        /* XXX: NBD Protocol only documents use of FUA with WRITE */
        if (request.flags & NBD_CMD_FLAG_FUA) {
            ret = blk_co_flush(exp->blk);
            if (ret < 0) {
                error_setg_errno(&local_err, -ret, "flush failed");
                reply.error = -ret;
                break;
            }
        }

        ret = blk_pread(exp->blk, request.from + exp->dev_offset,
                        req->data, request.len);
        if (ret < 0) {
            error_setg_errno(&local_err, -ret, "reading from file failed");
            reply.error = -ret;
            break;
        }

        reply_data_len = request.len;

        break;
    case NBD_CMD_WRITE:
        if (exp->nbdflags & NBD_FLAG_READ_ONLY) {
            reply.error = EROFS;
            break;
        }

        flags = 0;
        if (request.flags & NBD_CMD_FLAG_FUA) {
            flags |= BDRV_REQ_FUA;
        }
        ret = blk_pwrite(exp->blk, request.from + exp->dev_offset,
                         req->data, request.len, flags);
        if (ret < 0) {
            error_setg_errno(&local_err, -ret, "writing to file failed");
            reply.error = -ret;
        }

        break;
    case NBD_CMD_WRITE_ZEROES:
        if (exp->nbdflags & NBD_FLAG_READ_ONLY) {
            error_setg(&local_err, "Server is read-only, return error");
            reply.error = EROFS;
            break;
        }

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
            error_setg_errno(&local_err, -ret, "writing to file failed");
            reply.error = -ret;
        }

        break;
    case NBD_CMD_DISC:
        /* unreachable, thanks to special case in nbd_co_receive_request() */
        abort();

    case NBD_CMD_FLUSH:
        ret = blk_co_flush(exp->blk);
        if (ret < 0) {
            error_setg_errno(&local_err, -ret, "flush failed");
            reply.error = -ret;
        }

        break;
    case NBD_CMD_TRIM:
        ret = blk_co_pdiscard(exp->blk, request.from + exp->dev_offset,
                              request.len);
        if (ret < 0) {
            error_setg_errno(&local_err, -ret, "discard failed");
            reply.error = -ret;
        }

        break;
    default:
        error_setg(&local_err, "invalid request type (%" PRIu32 ") received",
                   request.type);
        reply.error = EINVAL;
    }

reply:
    if (local_err) {
        /* If we are here local_err is not fatal error, already stored in
         * reply.error */
        error_report_err(local_err);
        local_err = NULL;
    }

    if (nbd_co_send_reply(req, &reply, reply_data_len, &local_err) < 0) {
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
    NBDExport *exp = client->exp;
    Error *local_err = NULL;

    if (exp) {
        nbd_export_get(exp);
        QTAILQ_INSERT_TAIL(&exp->clients, client, next);
    }
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
 * Create a new client listener on the given export @exp, using the
 * given channel @sioc.  Begin servicing it in a coroutine.  When the
 * connection closes, call @close_fn with an indication of whether the
 * client completed negotiation.
 */
void nbd_client_new(NBDExport *exp,
                    QIOChannelSocket *sioc,
                    QCryptoTLSCreds *tlscreds,
                    const char *tlsaclname,
                    void (*close_fn)(NBDClient *, bool))
{
    NBDClient *client;
    Coroutine *co;

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
    client->close_fn = close_fn;

    co = qemu_coroutine_create(nbd_co_client_start, client);
    qemu_coroutine_enter(co);
}
