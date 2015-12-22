/*
 * COarse-grain LOck-stepping Virtual Machines for Non-stop Service (COLO)
 * (a.k.a. Fault Tolerance or Continuous Replication)
 *
 * Copyright (c) 2015 HUAWEI TECHNOLOGIES CO., LTD.
 * Copyright (c) 2015 FUJITSU LIMITED
 * Copyright (c) 2015 Intel Corporation
 *
 * Author: Zhang Chen <zhangchen.fnst@cn.fujitsu.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later.  See the COPYING file in the top-level directory.
 */

#include "net/filter.h"
#include "net/queue.h"
#include "qemu-common.h"
#include "qemu/iov.h"
#include "qapi/qmp/qerror.h"
#include "qapi-visit.h"
#include "qom/object.h"
#include "qemu/sockets.h"
#include "qemu/main-loop.h"
#include "qemu/jhash.h"
#include "qemu/coroutine.h"
#include "net/eth.h"
#include "slirp/slirp.h"
#include "slirp/slirp_config.h"
#include "slirp/ip.h"
#include "net/net.h"
#include "qemu/error-report.h"
#include "net/colo-proxy.h"
#include "trace.h"
#include <sys/sysinfo.h>

#define FILTER_COLO_PROXY(obj) \
    OBJECT_CHECK(COLOProxyState, (obj), TYPE_FILTER_COLO_PROXY)

#define TYPE_FILTER_COLO_PROXY "colo-proxy"
#define PRIMARY_MODE "primary"
#define SECONDARY_MODE "secondary"

/*

  |COLOProxyState++
  |               |
  +---------------+   +---------------+         +---------------+
  |conn list      +--->conn           +--------->conn           |
  +---------------+   +---------------+         +---------------+
  |               |     |           |             |           |
  +---------------+ +---v----+  +---v----+    +---v----+  +---v----+
                    |primary |  |secondary    |primary |  |secondary
                    |packet  |  |packet  +    |packet  |  |packet  +
                    +--------+  +--------+    +--------+  +--------+
                        |           |             |           |
                    +---v----+  +---v----+    +---v----+  +---v----+
                    |primary |  |secondary    |primary |  |secondary
                    |packet  |  |packet  +    |packet  |  |packet  +
                    +--------+  +--------+    +--------+  +--------+
                        |           |             |           |
                    +---v----+  +---v----+    +---v----+  +---v----+
                    |primary |  |secondary    |primary |  |secondary
                    |packet  |  |packet  +    |packet  |  |packet  +
                    +--------+  +--------+    +--------+  +--------+


*/

typedef struct COLOProxyState {
    NetFilterState parent_obj;
    NetQueue *incoming_queue;/* guest normal net queue */
    NetFilterDirection direction; /* packet direction */
    /* colo mode (primary or secondary) */
    int colo_mode;
    /* primary colo connect address(192.168.0.100:12345)
     * or secondary listening address(:12345)
     */
    char *addr;
    int sockfd;

     /* connection list: the packet belonged to this NIC
     * could be found in this list.
     * element type: Connection
     */
    GQueue conn_list;
    int status; /* proxy is running or not */
    ssize_t hashtable_size; /* proxy current hash size */
    QemuEvent need_compare_ev;  /* notify compare thread */
    QemuThread thread; /* compare thread, a thread for each NIC */

} COLOProxyState;

enum {
    COLO_PROXY_NONE,     /* colo proxy is not started */
    COLO_PROXY_RUNNING,  /* colo proxy is running */
    COLO_PROXY_DONE,     /* colo proxyis done(failover) */
};

/* save all the connections of a vm instance in this table */
GHashTable *colo_conn_hash;
static bool colo_do_checkpoint;
static ssize_t hashtable_max_size;

static ssize_t colo_proxy_receive_iov(NetFilterState *nf,
                                         NetClientState *sender,
                                         unsigned flags,
                                         const struct iovec *iov,
                                         int iovcnt,
                                         NetPacketSent *sent_cb)
{
    /*
     * We return size when buffer a packet, the sender will take it as
     * a already sent packet, so sent_cb should not be called later.
     *
     */
    COLOProxyState *s = FILTER_COLO_PROXY(nf);
    ssize_t ret = 0;

    if (s->status != COLO_PROXY_RUNNING) {
        /* proxy is not started or failovered */
        return 0;
    }

    if (s->colo_mode == COLO_MODE_PRIMARY) {
        /* colo_proxy_primary_handler */
    } else {
        /* colo_proxy_secondary_handler */
    }
    return iov_size(iov, iovcnt);
}

static void colo_proxy_cleanup(NetFilterState *nf)
{
    COLOProxyState *s = FILTER_COLO_PROXY(nf);
    close(s->sockfd);
    s->sockfd = -1;
    qemu_event_destroy(&s->need_compare_ev);
}

static void colo_proxy_setup(NetFilterState *nf, Error **errp)
{
    COLOProxyState *s = FILTER_COLO_PROXY(nf);

    if (!s->addr) {
        error_setg(errp, "filter colo_proxy needs 'addr' property set!");
        return;
    }

    if (nf->direction != NET_FILTER_DIRECTION_ALL) {
        error_setg(errp, "colo need queue all packet,"
                        "please startup colo-proxy with queue=all\n");
        return;
    }

    s->sockfd = -1;
    s->hashtable_size = 0;
    colo_do_checkpoint = false;
    qemu_event_init(&s->need_compare_ev, false);

    s->incoming_queue = qemu_new_net_queue(qemu_netfilter_pass_to_next, nf);
    colo_conn_hash = g_hash_table_new_full(connection_key_hash,
                                           connection_key_equal,
                                           g_free,
                                           connection_destroy);
    g_queue_init(&s->conn_list);
}

static void colo_proxy_class_init(ObjectClass *oc, void *data)
{
    NetFilterClass *nfc = NETFILTER_CLASS(oc);

    nfc->setup = colo_proxy_setup;
    nfc->cleanup = colo_proxy_cleanup;
    nfc->receive_iov = colo_proxy_receive_iov;
}

static int colo_proxy_get_mode(Object *obj, Error **errp)
{
    COLOProxyState *s = FILTER_COLO_PROXY(obj);

    return s->colo_mode;
}

static void
colo_proxy_set_mode(Object *obj, int mode, Error **errp)
{
    COLOProxyState *s = FILTER_COLO_PROXY(obj);

    s->colo_mode = mode;
}

static char *colo_proxy_get_addr(Object *obj, Error **errp)
{
    COLOProxyState *s = FILTER_COLO_PROXY(obj);

    return g_strdup(s->addr);
}

static void
colo_proxy_set_addr(Object *obj, const char *value, Error **errp)
{
    COLOProxyState *s = FILTER_COLO_PROXY(obj);
    g_free(s->addr);
    s->addr = g_strdup(value);
    if (!s->addr) {
        error_setg(errp, "colo_proxy needs 'addr'"
                     "property set!");
        return;
    }
}

static void colo_proxy_init(Object *obj)
{
    object_property_add_enum(obj, "mode", "COLOMode", COLOMode_lookup,
                             colo_proxy_get_mode, colo_proxy_set_mode, NULL);
    object_property_add_str(obj, "addr", colo_proxy_get_addr,
                            colo_proxy_set_addr, NULL);
}

static void colo_proxy_fini(Object *obj)
{
    COLOProxyState *s = FILTER_COLO_PROXY(obj);
    g_free(s->addr);
}

static const TypeInfo colo_proxy_info = {
    .name = TYPE_FILTER_COLO_PROXY,
    .parent = TYPE_NETFILTER,
    .class_init = colo_proxy_class_init,
    .instance_init = colo_proxy_init,
    .instance_finalize = colo_proxy_fini,
    .instance_size = sizeof(COLOProxyState),
};

static void register_types(void)
{
    type_register_static(&colo_proxy_info);
}

type_init(register_types);
