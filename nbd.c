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

#include "nbd.h"

#include <errno.h>
#include <string.h>
#ifndef _WIN32
#include <sys/ioctl.h>
#endif
#ifdef __sun__
#include <sys/ioccom.h>
#endif
#include <ctype.h>
#include <inttypes.h>

#include "qemu_socket.h"

//#define DEBUG_NBD

#ifdef DEBUG_NBD
#define TRACE(msg, ...) do { \
    LOG(msg, ## __VA_ARGS__); \
} while(0)
#else
#define TRACE(msg, ...) \
    do { } while (0)
#endif

#define LOG(msg, ...) do { \
    fprintf(stderr, "%s:%s():L%d: " msg "\n", \
            __FILE__, __FUNCTION__, __LINE__, ## __VA_ARGS__); \
} while(0)

/* This is all part of the "official" NBD API */

#define NBD_REQUEST_MAGIC       0x25609513
#define NBD_REPLY_MAGIC         0x67446698

#define NBD_SET_SOCK            _IO(0xab, 0)
#define NBD_SET_BLKSIZE         _IO(0xab, 1)
#define NBD_SET_SIZE            _IO(0xab, 2)
#define NBD_DO_IT               _IO(0xab, 3)
#define NBD_CLEAR_SOCK          _IO(0xab, 4)
#define NBD_CLEAR_QUE           _IO(0xab, 5)
#define NBD_PRINT_DEBUG	        _IO(0xab, 6)
#define NBD_SET_SIZE_BLOCKS	_IO(0xab, 7)
#define NBD_DISCONNECT          _IO(0xab, 8)

/* That's all folks */

#define read_sync(fd, buffer, size) nbd_wr_sync(fd, buffer, size, true)
#define write_sync(fd, buffer, size) nbd_wr_sync(fd, buffer, size, false)

size_t nbd_wr_sync(int fd, void *buffer, size_t size, bool do_read)
{
    size_t offset = 0;

    while (offset < size) {
        ssize_t len;

        if (do_read) {
            len = recv(fd, buffer + offset, size - offset, 0);
        } else {
            len = send(fd, buffer + offset, size - offset, 0);
        }

        if (len == -1)
            errno = socket_error();

        /* recoverable error */
        if (len == -1 && (errno == EAGAIN || errno == EINTR)) {
            continue;
        }

        /* eof */
        if (len == 0) {
            break;
        }

        /* unrecoverable error */
        if (len == -1) {
            return 0;
        }

        offset += len;
    }

    return offset;
}

int tcp_socket_outgoing(const char *address, uint16_t port)
{
    int s;
    struct in_addr in;
    struct sockaddr_in addr;

    s = socket(PF_INET, SOCK_STREAM, 0);
    if (s == -1) {
        return -1;
    }

    if (inet_aton(address, &in) == 0) {
        struct hostent *ent;

        ent = gethostbyname(address);
        if (ent == NULL) {
            goto error;
        }

        memcpy(&in, ent->h_addr, sizeof(in));
    }

    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    memcpy(&addr.sin_addr.s_addr, &in, sizeof(in));

    if (connect(s, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        goto error;
    }

    return s;
error:
    closesocket(s);
    return -1;
}

int tcp_socket_incoming(const char *address, uint16_t port)
{
    int s;
    struct in_addr in;
    struct sockaddr_in addr;
    int opt;

    s = socket(PF_INET, SOCK_STREAM, 0);
    if (s == -1) {
        return -1;
    }

    if (inet_aton(address, &in) == 0) {
        struct hostent *ent;

        ent = gethostbyname(address);
        if (ent == NULL) {
            goto error;
        }

        memcpy(&in, ent->h_addr, sizeof(in));
    }

    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    memcpy(&addr.sin_addr.s_addr, &in, sizeof(in));

    opt = 1;
    if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR,
                   (const void *) &opt, sizeof(opt)) == -1) {
        goto error;
    }

    if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        goto error;
    }

    if (listen(s, 128) == -1) {
        goto error;
    }

    return s;
error:
    closesocket(s);
    return -1;
}

#ifndef _WIN32
int unix_socket_incoming(const char *path)
{
    int s;
    struct sockaddr_un addr;

    s = socket(PF_UNIX, SOCK_STREAM, 0);
    if (s == -1) {
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    pstrcpy(addr.sun_path, sizeof(addr.sun_path), path);

    if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        goto error;
    }

    if (listen(s, 128) == -1) {
        goto error;
    }

    return s;
error:
    closesocket(s);
    return -1;
}

int unix_socket_outgoing(const char *path)
{
    int s;
    struct sockaddr_un addr;

    s = socket(PF_UNIX, SOCK_STREAM, 0);
    if (s == -1) {
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    pstrcpy(addr.sun_path, sizeof(addr.sun_path), path);

    if (connect(s, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        goto error;
    }

    return s;
error:
    closesocket(s);
    return -1;
}
#else
int unix_socket_incoming(const char *path)
{
    errno = ENOTSUP;
    return -1;
}

int unix_socket_outgoing(const char *path)
{
    errno = ENOTSUP;
    return -1;
}
#endif


/* Basic flow

   Server         Client

   Negotiate
                  Request
   Response
                  Request
   Response
                  ...
   ...
                  Request (type == 2)
*/

int nbd_negotiate(int csock, off_t size)
{
	char buf[8 + 8 + 8 + 128];

	/* Negotiate
	   [ 0 ..   7]   passwd   ("NBDMAGIC")
	   [ 8 ..  15]   magic    (0x00420281861253)
	   [16 ..  23]   size
	   [24 .. 151]   reserved (0)
	 */

	TRACE("Beginning negotiation.");
	memcpy(buf, "NBDMAGIC", 8);
	cpu_to_be64w((uint64_t*)(buf + 8), 0x00420281861253LL);
	cpu_to_be64w((uint64_t*)(buf + 16), size);
	memset(buf + 24, 0, 128);

	if (write_sync(csock, buf, sizeof(buf)) != sizeof(buf)) {
		LOG("write failed");
		errno = EINVAL;
		return -1;
	}

	TRACE("Negotation succeeded.");

	return 0;
}

int nbd_receive_negotiate(int csock, off_t *size, size_t *blocksize)
{
	char buf[8 + 8 + 8 + 128];
	uint64_t magic;

	TRACE("Receiving negotation.");

	if (read_sync(csock, buf, sizeof(buf)) != sizeof(buf)) {
		LOG("read failed");
		errno = EINVAL;
		return -1;
	}

	magic = be64_to_cpup((uint64_t*)(buf + 8));
	*size = be64_to_cpup((uint64_t*)(buf + 16));
	*blocksize = 1024;

	TRACE("Magic is %c%c%c%c%c%c%c%c",
	      qemu_isprint(buf[0]) ? buf[0] : '.',
	      qemu_isprint(buf[1]) ? buf[1] : '.',
	      qemu_isprint(buf[2]) ? buf[2] : '.',
	      qemu_isprint(buf[3]) ? buf[3] : '.',
	      qemu_isprint(buf[4]) ? buf[4] : '.',
	      qemu_isprint(buf[5]) ? buf[5] : '.',
	      qemu_isprint(buf[6]) ? buf[6] : '.',
	      qemu_isprint(buf[7]) ? buf[7] : '.');
	TRACE("Magic is 0x%" PRIx64, magic);
	TRACE("Size is %" PRIu64, *size);

	if (memcmp(buf, "NBDMAGIC", 8) != 0) {
		LOG("Invalid magic received");
		errno = EINVAL;
		return -1;
	}

	TRACE("Checking magic");

	if (magic != 0x00420281861253LL) {
		LOG("Bad magic received");
		errno = EINVAL;
		return -1;
	}
        return 0;
}

#ifndef _WIN32
int nbd_init(int fd, int csock, off_t size, size_t blocksize)
{
	TRACE("Setting block size to %lu", (unsigned long)blocksize);

	if (ioctl(fd, NBD_SET_BLKSIZE, blocksize) == -1) {
		int serrno = errno;
		LOG("Failed setting NBD block size");
		errno = serrno;
		return -1;
	}

	TRACE("Setting size to %llu block(s)",
	      (unsigned long long)(size / blocksize));

	if (ioctl(fd, NBD_SET_SIZE_BLOCKS, size / blocksize) == -1) {
		int serrno = errno;
		LOG("Failed setting size (in blocks)");
		errno = serrno;
		return -1;
	}

	TRACE("Clearing NBD socket");

	if (ioctl(fd, NBD_CLEAR_SOCK) == -1) {
		int serrno = errno;
		LOG("Failed clearing NBD socket");
		errno = serrno;
		return -1;
	}

	TRACE("Setting NBD socket");

	if (ioctl(fd, NBD_SET_SOCK, csock) == -1) {
		int serrno = errno;
		LOG("Failed to set NBD socket");
		errno = serrno;
		return -1;
	}

	TRACE("Negotiation ended");

	return 0;
}

int nbd_disconnect(int fd)
{
	ioctl(fd, NBD_CLEAR_QUE);
	ioctl(fd, NBD_DISCONNECT);
	ioctl(fd, NBD_CLEAR_SOCK);
	return 0;
}

int nbd_client(int fd, int csock)
{
	int ret;
	int serrno;

	TRACE("Doing NBD loop");

	ret = ioctl(fd, NBD_DO_IT);
	serrno = errno;

	TRACE("NBD loop returned %d: %s", ret, strerror(serrno));

	TRACE("Clearing NBD queue");
	ioctl(fd, NBD_CLEAR_QUE);

	TRACE("Clearing NBD socket");
	ioctl(fd, NBD_CLEAR_SOCK);

	errno = serrno;
	return ret;
}
#else
int nbd_init(int fd, int csock, off_t size, size_t blocksize)
{
    errno = ENOTSUP;
    return -1;
}

int nbd_disconnect(int fd)
{
    errno = ENOTSUP;
    return -1;
}

int nbd_client(int fd, int csock)
{
    errno = ENOTSUP;
    return -1;
}
#endif

int nbd_send_request(int csock, struct nbd_request *request)
{
	uint8_t buf[4 + 4 + 8 + 8 + 4];

	cpu_to_be32w((uint32_t*)buf, NBD_REQUEST_MAGIC);
	cpu_to_be32w((uint32_t*)(buf + 4), request->type);
	cpu_to_be64w((uint64_t*)(buf + 8), request->handle);
	cpu_to_be64w((uint64_t*)(buf + 16), request->from);
	cpu_to_be32w((uint32_t*)(buf + 24), request->len);

	TRACE("Sending request to client");

	if (write_sync(csock, buf, sizeof(buf)) != sizeof(buf)) {
		LOG("writing to socket failed");
		errno = EINVAL;
		return -1;
	}
	return 0;
}


static int nbd_receive_request(int csock, struct nbd_request *request)
{
	uint8_t buf[4 + 4 + 8 + 8 + 4];
	uint32_t magic;

	if (read_sync(csock, buf, sizeof(buf)) != sizeof(buf)) {
		LOG("read failed");
		errno = EINVAL;
		return -1;
	}

	/* Request
	   [ 0 ..  3]   magic   (NBD_REQUEST_MAGIC)
	   [ 4 ..  7]   type    (0 == READ, 1 == WRITE)
	   [ 8 .. 15]   handle
	   [16 .. 23]   from
	   [24 .. 27]   len
	 */

	magic = be32_to_cpup((uint32_t*)buf);
	request->type  = be32_to_cpup((uint32_t*)(buf + 4));
	request->handle = be64_to_cpup((uint64_t*)(buf + 8));
	request->from  = be64_to_cpup((uint64_t*)(buf + 16));
	request->len   = be32_to_cpup((uint32_t*)(buf + 24));

	TRACE("Got request: "
	      "{ magic = 0x%x, .type = %d, from = %" PRIu64" , len = %u }",
	      magic, request->type, request->from, request->len);

	if (magic != NBD_REQUEST_MAGIC) {
		LOG("invalid magic (got 0x%x)", magic);
		errno = EINVAL;
		return -1;
	}
	return 0;
}

int nbd_receive_reply(int csock, struct nbd_reply *reply)
{
	uint8_t buf[4 + 4 + 8];
	uint32_t magic;

	memset(buf, 0xAA, sizeof(buf));

	if (read_sync(csock, buf, sizeof(buf)) != sizeof(buf)) {
		LOG("read failed");
		errno = EINVAL;
		return -1;
	}

	/* Reply
	   [ 0 ..  3]    magic   (NBD_REPLY_MAGIC)
	   [ 4 ..  7]    error   (0 == no error)
	   [ 7 .. 15]    handle
	 */

	magic = be32_to_cpup((uint32_t*)buf);
	reply->error  = be32_to_cpup((uint32_t*)(buf + 4));
	reply->handle = be64_to_cpup((uint64_t*)(buf + 8));

	TRACE("Got reply: "
	      "{ magic = 0x%x, .error = %d, handle = %" PRIu64" }",
	      magic, reply->error, reply->handle);

	if (magic != NBD_REPLY_MAGIC) {
		LOG("invalid magic (got 0x%x)", magic);
		errno = EINVAL;
		return -1;
	}
	return 0;
}

static int nbd_send_reply(int csock, struct nbd_reply *reply)
{
	uint8_t buf[4 + 4 + 8];

	/* Reply
	   [ 0 ..  3]    magic   (NBD_REPLY_MAGIC)
	   [ 4 ..  7]    error   (0 == no error)
	   [ 7 .. 15]    handle
	 */
	cpu_to_be32w((uint32_t*)buf, NBD_REPLY_MAGIC);
	cpu_to_be32w((uint32_t*)(buf + 4), reply->error);
	cpu_to_be64w((uint64_t*)(buf + 8), reply->handle);

	TRACE("Sending response to client");

	if (write_sync(csock, buf, sizeof(buf)) != sizeof(buf)) {
		LOG("writing to socket failed");
		errno = EINVAL;
		return -1;
	}
	return 0;
}

int nbd_trip(BlockDriverState *bs, int csock, off_t size, uint64_t dev_offset,
             off_t *offset, bool readonly, uint8_t *data, int data_size)
{
	struct nbd_request request;
	struct nbd_reply reply;

	TRACE("Reading request.");

	if (nbd_receive_request(csock, &request) == -1)
		return -1;

	if (request.len > data_size) {
		LOG("len (%u) is larger than max len (%u)",
		    request.len, data_size);
		errno = EINVAL;
		return -1;
	}

	if ((request.from + request.len) < request.from) {
		LOG("integer overflow detected! "
		    "you're probably being attacked");
		errno = EINVAL;
		return -1;
	}

	if ((request.from + request.len) > size) {
	        LOG("From: %" PRIu64 ", Len: %u, Size: %" PRIu64
		    ", Offset: %" PRIu64 "\n",
                    request.from, request.len, (uint64_t)size, dev_offset);
		LOG("requested operation past EOF--bad client?");
		errno = EINVAL;
		return -1;
	}

	TRACE("Decoding type");

	reply.handle = request.handle;
	reply.error = 0;

	switch (request.type) {
	case NBD_CMD_READ:
		TRACE("Request type is READ");

		if (bdrv_read(bs, (request.from + dev_offset) / 512, data,
			      request.len / 512) == -1) {
			LOG("reading from file failed");
			errno = EINVAL;
			return -1;
		}
		*offset += request.len;

		TRACE("Read %u byte(s)", request.len);

		if (nbd_send_reply(csock, &reply) == -1)
			return -1;

		TRACE("Sending data to client");

		if (write_sync(csock, data, request.len) != request.len) {
			LOG("writing to socket failed");
			errno = EINVAL;
			return -1;
		}
		break;
	case NBD_CMD_WRITE:
		TRACE("Request type is WRITE");

		TRACE("Reading %u byte(s)", request.len);

		if (read_sync(csock, data, request.len) != request.len) {
			LOG("reading from socket failed");
			errno = EINVAL;
			return -1;
		}

		if (readonly) {
			TRACE("Server is read-only, return error");
			reply.error = 1;
		} else {
			TRACE("Writing to device");

			if (bdrv_write(bs, (request.from + dev_offset) / 512,
				       data, request.len / 512) == -1) {
				LOG("writing to file failed");
				errno = EINVAL;
				return -1;
			}

			*offset += request.len;
		}

		if (nbd_send_reply(csock, &reply) == -1)
			return -1;
		break;
	case NBD_CMD_DISC:
		TRACE("Request type is DISCONNECT");
		errno = 0;
		return 1;
	default:
		LOG("invalid request type (%u) received", request.type);
		errno = EINVAL;
		return -1;
	}

	TRACE("Request/Reply complete");

	return 0;
}
