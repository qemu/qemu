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

#include "block/nbd.h"
#include "block/block.h"

#include "block/coroutine.h"

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

#include "qemu/sockets.h"
#include "qemu/queue.h"
#include "qemu/main-loop.h"

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

/* This is all part of the "official" NBD API.
 *
 * The most up-to-date documentation is available at:
 * https://github.com/yoe/nbd/blob/master/doc/proto.txt
 */

#define NBD_REQUEST_SIZE        (4 + 4 + 8 + 8 + 4)
#define NBD_REPLY_SIZE          (4 + 4 + 8)
#define NBD_REQUEST_MAGIC       0x25609513
#define NBD_REPLY_MAGIC         0x67446698
#define NBD_OPTS_MAGIC          0x49484156454F5054LL
#define NBD_CLIENT_MAGIC        0x0000420281861253LL
#define NBD_REP_MAGIC           0x3e889045565a9LL

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

#define NBD_OPT_EXPORT_NAME     (1)
#define NBD_OPT_ABORT           (2)
#define NBD_OPT_LIST            (3)

/* Definitions for opaque data types */

typedef struct NBDRequest NBDRequest;

struct NBDRequest {
    QSIMPLEQ_ENTRY(NBDRequest) entry;
    NBDClient *client;
    uint8_t *data;
};

struct NBDExport {
    int refcount;
    void (*close)(NBDExport *exp);

    BlockDriverState *bs;
    char *name;
    off_t dev_offset;
    off_t size;
    uint32_t nbdflags;
    QTAILQ_HEAD(, NBDClient) clients;
    QTAILQ_ENTRY(NBDExport) next;
};

static QTAILQ_HEAD(, NBDExport) exports = QTAILQ_HEAD_INITIALIZER(exports);

struct NBDClient {
    int refcount;
    void (*close)(NBDClient *client);

    NBDExport *exp;
    int sock;

    Coroutine *recv_coroutine;

    CoMutex send_lock;
    Coroutine *send_coroutine;

    QTAILQ_ENTRY(NBDClient) next;
    int nb_requests;
    bool closing;
};

/* That's all folks */

ssize_t nbd_wr_sync(int fd, void *buffer, size_t size, bool do_read)
{
    size_t offset = 0;
    int err;

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

        if (len < 0) {
            err = socket_error();

            /* recoverable error */
            if (err == EINTR || (offset > 0 && err == EAGAIN)) {
                continue;
            }

            /* unrecoverable error */
            return -err;
        }

        /* eof */
        if (len == 0) {
            break;
        }

        offset += len;
    }

    return offset;
}

static ssize_t read_sync(int fd, void *buffer, size_t size)
{
    /* Sockets are kept in blocking mode in the negotiation phase.  After
     * that, a non-readable socket simply means that another thread stole
     * our request/reply.  Synchronization is done with recv_coroutine, so
     * that this is coroutine-safe.
     */
    return nbd_wr_sync(fd, buffer, size, true);
}

static ssize_t write_sync(int fd, void *buffer, size_t size)
{
    int ret;
    do {
        /* For writes, we do expect the socket to be writable.  */
        ret = nbd_wr_sync(fd, buffer, size, false);
    } while (ret == -EAGAIN);
    return ret;
}

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

static int nbd_send_rep(int csock, uint32_t type, uint32_t opt)
{
    uint64_t magic;
    uint32_t len;

    magic = cpu_to_be64(NBD_REP_MAGIC);
    if (write_sync(csock, &magic, sizeof(magic)) != sizeof(magic)) {
        LOG("write failed (rep magic)");
        return -EINVAL;
    }
    opt = cpu_to_be32(opt);
    if (write_sync(csock, &opt, sizeof(opt)) != sizeof(opt)) {
        LOG("write failed (rep opt)");
        return -EINVAL;
    }
    type = cpu_to_be32(type);
    if (write_sync(csock, &type, sizeof(type)) != sizeof(type)) {
        LOG("write failed (rep type)");
        return -EINVAL;
    }
    len = cpu_to_be32(0);
    if (write_sync(csock, &len, sizeof(len)) != sizeof(len)) {
        LOG("write failed (rep data length)");
        return -EINVAL;
    }
    return 0;
}

static int nbd_send_rep_list(int csock, NBDExport *exp)
{
    uint64_t magic, name_len;
    uint32_t opt, type, len;

    name_len = strlen(exp->name);
    magic = cpu_to_be64(NBD_REP_MAGIC);
    if (write_sync(csock, &magic, sizeof(magic)) != sizeof(magic)) {
        LOG("write failed (magic)");
        return -EINVAL;
     }
    opt = cpu_to_be32(NBD_OPT_LIST);
    if (write_sync(csock, &opt, sizeof(opt)) != sizeof(opt)) {
        LOG("write failed (opt)");
        return -EINVAL;
    }
    type = cpu_to_be32(NBD_REP_SERVER);
    if (write_sync(csock, &type, sizeof(type)) != sizeof(type)) {
        LOG("write failed (reply type)");
        return -EINVAL;
    }
    len = cpu_to_be32(name_len + sizeof(len));
    if (write_sync(csock, &len, sizeof(len)) != sizeof(len)) {
        LOG("write failed (length)");
        return -EINVAL;
    }
    len = cpu_to_be32(name_len);
    if (write_sync(csock, &len, sizeof(len)) != sizeof(len)) {
        LOG("write failed (length)");
        return -EINVAL;
    }
    if (write_sync(csock, exp->name, name_len) != name_len) {
        LOG("write failed (buffer)");
        return -EINVAL;
    }
    return 0;
}

static int nbd_handle_list(NBDClient *client, uint32_t length)
{
    int csock;
    NBDExport *exp;

    csock = client->sock;
    if (length) {
        return nbd_send_rep(csock, NBD_REP_ERR_INVALID, NBD_OPT_LIST);
    }

    /* For each export, send a NBD_REP_SERVER reply. */
    QTAILQ_FOREACH(exp, &exports, next) {
        if (nbd_send_rep_list(csock, exp)) {
            return -EINVAL;
        }
    }
    /* Finish with a NBD_REP_ACK. */
    return nbd_send_rep(csock, NBD_REP_ACK, NBD_OPT_LIST);
}

static int nbd_handle_export_name(NBDClient *client, uint32_t length)
{
    int rc = -EINVAL, csock = client->sock;
    char name[256];

    /* Client sends:
        [20 ..  xx]   export name (length bytes)
     */
    TRACE("Checking length");
    if (length > 255) {
        LOG("Bad length received");
        goto fail;
    }
    if (read_sync(csock, name, length) != length) {
        LOG("read failed");
        goto fail;
    }
    name[length] = '\0';

    client->exp = nbd_export_find(name);
    if (!client->exp) {
        LOG("export not found");
        goto fail;
    }

    QTAILQ_INSERT_TAIL(&client->exp->clients, client, next);
    nbd_export_get(client->exp);
    rc = 0;
fail:
    return rc;
}

static int nbd_receive_options(NBDClient *client)
{
    while (1) {
        int csock = client->sock;
        uint32_t tmp, length;
        uint64_t magic;

        /* Client sends:
            [ 0 ..   3]   client flags
            [ 4 ..  11]   NBD_OPTS_MAGIC
            [12 ..  15]   NBD option
            [16 ..  19]   length
            ...           Rest of request
        */

        if (read_sync(csock, &tmp, sizeof(tmp)) != sizeof(tmp)) {
            LOG("read failed");
            return -EINVAL;
        }
        TRACE("Checking client flags");
        tmp = be32_to_cpu(tmp);
        if (tmp != 0 && tmp != NBD_FLAG_C_FIXED_NEWSTYLE) {
            LOG("Bad client flags received");
            return -EINVAL;
        }

        if (read_sync(csock, &magic, sizeof(magic)) != sizeof(magic)) {
            LOG("read failed");
            return -EINVAL;
        }
        TRACE("Checking opts magic");
        if (magic != be64_to_cpu(NBD_OPTS_MAGIC)) {
            LOG("Bad magic received");
            return -EINVAL;
        }

        if (read_sync(csock, &tmp, sizeof(tmp)) != sizeof(tmp)) {
            LOG("read failed");
            return -EINVAL;
        }

        if (read_sync(csock, &length, sizeof(length)) != sizeof(length)) {
            LOG("read failed");
            return -EINVAL;
        }
        length = be32_to_cpu(length);

        TRACE("Checking option");
        switch (be32_to_cpu(tmp)) {
        case NBD_OPT_LIST:
            if (nbd_handle_list(client, length) < 0) {
                return 1;
            }
            break;

        case NBD_OPT_ABORT:
            return -EINVAL;

        case NBD_OPT_EXPORT_NAME:
            return nbd_handle_export_name(client, length);

        default:
            tmp = be32_to_cpu(tmp);
            LOG("Unsupported option 0x%x", tmp);
            nbd_send_rep(client->sock, NBD_REP_ERR_UNSUP, tmp);
            return -EINVAL;
        }
    }
}

static int nbd_send_negotiate(NBDClient *client)
{
    int csock = client->sock;
    char buf[8 + 8 + 8 + 128];
    int rc;
    const int myflags = (NBD_FLAG_HAS_FLAGS | NBD_FLAG_SEND_TRIM |
                         NBD_FLAG_SEND_FLUSH | NBD_FLAG_SEND_FUA);

    /* Negotiation header without options:
        [ 0 ..   7]   passwd       ("NBDMAGIC")
        [ 8 ..  15]   magic        (NBD_CLIENT_MAGIC)
        [16 ..  23]   size
        [24 ..  25]   server flags (0)
        [26 ..  27]   export flags
        [28 .. 151]   reserved     (0)

       Negotiation header with options, part 1:
        [ 0 ..   7]   passwd       ("NBDMAGIC")
        [ 8 ..  15]   magic        (NBD_OPTS_MAGIC)
        [16 ..  17]   server flags (0)

       part 2 (after options are sent):
        [18 ..  25]   size
        [26 ..  27]   export flags
        [28 .. 151]   reserved     (0)
     */

    qemu_set_block(csock);
    rc = -EINVAL;

    TRACE("Beginning negotiation.");
    memset(buf, 0, sizeof(buf));
    memcpy(buf, "NBDMAGIC", 8);
    if (client->exp) {
        assert ((client->exp->nbdflags & ~65535) == 0);
        cpu_to_be64w((uint64_t*)(buf + 8), NBD_CLIENT_MAGIC);
        cpu_to_be64w((uint64_t*)(buf + 16), client->exp->size);
        cpu_to_be16w((uint16_t*)(buf + 26), client->exp->nbdflags | myflags);
    } else {
        cpu_to_be64w((uint64_t*)(buf + 8), NBD_OPTS_MAGIC);
        cpu_to_be16w((uint16_t *)(buf + 16), NBD_FLAG_FIXED_NEWSTYLE);
    }

    if (client->exp) {
        if (write_sync(csock, buf, sizeof(buf)) != sizeof(buf)) {
            LOG("write failed");
            goto fail;
        }
    } else {
        if (write_sync(csock, buf, 18) != 18) {
            LOG("write failed");
            goto fail;
        }
        rc = nbd_receive_options(client);
        if (rc != 0) {
            LOG("option negotiation failed");
            goto fail;
        }

        assert ((client->exp->nbdflags & ~65535) == 0);
        cpu_to_be64w((uint64_t*)(buf + 18), client->exp->size);
        cpu_to_be16w((uint16_t*)(buf + 26), client->exp->nbdflags | myflags);
        if (write_sync(csock, buf + 18, sizeof(buf) - 18) != sizeof(buf) - 18) {
            LOG("write failed");
            goto fail;
        }
    }

    TRACE("Negotiation succeeded.");
    rc = 0;
fail:
    qemu_set_nonblock(csock);
    return rc;
}

int nbd_receive_negotiate(int csock, const char *name, uint32_t *flags,
                          off_t *size, size_t *blocksize)
{
    char buf[256];
    uint64_t magic, s;
    uint16_t tmp;
    int rc;

    TRACE("Receiving negotiation.");

    rc = -EINVAL;

    if (read_sync(csock, buf, 8) != 8) {
        LOG("read failed");
        goto fail;
    }

    buf[8] = '\0';
    if (strlen(buf) == 0) {
        LOG("server connection closed");
        goto fail;
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
        goto fail;
    }

    if (read_sync(csock, &magic, sizeof(magic)) != sizeof(magic)) {
        LOG("read failed");
        goto fail;
    }
    magic = be64_to_cpu(magic);
    TRACE("Magic is 0x%" PRIx64, magic);

    if (name) {
        uint32_t reserved = 0;
        uint32_t opt;
        uint32_t namesize;

        TRACE("Checking magic (opts_magic)");
        if (magic != NBD_OPTS_MAGIC) {
            LOG("Bad magic received");
            goto fail;
        }
        if (read_sync(csock, &tmp, sizeof(tmp)) != sizeof(tmp)) {
            LOG("flags read failed");
            goto fail;
        }
        *flags = be16_to_cpu(tmp) << 16;
        /* reserved for future use */
        if (write_sync(csock, &reserved, sizeof(reserved)) !=
            sizeof(reserved)) {
            LOG("write failed (reserved)");
            goto fail;
        }
        /* write the export name */
        magic = cpu_to_be64(magic);
        if (write_sync(csock, &magic, sizeof(magic)) != sizeof(magic)) {
            LOG("write failed (magic)");
            goto fail;
        }
        opt = cpu_to_be32(NBD_OPT_EXPORT_NAME);
        if (write_sync(csock, &opt, sizeof(opt)) != sizeof(opt)) {
            LOG("write failed (opt)");
            goto fail;
        }
        namesize = cpu_to_be32(strlen(name));
        if (write_sync(csock, &namesize, sizeof(namesize)) !=
            sizeof(namesize)) {
            LOG("write failed (namesize)");
            goto fail;
        }
        if (write_sync(csock, (char*)name, strlen(name)) != strlen(name)) {
            LOG("write failed (name)");
            goto fail;
        }
    } else {
        TRACE("Checking magic (cli_magic)");

        if (magic != NBD_CLIENT_MAGIC) {
            LOG("Bad magic received");
            goto fail;
        }
    }

    if (read_sync(csock, &s, sizeof(s)) != sizeof(s)) {
        LOG("read failed");
        goto fail;
    }
    *size = be64_to_cpu(s);
    *blocksize = 1024;
    TRACE("Size is %" PRIu64, *size);

    if (!name) {
        if (read_sync(csock, flags, sizeof(*flags)) != sizeof(*flags)) {
            LOG("read failed (flags)");
            goto fail;
        }
        *flags = be32_to_cpup(flags);
    } else {
        if (read_sync(csock, &tmp, sizeof(tmp)) != sizeof(tmp)) {
            LOG("read failed (tmp)");
            goto fail;
        }
        *flags |= be32_to_cpu(tmp);
    }
    if (read_sync(csock, &buf, 124) != 124) {
        LOG("read failed (buf)");
        goto fail;
    }
    rc = 0;

fail:
    return rc;
}

#ifdef __linux__
int nbd_init(int fd, int csock, uint32_t flags, off_t size, size_t blocksize)
{
    TRACE("Setting NBD socket");

    if (ioctl(fd, NBD_SET_SOCK, csock) < 0) {
        int serrno = errno;
        LOG("Failed to set NBD socket");
        return -serrno;
    }

    TRACE("Setting block size to %lu", (unsigned long)blocksize);

    if (ioctl(fd, NBD_SET_BLKSIZE, blocksize) < 0) {
        int serrno = errno;
        LOG("Failed setting NBD block size");
        return -serrno;
    }

        TRACE("Setting size to %zd block(s)", (size_t)(size / blocksize));

    if (ioctl(fd, NBD_SET_SIZE_BLOCKS, size / blocksize) < 0) {
        int serrno = errno;
        LOG("Failed setting size (in blocks)");
        return -serrno;
    }

    if (ioctl(fd, NBD_SET_FLAGS, flags) < 0) {
        if (errno == ENOTTY) {
            int read_only = (flags & NBD_FLAG_READ_ONLY) != 0;
            TRACE("Setting readonly attribute");

            if (ioctl(fd, BLKROSET, (unsigned long) &read_only) < 0) {
                int serrno = errno;
                LOG("Failed setting read-only attribute");
                return -serrno;
            }
        } else {
            int serrno = errno;
            LOG("Failed setting flags");
            return -serrno;
        }
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
    if (ret < 0 && errno == EPIPE) {
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
    return -ENOTSUP;
}

int nbd_disconnect(int fd)
{
    return -ENOTSUP;
}

int nbd_client(int fd)
{
    return -ENOTSUP;
}
#endif

ssize_t nbd_send_request(int csock, struct nbd_request *request)
{
    uint8_t buf[NBD_REQUEST_SIZE];
    ssize_t ret;

    cpu_to_be32w((uint32_t*)buf, NBD_REQUEST_MAGIC);
    cpu_to_be32w((uint32_t*)(buf + 4), request->type);
    cpu_to_be64w((uint64_t*)(buf + 8), request->handle);
    cpu_to_be64w((uint64_t*)(buf + 16), request->from);
    cpu_to_be32w((uint32_t*)(buf + 24), request->len);

    TRACE("Sending request to client: "
          "{ .from = %" PRIu64", .len = %u, .handle = %" PRIu64", .type=%i}",
          request->from, request->len, request->handle, request->type);

    ret = write_sync(csock, buf, sizeof(buf));
    if (ret < 0) {
        return ret;
    }

    if (ret != sizeof(buf)) {
        LOG("writing to socket failed");
        return -EINVAL;
    }
    return 0;
}

static ssize_t nbd_receive_request(int csock, struct nbd_request *request)
{
    uint8_t buf[NBD_REQUEST_SIZE];
    uint32_t magic;
    ssize_t ret;

    ret = read_sync(csock, buf, sizeof(buf));
    if (ret < 0) {
        return ret;
    }

    if (ret != sizeof(buf)) {
        LOG("read failed");
        return -EINVAL;
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
        return -EINVAL;
    }
    return 0;
}

ssize_t nbd_receive_reply(int csock, struct nbd_reply *reply)
{
    uint8_t buf[NBD_REPLY_SIZE];
    uint32_t magic;
    ssize_t ret;

    ret = read_sync(csock, buf, sizeof(buf));
    if (ret < 0) {
        return ret;
    }

    if (ret != sizeof(buf)) {
        LOG("read failed");
        return -EINVAL;
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
        return -EINVAL;
    }
    return 0;
}

static ssize_t nbd_send_reply(int csock, struct nbd_reply *reply)
{
    uint8_t buf[NBD_REPLY_SIZE];
    ssize_t ret;

    /* Reply
       [ 0 ..  3]    magic   (NBD_REPLY_MAGIC)
       [ 4 ..  7]    error   (0 == no error)
       [ 7 .. 15]    handle
     */
    cpu_to_be32w((uint32_t*)buf, NBD_REPLY_MAGIC);
    cpu_to_be32w((uint32_t*)(buf + 4), reply->error);
    cpu_to_be64w((uint64_t*)(buf + 8), reply->handle);

    TRACE("Sending response to client");

    ret = write_sync(csock, buf, sizeof(buf));
    if (ret < 0) {
        return ret;
    }

    if (ret != sizeof(buf)) {
        LOG("writing to socket failed");
        return -EINVAL;
    }
    return 0;
}

#define MAX_NBD_REQUESTS 16

void nbd_client_get(NBDClient *client)
{
    client->refcount++;
}

void nbd_client_put(NBDClient *client)
{
    if (--client->refcount == 0) {
        /* The last reference should be dropped by client->close,
         * which is called by nbd_client_close.
         */
        assert(client->closing);

        qemu_set_fd_handler2(client->sock, NULL, NULL, NULL, NULL);
        close(client->sock);
        client->sock = -1;
        if (client->exp) {
            QTAILQ_REMOVE(&client->exp->clients, client, next);
            nbd_export_put(client->exp);
        }
        g_free(client);
    }
}

void nbd_client_close(NBDClient *client)
{
    if (client->closing) {
        return;
    }

    client->closing = true;

    /* Force requests to finish.  They will drop their own references,
     * then we'll close the socket and free the NBDClient.
     */
    shutdown(client->sock, 2);

    /* Also tell the client, so that they release their reference.  */
    if (client->close) {
        client->close(client);
    }
}

static NBDRequest *nbd_request_get(NBDClient *client)
{
    NBDRequest *req;

    assert(client->nb_requests <= MAX_NBD_REQUESTS - 1);
    client->nb_requests++;

    req = g_slice_new0(NBDRequest);
    nbd_client_get(client);
    req->client = client;
    return req;
}

static void nbd_request_put(NBDRequest *req)
{
    NBDClient *client = req->client;

    if (req->data) {
        qemu_vfree(req->data);
    }
    g_slice_free(NBDRequest, req);

    if (client->nb_requests-- == MAX_NBD_REQUESTS) {
        qemu_notify_event();
    }
    nbd_client_put(client);
}

NBDExport *nbd_export_new(BlockDriverState *bs, off_t dev_offset,
                          off_t size, uint32_t nbdflags,
                          void (*close)(NBDExport *))
{
    NBDExport *exp = g_malloc0(sizeof(NBDExport));
    exp->refcount = 1;
    QTAILQ_INIT(&exp->clients);
    exp->bs = bs;
    exp->dev_offset = dev_offset;
    exp->nbdflags = nbdflags;
    exp->size = size == -1 ? bdrv_getlength(bs) : size;
    exp->close = close;
    bdrv_ref(bs);
    return exp;
}

NBDExport *nbd_export_find(const char *name)
{
    NBDExport *exp;
    QTAILQ_FOREACH(exp, &exports, next) {
        if (strcmp(name, exp->name) == 0) {
            return exp;
        }
    }

    return NULL;
}

void nbd_export_set_name(NBDExport *exp, const char *name)
{
    if (exp->name == name) {
        return;
    }

    nbd_export_get(exp);
    if (exp->name != NULL) {
        g_free(exp->name);
        exp->name = NULL;
        QTAILQ_REMOVE(&exports, exp, next);
        nbd_export_put(exp);
    }
    if (name != NULL) {
        nbd_export_get(exp);
        exp->name = g_strdup(name);
        QTAILQ_INSERT_TAIL(&exports, exp, next);
    }
    nbd_export_put(exp);
}

void nbd_export_close(NBDExport *exp)
{
    NBDClient *client, *next;

    nbd_export_get(exp);
    QTAILQ_FOREACH_SAFE(client, &exp->clients, next, next) {
        nbd_client_close(client);
    }
    nbd_export_set_name(exp, NULL);
    nbd_export_put(exp);
    if (exp->bs) {
        bdrv_unref(exp->bs);
        exp->bs = NULL;
    }
}

void nbd_export_get(NBDExport *exp)
{
    assert(exp->refcount > 0);
    exp->refcount++;
}

void nbd_export_put(NBDExport *exp)
{
    assert(exp->refcount > 0);
    if (exp->refcount == 1) {
        nbd_export_close(exp);
    }

    if (--exp->refcount == 0) {
        assert(exp->name == NULL);

        if (exp->close) {
            exp->close(exp);
        }

        g_free(exp);
    }
}

BlockDriverState *nbd_export_get_blockdev(NBDExport *exp)
{
    return exp->bs;
}

void nbd_export_close_all(void)
{
    NBDExport *exp, *next;

    QTAILQ_FOREACH_SAFE(exp, &exports, next, next) {
        nbd_export_close(exp);
    }
}

static int nbd_can_read(void *opaque);
static void nbd_read(void *opaque);
static void nbd_restart_write(void *opaque);

static ssize_t nbd_co_send_reply(NBDRequest *req, struct nbd_reply *reply,
                                 int len)
{
    NBDClient *client = req->client;
    int csock = client->sock;
    ssize_t rc, ret;

    qemu_co_mutex_lock(&client->send_lock);
    qemu_set_fd_handler2(csock, nbd_can_read, nbd_read,
                         nbd_restart_write, client);
    client->send_coroutine = qemu_coroutine_self();

    if (!len) {
        rc = nbd_send_reply(csock, reply);
    } else {
        socket_set_cork(csock, 1);
        rc = nbd_send_reply(csock, reply);
        if (rc >= 0) {
            ret = qemu_co_send(csock, req->data, len);
            if (ret != len) {
                rc = -EIO;
            }
        }
        socket_set_cork(csock, 0);
    }

    client->send_coroutine = NULL;
    qemu_set_fd_handler2(csock, nbd_can_read, nbd_read, NULL, client);
    qemu_co_mutex_unlock(&client->send_lock);
    return rc;
}

static ssize_t nbd_co_receive_request(NBDRequest *req, struct nbd_request *request)
{
    NBDClient *client = req->client;
    int csock = client->sock;
    uint32_t command;
    ssize_t rc;

    client->recv_coroutine = qemu_coroutine_self();
    rc = nbd_receive_request(csock, request);
    if (rc < 0) {
        if (rc != -EAGAIN) {
            rc = -EIO;
        }
        goto out;
    }

    if (request->len > NBD_MAX_BUFFER_SIZE) {
        LOG("len (%u) is larger than max len (%u)",
            request->len, NBD_MAX_BUFFER_SIZE);
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

    command = request->type & NBD_CMD_MASK_COMMAND;
    if (command == NBD_CMD_READ || command == NBD_CMD_WRITE) {
        req->data = qemu_blockalign(client->exp->bs, request->len);
    }
    if (command == NBD_CMD_WRITE) {
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
    NBDExport *exp = client->exp;
    NBDRequest *req;
    struct nbd_request request;
    struct nbd_reply reply;
    ssize_t ret;
    uint32_t command;

    TRACE("Reading request.");
    if (client->closing) {
        return;
    }

    req = nbd_request_get(client);
    ret = nbd_co_receive_request(req, &request);
    if (ret == -EAGAIN) {
        goto done;
    }
    if (ret == -EIO) {
        goto out;
    }

    reply.handle = request.handle;
    reply.error = 0;

    if (ret < 0) {
        reply.error = -ret;
        goto error_reply;
    }
    command = request.type & NBD_CMD_MASK_COMMAND;
    if (command != NBD_CMD_DISC && (request.from + request.len) > exp->size) {
            LOG("From: %" PRIu64 ", Len: %u, Size: %" PRIu64
            ", Offset: %" PRIu64 "\n",
                    request.from, request.len,
                    (uint64_t)exp->size, (uint64_t)exp->dev_offset);
        LOG("requested operation past EOF--bad client?");
        goto invalid_request;
    }

    switch (command) {
    case NBD_CMD_READ:
        TRACE("Request type is READ");

        if (request.type & NBD_CMD_FLAG_FUA) {
            ret = bdrv_co_flush(exp->bs);
            if (ret < 0) {
                LOG("flush failed");
                reply.error = -ret;
                goto error_reply;
            }
        }

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

        if (nbd_co_send_reply(req, &reply, 0) < 0) {
            goto out;
        }
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
        if (nbd_co_send_reply(req, &reply, 0) < 0) {
            goto out;
        }
        break;
    case NBD_CMD_TRIM:
        TRACE("Request type is TRIM");
        ret = bdrv_co_discard(exp->bs, (request.from + exp->dev_offset) / 512,
                              request.len / 512);
        if (ret < 0) {
            LOG("discard failed");
            reply.error = -ret;
        }
        if (nbd_co_send_reply(req, &reply, 0) < 0) {
            goto out;
        }
        break;
    default:
        LOG("invalid request type (%u) received", request.type);
    invalid_request:
        reply.error = -EINVAL;
    error_reply:
        if (nbd_co_send_reply(req, &reply, 0) < 0) {
            goto out;
        }
        break;
    }

    TRACE("Request/Reply complete");

done:
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
    client = g_malloc0(sizeof(NBDClient));
    client->refcount = 1;
    client->exp = exp;
    client->sock = csock;
    if (nbd_send_negotiate(client)) {
        g_free(client);
        return NULL;
    }
    client->close = close;
    qemu_co_mutex_init(&client->send_lock);
    qemu_set_fd_handler2(csock, nbd_can_read, nbd_read, NULL, client);

    if (exp) {
        QTAILQ_INSERT_TAIL(&exp->clients, client, next);
        nbd_export_get(exp);
    }
    return client;
}
