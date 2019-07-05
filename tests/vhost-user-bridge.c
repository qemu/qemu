/*
 * Vhost User Bridge
 *
 * Copyright (c) 2015 Red Hat, Inc.
 *
 * Authors:
 *  Victor Kaplansky <victork@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later.  See the COPYING file in the top-level directory.
 */

/*
 * TODO:
 *     - main should get parameters from the command line.
 *     - implement all request handlers. Still not implemented:
 *          vubr_get_queue_num_exec()
 *          vubr_send_rarp_exec()
 *     - test for broken requests and virtqueue.
 *     - implement features defined by Virtio 1.0 spec.
 *     - support mergeable buffers and indirect descriptors.
 *     - implement clean shutdown.
 *     - implement non-blocking writes to UDP backend.
 *     - implement polling strategy.
 *     - implement clean starting/stopping of vq processing
 *     - implement clean starting/stopping of used and buffers
 *       dirty page logging.
 */

#define _FILE_OFFSET_BITS 64

#include "qemu/osdep.h"
#include "qemu/atomic.h"
#include "qemu/ctype.h"
#include "qemu/iov.h"
#include "standard-headers/linux/virtio_net.h"
#include "contrib/libvhost-user/libvhost-user.h"

#define VHOST_USER_BRIDGE_DEBUG 1

#define DPRINT(...) \
    do { \
        if (VHOST_USER_BRIDGE_DEBUG) { \
            printf(__VA_ARGS__); \
        } \
    } while (0)

enum {
    VHOST_USER_BRIDGE_MAX_QUEUES = 8,
};

typedef void (*CallbackFunc)(int sock, void *ctx);

typedef struct Event {
    void *ctx;
    CallbackFunc callback;
} Event;

typedef struct Dispatcher {
    int max_sock;
    fd_set fdset;
    Event events[FD_SETSIZE];
} Dispatcher;

typedef struct VubrDev {
    VuDev vudev;
    Dispatcher dispatcher;
    int backend_udp_sock;
    struct sockaddr_in backend_udp_dest;
    int hdrlen;
    int sock;
    int ready;
    int quit;
    struct {
        int fd;
        void *addr;
        pthread_t thread;
    } notifier;
} VubrDev;

static void
vubr_die(const char *s)
{
    perror(s);
    exit(1);
}

static int
dispatcher_init(Dispatcher *dispr)
{
    FD_ZERO(&dispr->fdset);
    dispr->max_sock = -1;
    return 0;
}

static int
dispatcher_add(Dispatcher *dispr, int sock, void *ctx, CallbackFunc cb)
{
    if (sock >= FD_SETSIZE) {
        fprintf(stderr,
                "Error: Failed to add new event. sock %d should be less than %d\n",
                sock, FD_SETSIZE);
        return -1;
    }

    dispr->events[sock].ctx = ctx;
    dispr->events[sock].callback = cb;

    FD_SET(sock, &dispr->fdset);
    if (sock > dispr->max_sock) {
        dispr->max_sock = sock;
    }
    DPRINT("Added sock %d for watching. max_sock: %d\n",
           sock, dispr->max_sock);
    return 0;
}

static int
dispatcher_remove(Dispatcher *dispr, int sock)
{
    if (sock >= FD_SETSIZE) {
        fprintf(stderr,
                "Error: Failed to remove event. sock %d should be less than %d\n",
                sock, FD_SETSIZE);
        return -1;
    }

    FD_CLR(sock, &dispr->fdset);
    DPRINT("Sock %d removed from dispatcher watch.\n", sock);
    return 0;
}

/* timeout in us */
static int
dispatcher_wait(Dispatcher *dispr, uint32_t timeout)
{
    struct timeval tv;
    tv.tv_sec = timeout / 1000000;
    tv.tv_usec = timeout % 1000000;

    fd_set fdset = dispr->fdset;

    /* wait until some of sockets become readable. */
    int rc = select(dispr->max_sock + 1, &fdset, 0, 0, &tv);

    if (rc == -1) {
        vubr_die("select");
    }

    /* Timeout */
    if (rc == 0) {
        return 0;
    }

    /* Now call callback for every ready socket. */

    int sock;
    for (sock = 0; sock < dispr->max_sock + 1; sock++) {
        /* The callback on a socket can remove other sockets from the
         * dispatcher, thus we have to check that the socket is
         * still not removed from dispatcher's list
         */
        if (FD_ISSET(sock, &fdset) && FD_ISSET(sock, &dispr->fdset)) {
            Event *e = &dispr->events[sock];
            e->callback(sock, e->ctx);
        }
    }

    return 0;
}

static void
vubr_handle_tx(VuDev *dev, int qidx)
{
    VuVirtq *vq = vu_get_queue(dev, qidx);
    VubrDev *vubr = container_of(dev, VubrDev, vudev);
    int hdrlen = vubr->hdrlen;
    VuVirtqElement *elem = NULL;

    assert(qidx % 2);

    for (;;) {
        ssize_t ret;
        unsigned int out_num;
        struct iovec sg[VIRTQUEUE_MAX_SIZE], *out_sg;

        elem = vu_queue_pop(dev, vq, sizeof(VuVirtqElement));
        if (!elem) {
            break;
        }

        out_num = elem->out_num;
        out_sg = elem->out_sg;
        if (out_num < 1) {
            fprintf(stderr, "virtio-net header not in first element\n");
            break;
        }
        if (VHOST_USER_BRIDGE_DEBUG) {
            iov_hexdump(out_sg, out_num, stderr, "TX:", 1024);
        }

        if (hdrlen) {
            unsigned sg_num = iov_copy(sg, ARRAY_SIZE(sg),
                                       out_sg, out_num,
                                       hdrlen, -1);
            out_num = sg_num;
            out_sg = sg;
        }

        struct msghdr msg = {
            .msg_name = (struct sockaddr *) &vubr->backend_udp_dest,
            .msg_namelen = sizeof(struct sockaddr_in),
            .msg_iov = out_sg,
            .msg_iovlen = out_num,
        };
        do {
            ret = sendmsg(vubr->backend_udp_sock, &msg, 0);
        } while (ret == -1 && (errno == EAGAIN || errno == EINTR));

        if (ret == -1) {
            vubr_die("sendmsg()");
        }

        vu_queue_push(dev, vq, elem, 0);
        vu_queue_notify(dev, vq);

        free(elem);
        elem = NULL;
    }

    free(elem);
}


/* this function reverse the effect of iov_discard_front() it must be
 * called with 'front' being the original struct iovec and 'bytes'
 * being the number of bytes you shaved off
 */
static void
iov_restore_front(struct iovec *front, struct iovec *iov, size_t bytes)
{
    struct iovec *cur;

    for (cur = front; cur != iov; cur++) {
        assert(bytes >= cur->iov_len);
        bytes -= cur->iov_len;
    }

    cur->iov_base -= bytes;
    cur->iov_len += bytes;
}

static void
iov_truncate(struct iovec *iov, unsigned iovc, size_t bytes)
{
    unsigned i;

    for (i = 0; i < iovc; i++, iov++) {
        if (bytes < iov->iov_len) {
            iov->iov_len = bytes;
            return;
        }

        bytes -= iov->iov_len;
    }

    assert(!"couldn't truncate iov");
}

static void
vubr_backend_recv_cb(int sock, void *ctx)
{
    VubrDev *vubr = (VubrDev *) ctx;
    VuDev *dev = &vubr->vudev;
    VuVirtq *vq = vu_get_queue(dev, 0);
    VuVirtqElement *elem = NULL;
    struct iovec mhdr_sg[VIRTQUEUE_MAX_SIZE];
    struct virtio_net_hdr_mrg_rxbuf mhdr;
    unsigned mhdr_cnt = 0;
    int hdrlen = vubr->hdrlen;
    int i = 0;
    struct virtio_net_hdr hdr = {
        .flags = 0,
        .gso_type = VIRTIO_NET_HDR_GSO_NONE
    };

    DPRINT("\n\n   ***   IN UDP RECEIVE CALLBACK    ***\n\n");
    DPRINT("    hdrlen = %d\n", hdrlen);

    if (!vu_queue_enabled(dev, vq) ||
        !vu_queue_started(dev, vq) ||
        !vu_queue_avail_bytes(dev, vq, hdrlen, 0)) {
        DPRINT("Got UDP packet, but no available descriptors on RX virtq.\n");
        return;
    }

    while (1) {
        struct iovec *sg;
        ssize_t ret, total = 0;
        unsigned int num;

        elem = vu_queue_pop(dev, vq, sizeof(VuVirtqElement));
        if (!elem) {
            break;
        }

        if (elem->in_num < 1) {
            fprintf(stderr, "virtio-net contains no in buffers\n");
            break;
        }

        sg = elem->in_sg;
        num = elem->in_num;
        if (i == 0) {
            if (hdrlen == 12) {
                mhdr_cnt = iov_copy(mhdr_sg, ARRAY_SIZE(mhdr_sg),
                                    sg, elem->in_num,
                                    offsetof(typeof(mhdr), num_buffers),
                                    sizeof(mhdr.num_buffers));
            }
            iov_from_buf(sg, elem->in_num, 0, &hdr, sizeof hdr);
            total += hdrlen;
            ret = iov_discard_front(&sg, &num, hdrlen);
            assert(ret == hdrlen);
        }

        struct msghdr msg = {
            .msg_name = (struct sockaddr *) &vubr->backend_udp_dest,
            .msg_namelen = sizeof(struct sockaddr_in),
            .msg_iov = sg,
            .msg_iovlen = num,
            .msg_flags = MSG_DONTWAIT,
        };
        do {
            ret = recvmsg(vubr->backend_udp_sock, &msg, 0);
        } while (ret == -1 && (errno == EINTR));

        if (i == 0) {
            iov_restore_front(elem->in_sg, sg, hdrlen);
        }

        if (ret == -1) {
            if (errno == EWOULDBLOCK) {
                vu_queue_rewind(dev, vq, 1);
                break;
            }

            vubr_die("recvmsg()");
        }

        total += ret;
        iov_truncate(elem->in_sg, elem->in_num, total);
        vu_queue_fill(dev, vq, elem, total, i++);

        free(elem);
        elem = NULL;

        break;        /* could loop if DONTWAIT worked? */
    }

    if (mhdr_cnt) {
        mhdr.num_buffers = i;
        iov_from_buf(mhdr_sg, mhdr_cnt,
                     0,
                     &mhdr.num_buffers, sizeof mhdr.num_buffers);
    }

    vu_queue_flush(dev, vq, i);
    vu_queue_notify(dev, vq);

    free(elem);
}

static void
vubr_receive_cb(int sock, void *ctx)
{
    VubrDev *vubr = (VubrDev *)ctx;

    if (!vu_dispatch(&vubr->vudev)) {
        fprintf(stderr, "Error while dispatching\n");
    }
}

typedef struct WatchData {
    VuDev *dev;
    vu_watch_cb cb;
    void *data;
} WatchData;

static void
watch_cb(int sock, void *ctx)
{
    struct WatchData *wd = ctx;

    wd->cb(wd->dev, VU_WATCH_IN, wd->data);
}

static void
vubr_set_watch(VuDev *dev, int fd, int condition,
               vu_watch_cb cb, void *data)
{
    VubrDev *vubr = container_of(dev, VubrDev, vudev);
    static WatchData watches[FD_SETSIZE];
    struct WatchData *wd = &watches[fd];

    wd->cb = cb;
    wd->data = data;
    wd->dev = dev;
    dispatcher_add(&vubr->dispatcher, fd, wd, watch_cb);
}

static void
vubr_remove_watch(VuDev *dev, int fd)
{
    VubrDev *vubr = container_of(dev, VubrDev, vudev);

    dispatcher_remove(&vubr->dispatcher, fd);
}

static int
vubr_send_rarp_exec(VuDev *dev, VhostUserMsg *vmsg)
{
    DPRINT("Function %s() not implemented yet.\n", __func__);
    return 0;
}

static int
vubr_process_msg(VuDev *dev, VhostUserMsg *vmsg, int *do_reply)
{
    switch (vmsg->request) {
    case VHOST_USER_SEND_RARP:
        *do_reply = vubr_send_rarp_exec(dev, vmsg);
        return 1;
    default:
        /* let the library handle the rest */
        return 0;
    }

    return 0;
}

static void
vubr_set_features(VuDev *dev, uint64_t features)
{
    VubrDev *vubr = container_of(dev, VubrDev, vudev);

    if ((features & (1ULL << VIRTIO_F_VERSION_1)) ||
        (features & (1ULL << VIRTIO_NET_F_MRG_RXBUF))) {
        vubr->hdrlen = 12;
    } else {
        vubr->hdrlen = 10;
    }
}

static uint64_t
vubr_get_features(VuDev *dev)
{
    return 1ULL << VIRTIO_NET_F_GUEST_ANNOUNCE |
        1ULL << VIRTIO_NET_F_MRG_RXBUF |
        1ULL << VIRTIO_F_VERSION_1;
}

static void
vubr_queue_set_started(VuDev *dev, int qidx, bool started)
{
    VubrDev *vubr = container_of(dev, VubrDev, vudev);
    VuVirtq *vq = vu_get_queue(dev, qidx);

    if (started && vubr->notifier.fd >= 0) {
        vu_set_queue_host_notifier(dev, vq, vubr->notifier.fd,
                                   getpagesize(),
                                   qidx * getpagesize());
    }

    if (qidx % 2 == 1) {
        vu_set_queue_handler(dev, vq, started ? vubr_handle_tx : NULL);
    }
}

static void
vubr_panic(VuDev *dev, const char *msg)
{
    VubrDev *vubr = container_of(dev, VubrDev, vudev);

    fprintf(stderr, "PANIC: %s\n", msg);

    dispatcher_remove(&vubr->dispatcher, dev->sock);
    vubr->quit = 1;
}

static bool
vubr_queue_is_processed_in_order(VuDev *dev, int qidx)
{
    return true;
}

static const VuDevIface vuiface = {
    .get_features = vubr_get_features,
    .set_features = vubr_set_features,
    .process_msg = vubr_process_msg,
    .queue_set_started = vubr_queue_set_started,
    .queue_is_processed_in_order = vubr_queue_is_processed_in_order,
};

static void
vubr_accept_cb(int sock, void *ctx)
{
    VubrDev *dev = (VubrDev *)ctx;
    int conn_fd;
    struct sockaddr_un un;
    socklen_t len = sizeof(un);

    conn_fd = accept(sock, (struct sockaddr *) &un, &len);
    if (conn_fd == -1) {
        vubr_die("accept()");
    }
    DPRINT("Got connection from remote peer on sock %d\n", conn_fd);

    if (!vu_init(&dev->vudev,
                 VHOST_USER_BRIDGE_MAX_QUEUES,
                 conn_fd,
                 vubr_panic,
                 vubr_set_watch,
                 vubr_remove_watch,
                 &vuiface)) {
        fprintf(stderr, "Failed to initialize libvhost-user\n");
        exit(1);
    }

    dispatcher_add(&dev->dispatcher, conn_fd, ctx, vubr_receive_cb);
    dispatcher_remove(&dev->dispatcher, sock);
}

static VubrDev *
vubr_new(const char *path, bool client)
{
    VubrDev *dev = (VubrDev *) calloc(1, sizeof(VubrDev));
    struct sockaddr_un un;
    CallbackFunc cb;
    size_t len;

    /* Get a UNIX socket. */
    dev->sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (dev->sock == -1) {
        vubr_die("socket");
    }

    dev->notifier.fd = -1;

    un.sun_family = AF_UNIX;
    strcpy(un.sun_path, path);
    len = sizeof(un.sun_family) + strlen(path);

    if (!client) {
        unlink(path);

        if (bind(dev->sock, (struct sockaddr *) &un, len) == -1) {
            vubr_die("bind");
        }

        if (listen(dev->sock, 1) == -1) {
            vubr_die("listen");
        }
        cb = vubr_accept_cb;

        DPRINT("Waiting for connections on UNIX socket %s ...\n", path);
    } else {
        if (connect(dev->sock, (struct sockaddr *)&un, len) == -1) {
            vubr_die("connect");
        }

        if (!vu_init(&dev->vudev,
                     VHOST_USER_BRIDGE_MAX_QUEUES,
                     dev->sock,
                     vubr_panic,
                     vubr_set_watch,
                     vubr_remove_watch,
                     &vuiface)) {
            fprintf(stderr, "Failed to initialize libvhost-user\n");
            exit(1);
        }

        cb = vubr_receive_cb;
    }

    dispatcher_init(&dev->dispatcher);

    dispatcher_add(&dev->dispatcher, dev->sock, (void *)dev, cb);

    return dev;
}

static void *notifier_thread(void *arg)
{
    VuDev *dev = (VuDev *)arg;
    VubrDev *vubr = container_of(dev, VubrDev, vudev);
    int pagesize = getpagesize();
    int qidx;

    while (true) {
        for (qidx = 0; qidx < VHOST_USER_BRIDGE_MAX_QUEUES; qidx++) {
            uint16_t *n = vubr->notifier.addr + pagesize * qidx;

            if (*n == qidx) {
                *n = 0xffff;
                /* We won't miss notifications if we reset
                 * the memory first. */
                smp_mb();

                DPRINT("Got a notification for queue%d via host notifier.\n",
                       qidx);

                if (qidx % 2 == 1) {
                    vubr_handle_tx(dev, qidx);
                }
            }
            usleep(1000);
        }
    }

    return NULL;
}

static void
vubr_host_notifier_setup(VubrDev *dev)
{
    char template[] = "/tmp/vubr-XXXXXX";
    pthread_t thread;
    size_t length;
    void *addr;
    int fd;

    length = getpagesize() * VHOST_USER_BRIDGE_MAX_QUEUES;

    fd = mkstemp(template);
    if (fd < 0) {
        vubr_die("mkstemp()");
    }

    if (posix_fallocate(fd, 0, length) != 0) {
        vubr_die("posix_fallocate()");
    }

    addr = mmap(NULL, length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (addr == MAP_FAILED) {
        vubr_die("mmap()");
    }

    memset(addr, 0xff, length);

    if (pthread_create(&thread, NULL, notifier_thread, &dev->vudev) != 0) {
        vubr_die("pthread_create()");
    }

    dev->notifier.fd = fd;
    dev->notifier.addr = addr;
    dev->notifier.thread = thread;
}

static void
vubr_set_host(struct sockaddr_in *saddr, const char *host)
{
    if (qemu_isdigit(host[0])) {
        if (!inet_aton(host, &saddr->sin_addr)) {
            fprintf(stderr, "inet_aton() failed.\n");
            exit(1);
        }
    } else {
        struct hostent *he = gethostbyname(host);

        if (!he) {
            fprintf(stderr, "gethostbyname() failed.\n");
            exit(1);
        }
        saddr->sin_addr = *(struct in_addr *)he->h_addr;
    }
}

static void
vubr_backend_udp_setup(VubrDev *dev,
                       const char *local_host,
                       const char *local_port,
                       const char *remote_host,
                       const char *remote_port)
{
    int sock;
    const char *r;

    int lport, rport;

    lport = strtol(local_port, (char **)&r, 0);
    if (r == local_port) {
        fprintf(stderr, "lport parsing failed.\n");
        exit(1);
    }

    rport = strtol(remote_port, (char **)&r, 0);
    if (r == remote_port) {
        fprintf(stderr, "rport parsing failed.\n");
        exit(1);
    }

    struct sockaddr_in si_local = {
        .sin_family = AF_INET,
        .sin_port = htons(lport),
    };

    vubr_set_host(&si_local, local_host);

    /* setup destination for sends */
    dev->backend_udp_dest = (struct sockaddr_in) {
        .sin_family = AF_INET,
        .sin_port = htons(rport),
    };
    vubr_set_host(&dev->backend_udp_dest, remote_host);

    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == -1) {
        vubr_die("socket");
    }

    if (bind(sock, (struct sockaddr *)&si_local, sizeof(si_local)) == -1) {
        vubr_die("bind");
    }

    dev->backend_udp_sock = sock;
    dispatcher_add(&dev->dispatcher, sock, dev, vubr_backend_recv_cb);
    DPRINT("Waiting for data from udp backend on %s:%d...\n",
           local_host, lport);
}

static void
vubr_run(VubrDev *dev)
{
    while (!dev->quit) {
        /* timeout 200ms */
        dispatcher_wait(&dev->dispatcher, 200000);
        /* Here one can try polling strategy. */
    }
}

static int
vubr_parse_host_port(const char **host, const char **port, const char *buf)
{
    char *p = strchr(buf, ':');

    if (!p) {
        return -1;
    }
    *p = '\0';
    *host = strdup(buf);
    *port = strdup(p + 1);
    return 0;
}

#define DEFAULT_UD_SOCKET "/tmp/vubr.sock"
#define DEFAULT_LHOST "127.0.0.1"
#define DEFAULT_LPORT "4444"
#define DEFAULT_RHOST "127.0.0.1"
#define DEFAULT_RPORT "5555"

static const char *ud_socket_path = DEFAULT_UD_SOCKET;
static const char *lhost = DEFAULT_LHOST;
static const char *lport = DEFAULT_LPORT;
static const char *rhost = DEFAULT_RHOST;
static const char *rport = DEFAULT_RPORT;

int
main(int argc, char *argv[])
{
    VubrDev *dev;
    int opt;
    bool client = false;
    bool host_notifier = false;

    while ((opt = getopt(argc, argv, "l:r:u:cH")) != -1) {

        switch (opt) {
        case 'l':
            if (vubr_parse_host_port(&lhost, &lport, optarg) < 0) {
                goto out;
            }
            break;
        case 'r':
            if (vubr_parse_host_port(&rhost, &rport, optarg) < 0) {
                goto out;
            }
            break;
        case 'u':
            ud_socket_path = strdup(optarg);
            break;
        case 'c':
            client = true;
            break;
        case 'H':
            host_notifier = true;
            break;
        default:
            goto out;
        }
    }

    DPRINT("ud socket: %s (%s)\n", ud_socket_path,
           client ? "client" : "server");
    DPRINT("local:     %s:%s\n", lhost, lport);
    DPRINT("remote:    %s:%s\n", rhost, rport);

    dev = vubr_new(ud_socket_path, client);
    if (!dev) {
        return 1;
    }

    if (host_notifier) {
        vubr_host_notifier_setup(dev);
    }

    vubr_backend_udp_setup(dev, lhost, lport, rhost, rport);
    vubr_run(dev);

    vu_deinit(&dev->vudev);

    return 0;

out:
    fprintf(stderr, "Usage: %s ", argv[0]);
    fprintf(stderr, "[-c] [-H] [-u ud_socket_path] [-l lhost:lport] [-r rhost:rport]\n");
    fprintf(stderr, "\t-u path to unix doman socket. default: %s\n",
            DEFAULT_UD_SOCKET);
    fprintf(stderr, "\t-l local host and port. default: %s:%s\n",
            DEFAULT_LHOST, DEFAULT_LPORT);
    fprintf(stderr, "\t-r remote host and port. default: %s:%s\n",
            DEFAULT_RHOST, DEFAULT_RPORT);
    fprintf(stderr, "\t-c client mode\n");
    fprintf(stderr, "\t-H use host notifier\n");

    return 1;
}
