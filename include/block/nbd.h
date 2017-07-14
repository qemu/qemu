/*
 *  Copyright (C) 2016-2017 Red Hat, Inc.
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


#include "qemu-common.h"
#include "qemu/option.h"
#include "io/channel-socket.h"
#include "crypto/tlscreds.h"

/* Handshake phase structs - this struct is passed on the wire */

struct nbd_option {
    uint64_t magic; /* NBD_OPTS_MAGIC */
    uint32_t option; /* NBD_OPT_* */
    uint32_t length;
} QEMU_PACKED;
typedef struct nbd_option nbd_option;

struct nbd_opt_reply {
    uint64_t magic; /* NBD_REP_MAGIC */
    uint32_t option; /* NBD_OPT_* */
    uint32_t type; /* NBD_REP_* */
    uint32_t length;
} QEMU_PACKED;
typedef struct nbd_opt_reply nbd_opt_reply;

/* Transmission phase structs
 *
 * Note: these are _NOT_ the same as the network representation of an NBD
 * request and reply!
 */
struct NBDRequest {
    uint64_t handle;
    uint64_t from;
    uint32_t len;
    uint16_t flags; /* NBD_CMD_FLAG_* */
    uint16_t type; /* NBD_CMD_* */
};
typedef struct NBDRequest NBDRequest;

struct NBDReply {
    uint64_t handle;
    uint32_t error;
};
typedef struct NBDReply NBDReply;

/* Transmission (export) flags: sent from server to client during handshake,
   but describe what will happen during transmission */
#define NBD_FLAG_HAS_FLAGS      (1 << 0)        /* Flags are there */
#define NBD_FLAG_READ_ONLY      (1 << 1)        /* Device is read-only */
#define NBD_FLAG_SEND_FLUSH     (1 << 2)        /* Send FLUSH */
#define NBD_FLAG_SEND_FUA       (1 << 3)        /* Send FUA (Force Unit Access) */
#define NBD_FLAG_ROTATIONAL     (1 << 4)        /* Use elevator algorithm - rotational media */
#define NBD_FLAG_SEND_TRIM      (1 << 5)        /* Send TRIM (discard) */
#define NBD_FLAG_SEND_WRITE_ZEROES (1 << 6)     /* Send WRITE_ZEROES */

/* New-style handshake (global) flags, sent from server to client, and
   control what will happen during handshake phase. */
#define NBD_FLAG_FIXED_NEWSTYLE   (1 << 0) /* Fixed newstyle protocol. */
#define NBD_FLAG_NO_ZEROES        (1 << 1) /* End handshake without zeroes. */

/* New-style client flags, sent from client to server to control what happens
   during handshake phase. */
#define NBD_FLAG_C_FIXED_NEWSTYLE (1 << 0) /* Fixed newstyle protocol. */
#define NBD_FLAG_C_NO_ZEROES      (1 << 1) /* End handshake without zeroes. */

/* Option requests. */
#define NBD_OPT_EXPORT_NAME      (1)
#define NBD_OPT_ABORT            (2)
#define NBD_OPT_LIST             (3)
/* #define NBD_OPT_PEEK_EXPORT   (4) not in use */
#define NBD_OPT_STARTTLS         (5)
#define NBD_OPT_INFO             (6)
#define NBD_OPT_GO               (7)
#define NBD_OPT_STRUCTURED_REPLY (8)

/* Option reply types. */
#define NBD_REP_ERR(value) ((UINT32_C(1) << 31) | (value))

#define NBD_REP_ACK             (1)             /* Data sending finished. */
#define NBD_REP_SERVER          (2)             /* Export description. */
#define NBD_REP_INFO            (3)             /* NBD_OPT_INFO/GO. */

#define NBD_REP_ERR_UNSUP           NBD_REP_ERR(1)  /* Unknown option */
#define NBD_REP_ERR_POLICY          NBD_REP_ERR(2)  /* Server denied */
#define NBD_REP_ERR_INVALID         NBD_REP_ERR(3)  /* Invalid length */
#define NBD_REP_ERR_PLATFORM        NBD_REP_ERR(4)  /* Not compiled in */
#define NBD_REP_ERR_TLS_REQD        NBD_REP_ERR(5)  /* TLS required */
#define NBD_REP_ERR_UNKNOWN         NBD_REP_ERR(6)  /* Export unknown */
#define NBD_REP_ERR_SHUTDOWN        NBD_REP_ERR(7)  /* Server shutting down */
#define NBD_REP_ERR_BLOCK_SIZE_REQD NBD_REP_ERR(8)  /* Need INFO_BLOCK_SIZE */

/* Info types, used during NBD_REP_INFO */
#define NBD_INFO_EXPORT         0
#define NBD_INFO_NAME           1
#define NBD_INFO_DESCRIPTION    2
#define NBD_INFO_BLOCK_SIZE     3

/* Request flags, sent from client to server during transmission phase */
#define NBD_CMD_FLAG_FUA        (1 << 0) /* 'force unit access' during write */
#define NBD_CMD_FLAG_NO_HOLE    (1 << 1) /* don't punch hole on zero run */

/* Supported request types */
enum {
    NBD_CMD_READ = 0,
    NBD_CMD_WRITE = 1,
    NBD_CMD_DISC = 2,
    NBD_CMD_FLUSH = 3,
    NBD_CMD_TRIM = 4,
    /* 5 reserved for failed experiment NBD_CMD_CACHE */
    NBD_CMD_WRITE_ZEROES = 6,
};

#define NBD_DEFAULT_PORT	10809

/* Maximum size of a single READ/WRITE data buffer */
#define NBD_MAX_BUFFER_SIZE (32 * 1024 * 1024)

/* Maximum size of an export name. The NBD spec requires 256 and
 * suggests that servers support up to 4096, but we stick to only the
 * required size so that we can stack-allocate the names, and because
 * going larger would require an audit of more code to make sure we
 * aren't overflowing some other buffer. */
#define NBD_MAX_NAME_SIZE 256

/* Details collected by NBD_OPT_EXPORT_NAME and NBD_OPT_GO */
struct NBDExportInfo {
    /* Set by client before nbd_receive_negotiate() */
    bool request_sizes;
    /* Set by server results during nbd_receive_negotiate() */
    uint64_t size;
    uint16_t flags;
    uint32_t min_block;
    uint32_t opt_block;
    uint32_t max_block;
};
typedef struct NBDExportInfo NBDExportInfo;

ssize_t nbd_rwv(QIOChannel *ioc, struct iovec *iov, size_t niov, size_t length,
                bool do_read, Error **errp);
int nbd_receive_negotiate(QIOChannel *ioc, const char *name,
                          QCryptoTLSCreds *tlscreds, const char *hostname,
                          QIOChannel **outioc, NBDExportInfo *info,
                          Error **errp);
int nbd_init(int fd, QIOChannelSocket *sioc, NBDExportInfo *info,
             Error **errp);
ssize_t nbd_send_request(QIOChannel *ioc, NBDRequest *request);
ssize_t nbd_receive_reply(QIOChannel *ioc, NBDReply *reply, Error **errp);
int nbd_client(int fd);
int nbd_disconnect(int fd);

typedef struct NBDExport NBDExport;
typedef struct NBDClient NBDClient;

NBDExport *nbd_export_new(BlockDriverState *bs, off_t dev_offset, off_t size,
                          uint16_t nbdflags, void (*close)(NBDExport *),
                          bool writethrough, BlockBackend *on_eject_blk,
                          Error **errp);
void nbd_export_close(NBDExport *exp);
void nbd_export_get(NBDExport *exp);
void nbd_export_put(NBDExport *exp);

BlockBackend *nbd_export_get_blockdev(NBDExport *exp);

NBDExport *nbd_export_find(const char *name);
void nbd_export_set_name(NBDExport *exp, const char *name);
void nbd_export_set_description(NBDExport *exp, const char *description);
void nbd_export_close_all(void);

void nbd_client_new(NBDExport *exp,
                    QIOChannelSocket *sioc,
                    QCryptoTLSCreds *tlscreds,
                    const char *tlsaclname,
                    void (*close_fn)(NBDClient *, bool));
void nbd_client_get(NBDClient *client);
void nbd_client_put(NBDClient *client);

void nbd_server_start(SocketAddress *addr, const char *tls_creds,
                      Error **errp);

#endif
