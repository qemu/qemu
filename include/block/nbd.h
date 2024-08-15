/*
 *  Copyright Red Hat
 *  Copyright (C) 2005  Anthony Liguori <anthony@codemonkey.ws>
 *
 *  Network Block Device
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

#ifndef NBD_H
#define NBD_H

#include "block/export.h"
#include "io/channel-socket.h"
#include "crypto/tlscreds.h"
#include "qapi/error.h"
#include "qemu/bswap.h"

typedef struct NBDExport NBDExport;
typedef struct NBDClient NBDClient;
typedef struct NBDClientConnection NBDClientConnection;
typedef struct NBDMetaContexts NBDMetaContexts;

extern const BlockExportDriver blk_exp_nbd;

/*
 * NBD_DEFAULT_HANDSHAKE_MAX_SECS: Number of seconds in which client must
 * succeed at NBD_OPT_GO before being forcefully dropped as too slow.
 */
#define NBD_DEFAULT_HANDSHAKE_MAX_SECS 10

/*
 * NBD_DEFAULT_MAX_CONNECTIONS: Number of client sockets to allow at
 * once; must be large enough to allow a MULTI_CONN-aware client like
 * nbdcopy to create its typical number of 8-16 sockets.
 */
#define NBD_DEFAULT_MAX_CONNECTIONS 100

/* Handshake phase structs - this struct is passed on the wire */

typedef struct NBDOption {
    uint64_t magic; /* NBD_OPTS_MAGIC */
    uint32_t option; /* NBD_OPT_* */
    uint32_t length;
} QEMU_PACKED NBDOption;

typedef struct NBDOptionReply {
    uint64_t magic; /* NBD_REP_MAGIC */
    uint32_t option; /* NBD_OPT_* */
    uint32_t type; /* NBD_REP_* */
    uint32_t length;
} QEMU_PACKED NBDOptionReply;

typedef struct NBDOptionReplyMetaContext {
    NBDOptionReply h; /* h.type = NBD_REP_META_CONTEXT, h.length > 4 */
    uint32_t context_id;
    /* metadata context name follows */
} QEMU_PACKED NBDOptionReplyMetaContext;

/* Track results of negotiation */
typedef enum NBDMode {
    /* Keep this list in a continuum of increasing features. */
    NBD_MODE_OLDSTYLE,     /* server lacks newstyle negotiation */
    NBD_MODE_EXPORT_NAME,  /* newstyle but only OPT_EXPORT_NAME safe */
    NBD_MODE_SIMPLE,       /* newstyle but only simple replies */
    NBD_MODE_STRUCTURED,   /* newstyle, structured replies enabled */
    NBD_MODE_EXTENDED,     /* newstyle, extended headers enabled */
} NBDMode;

/* Transmission phase structs */

/*
 * Note: NBDRequest is _NOT_ the same as the network representation of an NBD
 * request!
 */
typedef struct NBDRequest {
    uint64_t cookie;
    uint64_t from;  /* Offset touched by the command */
    uint64_t len;   /* Effect length; 32 bit limit without extended headers */
    uint16_t flags; /* NBD_CMD_FLAG_* */
    uint16_t type;  /* NBD_CMD_* */
    NBDMode mode;   /* Determines which network representation to use */
    NBDMetaContexts *contexts; /* Used by NBD_CMD_BLOCK_STATUS */
} NBDRequest;

typedef struct NBDSimpleReply {
    uint32_t magic;  /* NBD_SIMPLE_REPLY_MAGIC */
    uint32_t error;
    uint64_t cookie;
} QEMU_PACKED NBDSimpleReply;

/* Header of all structured replies */
typedef struct NBDStructuredReplyChunk {
    uint32_t magic;  /* NBD_STRUCTURED_REPLY_MAGIC */
    uint16_t flags;  /* combination of NBD_REPLY_FLAG_* */
    uint16_t type;   /* NBD_REPLY_TYPE_* */
    uint64_t cookie; /* request handle */
    uint32_t length; /* length of payload */
} QEMU_PACKED NBDStructuredReplyChunk;

typedef struct NBDExtendedReplyChunk {
    uint32_t magic;  /* NBD_EXTENDED_REPLY_MAGIC */
    uint16_t flags;  /* combination of NBD_REPLY_FLAG_* */
    uint16_t type;   /* NBD_REPLY_TYPE_* */
    uint64_t cookie; /* request handle */
    uint64_t offset; /* request offset */
    uint64_t length; /* length of payload */
} QEMU_PACKED NBDExtendedReplyChunk;

typedef union NBDReply {
    NBDSimpleReply simple;
    NBDStructuredReplyChunk structured;
    NBDExtendedReplyChunk extended;
    struct {
        /*
         * @magic and @cookie fields have the same offset and size in all
         * forms of replies, so let them be accessible without ".simple.",
         * ".structured.", or ".extended." specifications.
         */
        uint32_t magic;
        uint32_t _skip;
        uint64_t cookie;
    };
} NBDReply;
QEMU_BUILD_BUG_ON(offsetof(NBDReply, simple.cookie) !=
                  offsetof(NBDReply, cookie));
QEMU_BUILD_BUG_ON(offsetof(NBDReply, structured.cookie) !=
                  offsetof(NBDReply, cookie));
QEMU_BUILD_BUG_ON(offsetof(NBDReply, extended.cookie) !=
                  offsetof(NBDReply, cookie));

/* Header of chunk for NBD_REPLY_TYPE_OFFSET_DATA */
typedef struct NBDStructuredReadData {
    /* header's .length >= 9 */
    uint64_t offset;
    /* At least one byte of data payload follows, calculated from h.length */
} QEMU_PACKED NBDStructuredReadData;

/* Complete chunk for NBD_REPLY_TYPE_OFFSET_HOLE */
typedef struct NBDStructuredReadHole {
    /* header's length == 12 */
    uint64_t offset;
    uint32_t length;
} QEMU_PACKED NBDStructuredReadHole;

/* Header of all NBD_REPLY_TYPE_ERROR* errors */
typedef struct NBDStructuredError {
    /* header's length >= 6 */
    uint32_t error;
    uint16_t message_length;
} QEMU_PACKED NBDStructuredError;

/* Header of NBD_REPLY_TYPE_BLOCK_STATUS */
typedef struct NBDStructuredMeta {
    /* header's length >= 12 (at least one extent) */
    uint32_t context_id;
    /* NBDExtent32 extents[] follows, array length implied by header */
} QEMU_PACKED NBDStructuredMeta;

/* Extent array element for NBD_REPLY_TYPE_BLOCK_STATUS */
typedef struct NBDExtent32 {
    uint32_t length;
    uint32_t flags; /* NBD_STATE_* */
} QEMU_PACKED NBDExtent32;

/* Header of NBD_REPLY_TYPE_BLOCK_STATUS_EXT */
typedef struct NBDExtendedMeta {
    /* header's length >= 24 (at least one extent) */
    uint32_t context_id;
    uint32_t count; /* header length must be count * 16 + 8 */
    /* NBDExtent64 extents[count] follows */
} QEMU_PACKED NBDExtendedMeta;

/* Extent array element for NBD_REPLY_TYPE_BLOCK_STATUS_EXT */
typedef struct NBDExtent64 {
    uint64_t length;
    uint64_t flags; /* NBD_STATE_* */
} QEMU_PACKED NBDExtent64;

/* Client payload for limiting NBD_CMD_BLOCK_STATUS reply */
typedef struct NBDBlockStatusPayload {
    uint64_t effect_length;
    /* uint32_t ids[] follows, array length implied by header */
} QEMU_PACKED NBDBlockStatusPayload;

/* Transmission (export) flags: sent from server to client during handshake,
   but describe what will happen during transmission */
enum {
    NBD_FLAG_HAS_FLAGS_BIT          =  0, /* Flags are there */
    NBD_FLAG_READ_ONLY_BIT          =  1, /* Device is read-only */
    NBD_FLAG_SEND_FLUSH_BIT         =  2, /* Send FLUSH */
    NBD_FLAG_SEND_FUA_BIT           =  3, /* Send FUA (Force Unit Access) */
    NBD_FLAG_ROTATIONAL_BIT         =  4, /* Use elevator algorithm -
                                             rotational media */
    NBD_FLAG_SEND_TRIM_BIT          =  5, /* Send TRIM (discard) */
    NBD_FLAG_SEND_WRITE_ZEROES_BIT  =  6, /* Send WRITE_ZEROES */
    NBD_FLAG_SEND_DF_BIT            =  7, /* Send DF (Do not Fragment) */
    NBD_FLAG_CAN_MULTI_CONN_BIT     =  8, /* Multi-client cache consistent */
    NBD_FLAG_SEND_RESIZE_BIT        =  9, /* Send resize */
    NBD_FLAG_SEND_CACHE_BIT         = 10, /* Send CACHE (prefetch) */
    NBD_FLAG_SEND_FAST_ZERO_BIT     = 11, /* FAST_ZERO flag for WRITE_ZEROES */
    NBD_FLAG_BLOCK_STAT_PAYLOAD_BIT = 12, /* PAYLOAD flag for BLOCK_STATUS */
};

#define NBD_FLAG_HAS_FLAGS          (1 << NBD_FLAG_HAS_FLAGS_BIT)
#define NBD_FLAG_READ_ONLY          (1 << NBD_FLAG_READ_ONLY_BIT)
#define NBD_FLAG_SEND_FLUSH         (1 << NBD_FLAG_SEND_FLUSH_BIT)
#define NBD_FLAG_SEND_FUA           (1 << NBD_FLAG_SEND_FUA_BIT)
#define NBD_FLAG_ROTATIONAL         (1 << NBD_FLAG_ROTATIONAL_BIT)
#define NBD_FLAG_SEND_TRIM          (1 << NBD_FLAG_SEND_TRIM_BIT)
#define NBD_FLAG_SEND_WRITE_ZEROES  (1 << NBD_FLAG_SEND_WRITE_ZEROES_BIT)
#define NBD_FLAG_SEND_DF            (1 << NBD_FLAG_SEND_DF_BIT)
#define NBD_FLAG_CAN_MULTI_CONN     (1 << NBD_FLAG_CAN_MULTI_CONN_BIT)
#define NBD_FLAG_SEND_RESIZE        (1 << NBD_FLAG_SEND_RESIZE_BIT)
#define NBD_FLAG_SEND_CACHE         (1 << NBD_FLAG_SEND_CACHE_BIT)
#define NBD_FLAG_SEND_FAST_ZERO     (1 << NBD_FLAG_SEND_FAST_ZERO_BIT)
#define NBD_FLAG_BLOCK_STAT_PAYLOAD (1 << NBD_FLAG_BLOCK_STAT_PAYLOAD_BIT)

/* New-style handshake (global) flags, sent from server to client, and
   control what will happen during handshake phase. */
#define NBD_FLAG_FIXED_NEWSTYLE   (1 << 0) /* Fixed newstyle protocol. */
#define NBD_FLAG_NO_ZEROES        (1 << 1) /* End handshake without zeroes. */

/* New-style client flags, sent from client to server to control what happens
   during handshake phase. */
#define NBD_FLAG_C_FIXED_NEWSTYLE (1 << 0) /* Fixed newstyle protocol. */
#define NBD_FLAG_C_NO_ZEROES      (1 << 1) /* End handshake without zeroes. */

/* Option requests. */
#define NBD_OPT_EXPORT_NAME       (1)
#define NBD_OPT_ABORT             (2)
#define NBD_OPT_LIST              (3)
/* #define NBD_OPT_PEEK_EXPORT    (4) not in use */
#define NBD_OPT_STARTTLS          (5)
#define NBD_OPT_INFO              (6)
#define NBD_OPT_GO                (7)
#define NBD_OPT_STRUCTURED_REPLY  (8)
#define NBD_OPT_LIST_META_CONTEXT (9)
#define NBD_OPT_SET_META_CONTEXT  (10)
#define NBD_OPT_EXTENDED_HEADERS  (11)

/* Option reply types. */
#define NBD_REP_ERR(value) ((UINT32_C(1) << 31) | (value))

#define NBD_REP_ACK             (1)    /* Data sending finished. */
#define NBD_REP_SERVER          (2)    /* Export description. */
#define NBD_REP_INFO            (3)    /* NBD_OPT_INFO/GO. */
#define NBD_REP_META_CONTEXT    (4)    /* NBD_OPT_{LIST,SET}_META_CONTEXT */

#define NBD_REP_ERR_UNSUP           NBD_REP_ERR(1)  /* Unknown option */
#define NBD_REP_ERR_POLICY          NBD_REP_ERR(2)  /* Server denied */
#define NBD_REP_ERR_INVALID         NBD_REP_ERR(3)  /* Invalid length */
#define NBD_REP_ERR_PLATFORM        NBD_REP_ERR(4)  /* Not compiled in */
#define NBD_REP_ERR_TLS_REQD        NBD_REP_ERR(5)  /* TLS required */
#define NBD_REP_ERR_UNKNOWN         NBD_REP_ERR(6)  /* Export unknown */
#define NBD_REP_ERR_SHUTDOWN        NBD_REP_ERR(7)  /* Server shutting down */
#define NBD_REP_ERR_BLOCK_SIZE_REQD NBD_REP_ERR(8)  /* Need INFO_BLOCK_SIZE */
#define NBD_REP_ERR_TOO_BIG         NBD_REP_ERR(9)  /* Payload size overflow */
#define NBD_REP_ERR_EXT_HEADER_REQD NBD_REP_ERR(10) /* Need extended headers */

/* Info types, used during NBD_REP_INFO */
#define NBD_INFO_EXPORT         0
#define NBD_INFO_NAME           1
#define NBD_INFO_DESCRIPTION    2
#define NBD_INFO_BLOCK_SIZE     3

/* Request flags, sent from client to server during transmission phase */
#define NBD_CMD_FLAG_FUA         (1 << 0) /* 'force unit access' during write */
#define NBD_CMD_FLAG_NO_HOLE     (1 << 1) /* don't punch hole on zero run */
#define NBD_CMD_FLAG_DF          (1 << 2) /* don't fragment structured read */
#define NBD_CMD_FLAG_REQ_ONE     (1 << 3) \
    /* only one extent in BLOCK_STATUS reply chunk */
#define NBD_CMD_FLAG_FAST_ZERO   (1 << 4) /* fail if WRITE_ZEROES is not fast */
#define NBD_CMD_FLAG_PAYLOAD_LEN (1 << 5) \
    /* length describes payload, not effect; only with ext header */

/* Supported request types */
enum {
    NBD_CMD_READ = 0,
    NBD_CMD_WRITE = 1,
    NBD_CMD_DISC = 2,
    NBD_CMD_FLUSH = 3,
    NBD_CMD_TRIM = 4,
    NBD_CMD_CACHE = 5,
    NBD_CMD_WRITE_ZEROES = 6,
    NBD_CMD_BLOCK_STATUS = 7,
};

#define NBD_DEFAULT_PORT	10809

/* Maximum size of a single READ/WRITE data buffer */
#define NBD_MAX_BUFFER_SIZE (32 * 1024 * 1024)

/*
 * Maximum size of a protocol string (export name, metadata context name,
 * etc.).  Use malloc rather than stack allocation for storage of a
 * string.
 */
#define NBD_MAX_STRING_SIZE 4096

/* Two types of request structures, a given client will only use 1 */
#define NBD_REQUEST_MAGIC           0x25609513
#define NBD_EXTENDED_REQUEST_MAGIC  0x21e41c71

/*
 * Three types of reply structures, but what a client expects depends
 * on NBD_OPT_STRUCTURED_REPLY and NBD_OPT_EXTENDED_HEADERS.
 */
#define NBD_SIMPLE_REPLY_MAGIC      0x67446698
#define NBD_STRUCTURED_REPLY_MAGIC  0x668e33ef
#define NBD_EXTENDED_REPLY_MAGIC    0x6e8a278c

/* Chunk reply flags (for structured and extended replies) */
#define NBD_REPLY_FLAG_DONE          (1 << 0) /* This reply-chunk is last */

/* Chunk reply types */
#define NBD_REPLY_ERR(value)         ((1 << 15) | (value))

#define NBD_REPLY_TYPE_NONE              0
#define NBD_REPLY_TYPE_OFFSET_DATA       1
#define NBD_REPLY_TYPE_OFFSET_HOLE       2
#define NBD_REPLY_TYPE_BLOCK_STATUS      5
#define NBD_REPLY_TYPE_BLOCK_STATUS_EXT  6
#define NBD_REPLY_TYPE_ERROR             NBD_REPLY_ERR(1)
#define NBD_REPLY_TYPE_ERROR_OFFSET      NBD_REPLY_ERR(2)

/* Extent flags for base:allocation in NBD_REPLY_TYPE_BLOCK_STATUS */
#define NBD_STATE_HOLE (1 << 0)
#define NBD_STATE_ZERO (1 << 1)

/* Extent flags for qemu:dirty-bitmap in NBD_REPLY_TYPE_BLOCK_STATUS */
#define NBD_STATE_DIRTY (1 << 0)

/* No flags needed for qemu:allocation-depth in NBD_REPLY_TYPE_BLOCK_STATUS */

static inline bool nbd_reply_type_is_error(int type)
{
    return type & (1 << 15);
}

/* NBD errors are based on errno numbers, so there is a 1:1 mapping,
 * but only a limited set of errno values is specified in the protocol.
 * Everything else is squashed to EINVAL.
 */
#define NBD_SUCCESS    0
#define NBD_EPERM      1
#define NBD_EIO        5
#define NBD_ENOMEM     12
#define NBD_EINVAL     22
#define NBD_ENOSPC     28
#define NBD_EOVERFLOW  75
#define NBD_ENOTSUP    95
#define NBD_ESHUTDOWN  108

/* Details collected by NBD_OPT_EXPORT_NAME and NBD_OPT_GO */
typedef struct NBDExportInfo {
    /* Set by client before nbd_receive_negotiate() */
    bool request_sizes;
    char *x_dirty_bitmap;

    /* Set by client before nbd_receive_negotiate(), or by server results
     * during nbd_receive_export_list() */
    char *name; /* must be non-NULL */

    /* In-out fields, set by client before nbd_receive_negotiate() and
     * updated by server results during nbd_receive_negotiate() */
    NBDMode mode; /* input maximum mode tolerated; output actual mode chosen */
    bool base_allocation; /* base:allocation context for NBD_CMD_BLOCK_STATUS */

    /* Set by server results during nbd_receive_negotiate() and
     * nbd_receive_export_list() */
    uint64_t size;
    uint16_t flags;
    uint32_t min_block;
    uint32_t opt_block;
    uint32_t max_block;

    uint32_t context_id;

    /* Set by server results during nbd_receive_export_list() */
    char *description;
    int n_contexts;
    char **contexts;
} NBDExportInfo;

int nbd_receive_negotiate(QIOChannel *ioc, QCryptoTLSCreds *tlscreds,
                          const char *hostname, QIOChannel **outioc,
                          NBDExportInfo *info, Error **errp);
void nbd_free_export_list(NBDExportInfo *info, int count);
int nbd_receive_export_list(QIOChannel *ioc, QCryptoTLSCreds *tlscreds,
                            const char *hostname, NBDExportInfo **info,
                            Error **errp);
int nbd_init(int fd, QIOChannelSocket *sioc, NBDExportInfo *info,
             Error **errp);
int nbd_send_request(QIOChannel *ioc, NBDRequest *request);
int coroutine_fn nbd_receive_reply(BlockDriverState *bs, QIOChannel *ioc,
                                   NBDReply *reply, NBDMode mode,
                                   Error **errp);
int nbd_client(int fd);
int nbd_disconnect(int fd);
int nbd_errno_to_system_errno(int err);

void nbd_export_set_on_eject_blk(BlockExport *exp, BlockBackend *blk);

AioContext *nbd_export_aio_context(NBDExport *exp);
NBDExport *nbd_export_find(const char *name);

void nbd_client_new(QIOChannelSocket *sioc,
                    uint32_t handshake_max_secs,
                    QCryptoTLSCreds *tlscreds,
                    const char *tlsauthz,
                    void (*close_fn)(NBDClient *, bool),
                    void *owner);
void *nbd_client_owner(NBDClient *client);
void nbd_client_get(NBDClient *client);
void nbd_client_put(NBDClient *client);

void nbd_server_is_qemu_nbd(int max_connections);
bool nbd_server_is_running(void);
int nbd_server_max_connections(void);
void nbd_server_start(SocketAddress *addr, const char *tls_creds,
                      const char *tls_authz, uint32_t max_connections,
                      Error **errp);
void nbd_server_start_options(NbdServerOptions *arg, Error **errp);

/* nbd_read
 * Reads @size bytes from @ioc. Returns 0 on success.
 */
static inline int nbd_read(QIOChannel *ioc, void *buffer, size_t size,
                           const char *desc, Error **errp)
{
    ERRP_GUARD();
    int ret = qio_channel_read_all(ioc, buffer, size, errp) < 0 ? -EIO : 0;

    if (ret < 0) {
        if (desc) {
            error_prepend(errp, "Failed to read %s: ", desc);
        }
        return ret;
    }

    return 0;
}

#define DEF_NBD_READ_N(bits)                                            \
static inline int nbd_read##bits(QIOChannel *ioc,                       \
                                 uint##bits##_t *val,                   \
                                 const char *desc, Error **errp)        \
{                                                                       \
    int ret = nbd_read(ioc, val, sizeof(*val), desc, errp);             \
    if (ret < 0) {                                                      \
        return ret;                                                     \
    }                                                                   \
    *val = be##bits##_to_cpu(*val);                                     \
    return 0;                                                           \
}

DEF_NBD_READ_N(16) /* Defines nbd_read16(). */
DEF_NBD_READ_N(32) /* Defines nbd_read32(). */
DEF_NBD_READ_N(64) /* Defines nbd_read64(). */

#undef DEF_NBD_READ_N

static inline bool nbd_reply_is_simple(NBDReply *reply)
{
    return reply->magic == NBD_SIMPLE_REPLY_MAGIC;
}

static inline bool nbd_reply_is_structured(NBDReply *reply)
{
    return reply->magic == NBD_STRUCTURED_REPLY_MAGIC;
}

const char *nbd_reply_type_lookup(uint16_t type);
const char *nbd_opt_lookup(uint32_t opt);
const char *nbd_rep_lookup(uint32_t rep);
const char *nbd_info_lookup(uint16_t info);
const char *nbd_cmd_lookup(uint16_t info);
const char *nbd_err_lookup(int err);
const char *nbd_mode_lookup(NBDMode mode);

/* nbd/client-connection.c */
void nbd_client_connection_enable_retry(NBDClientConnection *conn);

NBDClientConnection *nbd_client_connection_new(const SocketAddress *saddr,
                                               bool do_negotiation,
                                               const char *export_name,
                                               const char *x_dirty_bitmap,
                                               QCryptoTLSCreds *tlscreds,
                                               const char *tlshostname);
void nbd_client_connection_release(NBDClientConnection *conn);

QIOChannel *coroutine_fn
nbd_co_establish_connection(NBDClientConnection *conn, NBDExportInfo *info,
                            bool blocking, Error **errp);

void nbd_co_establish_connection_cancel(NBDClientConnection *conn);

#endif
