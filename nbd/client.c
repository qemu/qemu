/*
 *  Copyright (C) 2016-2017 Red Hat, Inc.
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
#include "qapi/error.h"
#include "trace.h"
#include "nbd-internal.h"

static int nbd_errno_to_system_errno(int err)
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

/* Send an option request.
 *
 * The request is for option @opt, with @data containing @len bytes of
 * additional payload for the request (@len may be -1 to treat @data as
 * a C string; and @data may be NULL if @len is 0).
 * Return 0 if successful, -1 with errp set if it is impossible to
 * continue. */
static int nbd_send_option_request(QIOChannel *ioc, uint32_t opt,
                                   uint32_t len, const char *data,
                                   Error **errp)
{
    nbd_option req;
    QEMU_BUILD_BUG_ON(sizeof(req) != 16);

    if (len == -1) {
        req.length = len = strlen(data);
    }
    trace_nbd_send_option_request(opt, nbd_opt_lookup(opt), len);

    stq_be_p(&req.magic, NBD_OPTS_MAGIC);
    stl_be_p(&req.option, opt);
    stl_be_p(&req.length, len);

    if (nbd_write(ioc, &req, sizeof(req), errp) < 0) {
        error_prepend(errp, "Failed to send option request header");
        return -1;
    }

    if (len && nbd_write(ioc, (char *) data, len, errp) < 0) {
        error_prepend(errp, "Failed to send option request data");
        return -1;
    }

    return 0;
}

/* Send NBD_OPT_ABORT as a courtesy to let the server know that we are
 * not going to attempt further negotiation. */
static void nbd_send_opt_abort(QIOChannel *ioc)
{
    /* Technically, a compliant server is supposed to reply to us; but
     * older servers disconnected instead. At any rate, we're allowed
     * to disconnect without waiting for the server reply, so we don't
     * even care if the request makes it to the server, let alone
     * waiting around for whether the server replies. */
    nbd_send_option_request(ioc, NBD_OPT_ABORT, 0, NULL, NULL);
}


/* Receive the header of an option reply, which should match the given
 * opt.  Read through the length field, but NOT the length bytes of
 * payload. Return 0 if successful, -1 with errp set if it is
 * impossible to continue. */
static int nbd_receive_option_reply(QIOChannel *ioc, uint32_t opt,
                                    nbd_opt_reply *reply, Error **errp)
{
    QEMU_BUILD_BUG_ON(sizeof(*reply) != 20);
    if (nbd_read(ioc, reply, sizeof(*reply), errp) < 0) {
        error_prepend(errp, "failed to read option reply");
        nbd_send_opt_abort(ioc);
        return -1;
    }
    be64_to_cpus(&reply->magic);
    be32_to_cpus(&reply->option);
    be32_to_cpus(&reply->type);
    be32_to_cpus(&reply->length);

    trace_nbd_receive_option_reply(reply->option, nbd_opt_lookup(reply->option),
                                   reply->type, nbd_rep_lookup(reply->type),
                                   reply->length);

    if (reply->magic != NBD_REP_MAGIC) {
        error_setg(errp, "Unexpected option reply magic");
        nbd_send_opt_abort(ioc);
        return -1;
    }
    if (reply->option != opt) {
        error_setg(errp, "Unexpected option type %x expected %x",
                   reply->option, opt);
        nbd_send_opt_abort(ioc);
        return -1;
    }
    return 0;
}

/* If reply represents success, return 1 without further action.
 * If reply represents an error, consume the optional payload of
 * the packet on ioc.  Then return 0 for unsupported (so the client
 * can fall back to other approaches), or -1 with errp set for other
 * errors.
 */
static int nbd_handle_reply_err(QIOChannel *ioc, nbd_opt_reply *reply,
                                Error **errp)
{
    char *msg = NULL;
    int result = -1;

    if (!(reply->type & (1 << 31))) {
        return 1;
    }

    if (reply->length) {
        if (reply->length > NBD_MAX_BUFFER_SIZE) {
            error_setg(errp, "server error 0x%" PRIx32
                       " (%s) message is too long",
                       reply->type, nbd_rep_lookup(reply->type));
            goto cleanup;
        }
        msg = g_malloc(reply->length + 1);
        if (nbd_read(ioc, msg, reply->length, errp) < 0) {
            error_prepend(errp, "failed to read option error 0x%" PRIx32
                          " (%s) message",
                          reply->type, nbd_rep_lookup(reply->type));
            goto cleanup;
        }
        msg[reply->length] = '\0';
    }

    switch (reply->type) {
    case NBD_REP_ERR_UNSUP:
        trace_nbd_reply_err_unsup(reply->option, nbd_opt_lookup(reply->option));
        result = 0;
        goto cleanup;

    case NBD_REP_ERR_POLICY:
        error_setg(errp, "Denied by server for option %" PRIx32 " (%s)",
                   reply->option, nbd_opt_lookup(reply->option));
        break;

    case NBD_REP_ERR_INVALID:
        error_setg(errp, "Invalid data length for option %" PRIx32 " (%s)",
                   reply->option, nbd_opt_lookup(reply->option));
        break;

    case NBD_REP_ERR_PLATFORM:
        error_setg(errp, "Server lacks support for option %" PRIx32 " (%s)",
                   reply->option, nbd_opt_lookup(reply->option));
        break;

    case NBD_REP_ERR_TLS_REQD:
        error_setg(errp, "TLS negotiation required before option %" PRIx32
                   " (%s)", reply->option, nbd_opt_lookup(reply->option));
        break;

    case NBD_REP_ERR_UNKNOWN:
        error_setg(errp, "Requested export not available");
        break;

    case NBD_REP_ERR_SHUTDOWN:
        error_setg(errp, "Server shutting down before option %" PRIx32 " (%s)",
                   reply->option, nbd_opt_lookup(reply->option));
        break;

    case NBD_REP_ERR_BLOCK_SIZE_REQD:
        error_setg(errp, "Server requires INFO_BLOCK_SIZE for option %" PRIx32
                   " (%s)", reply->option, nbd_opt_lookup(reply->option));
        break;

    default:
        error_setg(errp, "Unknown error code when asking for option %" PRIx32
                   " (%s)", reply->option, nbd_opt_lookup(reply->option));
        break;
    }

    if (msg) {
        error_append_hint(errp, "server reported: %s\n", msg);
    }

 cleanup:
    g_free(msg);
    if (result < 0) {
        nbd_send_opt_abort(ioc);
    }
    return result;
}

/* Process another portion of the NBD_OPT_LIST reply.  Set *@match if
 * the current reply matches @want or if the server does not support
 * NBD_OPT_LIST, otherwise leave @match alone.  Return 0 if iteration
 * is complete, positive if more replies are expected, or negative
 * with @errp set if an unrecoverable error occurred. */
static int nbd_receive_list(QIOChannel *ioc, const char *want, bool *match,
                            Error **errp)
{
    nbd_opt_reply reply;
    uint32_t len;
    uint32_t namelen;
    char name[NBD_MAX_NAME_SIZE + 1];
    int error;

    if (nbd_receive_option_reply(ioc, NBD_OPT_LIST, &reply, errp) < 0) {
        return -1;
    }
    error = nbd_handle_reply_err(ioc, &reply, errp);
    if (error <= 0) {
        /* The server did not support NBD_OPT_LIST, so set *match on
         * the assumption that any name will be accepted.  */
        *match = true;
        return error;
    }
    len = reply.length;

    if (reply.type == NBD_REP_ACK) {
        if (len != 0) {
            error_setg(errp, "length too long for option end");
            nbd_send_opt_abort(ioc);
            return -1;
        }
        return 0;
    } else if (reply.type != NBD_REP_SERVER) {
        error_setg(errp, "Unexpected reply type %" PRIx32 " expected %x",
                   reply.type, NBD_REP_SERVER);
        nbd_send_opt_abort(ioc);
        return -1;
    }

    if (len < sizeof(namelen) || len > NBD_MAX_BUFFER_SIZE) {
        error_setg(errp, "incorrect option length %" PRIu32, len);
        nbd_send_opt_abort(ioc);
        return -1;
    }
    if (nbd_read(ioc, &namelen, sizeof(namelen), errp) < 0) {
        error_prepend(errp, "failed to read option name length");
        nbd_send_opt_abort(ioc);
        return -1;
    }
    namelen = be32_to_cpu(namelen);
    len -= sizeof(namelen);
    if (len < namelen) {
        error_setg(errp, "incorrect option name length");
        nbd_send_opt_abort(ioc);
        return -1;
    }
    if (namelen != strlen(want)) {
        if (nbd_drop(ioc, len, errp) < 0) {
            error_prepend(errp, "failed to skip export name with wrong length");
            nbd_send_opt_abort(ioc);
            return -1;
        }
        return 1;
    }

    assert(namelen < sizeof(name));
    if (nbd_read(ioc, name, namelen, errp) < 0) {
        error_prepend(errp, "failed to read export name");
        nbd_send_opt_abort(ioc);
        return -1;
    }
    name[namelen] = '\0';
    len -= namelen;
    if (nbd_drop(ioc, len, errp) < 0) {
        error_prepend(errp, "failed to read export description");
        nbd_send_opt_abort(ioc);
        return -1;
    }
    if (!strcmp(name, want)) {
        *match = true;
    }
    return 1;
}


/* Returns -1 if NBD_OPT_GO proves the export @wantname cannot be
 * used, 0 if NBD_OPT_GO is unsupported (fall back to NBD_OPT_LIST and
 * NBD_OPT_EXPORT_NAME in that case), and > 0 if the export is good to
 * go (with @info populated). */
static int nbd_opt_go(QIOChannel *ioc, const char *wantname,
                      NBDExportInfo *info, Error **errp)
{
    nbd_opt_reply reply;
    uint32_t len = strlen(wantname);
    uint16_t type;
    int error;
    char *buf;

    /* The protocol requires that the server send NBD_INFO_EXPORT with
     * a non-zero flags (at least NBD_FLAG_HAS_FLAGS must be set); so
     * flags still 0 is a witness of a broken server. */
    info->flags = 0;

    trace_nbd_opt_go_start(wantname);
    buf = g_malloc(4 + len + 2 + 2 * info->request_sizes + 1);
    stl_be_p(buf, len);
    memcpy(buf + 4, wantname, len);
    /* At most one request, everything else up to server */
    stw_be_p(buf + 4 + len, info->request_sizes);
    if (info->request_sizes) {
        stw_be_p(buf + 4 + len + 2, NBD_INFO_BLOCK_SIZE);
    }
    error = nbd_send_option_request(ioc, NBD_OPT_GO,
                                    4 + len + 2 + 2 * info->request_sizes,
                                    buf, errp);
    g_free(buf);
    if (error < 0) {
        return -1;
    }

    while (1) {
        if (nbd_receive_option_reply(ioc, NBD_OPT_GO, &reply, errp) < 0) {
            return -1;
        }
        error = nbd_handle_reply_err(ioc, &reply, errp);
        if (error <= 0) {
            return error;
        }
        len = reply.length;

        if (reply.type == NBD_REP_ACK) {
            /* Server is done sending info and moved into transmission
               phase, but make sure it sent flags */
            if (len) {
                error_setg(errp, "server sent invalid NBD_REP_ACK");
                nbd_send_opt_abort(ioc);
                return -1;
            }
            if (!info->flags) {
                error_setg(errp, "broken server omitted NBD_INFO_EXPORT");
                nbd_send_opt_abort(ioc);
                return -1;
            }
            trace_nbd_opt_go_success();
            return 1;
        }
        if (reply.type != NBD_REP_INFO) {
            error_setg(errp, "unexpected reply type %" PRIx32
                       " (%s), expected %x",
                       reply.type, nbd_rep_lookup(reply.type), NBD_REP_INFO);
            nbd_send_opt_abort(ioc);
            return -1;
        }
        if (len < sizeof(type)) {
            error_setg(errp, "NBD_REP_INFO length %" PRIu32 " is too short",
                       len);
            nbd_send_opt_abort(ioc);
            return -1;
        }
        if (nbd_read(ioc, &type, sizeof(type), errp) < 0) {
            error_prepend(errp, "failed to read info type");
            nbd_send_opt_abort(ioc);
            return -1;
        }
        len -= sizeof(type);
        be16_to_cpus(&type);
        switch (type) {
        case NBD_INFO_EXPORT:
            if (len != sizeof(info->size) + sizeof(info->flags)) {
                error_setg(errp, "remaining export info len %" PRIu32
                           " is unexpected size", len);
                nbd_send_opt_abort(ioc);
                return -1;
            }
            if (nbd_read(ioc, &info->size, sizeof(info->size), errp) < 0) {
                error_prepend(errp, "failed to read info size");
                nbd_send_opt_abort(ioc);
                return -1;
            }
            be64_to_cpus(&info->size);
            if (nbd_read(ioc, &info->flags, sizeof(info->flags), errp) < 0) {
                error_prepend(errp, "failed to read info flags");
                nbd_send_opt_abort(ioc);
                return -1;
            }
            be16_to_cpus(&info->flags);
            trace_nbd_receive_negotiate_size_flags(info->size, info->flags);
            break;

        case NBD_INFO_BLOCK_SIZE:
            if (len != sizeof(info->min_block) * 3) {
                error_setg(errp, "remaining export info len %" PRIu32
                           " is unexpected size", len);
                nbd_send_opt_abort(ioc);
                return -1;
            }
            if (nbd_read(ioc, &info->min_block, sizeof(info->min_block),
                         errp) < 0) {
                error_prepend(errp, "failed to read info minimum block size");
                nbd_send_opt_abort(ioc);
                return -1;
            }
            be32_to_cpus(&info->min_block);
            if (!is_power_of_2(info->min_block)) {
                error_setg(errp, "server minimum block size %" PRId32
                           "is not a power of two", info->min_block);
                nbd_send_opt_abort(ioc);
                return -1;
            }
            if (nbd_read(ioc, &info->opt_block, sizeof(info->opt_block),
                         errp) < 0) {
                error_prepend(errp, "failed to read info preferred block size");
                nbd_send_opt_abort(ioc);
                return -1;
            }
            be32_to_cpus(&info->opt_block);
            if (!is_power_of_2(info->opt_block) ||
                info->opt_block < info->min_block) {
                error_setg(errp, "server preferred block size %" PRId32
                           "is not valid", info->opt_block);
                nbd_send_opt_abort(ioc);
                return -1;
            }
            if (nbd_read(ioc, &info->max_block, sizeof(info->max_block),
                         errp) < 0) {
                error_prepend(errp, "failed to read info maximum block size");
                nbd_send_opt_abort(ioc);
                return -1;
            }
            be32_to_cpus(&info->max_block);
            trace_nbd_opt_go_info_block_size(info->min_block, info->opt_block,
                                             info->max_block);
            break;

        default:
            trace_nbd_opt_go_info_unknown(type, nbd_info_lookup(type));
            if (nbd_drop(ioc, len, errp) < 0) {
                error_prepend(errp, "Failed to read info payload");
                nbd_send_opt_abort(ioc);
                return -1;
            }
            break;
        }
    }
}

/* Return -1 on failure, 0 if wantname is an available export. */
static int nbd_receive_query_exports(QIOChannel *ioc,
                                     const char *wantname,
                                     Error **errp)
{
    bool foundExport = false;

    trace_nbd_receive_query_exports_start(wantname);
    if (nbd_send_option_request(ioc, NBD_OPT_LIST, 0, NULL, errp) < 0) {
        return -1;
    }

    while (1) {
        int ret = nbd_receive_list(ioc, wantname, &foundExport, errp);

        if (ret < 0) {
            /* Server gave unexpected reply */
            return -1;
        } else if (ret == 0) {
            /* Done iterating. */
            if (!foundExport) {
                error_setg(errp, "No export with name '%s' available",
                           wantname);
                nbd_send_opt_abort(ioc);
                return -1;
            }
            trace_nbd_receive_query_exports_success(wantname);
            return 0;
        }
    }
}

static QIOChannel *nbd_receive_starttls(QIOChannel *ioc,
                                        QCryptoTLSCreds *tlscreds,
                                        const char *hostname, Error **errp)
{
    nbd_opt_reply reply;
    QIOChannelTLS *tioc;
    struct NBDTLSHandshakeData data = { 0 };

    trace_nbd_receive_starttls_request();
    if (nbd_send_option_request(ioc, NBD_OPT_STARTTLS, 0, NULL, errp) < 0) {
        return NULL;
    }

    trace_nbd_receive_starttls_reply();
    if (nbd_receive_option_reply(ioc, NBD_OPT_STARTTLS, &reply, errp) < 0) {
        return NULL;
    }

    if (reply.type != NBD_REP_ACK) {
        error_setg(errp, "Server rejected request to start TLS %" PRIx32,
                   reply.type);
        nbd_send_opt_abort(ioc);
        return NULL;
    }

    if (reply.length != 0) {
        error_setg(errp, "Start TLS response was not zero %" PRIu32,
                   reply.length);
        nbd_send_opt_abort(ioc);
        return NULL;
    }

    trace_nbd_receive_starttls_new_client();
    tioc = qio_channel_tls_new_client(ioc, tlscreds, hostname, errp);
    if (!tioc) {
        return NULL;
    }
    qio_channel_set_name(QIO_CHANNEL(tioc), "nbd-client-tls");
    data.loop = g_main_loop_new(g_main_context_default(), FALSE);
    trace_nbd_receive_starttls_tls_handshake();
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


int nbd_receive_negotiate(QIOChannel *ioc, const char *name,
                          QCryptoTLSCreds *tlscreds, const char *hostname,
                          QIOChannel **outioc, NBDExportInfo *info,
                          Error **errp)
{
    char buf[256];
    uint64_t magic;
    int rc;
    bool zeroes = true;

    trace_nbd_receive_negotiate(tlscreds, hostname ? hostname : "<null>");

    rc = -EINVAL;

    if (outioc) {
        *outioc = NULL;
    }
    if (tlscreds && !outioc) {
        error_setg(errp, "Output I/O channel required for TLS");
        goto fail;
    }

    if (nbd_read(ioc, buf, 8, errp) < 0) {
        error_prepend(errp, "Failed to read data");
        goto fail;
    }

    buf[8] = '\0';
    if (strlen(buf) == 0) {
        error_setg(errp, "Server connection closed unexpectedly");
        goto fail;
    }

    magic = ldq_be_p(buf);
    trace_nbd_receive_negotiate_magic(magic);

    if (memcmp(buf, "NBDMAGIC", 8) != 0) {
        error_setg(errp, "Invalid magic received");
        goto fail;
    }

    if (nbd_read(ioc, &magic, sizeof(magic), errp) < 0) {
        error_prepend(errp, "Failed to read magic");
        goto fail;
    }
    magic = be64_to_cpu(magic);
    trace_nbd_receive_negotiate_magic(magic);

    if (magic == NBD_OPTS_MAGIC) {
        uint32_t clientflags = 0;
        uint16_t globalflags;
        bool fixedNewStyle = false;

        if (nbd_read(ioc, &globalflags, sizeof(globalflags), errp) < 0) {
            error_prepend(errp, "Failed to read server flags");
            goto fail;
        }
        globalflags = be16_to_cpu(globalflags);
        trace_nbd_receive_negotiate_server_flags(globalflags);
        if (globalflags & NBD_FLAG_FIXED_NEWSTYLE) {
            fixedNewStyle = true;
            clientflags |= NBD_FLAG_C_FIXED_NEWSTYLE;
        }
        if (globalflags & NBD_FLAG_NO_ZEROES) {
            zeroes = false;
            clientflags |= NBD_FLAG_C_NO_ZEROES;
        }
        /* client requested flags */
        clientflags = cpu_to_be32(clientflags);
        if (nbd_write(ioc, &clientflags, sizeof(clientflags), errp) < 0) {
            error_prepend(errp, "Failed to send clientflags field");
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
            trace_nbd_receive_negotiate_default_name();
            name = "";
        }
        if (fixedNewStyle) {
            int result;

            /* Try NBD_OPT_GO first - if it works, we are done (it
             * also gives us a good message if the server requires
             * TLS).  If it is not available, fall back to
             * NBD_OPT_LIST for nicer error messages about a missing
             * export, then use NBD_OPT_EXPORT_NAME.  */
            result = nbd_opt_go(ioc, name, info, errp);
            if (result < 0) {
                goto fail;
            }
            if (result > 0) {
                return 0;
            }
            /* Check our desired export is present in the
             * server export list. Since NBD_OPT_EXPORT_NAME
             * cannot return an error message, running this
             * query gives us better error reporting if the
             * export name is not available.
             */
            if (nbd_receive_query_exports(ioc, name, errp) < 0) {
                goto fail;
            }
        }
        /* write the export name request */
        if (nbd_send_option_request(ioc, NBD_OPT_EXPORT_NAME, -1, name,
                                    errp) < 0) {
            goto fail;
        }

        /* Read the response */
        if (nbd_read(ioc, &info->size, sizeof(info->size), errp) < 0) {
            error_prepend(errp, "Failed to read export length");
            goto fail;
        }
        be64_to_cpus(&info->size);

        if (nbd_read(ioc, &info->flags, sizeof(info->flags), errp) < 0) {
            error_prepend(errp, "Failed to read export flags");
            goto fail;
        }
        be16_to_cpus(&info->flags);
    } else if (magic == NBD_CLIENT_MAGIC) {
        uint32_t oldflags;

        if (name) {
            error_setg(errp, "Server does not support export names");
            goto fail;
        }
        if (tlscreds) {
            error_setg(errp, "Server does not support STARTTLS");
            goto fail;
        }

        if (nbd_read(ioc, &info->size, sizeof(info->size), errp) < 0) {
            error_prepend(errp, "Failed to read export length");
            goto fail;
        }
        be64_to_cpus(&info->size);

        if (nbd_read(ioc, &oldflags, sizeof(oldflags), errp) < 0) {
            error_prepend(errp, "Failed to read export flags");
            goto fail;
        }
        be32_to_cpus(&oldflags);
        if (oldflags & ~0xffff) {
            error_setg(errp, "Unexpected export flags %0x" PRIx32, oldflags);
            goto fail;
        }
        info->flags = oldflags;
    } else {
        error_setg(errp, "Bad magic received");
        goto fail;
    }

    trace_nbd_receive_negotiate_size_flags(info->size, info->flags);
    if (zeroes && nbd_drop(ioc, 124, errp) < 0) {
        error_prepend(errp, "Failed to read reserved block");
        goto fail;
    }
    rc = 0;

fail:
    return rc;
}

#ifdef __linux__
int nbd_init(int fd, QIOChannelSocket *sioc, NBDExportInfo *info,
             Error **errp)
{
    unsigned long sector_size = MAX(BDRV_SECTOR_SIZE, info->min_block);
    unsigned long sectors = info->size / sector_size;

    /* FIXME: Once the kernel module is patched to honor block sizes,
     * and to advertise that fact to user space, we should update the
     * hand-off to the kernel to use any block sizes we learned. */
    assert(!info->request_sizes);
    if (info->size / sector_size != sectors) {
        error_setg(errp, "Export size %" PRIu64 " too large for 32-bit kernel",
                   info->size);
        return -E2BIG;
    }

    trace_nbd_init_set_socket();

    if (ioctl(fd, NBD_SET_SOCK, (unsigned long) sioc->fd) < 0) {
        int serrno = errno;
        error_setg(errp, "Failed to set NBD socket");
        return -serrno;
    }

    trace_nbd_init_set_block_size(sector_size);

    if (ioctl(fd, NBD_SET_BLKSIZE, sector_size) < 0) {
        int serrno = errno;
        error_setg(errp, "Failed setting NBD block size");
        return -serrno;
    }

    trace_nbd_init_set_size(sectors);
    if (info->size % sector_size) {
        trace_nbd_init_trailing_bytes(info->size % sector_size);
    }

    if (ioctl(fd, NBD_SET_SIZE_BLOCKS, sectors) < 0) {
        int serrno = errno;
        error_setg(errp, "Failed setting size (in blocks)");
        return -serrno;
    }

    if (ioctl(fd, NBD_SET_FLAGS, (unsigned long) info->flags) < 0) {
        if (errno == ENOTTY) {
            int read_only = (info->flags & NBD_FLAG_READ_ONLY) != 0;
            trace_nbd_init_set_readonly();

            if (ioctl(fd, BLKROSET, (unsigned long) &read_only) < 0) {
                int serrno = errno;
                error_setg(errp, "Failed setting read-only attribute");
                return -serrno;
            }
        } else {
            int serrno = errno;
            error_setg(errp, "Failed setting flags");
            return -serrno;
        }
    }

    trace_nbd_init_finish();

    return 0;
}

int nbd_client(int fd)
{
    int ret;
    int serrno;

    trace_nbd_client_loop();

    ret = ioctl(fd, NBD_DO_IT);
    if (ret < 0 && errno == EPIPE) {
        /* NBD_DO_IT normally returns EPIPE when someone has disconnected
         * the socket via NBD_DISCONNECT.  We do not want to return 1 in
         * that case.
         */
        ret = 0;
    }
    serrno = errno;

    trace_nbd_client_loop_ret(ret, strerror(serrno));

    trace_nbd_client_clear_queue();
    ioctl(fd, NBD_CLEAR_QUE);

    trace_nbd_client_clear_socket();
    ioctl(fd, NBD_CLEAR_SOCK);

    errno = serrno;
    return ret;
}

int nbd_disconnect(int fd)
{
    ioctl(fd, NBD_CLEAR_QUE);
    ioctl(fd, NBD_DISCONNECT);
    ioctl(fd, NBD_CLEAR_SOCK);
    return 0;
}

#else
int nbd_init(int fd, QIOChannelSocket *ioc, NBDExportInfo *info,
	     Error **errp)
{
    error_setg(errp, "nbd_init is only supported on Linux");
    return -ENOTSUP;
}

int nbd_client(int fd)
{
    return -ENOTSUP;
}
int nbd_disconnect(int fd)
{
    return -ENOTSUP;
}
#endif

ssize_t nbd_send_request(QIOChannel *ioc, NBDRequest *request)
{
    uint8_t buf[NBD_REQUEST_SIZE];

    trace_nbd_send_request(request->from, request->len, request->handle,
                           request->flags, request->type,
                           nbd_cmd_lookup(request->type));

    stl_be_p(buf, NBD_REQUEST_MAGIC);
    stw_be_p(buf + 4, request->flags);
    stw_be_p(buf + 6, request->type);
    stq_be_p(buf + 8, request->handle);
    stq_be_p(buf + 16, request->from);
    stl_be_p(buf + 24, request->len);

    return nbd_write(ioc, buf, sizeof(buf), NULL);
}

ssize_t nbd_receive_reply(QIOChannel *ioc, NBDReply *reply, Error **errp)
{
    uint8_t buf[NBD_REPLY_SIZE];
    uint32_t magic;
    ssize_t ret;

    ret = nbd_read_eof(ioc, buf, sizeof(buf), errp);
    if (ret <= 0) {
        return ret;
    }

    if (ret != sizeof(buf)) {
        error_setg(errp, "read failed");
        return -EINVAL;
    }

    /* Reply
       [ 0 ..  3]    magic   (NBD_REPLY_MAGIC)
       [ 4 ..  7]    error   (0 == no error)
       [ 7 .. 15]    handle
     */

    magic = ldl_be_p(buf);
    reply->error  = ldl_be_p(buf + 4);
    reply->handle = ldq_be_p(buf + 8);

    reply->error = nbd_errno_to_system_errno(reply->error);

    if (reply->error == ESHUTDOWN) {
        /* This works even on mingw which lacks a native ESHUTDOWN */
        error_setg(errp, "server shutting down");
        return -EINVAL;
    }
    trace_nbd_receive_reply(magic, reply->error, reply->handle);

    if (magic != NBD_REPLY_MAGIC) {
        error_setg(errp, "invalid magic (got 0x%" PRIx32 ")", magic);
        return -EINVAL;
    }
    return sizeof(buf);
}

