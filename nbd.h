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
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA  02110-1301 USA
 */

#ifndef NBD_H
#define NBD_H

#include <sys/types.h>
#include <stdbool.h>

#include <qemu-common.h>
#include "block_int.h"

struct nbd_request {
    uint32_t type;
    uint64_t handle;
    uint64_t from;
    uint32_t len;
};

struct nbd_reply {
    uint32_t error;
    uint64_t handle;
};

enum {
    NBD_CMD_READ = 0,
    NBD_CMD_WRITE = 1,
    NBD_CMD_DISC = 2
};

size_t nbd_wr_sync(int fd, void *buffer, size_t size, bool do_read);
int tcp_socket_outgoing(const char *address, uint16_t port);
int tcp_socket_incoming(const char *address, uint16_t port);
int unix_socket_outgoing(const char *path);
int unix_socket_incoming(const char *path);

int nbd_negotiate(int csock, off_t size);
int nbd_receive_negotiate(int csock, off_t *size, size_t *blocksize);
int nbd_init(int fd, int csock, off_t size, size_t blocksize);
int nbd_send_request(int csock, struct nbd_request *request);
int nbd_receive_reply(int csock, struct nbd_reply *reply);
int nbd_trip(BlockDriverState *bs, int csock, off_t size, uint64_t dev_offset,
             off_t *offset, bool readonly, uint8_t *data, int data_size);
int nbd_client(int fd, int csock);
int nbd_disconnect(int fd);

#endif
