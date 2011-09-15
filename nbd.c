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

#include "nbd.h"
#include "block.h"

#include <errno.h>
#include <string.h>
#ifndef _WIN32
#include <sys/ioctl.h>
#endif
#if defined(__sun__) || defined(__HAIKU__)
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

#define NBD_REPLY_SIZE          (4 + 4 + 8)
#define NBD_REQUEST_MAGIC       0x25609513
#define NBD_REPLY_MAGIC         0x67446698

#define NBD_SET_SOCK            _IO(0xab, 0)
#define NBD_SET_BLKSIZE         _IO(0xab, 1)
#define NBD_SET_SIZE            _IO(0xab, 2)
#define NBD_DO_IT               _IO(0xab, 3)
#define NBD_CLEAR_SOCK          _IO(0xab, 4)
#define NBD_CLEAR_QUE           _IO(0xab, 5)
#define NBD_PRINT_DEBUG         _IO(0xab, 6)
#define NBD_SET_SIZE_BLOCKS     _IO(0xab, 7)
#define NBD_DISCONNECT          _IO(0xab, 8)

#define NBD_OPT_EXPORT_NAME     (1 << 0)

/* That's all folks */

#define read_sync(fd, buffer, size) nbd_wr_sync(fd, buffer, size, true)
#define write_sync(fd, buffer, size) nbd_wr_sync(fd, buffer, size, false)

size_t nbd_wr_sync(int fd, void *buffer, size_t size, bool do_read)
{
    size_t offset = 0;

    while (offset < size) {
        ssize_t len;

        if (do_read) {
            len = qemu_recv(fd, buffer + offset, size - offset, 0);
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

static void combine_addr(char *buf, size_t len, const char* address,
                         uint16_t port)
{
    /* If the address-part contains a colon, it's an IPv6 IP so needs [] */
    if (strstr(address, ":")) {
        snprintf(buf, len, "[%s]:%u", address, port);
    } else {
        snprintf(buf, len, "%s:%u", address, port);
    }
}

int tcp_socket_outgoing(const char *address, uint16_t port)
{
    char address_and_port[128];
    combine_addr(address_and_port, 128, address, port);
    return tcp_socket_outgoing_spec(address_and_port);
}

int tcp_socket_outgoing_spec(const char *address_and_port)
{
    return inet_connect(address_and_port, SOCK_STREAM);
}

int tcp_socket_incoming(const char *address, uint16_t port)
{
    char address_and_port[128];
    combine_addr(address_and_port, 128, address, port);
    return tcp_socket_incoming_spec(address_and_port);
}

int tcp_socket_incoming_spec(const char *address_and_port)
{
    char *ostr  = NULL;
    int olen = 0;
    return inet_listen(address_and_port, ostr, olen, SOCK_STREAM, 0);
}

int unix_socket_incoming(const char *path)
{
    char *ostr = NULL;
    int olen = 0;

    return unix_listen(path, ostr, olen);
}

int unix_socket_outgoing(const char *path)
{
    return unix_connect(path);
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

int nbd_receive_negotiate(int csock, const char *name, uint32_t *flags,
                          off_t *size, size_t *blocksize)
{
    char buf[256];
    uint64_t magic, s;
    uint16_t tmp;

    TRACE("Receiving negotation.");

    if (read_sync(csock, buf, 8) != 8) {
        LOG("read failed");
        errno = EINVAL;
        return -1;
    }

    buf[8] = '\0';
    if (strlen(buf) == 0) {
        LOG("server connection closed");
        errno = EINVAL;
        return -1;
    }

    TRACE("Magic is %c%c%c%c%c%c%c%c",
          qemu_isprint(buf[0]) ? buf[0] : '.',
          qemu_isprint(buf[1]) ? buf[1] : '.',
          qemu_isprint(buf[2]) ? buf[2] : '.',
          qemu_isprint(buf[3]) ? buf[3] : '.',
          qemu_isprint(buf[4]) ? buf[4] : '.',
          qemu_isprint(buf[5]) ? buf[5] : '.',
          qemu_isprint(buf[6]) ? buf[6] : '.',
          qemu_isprint(buf[7]) ? buf[7] : '.');

    if (memcmp(buf, "NBDMAGIC", 8) != 0) {
        LOG("Invalid magic received");
        errno = EINVAL;
        return -1;
    }

    if (read_sync(csock, &magic, sizeof(magic)) != sizeof(magic)) {
        LOG("read failed");
        errno = EINVAL;
        return -1;
    }
    magic = be64_to_cpu(magic);
    TRACE("Magic is 0x%" PRIx64, magic);

    if (name) {
        uint32_t reserved = 0;
        uint32_t opt;
        uint32_t namesize;

        TRACE("Checking magic (opts_magic)");
        if (magic != 0x49484156454F5054LL) {
            LOG("Bad magic received");
            errno = EINVAL;
            return -1;
        }
        if (read_sync(csock, &tmp, sizeof(tmp)) != sizeof(tmp)) {
            LOG("flags read failed");
            errno = EINVAL;
            return -1;
        }
        *flags = be16_to_cpu(tmp) << 16;
        /* reserved for future use */
        if (write_sync(csock, &reserved, sizeof(reserved)) !=
            sizeof(reserved)) {
            LOG("write failed (reserved)");
            errno = EINVAL;
            return -1;
        }
        /* write the export name */
        magic = cpu_to_be64(magic);
        if (write_sync(csock, &magic, sizeof(magic)) != sizeof(magic)) {
            LOG("write failed (magic)");
            errno = EINVAL;
            return -1;
        }
        opt = cpu_to_be32(NBD_OPT_EXPORT_NAME);
        if (write_sync(csock, &opt, sizeof(opt)) != sizeof(opt)) {
            LOG("write failed (opt)");
            errno = EINVAL;
            return -1;
        }
        namesize = cpu_to_be32(strlen(name));
        if (write_sync(csock, &namesize, sizeof(namesize)) !=
            sizeof(namesize)) {
            LOG("write failed (namesize)");
            errno = EINVAL;
            return -1;
        }
        if (write_sync(csock, (char*)name, strlen(name)) != strlen(name)) {
            LOG("write failed (name)");
            errno = EINVAL;
            return -1;
        }
    } else {
        TRACE("Checking magic (cli_magic)");

        if (magic != 0x00420281861253LL) {
            LOG("Bad magic received");
            errno = EINVAL;
            return -1;
        }
    }

    if (read_sync(csock, &s, sizeof(s)) != sizeof(s)) {
        LOG("read failed");
        errno = EINVAL;
        return -1;
    }
    *size = be64_to_cpu(s);
    *blocksize = 1024;
    TRACE("Size is %" PRIu64, *size);

    if (!name) {
        if (read_sync(csock, flags, sizeof(*flags)) != sizeof(*flags)) {
            LOG("read failed (flags)");
            errno = EINVAL;
            return -1;
        }
        *flags = be32_to_cpup(flags);
    } else {
        if (read_sync(csock, &tmp, sizeof(tmp)) != sizeof(tmp)) {
            LOG("read failed (tmp)");
            errno = EINVAL;
            return -1;
        }
        *flags |= be32_to_cpu(tmp);
    }
    if (read_sync(csock, &buf, 124) != 124) {
        LOG("read failed (buf)");
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

        TRACE("Setting size to %zd block(s)", (size_t)(size / blocksize));

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

int nbd_client(int fd)
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

int nbd_client(int fd)
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

    TRACE("Sending request to client: "
          "{ .from = %" PRIu64", .len = %u, .handle = %" PRIu64", .type=%i}",
          request->from, request->len, request->handle, request->type);

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
    uint8_t buf[NBD_REPLY_SIZE];
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

    if (request.len + NBD_REPLY_SIZE > data_size) {
        LOG("len (%u) is larger than max len (%u)",
            request.len + NBD_REPLY_SIZE, data_size);
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

        if (bdrv_read(bs, (request.from + dev_offset) / 512,
                  data + NBD_REPLY_SIZE,
                  request.len / 512) == -1) {
            LOG("reading from file failed");
            errno = EINVAL;
            return -1;
        }
        *offset += request.len;

        TRACE("Read %u byte(s)", request.len);

        /* Reply
           [ 0 ..  3]    magic   (NBD_REPLY_MAGIC)
           [ 4 ..  7]    error   (0 == no error)
           [ 7 .. 15]    handle
         */

        cpu_to_be32w((uint32_t*)data, NBD_REPLY_MAGIC);
        cpu_to_be32w((uint32_t*)(data + 4), reply.error);
        cpu_to_be64w((uint64_t*)(data + 8), reply.handle);

        TRACE("Sending data to client");

        if (write_sync(csock, data,
                   request.len + NBD_REPLY_SIZE) !=
                   request.len + NBD_REPLY_SIZE) {
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
