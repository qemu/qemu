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

#include "nbd.h"

#include <errno.h>
#include <string.h>
#include <sys/ioctl.h>
#include <ctype.h>
#include <inttypes.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>

extern int verbose;

#define LOG(msg, ...) do { \
    fprintf(stderr, "%s:%s():L%d: " msg "\n", \
            __FILE__, __FUNCTION__, __LINE__, ## __VA_ARGS__); \
} while(0)

#define TRACE(msg, ...) do { \
    if (verbose) LOG(msg, ## __VA_ARGS__); \
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

#define read_sync(fd, buffer, size) wr_sync(fd, buffer, size, true)
#define write_sync(fd, buffer, size) wr_sync(fd, buffer, size, false)

static size_t wr_sync(int fd, void *buffer, size_t size, bool do_read)
{
    size_t offset = 0;

    while (offset < size) {
        ssize_t len;

        if (do_read) {
            len = read(fd, buffer + offset, size - offset);
        } else {
            len = write(fd, buffer + offset, size - offset);
        }

        /* recoverable error */
        if (len == -1 && errno == EAGAIN) {
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

static int tcp_socket_outgoing(const char *address, uint16_t port)
{
    int s;
    struct in_addr in;
    struct sockaddr_in addr;
    int serrno;

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
    serrno = errno;
    close(s);
    errno = serrno;
    return -1;
}

int tcp_socket_incoming(const char *address, uint16_t port)
{
    int s;
    struct in_addr in;
    struct sockaddr_in addr;
    int serrno;
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
    if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
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
    serrno = errno;
    close(s);
    errno = serrno;
    return -1;
}

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

int nbd_negotiate(BlockDriverState *bs, int csock, off_t size)
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

int nbd_receive_negotiate(int fd, int csock)
{
	char buf[8 + 8 + 8 + 128];
	uint64_t magic;
	off_t size;
	size_t blocksize;

	TRACE("Receiving negotation.");

	if (read_sync(csock, buf, sizeof(buf)) != sizeof(buf)) {
		LOG("read failed");
		errno = EINVAL;
		return -1;
	}

	magic = be64_to_cpup((uint64_t*)(buf + 8));
	size = be64_to_cpup((uint64_t*)(buf + 16));
	blocksize = 1024;

	TRACE("Magic is %c%c%c%c%c%c%c%c",
	      isprint(buf[0]) ? buf[0] : '.',
	      isprint(buf[1]) ? buf[1] : '.',
	      isprint(buf[2]) ? buf[2] : '.',
	      isprint(buf[3]) ? buf[3] : '.',
	      isprint(buf[4]) ? buf[4] : '.',
	      isprint(buf[5]) ? buf[5] : '.',
	      isprint(buf[6]) ? buf[6] : '.',
	      isprint(buf[7]) ? buf[7] : '.');
	TRACE("Magic is 0x%" PRIx64, magic);
	TRACE("Size is %" PRIu64, size);

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

int nbd_trip(BlockDriverState *bs, int csock, off_t size, uint64_t dev_offset, off_t *offset, bool readonly)
{
#ifndef _REENTRANT
	static uint8_t data[1024 * 1024]; // keep this off of the stack
#else
	uint8_t data[1024 * 1024];
#endif
	uint8_t buf[4 + 4 + 8 + 8 + 4];
	uint32_t magic;
	uint32_t type;
	uint64_t from;
	uint32_t len;

	TRACE("Reading request.");

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
	type  = be32_to_cpup((uint32_t*)(buf + 4));
	from  = be64_to_cpup((uint64_t*)(buf + 16));
	len   = be32_to_cpup((uint32_t*)(buf + 24));

	TRACE("Got request: "
	      "{ magic = 0x%x, .type = %d, from = %" PRIu64" , len = %u }",
	      magic, type, from, len);


	if (magic != NBD_REQUEST_MAGIC) {
		LOG("invalid magic (got 0x%x)", magic);
		errno = EINVAL;
		return -1;
	}

	if (len > sizeof(data)) {
		LOG("len (%u) is larger than max len (%lu)",
		    len, (unsigned long)sizeof(data));
		errno = EINVAL;
		return -1;
	}

	if ((from + len) < from) {
		LOG("integer overflow detected! "
		    "you're probably being attacked");
		errno = EINVAL;
		return -1;
	}

	if ((from + len) > size) {
	        LOG("From: %" PRIu64 ", Len: %u, Size: %" PRIu64
		    ", Offset: %" PRIu64 "\n",
		     from, len, size, dev_offset);
		LOG("requested operation past EOF--bad client?");
		errno = EINVAL;
		return -1;
	}

	/* Reply
	 [ 0 ..  3]    magic   (NBD_REPLY_MAGIC)
	 [ 4 ..  7]    error   (0 == no error)
         [ 7 .. 15]    handle
	 */
	cpu_to_be32w((uint32_t*)buf, NBD_REPLY_MAGIC);
	cpu_to_be32w((uint32_t*)(buf + 4), 0);

	TRACE("Decoding type");

	switch (type) {
	case 0:
		TRACE("Request type is READ");

		if (bdrv_read(bs, (from + dev_offset) / 512, data, len / 512) == -1) {
			LOG("reading from file failed");
			errno = EINVAL;
			return -1;
		}
		*offset += len;

		TRACE("Read %u byte(s)", len);

		TRACE("Sending OK response");

		if (write_sync(csock, buf, 16) != 16) {
			LOG("writing to socket failed");
			errno = EINVAL;
			return -1;
		}

		TRACE("Sending data to client");

		if (write_sync(csock, data, len) != len) {
			LOG("writing to socket failed");
			errno = EINVAL;
			return -1;
		}
		break;
	case 1:
		TRACE("Request type is WRITE");

		TRACE("Reading %u byte(s)", len);

		if (read_sync(csock, data, len) != len) {
			LOG("reading from socket failed");
			errno = EINVAL;
			return -1;
		}

		if (readonly) {
			TRACE("Server is read-only, return error");

			cpu_to_be32w((uint32_t*)(buf + 4), 1);
		} else {
			TRACE("Writing to device");

			if (bdrv_write(bs, (from + dev_offset) / 512, data, len / 512) == -1) {
				LOG("writing to file failed");
				errno = EINVAL;
				return -1;
			}

			*offset += len;
		}

		TRACE("Sending response to client");

		if (write_sync(csock, buf, 16) != 16) {
			LOG("writing to socket failed");
			errno = EINVAL;
			return -1;
		}
		break;
	case 2:
		TRACE("Request type is DISCONNECT");
		errno = 0;
		return 1;
	default:
		LOG("invalid request type (%u) received", type);
		errno = EINVAL;
		return -1;
	}

	TRACE("Request/Reply complete");

	return 0;
}
