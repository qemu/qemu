/*
 *  Copyright Red Hat
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
#include "qemu/queue.h"
#include "trace.h"
#include "nbd-internal.h"
#include "qemu/cutils.h"

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
    ERRP_GUARD();
    NBDOption req;
    QEMU_BUILD_BUG_ON(sizeof(req) != 16);

    if (len == -1) {
        req.length = len = strlen(data);
    }
    trace_nbd_send_option_request(opt, nbd_opt_lookup(opt), len);

    stq_be_p(&req.magic, NBD_OPTS_MAGIC);
    stl_be_p(&req.option, opt);
    stl_be_p(&req.length, len);

    if (nbd_write(ioc, &req, sizeof(req), errp) < 0) {
        error_prepend(errp, "Failed to send option request header: ");
        return -1;
    }

    if (len && nbd_write(ioc, (char *) data, len, errp) < 0) {
        error_prepend(errp, "Failed to send option request data: ");
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
                                    NBDOptionReply *reply, Error **errp)
{
    QEMU_BUILD_BUG_ON(sizeof(*reply) != 20);
    if (nbd_read(ioc, reply, sizeof(*reply), "option reply", errp) < 0) {
        nbd_send_opt_abort(ioc);
        return -1;
    }
    reply->magic = be64_to_cpu(reply->magic);
    reply->option = be32_to_cpu(reply->option);
    reply->type = be32_to_cpu(reply->type);
    reply->length = be32_to_cpu(reply->length);

    trace_nbd_receive_option_reply(reply->option, nbd_opt_lookup(reply->option),
                                   reply->type, nbd_rep_lookup(reply->type),
                                   reply->length);

    if (reply->magic != NBD_REP_MAGIC) {
        error_setg(errp, "Unexpected option reply magic");
        nbd_send_opt_abort(ioc);
        return -1;
    }
    if (reply->option != opt) {
        error_setg(errp, "Unexpected option type %u (%s), expected %u (%s)",
                   reply->option, nbd_opt_lookup(reply->option),
                   opt, nbd_opt_lookup(opt));
        nbd_send_opt_abort(ioc);
        return -1;
    }
    return 0;
}

/*
 * If reply represents success, return 1 without further action.  If
 * reply represents an error, consume the optional payload of the
 * packet on ioc.  Then return 0 for unsupported (so the client can
 * fall back to other approaches), where @strict determines if only
 * ERR_UNSUP or all errors fit that category, or -1 with errp set for
 * other errors.
 */
static int nbd_handle_reply_err(QIOChannel *ioc, NBDOptionReply *reply,
                                bool strict, Error **errp)
{
    ERRP_GUARD();
    g_autofree char *msg = NULL;

    if (!(reply->type & (1 << 31))) {
        return 1;
    }

    if (reply->length) {
        if (reply->length > NBD_MAX_BUFFER_SIZE) {
            error_setg(errp, "server error %" PRIu32
                       " (%s) message is too long",
                       reply->type, nbd_rep_lookup(reply->type));
            goto err;
        }
        msg = g_malloc(reply->length + 1);
        if (nbd_read(ioc, msg, reply->length, NULL, errp) < 0) {
            error_prepend(errp, "Failed to read option error %" PRIu32
                          " (%s) message: ",
                          reply->type, nbd_rep_lookup(reply->type));
            goto err;
        }
        msg[reply->length] = '\0';
        trace_nbd_server_error_msg(reply->type,
                                   nbd_reply_type_lookup(reply->type), msg);
    }

    if (reply->type == NBD_REP_ERR_UNSUP || !strict) {
        trace_nbd_reply_err_ignored(reply->option,
                                    nbd_opt_lookup(reply->option),
                                    reply->type, nbd_rep_lookup(reply->type));
        return 0;
    }

    switch (reply->type) {
    case NBD_REP_ERR_POLICY:
        error_setg(errp, "Denied by server for option %" PRIu32 " (%s)",
                   reply->option, nbd_opt_lookup(reply->option));
        break;

    case NBD_REP_ERR_INVALID:
        error_setg(errp, "Invalid parameters for option %" PRIu32 " (%s)",
                   reply->option, nbd_opt_lookup(reply->option));
        break;

    case NBD_REP_ERR_PLATFORM:
        error_setg(errp, "Server lacks support for option %" PRIu32 " (%s)",
                   reply->option, nbd_opt_lookup(reply->option));
        break;

    case NBD_REP_ERR_TLS_REQD:
        error_setg(errp, "TLS negotiation required before option %" PRIu32
                   " (%s)", reply->option, nbd_opt_lookup(reply->option));
        error_append_hint(errp, "Did you forget a valid tls-creds?\n");
        break;

    case NBD_REP_ERR_UNKNOWN:
        error_setg(errp, "Requested export not available");
        break;

    case NBD_REP_ERR_SHUTDOWN:
        error_setg(errp, "Server shutting down before option %" PRIu32 " (%s)",
                   reply->option, nbd_opt_lookup(reply->option));
        break;

    case NBD_REP_ERR_BLOCK_SIZE_REQD:
        error_setg(errp, "Server requires INFO_BLOCK_SIZE for option %" PRIu32
                   " (%s)", reply->option, nbd_opt_lookup(reply->option));
        break;

    default:
        error_setg(errp, "Unknown error code when asking for option %" PRIu32
                   " (%s)", reply->option, nbd_opt_lookup(reply->option));
        break;
    }

    if (msg) {
        error_append_hint(errp, "server reported: %s\n", msg);
    }

 err:
    nbd_send_opt_abort(ioc);
    return -1;
}

/* nbd_receive_list:
 * Process another portion of the NBD_OPT_LIST reply, populating any
 * name received into *@name. If @description is non-NULL, and the
 * server provided a description, that is also populated. The caller
 * must eventually call g_free() on success.
 * Returns 1 if name and description were set and iteration must continue,
 *         0 if iteration is complete (including if OPT_LIST unsupported),
 *         -1 with @errp set if an unrecoverable error occurred.
 */
static int nbd_receive_list(QIOChannel *ioc, char **name, char **description,
                            Error **errp)
{
    NBDOptionReply reply;
    uint32_t len;
    uint32_t namelen;
    g_autofree char *local_name = NULL;
    g_autofree char *local_desc = NULL;
    int error;

    if (nbd_receive_option_reply(ioc, NBD_OPT_LIST, &reply, errp) < 0) {
        return -1;
    }
    error = nbd_handle_reply_err(ioc, &reply, true, errp);
    if (error <= 0) {
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
        error_setg(errp, "Unexpected reply type %u (%s), expected %u (%s)",
                   reply.type, nbd_rep_lookup(reply.type),
                   NBD_REP_SERVER, nbd_rep_lookup(NBD_REP_SERVER));
        nbd_send_opt_abort(ioc);
        return -1;
    }

    if (len < sizeof(namelen) || len > NBD_MAX_BUFFER_SIZE) {
        error_setg(errp, "incorrect option length %" PRIu32, len);
        nbd_send_opt_abort(ioc);
        return -1;
    }
    if (nbd_read32(ioc, &namelen, "option name length", errp) < 0) {
        nbd_send_opt_abort(ioc);
        return -1;
    }
    len -= sizeof(namelen);
    if (len < namelen || namelen > NBD_MAX_STRING_SIZE) {
        error_setg(errp, "incorrect name length in server's list response");
        nbd_send_opt_abort(ioc);
        return -1;
    }

    local_name = g_malloc(namelen + 1);
    if (nbd_read(ioc, local_name, namelen, "export name", errp) < 0) {
        nbd_send_opt_abort(ioc);
        return -1;
    }
    local_name[namelen] = '\0';
    len -= namelen;
    if (len) {
        if (len > NBD_MAX_STRING_SIZE) {
            error_setg(errp, "incorrect description length in server's "
                       "list response");
            nbd_send_opt_abort(ioc);
            return -1;
        }
        local_desc = g_malloc(len + 1);
        if (nbd_read(ioc, local_desc, len, "export description", errp) < 0) {
            nbd_send_opt_abort(ioc);
            return -1;
        }
        local_desc[len] = '\0';
    }

    trace_nbd_receive_list(local_name, local_desc ?: "");
    *name = g_steal_pointer(&local_name);
    if (description) {
        *description = g_steal_pointer(&local_desc);
    }
    return 1;
}


/*
 * nbd_opt_info_or_go:
 * Send option for NBD_OPT_INFO or NBD_OPT_GO and parse the reply.
 * Returns -1 if the option proves the export @info->name cannot be
 * used, 0 if the option is unsupported (fall back to NBD_OPT_LIST and
 * NBD_OPT_EXPORT_NAME in that case), and > 0 if the export is good to
 * go (with the rest of @info populated).
 */
static int nbd_opt_info_or_go(QIOChannel *ioc, uint32_t opt,
                              NBDExportInfo *info, Error **errp)
{
    ERRP_GUARD();
    NBDOptionReply reply;
    uint32_t len = strlen(info->name);
    uint16_t type;
    int error;
    char *buf;

    /* The protocol requires that the server send NBD_INFO_EXPORT with
     * a non-zero flags (at least NBD_FLAG_HAS_FLAGS must be set); so
     * flags still 0 is a witness of a broken server. */
    info->flags = 0;

    assert(opt == NBD_OPT_GO || opt == NBD_OPT_INFO);
    trace_nbd_opt_info_go_start(nbd_opt_lookup(opt), info->name);
    buf = g_malloc(4 + len + 2 + 2 * info->request_sizes + 1);
    stl_be_p(buf, len);
    memcpy(buf + 4, info->name, len);
    /* At most one request, everything else up to server */
    stw_be_p(buf + 4 + len, info->request_sizes);
    if (info->request_sizes) {
        stw_be_p(buf + 4 + len + 2, NBD_INFO_BLOCK_SIZE);
    }
    error = nbd_send_option_request(ioc, opt,
                                    4 + len + 2 + 2 * info->request_sizes,
                                    buf, errp);
    g_free(buf);
    if (error < 0) {
        return -1;
    }

    while (1) {
        if (nbd_receive_option_reply(ioc, opt, &reply, errp) < 0) {
            return -1;
        }
        error = nbd_handle_reply_err(ioc, &reply, true, errp);
        if (error <= 0) {
            return error;
        }
        len = reply.length;

        if (reply.type == NBD_REP_ACK) {
            /*
             * Server is done sending info, and moved into transmission
             * phase for NBD_OPT_GO, but make sure it sent flags
             */
            if (len) {
                error_setg(errp, "server sent invalid NBD_REP_ACK");
                return -1;
            }
            if (!info->flags) {
                error_setg(errp, "broken server omitted NBD_INFO_EXPORT");
                return -1;
            }
            trace_nbd_opt_info_go_success(nbd_opt_lookup(opt));
            return 1;
        }
        if (reply.type != NBD_REP_INFO) {
            error_setg(errp, "unexpected reply type %u (%s), expected %u (%s)",
                       reply.type, nbd_rep_lookup(reply.type),
                       NBD_REP_INFO, nbd_rep_lookup(NBD_REP_INFO));
            nbd_send_opt_abort(ioc);
            return -1;
        }
        if (len < sizeof(type)) {
            error_setg(errp, "NBD_REP_INFO length %" PRIu32 " is too short",
                       len);
            nbd_send_opt_abort(ioc);
            return -1;
        }
        if (nbd_read16(ioc, &type, "info type", errp) < 0) {
            nbd_send_opt_abort(ioc);
            return -1;
        }
        len -= sizeof(type);
        switch (type) {
        case NBD_INFO_EXPORT:
            if (len != sizeof(info->size) + sizeof(info->flags)) {
                error_setg(errp, "remaining export info len %" PRIu32
                           " is unexpected size", len);
                nbd_send_opt_abort(ioc);
                return -1;
            }
            if (nbd_read64(ioc, &info->size, "info size", errp) < 0) {
                nbd_send_opt_abort(ioc);
                return -1;
            }
            if (nbd_read16(ioc, &info->flags, "info flags", errp) < 0) {
                nbd_send_opt_abort(ioc);
                return -1;
            }
            if (info->min_block &&
                !QEMU_IS_ALIGNED(info->size, info->min_block)) {
                error_setg(errp, "export size %" PRIu64 " is not multiple of "
                           "minimum block size %" PRIu32, info->size,
                           info->min_block);
                nbd_send_opt_abort(ioc);
                return -1;
            }
            trace_nbd_receive_negotiate_size_flags(info->size, info->flags);
            break;

        case NBD_INFO_BLOCK_SIZE:
            if (len != sizeof(info->min_block) * 3) {
                error_setg(errp, "remaining export info len %" PRIu32
                           " is unexpected size", len);
                nbd_send_opt_abort(ioc);
                return -1;
            }
            if (nbd_read32(ioc, &info->min_block, "info minimum block size",
                           errp) < 0) {
                nbd_send_opt_abort(ioc);
                return -1;
            }
            if (!is_power_of_2(info->min_block)) {
                error_setg(errp, "server minimum block size %" PRIu32
                           " is not a power of two", info->min_block);
                nbd_send_opt_abort(ioc);
                return -1;
            }
            if (nbd_read32(ioc, &info->opt_block, "info preferred block size",
                           errp) < 0)
            {
                nbd_send_opt_abort(ioc);
                return -1;
            }
            if (!is_power_of_2(info->opt_block) ||
                info->opt_block < info->min_block) {
                error_setg(errp, "server preferred block size %" PRIu32
                           " is not valid", info->opt_block);
                nbd_send_opt_abort(ioc);
                return -1;
            }
            if (nbd_read32(ioc, &info->max_block, "info maximum block size",
                           errp) < 0)
            {
                nbd_send_opt_abort(ioc);
                return -1;
            }
            if (info->max_block < info->min_block) {
                error_setg(errp, "server maximum block size %" PRIu32
                           " is not valid", info->max_block);
                nbd_send_opt_abort(ioc);
                return -1;
            }
            trace_nbd_opt_info_block_size(info->min_block, info->opt_block,
                                          info->max_block);
            break;

        default:
            /*
             * Not worth the bother to check if NBD_INFO_NAME or
             * NBD_INFO_DESCRIPTION exceed NBD_MAX_STRING_SIZE.
             */
            trace_nbd_opt_info_unknown(type, nbd_info_lookup(type));
            if (nbd_drop(ioc, len, errp) < 0) {
                error_prepend(errp, "Failed to read info payload: ");
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
    bool list_empty = true;
    bool found_export = false;

    trace_nbd_receive_query_exports_start(wantname);
    if (nbd_send_option_request(ioc, NBD_OPT_LIST, 0, NULL, errp) < 0) {
        return -1;
    }

    while (1) {
        char *name;
        int ret = nbd_receive_list(ioc, &name, NULL, errp);

        if (ret < 0) {
            /* Server gave unexpected reply */
            return -1;
        } else if (ret == 0) {
            /* Done iterating. */
            if (list_empty) {
                /*
                 * We don't have enough context to tell a server that
                 * sent an empty list apart from a server that does
                 * not support the list command; but as this function
                 * is just used to trigger a nicer error message
                 * before trying NBD_OPT_EXPORT_NAME, assume the
                 * export is available.
                 */
                return 0;
            } else if (!found_export) {
                error_setg(errp, "No export with name '%s' available",
                           wantname);
                nbd_send_opt_abort(ioc);
                return -1;
            }
            trace_nbd_receive_query_exports_success(wantname);
            return 0;
        }
        list_empty = false;
        if (!strcmp(name, wantname)) {
            found_export = true;
        }
        g_free(name);
    }
}

/*
 * nbd_request_simple_option: Send an option request, and parse the reply.
 * @strict controls whether ERR_UNSUP or all errors produce 0 status.
 * return 1 for successful negotiation,
 *        0 if operation is unsupported,
 *        -1 with errp set for any other error
 */
static int nbd_request_simple_option(QIOChannel *ioc, int opt, bool strict,
                                     Error **errp)
{
    NBDOptionReply reply;
    int error;

    if (nbd_send_option_request(ioc, opt, 0, NULL, errp) < 0) {
        return -1;
    }

    if (nbd_receive_option_reply(ioc, opt, &reply, errp) < 0) {
        return -1;
    }
    error = nbd_handle_reply_err(ioc, &reply, strict, errp);
    if (error <= 0) {
        return error;
    }

    if (reply.type != NBD_REP_ACK) {
        error_setg(errp, "Server answered option %d (%s) with unexpected "
                   "reply %" PRIu32 " (%s)", opt, nbd_opt_lookup(opt),
                   reply.type, nbd_rep_lookup(reply.type));
        nbd_send_opt_abort(ioc);
        return -1;
    }

    if (reply.length != 0) {
        error_setg(errp, "Option %d ('%s') response length is %" PRIu32
                   " (it should be zero)", opt, nbd_opt_lookup(opt),
                   reply.length);
        nbd_send_opt_abort(ioc);
        return -1;
    }

    return 1;
}

static QIOChannel *nbd_receive_starttls(QIOChannel *ioc,
                                        QCryptoTLSCreds *tlscreds,
                                        const char *hostname, Error **errp)
{
    int ret;
    QIOChannelTLS *tioc;
    struct NBDTLSHandshakeData data = { 0 };

    ret = nbd_request_simple_option(ioc, NBD_OPT_STARTTLS, true, errp);
    if (ret <= 0) {
        if (ret == 0) {
            error_setg(errp, "Server don't support STARTTLS option");
            nbd_send_opt_abort(ioc);
        }
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
                              NULL,
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

/*
 * nbd_send_meta_query:
 * Send 0 or 1 set/list meta context queries.
 * Return 0 on success, -1 with errp set for any error
 */
static int nbd_send_meta_query(QIOChannel *ioc, uint32_t opt,
                               const char *export, const char *query,
                               Error **errp)
{
    int ret;
    uint32_t export_len;
    uint32_t queries = !!query;
    uint32_t query_len = 0;
    uint32_t data_len;
    char *data;
    char *p;

    assert(strnlen(export, NBD_MAX_STRING_SIZE + 1) <= NBD_MAX_STRING_SIZE);
    export_len = strlen(export);
    data_len = sizeof(export_len) + export_len + sizeof(queries);
    if (query) {
        assert(strnlen(query, NBD_MAX_STRING_SIZE + 1) <= NBD_MAX_STRING_SIZE);
        query_len = strlen(query);
        data_len += sizeof(query_len) + query_len;
    } else {
        assert(opt == NBD_OPT_LIST_META_CONTEXT);
    }
    p = data = g_malloc(data_len);

    trace_nbd_opt_meta_request(nbd_opt_lookup(opt), query ?: "(all)", export);
    stl_be_p(p, export_len);
    memcpy(p += sizeof(export_len), export, export_len);
    stl_be_p(p += export_len, queries);
    if (query) {
        stl_be_p(p += sizeof(queries), query_len);
        memcpy(p += sizeof(query_len), query, query_len);
    }

    ret = nbd_send_option_request(ioc, opt, data_len, data, errp);
    g_free(data);
    return ret;
}

/*
 * nbd_receive_one_meta_context:
 * Called in a loop to receive and trace one set/list meta context reply.
 * Pass non-NULL @name or @id to collect results back to the caller, which
 * must eventually call g_free().
 * return 1 if name is set and iteration must continue,
 *        0 if iteration is complete (including if option is unsupported),
 *        -1 with errp set for any error
 */
static int nbd_receive_one_meta_context(QIOChannel *ioc,
                                        uint32_t opt,
                                        char **name,
                                        uint32_t *id,
                                        Error **errp)
{
    int ret;
    NBDOptionReply reply;
    char *local_name = NULL;
    uint32_t local_id;

    if (nbd_receive_option_reply(ioc, opt, &reply, errp) < 0) {
        return -1;
    }

    ret = nbd_handle_reply_err(ioc, &reply, false, errp);
    if (ret <= 0) {
        return ret;
    }

    if (reply.type == NBD_REP_ACK) {
        if (reply.length != 0) {
            error_setg(errp, "Unexpected length to ACK response");
            nbd_send_opt_abort(ioc);
            return -1;
        }
        return 0;
    } else if (reply.type != NBD_REP_META_CONTEXT) {
        error_setg(errp, "Unexpected reply type %u (%s), expected %u (%s)",
                   reply.type, nbd_rep_lookup(reply.type),
                   NBD_REP_META_CONTEXT, nbd_rep_lookup(NBD_REP_META_CONTEXT));
        nbd_send_opt_abort(ioc);
        return -1;
    }

    if (reply.length <= sizeof(local_id) ||
        reply.length > NBD_MAX_BUFFER_SIZE) {
        error_setg(errp, "Failed to negotiate meta context, server "
                   "answered with unexpected length %" PRIu32,
                   reply.length);
        nbd_send_opt_abort(ioc);
        return -1;
    }

    if (nbd_read32(ioc, &local_id, "context id", errp) < 0) {
        return -1;
    }

    reply.length -= sizeof(local_id);
    local_name = g_malloc(reply.length + 1);
    if (nbd_read(ioc, local_name, reply.length, "context name", errp) < 0) {
        g_free(local_name);
        return -1;
    }
    local_name[reply.length] = '\0';
    trace_nbd_opt_meta_reply(nbd_opt_lookup(opt), local_name, local_id);

    if (name) {
        *name = local_name;
    } else {
        g_free(local_name);
    }
    if (id) {
        *id = local_id;
    }
    return 1;
}

/*
 * nbd_negotiate_simple_meta_context:
 * Request the server to set the meta context for export @info->name
 * using @info->x_dirty_bitmap with a fallback to "base:allocation",
 * setting @info->context_id to the resulting id. Fail if the server
 * responds with more than one context or with a context different
 * than the query.
 * return 1 for successful negotiation,
 *        0 if operation is unsupported,
 *        -1 with errp set for any other error
 */
static int nbd_negotiate_simple_meta_context(QIOChannel *ioc,
                                             NBDExportInfo *info,
                                             Error **errp)
{
    /*
     * TODO: Removing the x_dirty_bitmap hack will mean refactoring
     * this function to request and store ids for multiple contexts
     * (both base:allocation and a dirty bitmap), at which point this
     * function should lose the term _simple.
     */
    int ret;
    const char *context = info->x_dirty_bitmap ?: "base:allocation";
    bool received = false;
    char *name = NULL;

    if (nbd_send_meta_query(ioc, NBD_OPT_SET_META_CONTEXT,
                            info->name, context, errp) < 0) {
        return -1;
    }

    ret = nbd_receive_one_meta_context(ioc, NBD_OPT_SET_META_CONTEXT,
                                       &name, &info->context_id, errp);
    if (ret < 0) {
        return -1;
    }
    if (ret == 1) {
        if (strcmp(context, name)) {
            error_setg(errp, "Failed to negotiate meta context '%s', server "
                       "answered with different context '%s'", context,
                       name);
            g_free(name);
            nbd_send_opt_abort(ioc);
            return -1;
        }
        g_free(name);
        received = true;

        ret = nbd_receive_one_meta_context(ioc, NBD_OPT_SET_META_CONTEXT,
                                           NULL, NULL, errp);
        if (ret < 0) {
            return -1;
        }
    }
    if (ret != 0) {
        error_setg(errp, "Server answered with more than one context");
        nbd_send_opt_abort(ioc);
        return -1;
    }
    return received;
}

/*
 * nbd_list_meta_contexts:
 * Request the server to list all meta contexts for export @info->name.
 * return 0 if list is complete (even if empty),
 *        -1 with errp set for any error
 */
static int nbd_list_meta_contexts(QIOChannel *ioc,
                                  NBDExportInfo *info,
                                  Error **errp)
{
    int ret;
    int seen_any = false;
    int seen_qemu = false;

    if (nbd_send_meta_query(ioc, NBD_OPT_LIST_META_CONTEXT,
                            info->name, NULL, errp) < 0) {
        return -1;
    }

    while (1) {
        char *context;

        ret = nbd_receive_one_meta_context(ioc, NBD_OPT_LIST_META_CONTEXT,
                                           &context, NULL, errp);
        if (ret == 0 && seen_any && !seen_qemu) {
            /*
             * Work around qemu 3.0 bug: the server forgot to send
             * "qemu:" replies to 0 queries. If we saw at least one
             * reply (probably base:allocation), but none of them were
             * qemu:, then run a more specific query to make sure.
             */
            seen_qemu = true;
            if (nbd_send_meta_query(ioc, NBD_OPT_LIST_META_CONTEXT,
                                    info->name, "qemu:", errp) < 0) {
                return -1;
            }
            continue;
        }
        if (ret <= 0) {
            return ret;
        }
        seen_any = true;
        seen_qemu |= strstart(context, "qemu:", NULL);
        info->contexts = g_renew(char *, info->contexts, ++info->n_contexts);
        info->contexts[info->n_contexts - 1] = context;
    }
}

/*
 * nbd_start_negotiate:
 * Start the handshake to the server.  After a positive return, the server
 * is ready to accept additional NBD_OPT requests.
 * Returns: negative errno: failure talking to server
 *          non-negative: enum NBDMode describing server abilities
 */
static int nbd_start_negotiate(QIOChannel *ioc, QCryptoTLSCreds *tlscreds,
                               const char *hostname, QIOChannel **outioc,
                               NBDMode max_mode, bool *zeroes,
                               Error **errp)
{
    ERRP_GUARD();
    uint64_t magic;

    trace_nbd_start_negotiate(tlscreds, hostname ? hostname : "<null>");

    if (zeroes) {
        *zeroes = true;
    }
    if (outioc) {
        *outioc = NULL;
    }
    if (tlscreds && !outioc) {
        error_setg(errp, "Output I/O channel required for TLS");
        return -EINVAL;
    }

    if (nbd_read64(ioc, &magic, "initial magic", errp) < 0) {
        return -EINVAL;
    }
    trace_nbd_receive_negotiate_magic(magic);

    if (magic != NBD_INIT_MAGIC) {
        error_setg(errp, "Bad initial magic received: 0x%" PRIx64, magic);
        return -EINVAL;
    }

    if (nbd_read64(ioc, &magic, "server magic", errp) < 0) {
        return -EINVAL;
    }
    trace_nbd_receive_negotiate_magic(magic);

    if (magic == NBD_OPTS_MAGIC) {
        uint32_t clientflags = 0;
        uint16_t globalflags;
        bool fixedNewStyle = false;

        if (nbd_read16(ioc, &globalflags, "server flags", errp) < 0) {
            return -EINVAL;
        }
        trace_nbd_receive_negotiate_server_flags(globalflags);
        if (globalflags & NBD_FLAG_FIXED_NEWSTYLE) {
            fixedNewStyle = true;
            clientflags |= NBD_FLAG_C_FIXED_NEWSTYLE;
        }
        if (globalflags & NBD_FLAG_NO_ZEROES) {
            if (zeroes) {
                *zeroes = false;
            }
            clientflags |= NBD_FLAG_C_NO_ZEROES;
        }
        /* client requested flags */
        clientflags = cpu_to_be32(clientflags);
        if (nbd_write(ioc, &clientflags, sizeof(clientflags), errp) < 0) {
            error_prepend(errp, "Failed to send clientflags field: ");
            return -EINVAL;
        }
        if (tlscreds) {
            if (fixedNewStyle) {
                *outioc = nbd_receive_starttls(ioc, tlscreds, hostname, errp);
                if (!*outioc) {
                    return -EINVAL;
                }
                ioc = *outioc;
            } else {
                error_setg(errp, "Server does not support STARTTLS");
                return -EINVAL;
            }
        }
        if (fixedNewStyle) {
            int result = 0;

            if (max_mode >= NBD_MODE_EXTENDED) {
                result = nbd_request_simple_option(ioc,
                                                   NBD_OPT_EXTENDED_HEADERS,
                                                   false, errp);
                if (result) {
                    return result < 0 ? -EINVAL : NBD_MODE_EXTENDED;
                }
            }
            if (max_mode >= NBD_MODE_STRUCTURED) {
                result = nbd_request_simple_option(ioc,
                                                   NBD_OPT_STRUCTURED_REPLY,
                                                   false, errp);
                if (result) {
                    return result < 0 ? -EINVAL : NBD_MODE_STRUCTURED;
                }
            }
            return NBD_MODE_SIMPLE;
        } else {
            return NBD_MODE_EXPORT_NAME;
        }
    } else if (magic == NBD_CLIENT_MAGIC) {
        if (tlscreds) {
            error_setg(errp, "Server does not support STARTTLS");
            return -EINVAL;
        }
        return NBD_MODE_OLDSTYLE;
    } else {
        error_setg(errp, "Bad server magic received: 0x%" PRIx64, magic);
        return -EINVAL;
    }
}

/*
 * nbd_negotiate_finish_oldstyle:
 * Populate @info with the size and export flags from an oldstyle server,
 * but does not consume 124 bytes of reserved zero padding.
 * Returns 0 on success, -1 with @errp set on failure
 */
static int nbd_negotiate_finish_oldstyle(QIOChannel *ioc, NBDExportInfo *info,
                                         Error **errp)
{
    uint32_t oldflags;

    if (nbd_read64(ioc, &info->size, "export length", errp) < 0) {
        return -EINVAL;
    }

    if (nbd_read32(ioc, &oldflags, "export flags", errp) < 0) {
        return -EINVAL;
    }
    if (oldflags & ~0xffff) {
        error_setg(errp, "Unexpected export flags %0x" PRIx32, oldflags);
        return -EINVAL;
    }
    info->flags = oldflags;
    return 0;
}

/*
 * nbd_receive_negotiate:
 * Connect to server, complete negotiation, and move into transmission phase.
 * Returns: negative errno: failure talking to server
 *          0: server is connected
 */
int nbd_receive_negotiate(QIOChannel *ioc, QCryptoTLSCreds *tlscreds,
                          const char *hostname, QIOChannel **outioc,
                          NBDExportInfo *info, Error **errp)
{
    ERRP_GUARD();
    int result;
    bool zeroes;
    bool base_allocation = info->base_allocation;

    assert(info->name && strlen(info->name) <= NBD_MAX_STRING_SIZE);
    trace_nbd_receive_negotiate_name(info->name);

    result = nbd_start_negotiate(ioc, tlscreds, hostname, outioc,
                                 info->mode, &zeroes, errp);
    if (result < 0) {
        return result;
    }

    info->mode = result;
    info->base_allocation = false;
    if (tlscreds && *outioc) {
        ioc = *outioc;
    }

    switch (info->mode) {
    case NBD_MODE_EXTENDED:
    case NBD_MODE_STRUCTURED:
        if (base_allocation) {
            result = nbd_negotiate_simple_meta_context(ioc, info, errp);
            if (result < 0) {
                return -EINVAL;
            }
            info->base_allocation = result == 1;
        }
        /* fall through */
    case NBD_MODE_SIMPLE:
        /* Try NBD_OPT_GO first - if it works, we are done (it
         * also gives us a good message if the server requires
         * TLS).  If it is not available, fall back to
         * NBD_OPT_LIST for nicer error messages about a missing
         * export, then use NBD_OPT_EXPORT_NAME.  */
        result = nbd_opt_info_or_go(ioc, NBD_OPT_GO, info, errp);
        if (result < 0) {
            return -EINVAL;
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
        if (nbd_receive_query_exports(ioc, info->name, errp) < 0) {
            return -EINVAL;
        }
        /* fall through */
    case NBD_MODE_EXPORT_NAME:
        /* write the export name request */
        if (nbd_send_option_request(ioc, NBD_OPT_EXPORT_NAME, -1, info->name,
                                    errp) < 0) {
            return -EINVAL;
        }

        /* Read the response */
        if (nbd_read64(ioc, &info->size, "export length", errp) < 0) {
            return -EINVAL;
        }

        if (nbd_read16(ioc, &info->flags, "export flags", errp) < 0) {
            return -EINVAL;
        }
        break;
    case NBD_MODE_OLDSTYLE:
        if (*info->name) {
            error_setg(errp, "Server does not support non-empty export names");
            return -EINVAL;
        }
        if (nbd_negotiate_finish_oldstyle(ioc, info, errp) < 0) {
            return -EINVAL;
        }
        break;
    default:
        g_assert_not_reached();
    }

    trace_nbd_receive_negotiate_size_flags(info->size, info->flags);
    if (zeroes && nbd_drop(ioc, 124, errp) < 0) {
        error_prepend(errp, "Failed to read reserved block: ");
        return -EINVAL;
    }
    return 0;
}

/* Clean up result of nbd_receive_export_list */
void nbd_free_export_list(NBDExportInfo *info, int count)
{
    int i, j;

    if (!info) {
        return;
    }

    for (i = 0; i < count; i++) {
        g_free(info[i].name);
        g_free(info[i].description);
        for (j = 0; j < info[i].n_contexts; j++) {
            g_free(info[i].contexts[j]);
        }
        g_free(info[i].contexts);
    }
    g_free(info);
}

/*
 * nbd_receive_export_list:
 * Query details about a server's exports, then disconnect without
 * going into transmission phase. Return a count of the exports listed
 * in @info by the server, or -1 on error. Caller must free @info using
 * nbd_free_export_list().
 */
int nbd_receive_export_list(QIOChannel *ioc, QCryptoTLSCreds *tlscreds,
                            const char *hostname, NBDExportInfo **info,
                            Error **errp)
{
    int result;
    int count = 0;
    int i;
    int rc;
    int ret = -1;
    NBDExportInfo *array = NULL;
    QIOChannel *sioc = NULL;

    *info = NULL;
    result = nbd_start_negotiate(ioc, tlscreds, hostname, &sioc,
                                 NBD_MODE_EXTENDED, NULL, errp);
    if (tlscreds && sioc) {
        ioc = sioc;
    }
    if (result < 0) {
        goto out;
    }

    switch ((NBDMode)result) {
    case NBD_MODE_SIMPLE:
    case NBD_MODE_STRUCTURED:
    case NBD_MODE_EXTENDED:
        /* newstyle - use NBD_OPT_LIST to populate array, then try
         * NBD_OPT_INFO on each array member. If structured replies
         * are enabled, also try NBD_OPT_LIST_META_CONTEXT. */
        if (nbd_send_option_request(ioc, NBD_OPT_LIST, 0, NULL, errp) < 0) {
            goto out;
        }
        while (1) {
            char *name;
            char *desc;

            rc = nbd_receive_list(ioc, &name, &desc, errp);
            if (rc < 0) {
                goto out;
            } else if (rc == 0) {
                break;
            }
            array = g_renew(NBDExportInfo, array, ++count);
            memset(&array[count - 1], 0, sizeof(*array));
            array[count - 1].name = name;
            array[count - 1].description = desc;
            array[count - 1].mode = result;
        }

        for (i = 0; i < count; i++) {
            array[i].request_sizes = true;
            rc = nbd_opt_info_or_go(ioc, NBD_OPT_INFO, &array[i], errp);
            if (rc < 0) {
                goto out;
            } else if (rc == 0) {
                /*
                 * Pointless to try rest of loop. If OPT_INFO doesn't work,
                 * it's unlikely that meta contexts work either
                 */
                break;
            }

            if (result >= NBD_MODE_STRUCTURED &&
                nbd_list_meta_contexts(ioc, &array[i], errp) < 0) {
                goto out;
            }
        }

        /* Send NBD_OPT_ABORT as a courtesy before hanging up */
        nbd_send_opt_abort(ioc);
        break;
    case NBD_MODE_EXPORT_NAME:
        error_setg(errp, "Server does not support export lists");
        /* We can't even send NBD_OPT_ABORT, so merely hang up */
        goto out;
    case NBD_MODE_OLDSTYLE:
        /* Lone export name is implied, but we can parse length and flags */
        array = g_new0(NBDExportInfo, 1);
        array->name = g_strdup("");
        array->mode = NBD_MODE_OLDSTYLE;
        count = 1;

        if (nbd_negotiate_finish_oldstyle(ioc, array, errp) < 0) {
            goto out;
        }

        /* Send NBD_CMD_DISC as a courtesy to the server, but ignore all
         * errors now that we have the information we wanted. */
        if (nbd_drop(ioc, 124, NULL) == 0) {
            NBDRequest request = { .type = NBD_CMD_DISC, .mode = result };

            nbd_send_request(ioc, &request);
        }
        break;
    default:
        g_assert_not_reached();
    }

    *info = array;
    array = NULL;
    ret = count;

 out:
    qio_channel_shutdown(ioc, QIO_CHANNEL_SHUTDOWN_BOTH, NULL);
    qio_channel_close(ioc, NULL);
    object_unref(OBJECT(sioc));
    nbd_free_export_list(array, count);
    return ret;
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

#endif /* __linux__ */

int nbd_send_request(QIOChannel *ioc, NBDRequest *request)
{
    uint8_t buf[NBD_EXTENDED_REQUEST_SIZE];
    size_t len;

    trace_nbd_send_request(request->from, request->len, request->cookie,
                           request->flags, request->type,
                           nbd_cmd_lookup(request->type));

    stw_be_p(buf + 4, request->flags);
    stw_be_p(buf + 6, request->type);
    stq_be_p(buf + 8, request->cookie);
    stq_be_p(buf + 16, request->from);
    if (request->mode >= NBD_MODE_EXTENDED) {
        stl_be_p(buf, NBD_EXTENDED_REQUEST_MAGIC);
        stq_be_p(buf + 24, request->len);
        len = NBD_EXTENDED_REQUEST_SIZE;
    } else {
        assert(request->len <= UINT32_MAX);
        stl_be_p(buf, NBD_REQUEST_MAGIC);
        stl_be_p(buf + 24, request->len);
        len = NBD_REQUEST_SIZE;
    }

    return nbd_write(ioc, buf, len, NULL);
}

/* nbd_receive_simple_reply
 * Read simple reply except magic field (which should be already read).
 * Payload is not read (payload is possible for CMD_READ, but here we even
 * don't know whether it take place or not).
 */
static int nbd_receive_simple_reply(QIOChannel *ioc, NBDSimpleReply *reply,
                                    Error **errp)
{
    int ret;

    assert(reply->magic == NBD_SIMPLE_REPLY_MAGIC);

    ret = nbd_read(ioc, (uint8_t *)reply + sizeof(reply->magic),
                   sizeof(*reply) - sizeof(reply->magic), "reply", errp);
    if (ret < 0) {
        return ret;
    }

    reply->error = be32_to_cpu(reply->error);
    reply->cookie = be64_to_cpu(reply->cookie);

    return 0;
}

/* nbd_receive_reply_chunk_header
 * Read structured reply chunk except magic field (which should be already
 * read).  Normalize into the compact form.
 * Payload is not read.
 */
static int nbd_receive_reply_chunk_header(QIOChannel *ioc, NBDReply *chunk,
                                          Error **errp)
{
    int ret;
    size_t len;
    uint64_t payload_len;

    if (chunk->magic == NBD_STRUCTURED_REPLY_MAGIC) {
        len = sizeof(chunk->structured);
    } else {
        assert(chunk->magic == NBD_EXTENDED_REPLY_MAGIC);
        len = sizeof(chunk->extended);
    }

    ret = nbd_read(ioc, (uint8_t *)chunk + sizeof(chunk->magic),
                   len - sizeof(chunk->magic), "structured chunk",
                   errp);
    if (ret < 0) {
        return ret;
    }

    /* flags, type, and cookie occupy same space between forms */
    chunk->structured.flags = be16_to_cpu(chunk->structured.flags);
    chunk->structured.type = be16_to_cpu(chunk->structured.type);
    chunk->structured.cookie = be64_to_cpu(chunk->structured.cookie);

    /*
     * Because we use BLOCK_STATUS with REQ_ONE, and cap READ requests
     * at 32M, no valid server should send us payload larger than
     * this.  Even if we stopped using REQ_ONE, sane servers will cap
     * the number of extents they return for block status.
     */
    if (chunk->magic == NBD_STRUCTURED_REPLY_MAGIC) {
        payload_len = be32_to_cpu(chunk->structured.length);
    } else {
        /* For now, we are ignoring the extended header offset. */
        payload_len = be64_to_cpu(chunk->extended.length);
        chunk->magic = NBD_STRUCTURED_REPLY_MAGIC;
    }
    if (payload_len > NBD_MAX_BUFFER_SIZE + sizeof(NBDStructuredReadData)) {
        error_setg(errp, "server chunk %" PRIu32 " (%s) payload is too long",
                   chunk->structured.type,
                   nbd_rep_lookup(chunk->structured.type));
        return -EINVAL;
    }
    chunk->structured.length = payload_len;

    return 0;
}

/* nbd_read_eof
 * Tries to read @size bytes from @ioc.
 * Returns 1 on success
 *         0 on eof, when no data was read (errp is not set)
 *         negative errno on failure (errp is set)
 */
static inline int coroutine_fn
nbd_read_eof(BlockDriverState *bs, QIOChannel *ioc, void *buffer, size_t size,
             Error **errp)
{
    bool partial = false;

    assert(size);
    while (size > 0) {
        struct iovec iov = { .iov_base = buffer, .iov_len = size };
        ssize_t len;

        len = qio_channel_readv(ioc, &iov, 1, errp);
        if (len == QIO_CHANNEL_ERR_BLOCK) {
            qio_channel_yield(ioc, G_IO_IN);
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
        buffer = (uint8_t*) buffer + len;
    }
    return 1;
}

/* nbd_receive_reply
 *
 * Wait for a new reply. If this yields, the coroutine must be able to be
 * safely reentered for nbd_client_attach_aio_context().  @mode determines
 * which reply magic we are expecting, although this normalizes the result
 * so that the caller only has to work with compact headers.
 *
 * Returns 1 on success
 *         0 on eof, when no data was read
 *         negative errno on failure
 */
int coroutine_fn nbd_receive_reply(BlockDriverState *bs, QIOChannel *ioc,
                                   NBDReply *reply, NBDMode mode, Error **errp)
{
    int ret;
    const char *type;
    uint32_t expected;

    ret = nbd_read_eof(bs, ioc, &reply->magic, sizeof(reply->magic), errp);
    if (ret <= 0) {
        return ret;
    }

    reply->magic = be32_to_cpu(reply->magic);

    /* Diagnose but accept wrong-width header */
    switch (reply->magic) {
    case NBD_SIMPLE_REPLY_MAGIC:
        if (mode >= NBD_MODE_EXTENDED) {
            trace_nbd_receive_wrong_header(reply->magic,
                                           nbd_mode_lookup(mode));
        }
        ret = nbd_receive_simple_reply(ioc, &reply->simple, errp);
        if (ret < 0) {
            return ret;
        }
        trace_nbd_receive_simple_reply(reply->simple.error,
                                       nbd_err_lookup(reply->simple.error),
                                       reply->cookie);
        break;
    case NBD_STRUCTURED_REPLY_MAGIC:
    case NBD_EXTENDED_REPLY_MAGIC:
        expected = mode >= NBD_MODE_EXTENDED ? NBD_EXTENDED_REPLY_MAGIC
            : NBD_STRUCTURED_REPLY_MAGIC;
        if (reply->magic != expected) {
            trace_nbd_receive_wrong_header(reply->magic,
                                           nbd_mode_lookup(mode));
        }
        ret = nbd_receive_reply_chunk_header(ioc, reply, errp);
        if (ret < 0) {
            return ret;
        }
        type = nbd_reply_type_lookup(reply->structured.type);
        trace_nbd_receive_reply_chunk_header(reply->structured.flags,
                                             reply->structured.type, type,
                                             reply->structured.cookie,
                                             reply->structured.length);
        break;
    default:
        trace_nbd_receive_wrong_header(reply->magic, nbd_mode_lookup(mode));
        error_setg(errp, "invalid magic (got 0x%" PRIx32 ")", reply->magic);
        return -EINVAL;
    }

    return 1;
}

