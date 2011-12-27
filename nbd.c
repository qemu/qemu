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
#include "block_int.h"

#include "qemu-coroutine.h"

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

#ifdef __linux__
#include <linux/fs.h>
#endif

#include "qemu_socket.h"
#include "qemu-queue.h"

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
#define NBD_SET_TIMEOUT         _IO(0xab, 9)
#define NBD_SET_FLAGS           _IO(0xab, 10)

#define NBD_OPT_EXPORT_NAME     (1 << 0)

/* That's all folks */

#define read_sync(fd, buffer, size) nbd_wr_sync(fd, buffer, size, true)
#define write_sync(fd, buffer, size) nbd_wr_sync(fd, buffer, size, false)

size_t nbd_wr_sync(int fd, void *buffer, size_t size, bool do_read)
{
    size_t offset = 0;

    if (qemu_in_coroutine()) {
        if (do_read) {
            return qemu_co_recv(fd, buffer, size);
        } else {
            return qemu_co_send(fd, buffer, size);
        }
    }

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

static int nbd_send_negotiate(int csock, off_t size, uint32_t flags)
{
    char buf[8 + 8 + 8 + 128];

    /* Negotiate
        [ 0 ..   7]   passwd   ("NBDMAGIC")
        [ 8 ..  15]   magic    (0x00420281861253)
        [16 ..  23]   size
        [24 ..  27]   flags
        [28 .. 151]   reserved (0)
     */

    TRACE("Beginning negotiation.");
    memcpy(buf, "NBDMAGIC", 8);
    cpu_to_be64w((uint64_t*)(buf + 8), 0x00420281861253LL);
    cpu_to_be64w((uint64_t*)(buf + 16), size);
    cpu_to_be32w((uint32_t*)(buf + 24),
                 flags | NBD_FLAG_HAS_FLAGS | NBD_FLAG_SEND_TRIM |
                 NBD_FLAG_SEND_FLUSH | NBD_FLAG_SEND_FUA);
    memset(buf + 28, 0, 124);

    if (write_sync(csock, buf, sizeof(buf)) != sizeof(buf)) {
        LOG("write failed");
        errno = EINVAL;
        return -1;
    }

    TRACE("Negotiation succeeded.");

    return 0;
}

int nbd_receive_negotiate(int csock, const char *name, uint32_t *flags,
                          off_t *size, size_t *blocksize)
{
    char buf[256];
    uint64_t magic, s;
    uint16_t tmp;

    TRACE("Receiving negotiation.");

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

#ifdef __linux__
int nbd_init(int fd, int csock, uint32_t flags, off_t size, size_t blocksize)
{
    TRACE("Setting NBD socket");

    if (ioctl(fd, NBD_SET_SOCK, csock) == -1) {
        int serrno = errno;
        LOG("Failed to set NBD socket");
        errno = serrno;
        return -1;
    }

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

    if (flags & NBD_FLAG_READ_ONLY) {
        int read_only = 1;
        TRACE("Setting readonly attribute");

        if (ioctl(fd, BLKROSET, (unsigned long) &read_only) < 0) {
            int serrno = errno;
            LOG("Failed setting read-only attribute");
            errno = serrno;
            return -1;
        }
    }

    if (ioctl(fd, NBD_SET_FLAGS, flags) < 0
        && errno != ENOTTY) {
        int serrno = errno;
        LOG("Failed setting flags");
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
    if (ret == -1 && errno == EPIPE) {
        /* NBD_DO_IT normally returns EPIPE when someone has disconnected
         * the socket via NBD_DISCONNECT.  We do not want to return 1 in
         * that case.
         */
        ret = 0;
    }
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
int nbd_init(int fd, int csock, uint32_t flags, off_t size, size_t blocksize)
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

#define MAX_NBD_REQUESTS 16

typedef struct NBDRequest NBDRequest;

struct NBDRequest {
    QSIMPLEQ_ENTRY(NBDRequest) entry;
    NBDClient *client;
    uint8_t *data;
};

struct NBDExport {
    BlockDriverState *bs;
    off_t dev_offset;
    off_t size;
    uint32_t nbdflags;
    QSIMPLEQ_HEAD(, NBDRequest) requests;
};

struct NBDClient {
    int refcount;
    void (*close)(NBDClient *client);

    NBDExport *exp;
    int sock;

    Coroutine *recv_coroutine;

    CoMutex send_lock;
    Coroutine *send_coroutine;

    int nb_requests;
};

static void nbd_client_get(NBDClient *client)
{
    client->refcount++;
}

static void nbd_client_put(NBDClient *client)
{
    if (--client->refcount == 0) {
        g_free(client);
    }
}

static void nbd_client_close(NBDClient *client)
{
    qemu_set_fd_handler2(client->sock, NULL, NULL, NULL, NULL);
    close(client->sock);
    client->sock = -1;
    if (client->close) {
        client->close(client);
    }
    nbd_client_put(client);
}

static NBDRequest *nbd_request_get(NBDClient *client)
{
    NBDRequest *req;
    NBDExport *exp = client->exp;

    assert(client->nb_requests <= MAX_NBD_REQUESTS - 1);
    client->nb_requests++;

    if (QSIMPLEQ_EMPTY(&exp->requests)) {
        req = g_malloc0(sizeof(NBDRequest));
        req->data = qemu_blockalign(exp->bs, NBD_BUFFER_SIZE);
    } else {
        req = QSIMPLEQ_FIRST(&exp->requests);
        QSIMPLEQ_REMOVE_HEAD(&exp->requests, entry);
    }
    nbd_client_get(client);
    req->client = client;
    return req;
}

static void nbd_request_put(NBDRequest *req)
{
    NBDClient *client = req->client;
    QSIMPLEQ_INSERT_HEAD(&client->exp->requests, req, entry);
    if (client->nb_requests-- == MAX_NBD_REQUESTS) {
        qemu_notify_event();
    }
    nbd_client_put(client);
}

NBDExport *nbd_export_new(BlockDriverState *bs, off_t dev_offset,
                          off_t size, uint32_t nbdflags)
{
    NBDExport *exp = g_malloc0(sizeof(NBDExport));
    QSIMPLEQ_INIT(&exp->requests);
    exp->bs = bs;
    exp->dev_offset = dev_offset;
    exp->nbdflags = nbdflags;
    exp->size = size == -1 ? exp->bs->total_sectors * 512 : size;
    return exp;
}

void nbd_export_close(NBDExport *exp)
{
    while (!QSIMPLEQ_EMPTY(&exp->requests)) {
        NBDRequest *first = QSIMPLEQ_FIRST(&exp->requests);
        QSIMPLEQ_REMOVE_HEAD(&exp->requests, entry);
        qemu_vfree(first->data);
        g_free(first);
    }

    bdrv_close(exp->bs);
    g_free(exp);
}

static int nbd_can_read(void *opaque);
static void nbd_read(void *opaque);
static void nbd_restart_write(void *opaque);

static int nbd_co_send_reply(NBDRequest *req, struct nbd_reply *reply,
                             int len)
{
    NBDClient *client = req->client;
    int csock = client->sock;
    int rc, ret;

    qemu_co_mutex_lock(&client->send_lock);
    qemu_set_fd_handler2(csock, nbd_can_read, nbd_read,
                         nbd_restart_write, client);
    client->send_coroutine = qemu_coroutine_self();

    if (!len) {
        rc = nbd_send_reply(csock, reply);
        if (rc == -1) {
            rc = -errno;
        }
    } else {
        socket_set_cork(csock, 1);
        rc = nbd_send_reply(csock, reply);
        if (rc != -1) {
            ret = qemu_co_send(csock, req->data, len);
            if (ret != len) {
                errno = EIO;
                rc = -1;
            }
        }
        if (rc == -1) {
            rc = -errno;
        }
        socket_set_cork(csock, 0);
    }

    client->send_coroutine = NULL;
    qemu_set_fd_handler2(csock, nbd_can_read, nbd_read, NULL, client);
    qemu_co_mutex_unlock(&client->send_lock);
    return rc;
}

static int nbd_co_receive_request(NBDRequest *req, struct nbd_request *request)
{
    NBDClient *client = req->client;
    int csock = client->sock;
    int rc;

    client->recv_coroutine = qemu_coroutine_self();
    if (nbd_receive_request(csock, request) == -1) {
        rc = -EIO;
        goto out;
    }

    if (request->len > NBD_BUFFER_SIZE) {
        LOG("len (%u) is larger than max len (%u)",
            request->len, NBD_BUFFER_SIZE);
        rc = -EINVAL;
        goto out;
    }

    if ((request->from + request->len) < request->from) {
        LOG("integer overflow detected! "
            "you're probably being attacked");
        rc = -EINVAL;
        goto out;
    }

    TRACE("Decoding type");

    if ((request->type & NBD_CMD_MASK_COMMAND) == NBD_CMD_WRITE) {
        TRACE("Reading %u byte(s)", request->len);

        if (qemu_co_recv(csock, req->data, request->len) != request->len) {
            LOG("reading from socket failed");
            rc = -EIO;
            goto out;
        }
    }
    rc = 0;

out:
    client->recv_coroutine = NULL;
    return rc;
}

static void nbd_trip(void *opaque)
{
    NBDClient *client = opaque;
    NBDRequest *req = nbd_request_get(client);
    NBDExport *exp = client->exp;
    struct nbd_request request;
    struct nbd_reply reply;
    int ret;

    TRACE("Reading request.");

    ret = nbd_co_receive_request(req, &request);
    if (ret == -EIO) {
        goto out;
    }

    reply.handle = request.handle;
    reply.error = 0;

    if (ret < 0) {
        reply.error = -ret;
        goto error_reply;
    }

    if ((request.from + request.len) > exp->size) {
            LOG("From: %" PRIu64 ", Len: %u, Size: %" PRIu64
            ", Offset: %" PRIu64 "\n",
                    request.from, request.len,
                    (uint64_t)exp->size, exp->dev_offset);
        LOG("requested operation past EOF--bad client?");
        goto invalid_request;
    }

    switch (request.type & NBD_CMD_MASK_COMMAND) {
    case NBD_CMD_READ:
        TRACE("Request type is READ");

        ret = bdrv_read(exp->bs, (request.from + exp->dev_offset) / 512,
                        req->data, request.len / 512);
        if (ret < 0) {
            LOG("reading from file failed");
            reply.error = -ret;
            goto error_reply;
        }

        TRACE("Read %u byte(s)", request.len);
        if (nbd_co_send_reply(req, &reply, request.len) < 0)
            goto out;
        break;
    case NBD_CMD_WRITE:
        TRACE("Request type is WRITE");

        if (exp->nbdflags & NBD_FLAG_READ_ONLY) {
            TRACE("Server is read-only, return error");
            reply.error = EROFS;
            goto error_reply;
        }

        TRACE("Writing to device");

        ret = bdrv_write(exp->bs, (request.from + exp->dev_offset) / 512,
                         req->data, request.len / 512);
        if (ret < 0) {
            LOG("writing to file failed");
            reply.error = -ret;
            goto error_reply;
        }

        if (request.type & NBD_CMD_FLAG_FUA) {
            ret = bdrv_co_flush(exp->bs);
            if (ret < 0) {
                LOG("flush failed");
                reply.error = -ret;
                goto error_reply;
            }
        }

        if (nbd_co_send_reply(req, &reply, 0) < 0)
            goto out;
        break;
    case NBD_CMD_DISC:
        TRACE("Request type is DISCONNECT");
        errno = 0;
        goto out;
    case NBD_CMD_FLUSH:
        TRACE("Request type is FLUSH");

        ret = bdrv_co_flush(exp->bs);
        if (ret < 0) {
            LOG("flush failed");
            reply.error = -ret;
        }

        if (nbd_co_send_reply(req, &reply, 0) < 0)
            goto out;
        break;
    case NBD_CMD_TRIM:
        TRACE("Request type is TRIM");
        ret = bdrv_co_discard(exp->bs, (request.from + exp->dev_offset) / 512,
                              request.len / 512);
        if (ret < 0) {
            LOG("discard failed");
            reply.error = -ret;
        }
        if (nbd_co_send_reply(req, &reply, 0) < 0)
            goto out;
        break;
    default:
        LOG("invalid request type (%u) received", request.type);
    invalid_request:
        reply.error = -EINVAL;
    error_reply:
        if (nbd_co_send_reply(req, &reply, 0) == -1)
            goto out;
        break;
    }

    TRACE("Request/Reply complete");

    nbd_request_put(req);
    return;

out:
    nbd_request_put(req);
    nbd_client_close(client);
}

static int nbd_can_read(void *opaque)
{
    NBDClient *client = opaque;

    return client->recv_coroutine || client->nb_requests < MAX_NBD_REQUESTS;
}

static void nbd_read(void *opaque)
{
    NBDClient *client = opaque;

    if (client->recv_coroutine) {
        qemu_coroutine_enter(client->recv_coroutine, NULL);
    } else {
        qemu_coroutine_enter(qemu_coroutine_create(nbd_trip), client);
    }
}

static void nbd_restart_write(void *opaque)
{
    NBDClient *client = opaque;

    qemu_coroutine_enter(client->send_coroutine, NULL);
}

NBDClient *nbd_client_new(NBDExport *exp, int csock,
                          void (*close)(NBDClient *))
{
    NBDClient *client;
    if (nbd_send_negotiate(csock, exp->size, exp->nbdflags) == -1) {
        return NULL;
    }
    client = g_malloc0(sizeof(NBDClient));
    client->refcount = 1;
    client->exp = exp;
    client->sock = csock;
    client->close = close;
    qemu_co_mutex_init(&client->send_lock);
    qemu_set_fd_handler2(csock, nbd_can_read, nbd_read, NULL, client);
    return client;
}
