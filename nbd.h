/*\
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
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
\*/

#ifndef NBD_H
#define NBD_H

#include <sys/types.h>
#include <stdbool.h>

#include <qemu-common.h>
#include "block_int.h"

int tcp_socket_incoming(const char *address, uint16_t port);
int unix_socket_outgoing(const char *path);
int unix_socket_incoming(const char *path);

int nbd_negotiate(BlockDriverState *bs, int csock, off_t size);
int nbd_receive_negotiate(int csock, off_t *size, size_t *blocksize);
int nbd_init(int fd, int csock, off_t size, size_t blocksize);
int nbd_trip(BlockDriverState *bs, int csock, off_t size, uint64_t dev_offset, off_t *offset, bool readonly);
int nbd_client(int fd, int csock);
int nbd_disconnect(int fd);

#endif
