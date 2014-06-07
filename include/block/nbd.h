/*
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

#include <sys/types.h>

#include "qemu-common.h"
#include "qemu/option.h"

struct nbd_request {
    uint32_t magic;
    uint32_t type;
    uint64_t handle;
    uint64_t from;
    uint32_t len;
} QEMU_PACKED;

struct nbd_reply {
    uint32_t magic;
    uint32_t error;
    uint64_t handle;
} QEMU_PACKED;

#define NBD_FLAG_HAS_FLAGS      (1 << 0)        /* Flags are there */
#define NBD_FLAG_READ_ONLY      (1 << 1)        /* Device is read-only */
#define NBD_FLAG_SEND_FLUSH     (1 << 2)        /* Send FLUSH */
#define NBD_FLAG_SEND_FUA       (1 << 3)        /* Send FUA (Force Unit Access) */
#define NBD_FLAG_ROTATIONAL     (1 << 4)        /* Use elevator algorithm - rotational media */
#define NBD_FLAG_SEND_TRIM      (1 << 5)        /* Send TRIM (discard) */

/* New-style global flags. */
#define NBD_FLAG_FIXED_NEWSTYLE     (1 << 0)    /* Fixed newstyle protocol. */

/* New-style client flags. */
#define NBD_FLAG_C_FIXED_NEWSTYLE   (1 << 0)    /* Fixed newstyle protocol. */

/* Reply types. */
#define NBD_REP_ERR_UNSUP       ((1 << 31) | 1) /* Unknown option. */

#define NBD_CMD_MASK_COMMAND	0x0000ffff
#define NBD_CMD_FLAG_FUA	(1 << 16)

enum {
    NBD_CMD_READ = 0,
    NBD_CMD_WRITE = 1,
    NBD_CMD_DISC = 2,
    NBD_CMD_FLUSH = 3,
    NBD_CMD_TRIM = 4
};

#define NBD_DEFAULT_PORT	10809

/* Maximum size of a single READ/WRITE data buffer */
#define NBD_MAX_BUFFER_SIZE (32 * 1024 * 1024)

ssize_t nbd_wr_sync(int fd, void *buffer, size_t size, bool do_read);
int nbd_receive_negotiate(int csock, const char *name, uint32_t *flags,
                          off_t *size, size_t *blocksize);
int nbd_init(int fd, int csock, uint32_t flags, off_t size, size_t blocksize);
ssize_t nbd_send_request(int csock, struct nbd_request *request);
ssize_t nbd_receive_reply(int csock, struct nbd_reply *reply);
int nbd_client(int fd);
int nbd_disconnect(int fd);

typedef struct NBDExport NBDExport;
typedef struct NBDClient NBDClient;

NBDExport *nbd_export_new(BlockDriverState *bs, off_t dev_offset,
                          off_t size, uint32_t nbdflags,
                          void (*close)(NBDExport *));
void nbd_export_close(NBDExport *exp);
void nbd_export_get(NBDExport *exp);
void nbd_export_put(NBDExport *exp);

BlockDriverState *nbd_export_get_blockdev(NBDExport *exp);

NBDExport *nbd_export_find(const char *name);
void nbd_export_set_name(NBDExport *exp, const char *name);
void nbd_export_close_all(void);

NBDClient *nbd_client_new(NBDExport *exp, int csock,
                          void (*close)(NBDClient *));
void nbd_client_close(NBDClient *client);
void nbd_client_get(NBDClient *client);
void nbd_client_put(NBDClient *client);

#endif
