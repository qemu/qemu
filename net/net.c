/*
 * QEMU System Emulator
 *
 * Copyright (c) 2003-2008 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"

#include "net/net.h"
#include "clients.h"
#include "hub.h"
#include "net/slirp.h"
#include "net/eth.h"
#include "util.h"

#include "monitor/monitor.h"
#include "qemu/help_option.h"
#include "qapi/qapi-commands-net.h"
#include "qapi/qapi-visit-net.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qmp/qerror.h"
#include "qemu/error-report.h"
#include "qemu/sockets.h"
#include "qemu/cutils.h"
#include "qemu/config-file.h"
#include "hw/qdev.h"
#include "qemu/iov.h"
#include "qemu/main-loop.h"
#include "qemu/option.h"
#include "qapi/error.h"
#include "qapi/opts-visitor.h"
#include "sysemu/sysemu.h"
#include "sysemu/qtest.h"
#include "net/filter.h"
#include "qapi/string-output-visitor.h"

/* Net bridge is currently not supported for W32. */
#if !defined(_WIN32)
# define CONFIG_NET_BRIDGE
#endif

static VMChangeStateEntry *net_change_state_entry;
static QTAILQ_HEAD(, NetClientState) net_clients;

/***********************************************************/
/* network device redirectors */

static int get_str_sep(char *buf, int buf_size, const char **pp, int sep)
{
    const char *p, *p1;
    int len;
    p = *pp;
    p1 = strchr(p, sep);
    if (!p1)
        return -1;
    len = p1 - p;
    p1++;
    if (buf_size > 0) {
        if (len > buf_size - 1)
            len = buf_size - 1;
        memcpy(buf, p, len);
        buf[len] = '\0';
    }
    *pp = p1;
    return 0;
}

int parse_host_port(struct sockaddr_in *saddr, const char *str,
                    Error **errp)
{
    char buf[512];
    struct hostent *he;
    const char *p, *r;
    int port;

    p = str;
    if (get_str_sep(buf, sizeof(buf), &p, ':') < 0) {
        error_setg(errp, "host address '%s' doesn't contain ':' "
                   "separating host from port", str);
        return -1;
    }
    saddr->sin_family = AF_INET;
    if (buf[0] == '\0') {
        saddr->sin_addr.s_addr = 0;
    } else {
        if (qemu_isdigit(buf[0])) {
            if (!inet_aton(buf, &saddr->sin_addr)) {
                error_setg(errp, "host address '%s' is not a valid "
                           "IPv4 address", buf);
                return -1;
            }
        } else {
            he = gethostbyname(buf);
            if (he == NULL) {
                error_setg(errp, "can't resolve host address '%s'", buf);
                return - 1;
            }
            saddr->sin_addr = *(struct in_addr *)he->h_addr;
        }
    }
    port = strtol(p, (char **)&r, 0);
    if (r == p) {
        error_setg(errp, "port number '%s' is invalid", p);
        return -1;
    }
    saddr->sin_port = htons(port);
    return 0;
}

char *qemu_mac_strdup_printf(const uint8_t *macaddr)
{
    return g_strdup_printf("%.2x:%.2x:%.2x:%.2x:%.2x:%.2x",
                           macaddr[0], macaddr[1], macaddr[2],
                           macaddr[3], macaddr[4], macaddr[5]);
}

void qemu_format_nic_info_str(NetClientState *nc, uint8_t macaddr[6])
{
    snprintf(nc->info_str, sizeof(nc->info_str),
             "model=%s,macaddr=%02x:%02x:%02x:%02x:%02x:%02x",
             nc->model,
             macaddr[0], macaddr[1], macaddr[2],
             macaddr[3], macaddr[4], macaddr[5]);
}

static int mac_table[256] = {0};

static void qemu_macaddr_set_used(MACAddr *macaddr)
{
    int index;

    for (index = 0x56; index < 0xFF; index++) {
        if (macaddr->a[5] == index) {
            mac_table[index]++;
        }
    }
}

static void qemu_macaddr_set_free(MACAddr *macaddr)
{
    int index;
    static const MACAddr base = { .a = { 0x52, 0x54, 0x00, 0x12, 0x34, 0 } };

    if (memcmp(macaddr->a, &base.a, (sizeof(base.a) - 1)) != 0) {
        return;
    }
    for (index = 0x56; index < 0xFF; index++) {
        if (macaddr->a[5] == index) {
            mac_table[index]--;
        }
    }
}

static int qemu_macaddr_get_free(void)
{
    int index;

    for (index = 0x56; index < 0xFF; index++) {
        if (mac_table[index] == 0) {
            return index;
        }
    }

    return -1;
}

void qemu_macaddr_default_if_unset(MACAddr *macaddr)
{
    static const MACAddr zero = { .a = { 0,0,0,0,0,0 } };
    static const MACAddr base = { .a = { 0x52, 0x54, 0x00, 0x12, 0x34, 0 } };

    if (memcmp(macaddr, &zero, sizeof(zero)) != 0) {
        if (memcmp(macaddr->a, &base.a, (sizeof(base.a) - 1)) != 0) {
            return;
        } else {
            qemu_macaddr_set_used(macaddr);
            return;
        }
    }

    macaddr->a[0] = 0x52;
    macaddr->a[1] = 0x54;
    macaddr->a[2] = 0x00;
    macaddr->a[3] = 0x12;
    macaddr->a[4] = 0x34;
    macaddr->a[5] = qemu_macaddr_get_free();
    qemu_macaddr_set_used(macaddr);
}

/**
 * Generate a name for net client
 *
 * Only net clients created with the legacy -net option and NICs need this.
 */
static char *assign_name(NetClientState *nc1, const char *model)
{
    NetClientState *nc;
    int id = 0;

    QTAILQ_FOREACH(nc, &net_clients, next) {
        if (nc == nc1) {
            continue;
        }
        if (strcmp(nc->model, model) == 0) {
            id++;
        }
    }

    return g_strdup_printf("%s.%d", model, id);
}

static void qemu_net_client_destructor(NetClientState *nc)
{
    g_free(nc);
}
static ssize_t qemu_deliver_packet_iov(NetClientState *sender,
                                       unsigned flags,
                                       const struct iovec *iov,
                                       int iovcnt,
                                       void *opaque);

static void qemu_net_client_setup(NetClientState *nc,
                                  NetClientInfo *info,
                                  NetClientState *peer,
                                  const char *model,
                                  const char *name,
                                  NetClientDestructor *destructor)
{
    nc->info = info;
    nc->model = g_strdup(model);
    if (name) {
        nc->name = g_strdup(name);
    } else {
        nc->name = assign_name(nc, model);
    }

    if (peer) {
        assert(!peer->peer);
        nc->peer = peer;
        peer->peer = nc;
    }
    QTAILQ_INSERT_TAIL(&net_clients, nc, next);

    nc->incoming_queue = qemu_new_net_queue(qemu_deliver_packet_iov, nc);
    nc->destructor = destructor;
    QTAILQ_INIT(&nc->filters);
}

NetClientState *qemu_new_net_client(NetClientInfo *info,
                                    NetClientState *peer,
                                    const char *model,
                                    const char *name)
{
    NetClientState *nc;

    assert(info->size >= sizeof(NetClientState));

    nc = g_malloc0(info->size);
    qemu_net_client_setup(nc, info, peer, model, name,
                          qemu_net_client_destructor);

    return nc;
}

NICState *qemu_new_nic(NetClientInfo *info,
                       NICConf *conf,
                       const char *model,
                       const char *name,
                       void *opaque)
{
    NetClientState **peers = conf->peers.ncs;
    NICState *nic;
    int i, queues = MAX(1, conf->peers.queues);

    assert(info->type == NET_CLIENT_DRIVER_NIC);
    assert(info->size >= sizeof(NICState));

    nic = g_malloc0(info->size + sizeof(NetClientState) * queues);
    nic->ncs = (void *)nic + info->size;
    nic->conf = conf;
    nic->opaque = opaque;

    for (i = 0; i < queues; i++) {
        qemu_net_client_setup(&nic->ncs[i], info, peers[i], model, name,
                              NULL);
        nic->ncs[i].queue_index = i;
    }

    return nic;
}

NetClientState *qemu_get_subqueue(NICState *nic, int queue_index)
{
    return nic->ncs + queue_index;
}

NetClientState *qemu_get_queue(NICState *nic)
{
    return qemu_get_subqueue(nic, 0);
}

NICState *qemu_get_nic(NetClientState *nc)
{
    NetClientState *nc0 = nc - nc->queue_index;

    return (NICState *)((void *)nc0 - nc->info->size);
}

void *qemu_get_nic_opaque(NetClientState *nc)
{
    NICState *nic = qemu_get_nic(nc);

    return nic->opaque;
}

static void qemu_cleanup_net_client(NetClientState *nc)
{
    QTAILQ_REMOVE(&net_clients, nc, next);

    if (nc->info->cleanup) {
        nc->info->cleanup(nc);
    }
}

static void qemu_free_net_client(NetClientState *nc)
{
    if (nc->incoming_queue) {
        qemu_del_net_queue(nc->incoming_queue);
    }
    if (nc->peer) {
        nc->peer->peer = NULL;
    }
    g_free(nc->name);
    g_free(nc->model);
    if (nc->destructor) {
        nc->destructor(nc);
    }
}

void qemu_del_net_client(NetClientState *nc)
{
    NetClientState *ncs[MAX_QUEUE_NUM];
    int queues, i;
    NetFilterState *nf, *next;

    assert(nc->info->type != NET_CLIENT_DRIVER_NIC);

    /* If the NetClientState belongs to a multiqueue backend, we will change all
     * other NetClientStates also.
     */
    queues = qemu_find_net_clients_except(nc->name, ncs,
                                          NET_CLIENT_DRIVER_NIC,
                                          MAX_QUEUE_NUM);
    assert(queues != 0);

    QTAILQ_FOREACH_SAFE(nf, &nc->filters, next, next) {
        object_unparent(OBJECT(nf));
    }

    /* If there is a peer NIC, delete and cleanup client, but do not free. */
    if (nc->peer && nc->peer->info->type == NET_CLIENT_DRIVER_NIC) {
        NICState *nic = qemu_get_nic(nc->peer);
        if (nic->peer_deleted) {
            return;
        }
        nic->peer_deleted = true;

        for (i = 0; i < queues; i++) {
            ncs[i]->peer->link_down = true;
        }

        if (nc->peer->info->link_status_changed) {
            nc->peer->info->link_status_changed(nc->peer);
        }

        for (i = 0; i < queues; i++) {
            qemu_cleanup_net_client(ncs[i]);
        }

        return;
    }

    for (i = 0; i < queues; i++) {
        qemu_cleanup_net_client(ncs[i]);
        qemu_free_net_client(ncs[i]);
    }
}

void qemu_del_nic(NICState *nic)
{
    int i, queues = MAX(nic->conf->peers.queues, 1);

    qemu_macaddr_set_free(&nic->conf->macaddr);

    /* If this is a peer NIC and peer has already been deleted, free it now. */
    if (nic->peer_deleted) {
        for (i = 0; i < queues; i++) {
            qemu_free_net_client(qemu_get_subqueue(nic, i)->peer);
        }
    }

    for (i = queues - 1; i >= 0; i--) {
        NetClientState *nc = qemu_get_subqueue(nic, i);

        qemu_cleanup_net_client(nc);
        qemu_free_net_client(nc);
    }

    g_free(nic);
}

void qemu_foreach_nic(qemu_nic_foreach func, void *opaque)
{
    NetClientState *nc;

    QTAILQ_FOREACH(nc, &net_clients, next) {
        if (nc->info->type == NET_CLIENT_DRIVER_NIC) {
            if (nc->queue_index == 0) {
                func(qemu_get_nic(nc), opaque);
            }
        }
    }
}

bool qemu_has_ufo(NetClientState *nc)
{
    if (!nc || !nc->info->has_ufo) {
        return false;
    }

    return nc->info->has_ufo(nc);
}

bool qemu_has_vnet_hdr(NetClientState *nc)
{
    if (!nc || !nc->info->has_vnet_hdr) {
        return false;
    }

    return nc->info->has_vnet_hdr(nc);
}

bool qemu_has_vnet_hdr_len(NetClientState *nc, int len)
{
    if (!nc || !nc->info->has_vnet_hdr_len) {
        return false;
    }

    return nc->info->has_vnet_hdr_len(nc, len);
}

void qemu_using_vnet_hdr(NetClientState *nc, bool enable)
{
    if (!nc || !nc->info->using_vnet_hdr) {
        return;
    }

    nc->info->using_vnet_hdr(nc, enable);
}

void qemu_set_offload(NetClientState *nc, int csum, int tso4, int tso6,
                          int ecn, int ufo)
{
    if (!nc || !nc->info->set_offload) {
        return;
    }

    nc->info->set_offload(nc, csum, tso4, tso6, ecn, ufo);
}

void qemu_set_vnet_hdr_len(NetClientState *nc, int len)
{
    if (!nc || !nc->info->set_vnet_hdr_len) {
        return;
    }

    nc->vnet_hdr_len = len;
    nc->info->set_vnet_hdr_len(nc, len);
}

int qemu_set_vnet_le(NetClientState *nc, bool is_le)
{
#ifdef HOST_WORDS_BIGENDIAN
    if (!nc || !nc->info->set_vnet_le) {
        return -ENOSYS;
    }

    return nc->info->set_vnet_le(nc, is_le);
#else
    return 0;
#endif
}

int qemu_set_vnet_be(NetClientState *nc, bool is_be)
{
#ifdef HOST_WORDS_BIGENDIAN
    return 0;
#else
    if (!nc || !nc->info->set_vnet_be) {
        return -ENOSYS;
    }

    return nc->info->set_vnet_be(nc, is_be);
#endif
}

int qemu_can_send_packet(NetClientState *sender)
{
    int vm_running = runstate_is_running();

    if (!vm_running) {
        return 0;
    }

    if (!sender->peer) {
        return 1;
    }

    if (sender->peer->receive_disabled) {
        return 0;
    } else if (sender->peer->info->can_receive &&
               !sender->peer->info->can_receive(sender->peer)) {
        return 0;
    }
    return 1;
}

static ssize_t filter_receive_iov(NetClientState *nc,
                                  NetFilterDirection direction,
                                  NetClientState *sender,
                                  unsigned flags,
                                  const struct iovec *iov,
                                  int iovcnt,
                                  NetPacketSent *sent_cb)
{
    ssize_t ret = 0;
    NetFilterState *nf = NULL;

    if (direction == NET_FILTER_DIRECTION_TX) {
        QTAILQ_FOREACH(nf, &nc->filters, next) {
            ret = qemu_netfilter_receive(nf, direction, sender, flags, iov,
                                         iovcnt, sent_cb);
            if (ret) {
                return ret;
            }
        }
    } else {
        QTAILQ_FOREACH_REVERSE(nf, &nc->filters, NetFilterHead, next) {
            ret = qemu_netfilter_receive(nf, direction, sender, flags, iov,
                                         iovcnt, sent_cb);
            if (ret) {
                return ret;
            }
        }
    }

    return ret;
}

static ssize_t filter_receive(NetClientState *nc,
                              NetFilterDirection direction,
                              NetClientState *sender,
                              unsigned flags,
                              const uint8_t *data,
                              size_t size,
                              NetPacketSent *sent_cb)
{
    struct iovec iov = {
        .iov_base = (void *)data,
        .iov_len = size
    };

    return filter_receive_iov(nc, direction, sender, flags, &iov, 1, sent_cb);
}

void qemu_purge_queued_packets(NetClientState *nc)
{
    if (!nc->peer) {
        return;
    }

    qemu_net_queue_purge(nc->peer->incoming_queue, nc);
}

void qemu_flush_or_purge_queued_packets(NetClientState *nc, bool purge)
{
    nc->receive_disabled = 0;

    if (nc->peer && nc->peer->info->type == NET_CLIENT_DRIVER_HUBPORT) {
        if (net_hub_flush(nc->peer)) {
            qemu_notify_event();
        }
    }
    if (qemu_net_queue_flush(nc->incoming_queue)) {
        /* We emptied the queue successfully, signal to the IO thread to repoll
         * the file descriptor (for tap, for example).
         */
        qemu_notify_event();
    } else if (purge) {
        /* Unable to empty the queue, purge remaining packets */
        qemu_net_queue_purge(nc->incoming_queue, nc);
    }
}

void qemu_flush_queued_packets(NetClientState *nc)
{
    qemu_flush_or_purge_queued_packets(nc, false);
}

static ssize_t qemu_send_packet_async_with_flags(NetClientState *sender,
                                                 unsigned flags,
                                                 const uint8_t *buf, int size,
                                                 NetPacketSent *sent_cb)
{
    NetQueue *queue;
    int ret;

#ifdef DEBUG_NET
    printf("qemu_send_packet_async:\n");
    qemu_hexdump((const char *)buf, stdout, "net", size);
#endif

    if (sender->link_down || !sender->peer) {
        return size;
    }

    /* Let filters handle the packet first */
    ret = filter_receive(sender, NET_FILTER_DIRECTION_TX,
                         sender, flags, buf, size, sent_cb);
    if (ret) {
        return ret;
    }

    ret = filter_receive(sender->peer, NET_FILTER_DIRECTION_RX,
                         sender, flags, buf, size, sent_cb);
    if (ret) {
        return ret;
    }

    queue = sender->peer->incoming_queue;

    return qemu_net_queue_send(queue, sender, flags, buf, size, sent_cb);
}

ssize_t qemu_send_packet_async(NetClientState *sender,
                               const uint8_t *buf, int size,
                               NetPacketSent *sent_cb)
{
    return qemu_send_packet_async_with_flags(sender, QEMU_NET_PACKET_FLAG_NONE,
                                             buf, size, sent_cb);
}

void qemu_send_packet(NetClientState *nc, const uint8_t *buf, int size)
{
    qemu_send_packet_async(nc, buf, size, NULL);
}

ssize_t qemu_send_packet_raw(NetClientState *nc, const uint8_t *buf, int size)
{
    return qemu_send_packet_async_with_flags(nc, QEMU_NET_PACKET_FLAG_RAW,
                                             buf, size, NULL);
}

static ssize_t nc_sendv_compat(NetClientState *nc, const struct iovec *iov,
                               int iovcnt, unsigned flags)
{
    uint8_t *buf = NULL;
    uint8_t *buffer;
    size_t offset;
    ssize_t ret;

    if (iovcnt == 1) {
        buffer = iov[0].iov_base;
        offset = iov[0].iov_len;
    } else {
        offset = iov_size(iov, iovcnt);
        if (offset > NET_BUFSIZE) {
            return -1;
        }
        buf = g_malloc(offset);
        buffer = buf;
        offset = iov_to_buf(iov, iovcnt, 0, buf, offset);
    }

    if (flags & QEMU_NET_PACKET_FLAG_RAW && nc->info->receive_raw) {
        ret = nc->info->receive_raw(nc, buffer, offset);
    } else {
        ret = nc->info->receive(nc, buffer, offset);
    }

    g_free(buf);
    return ret;
}

static ssize_t qemu_deliver_packet_iov(NetClientState *sender,
                                       unsigned flags,
                                       const struct iovec *iov,
                                       int iovcnt,
                                       void *opaque)
{
    NetClientState *nc = opaque;
    int ret;


    if (nc->link_down) {
        return iov_size(iov, iovcnt);
    }

    if (nc->receive_disabled) {
        return 0;
    }

    if (nc->info->receive_iov && !(flags & QEMU_NET_PACKET_FLAG_RAW)) {
        ret = nc->info->receive_iov(nc, iov, iovcnt);
    } else {
        ret = nc_sendv_compat(nc, iov, iovcnt, flags);
    }

    if (ret == 0) {
        nc->receive_disabled = 1;
    }

    return ret;
}

ssize_t qemu_sendv_packet_async(NetClientState *sender,
                                const struct iovec *iov, int iovcnt,
                                NetPacketSent *sent_cb)
{
    NetQueue *queue;
    size_t size = iov_size(iov, iovcnt);
    int ret;

    if (size > NET_BUFSIZE) {
        return size;
    }

    if (sender->link_down || !sender->peer) {
        return size;
    }

    /* Let filters handle the packet first */
    ret = filter_receive_iov(sender, NET_FILTER_DIRECTION_TX, sender,
                             QEMU_NET_PACKET_FLAG_NONE, iov, iovcnt, sent_cb);
    if (ret) {
        return ret;
    }

    ret = filter_receive_iov(sender->peer, NET_FILTER_DIRECTION_RX, sender,
                             QEMU_NET_PACKET_FLAG_NONE, iov, iovcnt, sent_cb);
    if (ret) {
        return ret;
    }

    queue = sender->peer->incoming_queue;

    return qemu_net_queue_send_iov(queue, sender,
                                   QEMU_NET_PACKET_FLAG_NONE,
                                   iov, iovcnt, sent_cb);
}

ssize_t
qemu_sendv_packet(NetClientState *nc, const struct iovec *iov, int iovcnt)
{
    return qemu_sendv_packet_async(nc, iov, iovcnt, NULL);
}

NetClientState *qemu_find_netdev(const char *id)
{
    NetClientState *nc;

    QTAILQ_FOREACH(nc, &net_clients, next) {
        if (nc->info->type == NET_CLIENT_DRIVER_NIC)
            continue;
        if (!strcmp(nc->name, id)) {
            return nc;
        }
    }

    return NULL;
}

int qemu_find_net_clients_except(const char *id, NetClientState **ncs,
                                 NetClientDriver type, int max)
{
    NetClientState *nc;
    int ret = 0;

    QTAILQ_FOREACH(nc, &net_clients, next) {
        if (nc->info->type == type) {
            continue;
        }
        if (!id || !strcmp(nc->name, id)) {
            if (ret < max) {
                ncs[ret] = nc;
            }
            ret++;
        }
    }

    return ret;
}

static int nic_get_free_idx(void)
{
    int index;

    for (index = 0; index < MAX_NICS; index++)
        if (!nd_table[index].used)
            return index;
    return -1;
}

int qemu_show_nic_models(const char *arg, const char *const *models)
{
    int i;

    if (!arg || !is_help_option(arg)) {
        return 0;
    }

    fprintf(stderr, "qemu: Supported NIC models: ");
    for (i = 0 ; models[i]; i++)
        fprintf(stderr, "%s%c", models[i], models[i+1] ? ',' : '\n');
    return 1;
}

void qemu_check_nic_model(NICInfo *nd, const char *model)
{
    const char *models[2];

    models[0] = model;
    models[1] = NULL;

    if (qemu_show_nic_models(nd->model, models))
        exit(0);
    if (qemu_find_nic_model(nd, models, model) < 0)
        exit(1);
}

int qemu_find_nic_model(NICInfo *nd, const char * const *models,
                        const char *default_model)
{
    int i;

    if (!nd->model)
        nd->model = g_strdup(default_model);

    for (i = 0 ; models[i]; i++) {
        if (strcmp(nd->model, models[i]) == 0)
            return i;
    }

    error_report("Unsupported NIC model: %s", nd->model);
    return -1;
}

static int net_init_nic(const Netdev *netdev, const char *name,
                        NetClientState *peer, Error **errp)
{
    int idx;
    NICInfo *nd;
    const NetLegacyNicOptions *nic;

    assert(netdev->type == NET_CLIENT_DRIVER_NIC);
    nic = &netdev->u.nic;

    idx = nic_get_free_idx();
    if (idx == -1 || nb_nics >= MAX_NICS) {
        error_setg(errp, "too many NICs");
        return -1;
    }

    nd = &nd_table[idx];

    memset(nd, 0, sizeof(*nd));

    if (nic->has_netdev) {
        nd->netdev = qemu_find_netdev(nic->netdev);
        if (!nd->netdev) {
            error_setg(errp, "netdev '%s' not found", nic->netdev);
            return -1;
        }
    } else {
        assert(peer);
        nd->netdev = peer;
    }
    nd->name = g_strdup(name);
    if (nic->has_model) {
        nd->model = g_strdup(nic->model);
    }
    if (nic->has_addr) {
        nd->devaddr = g_strdup(nic->addr);
    }

    if (nic->has_macaddr &&
        net_parse_macaddr(nd->macaddr.a, nic->macaddr) < 0) {
        error_setg(errp, "invalid syntax for ethernet address");
        return -1;
    }
    if (nic->has_macaddr &&
        is_multicast_ether_addr(nd->macaddr.a)) {
        error_setg(errp,
                   "NIC cannot have multicast MAC address (odd 1st byte)");
        return -1;
    }
    qemu_macaddr_default_if_unset(&nd->macaddr);

    if (nic->has_vectors) {
        if (nic->vectors > 0x7ffffff) {
            error_setg(errp, "invalid # of vectors: %"PRIu32, nic->vectors);
            return -1;
        }
        nd->nvectors = nic->vectors;
    } else {
        nd->nvectors = DEV_NVECTORS_UNSPECIFIED;
    }

    nd->used = 1;
    nb_nics++;

    return idx;
}


static int (* const net_client_init_fun[NET_CLIENT_DRIVER__MAX])(
    const Netdev *netdev,
    const char *name,
    NetClientState *peer, Error **errp) = {
        [NET_CLIENT_DRIVER_NIC]       = net_init_nic,
#ifdef CONFIG_SLIRP
        [NET_CLIENT_DRIVER_USER]      = net_init_slirp,
#endif
        [NET_CLIENT_DRIVER_TAP]       = net_init_tap,
        [NET_CLIENT_DRIVER_SOCKET]    = net_init_socket,
#ifdef CONFIG_VDE
        [NET_CLIENT_DRIVER_VDE]       = net_init_vde,
#endif
#ifdef CONFIG_NETMAP
        [NET_CLIENT_DRIVER_NETMAP]    = net_init_netmap,
#endif
#ifdef CONFIG_NET_BRIDGE
        [NET_CLIENT_DRIVER_BRIDGE]    = net_init_bridge,
#endif
        [NET_CLIENT_DRIVER_HUBPORT]   = net_init_hubport,
#ifdef CONFIG_VHOST_NET_USED
        [NET_CLIENT_DRIVER_VHOST_USER] = net_init_vhost_user,
#endif
#ifdef CONFIG_L2TPV3
        [NET_CLIENT_DRIVER_L2TPV3]    = net_init_l2tpv3,
#endif
};


static int net_client_init1(const void *object, bool is_netdev, Error **errp)
{
    Netdev legacy = {0};
    const Netdev *netdev;
    const char *name;
    NetClientState *peer = NULL;

    if (is_netdev) {
        netdev = object;
        name = netdev->id;

        if (netdev->type == NET_CLIENT_DRIVER_NIC ||
            !net_client_init_fun[netdev->type]) {
            error_setg(errp, QERR_INVALID_PARAMETER_VALUE, "type",
                       "a netdev backend type");
            return -1;
        }
    } else {
        const NetLegacy *net = object;
        const NetLegacyOptions *opts = net->opts;
        legacy.id = net->id;
        netdev = &legacy;
        /* missing optional values have been initialized to "all bits zero" */
        name = net->has_id ? net->id : net->name;

        if (net->has_name) {
            warn_report("The 'name' parameter is deprecated, use 'id' instead");
        }

        /* Map the old options to the new flat type */
        switch (opts->type) {
        case NET_LEGACY_OPTIONS_TYPE_NONE:
            return 0; /* nothing to do */
        case NET_LEGACY_OPTIONS_TYPE_NIC:
            legacy.type = NET_CLIENT_DRIVER_NIC;
            legacy.u.nic = opts->u.nic;
            break;
        case NET_LEGACY_OPTIONS_TYPE_USER:
            legacy.type = NET_CLIENT_DRIVER_USER;
            legacy.u.user = opts->u.user;
            break;
        case NET_LEGACY_OPTIONS_TYPE_TAP:
            legacy.type = NET_CLIENT_DRIVER_TAP;
            legacy.u.tap = opts->u.tap;
            break;
        case NET_LEGACY_OPTIONS_TYPE_L2TPV3:
            legacy.type = NET_CLIENT_DRIVER_L2TPV3;
            legacy.u.l2tpv3 = opts->u.l2tpv3;
            break;
        case NET_LEGACY_OPTIONS_TYPE_SOCKET:
            legacy.type = NET_CLIENT_DRIVER_SOCKET;
            legacy.u.socket = opts->u.socket;
            break;
        case NET_LEGACY_OPTIONS_TYPE_VDE:
            legacy.type = NET_CLIENT_DRIVER_VDE;
            legacy.u.vde = opts->u.vde;
            break;
        case NET_LEGACY_OPTIONS_TYPE_BRIDGE:
            legacy.type = NET_CLIENT_DRIVER_BRIDGE;
            legacy.u.bridge = opts->u.bridge;
            break;
        case NET_LEGACY_OPTIONS_TYPE_NETMAP:
            legacy.type = NET_CLIENT_DRIVER_NETMAP;
            legacy.u.netmap = opts->u.netmap;
            break;
        case NET_LEGACY_OPTIONS_TYPE_VHOST_USER:
            legacy.type = NET_CLIENT_DRIVER_VHOST_USER;
            legacy.u.vhost_user = opts->u.vhost_user;
            break;
        default:
            abort();
        }

        if (!net_client_init_fun[netdev->type]) {
            error_setg(errp, QERR_INVALID_PARAMETER_VALUE, "type",
                       "a net backend type (maybe it is not compiled "
                       "into this binary)");
            return -1;
        }

        /* Do not add to a hub if it's a nic with a netdev= parameter. */
        if (netdev->type != NET_CLIENT_DRIVER_NIC ||
            !opts->u.nic.has_netdev) {
            peer = net_hub_add_port(0, NULL, NULL);
        }
    }

    if (net_client_init_fun[netdev->type](netdev, name, peer, errp) < 0) {
        /* FIXME drop when all init functions store an Error */
        if (errp && !*errp) {
            error_setg(errp, QERR_DEVICE_INIT_FAILED,
                       NetClientDriver_str(netdev->type));
        }
        return -1;
    }
    return 0;
}

static void show_netdevs(void)
{
    int idx;
    const char *available_netdevs[] = {
        "socket",
        "hubport",
        "tap",
#ifdef CONFIG_SLIRP
        "user",
#endif
#ifdef CONFIG_L2TPV3
        "l2tpv3",
#endif
#ifdef CONFIG_VDE
        "vde",
#endif
#ifdef CONFIG_NET_BRIDGE
        "bridge",
#endif
#ifdef CONFIG_NETMAP
        "netmap",
#endif
#ifdef CONFIG_POSIX
        "vhost-user",
#endif
    };

    printf("Available netdev backend types:\n");
    for (idx = 0; idx < ARRAY_SIZE(available_netdevs); idx++) {
        puts(available_netdevs[idx]);
    }
}

static int net_client_init(QemuOpts *opts, bool is_netdev, Error **errp)
{
    void *object = NULL;
    Error *err = NULL;
    int ret = -1;
    Visitor *v = opts_visitor_new(opts);

    const char *type = qemu_opt_get(opts, "type");

    if (is_netdev && type && is_help_option(type)) {
        show_netdevs();
        exit(0);
    } else {
        /* Parse convenience option format ip6-net=fec0::0[/64] */
        const char *ip6_net = qemu_opt_get(opts, "ipv6-net");

        if (ip6_net) {
            char buf[strlen(ip6_net) + 1];

            if (get_str_sep(buf, sizeof(buf), &ip6_net, '/') < 0) {
                /* Default 64bit prefix length.  */
                qemu_opt_set(opts, "ipv6-prefix", ip6_net, &error_abort);
                qemu_opt_set_number(opts, "ipv6-prefixlen", 64, &error_abort);
            } else {
                /* User-specified prefix length.  */
                unsigned long len;
                int err;

                qemu_opt_set(opts, "ipv6-prefix", buf, &error_abort);
                err = qemu_strtoul(ip6_net, NULL, 10, &len);

                if (err) {
                    error_setg(errp, QERR_INVALID_PARAMETER_VALUE,
                              "ipv6-prefix", "a number");
                } else {
                    qemu_opt_set_number(opts, "ipv6-prefixlen", len,
                                        &error_abort);
                }
            }
            qemu_opt_unset(opts, "ipv6-net");
        }
    }

    if (is_netdev) {
        visit_type_Netdev(v, NULL, (Netdev **)&object, &err);
    } else {
        visit_type_NetLegacy(v, NULL, (NetLegacy **)&object, &err);
    }

    if (!err) {
        ret = net_client_init1(object, is_netdev, &err);
    }

    if (is_netdev) {
        qapi_free_Netdev(object);
    } else {
        qapi_free_NetLegacy(object);
    }

    error_propagate(errp, err);
    visit_free(v);
    return ret;
}

void netdev_add(QemuOpts *opts, Error **errp)
{
    net_client_init(opts, true, errp);
}

void qmp_netdev_add(QDict *qdict, QObject **ret, Error **errp)
{
    Error *local_err = NULL;
    QemuOptsList *opts_list;
    QemuOpts *opts;

    opts_list = qemu_find_opts_err("netdev", &local_err);
    if (local_err) {
        goto out;
    }

    opts = qemu_opts_from_qdict(opts_list, qdict, &local_err);
    if (local_err) {
        goto out;
    }

    netdev_add(opts, &local_err);
    if (local_err) {
        qemu_opts_del(opts);
        goto out;
    }

out:
    error_propagate(errp, local_err);
}

void qmp_netdev_del(const char *id, Error **errp)
{
    NetClientState *nc;
    QemuOpts *opts;

    nc = qemu_find_netdev(id);
    if (!nc) {
        error_set(errp, ERROR_CLASS_DEVICE_NOT_FOUND,
                  "Device '%s' not found", id);
        return;
    }

    opts = qemu_opts_find(qemu_find_opts_err("netdev", NULL), id);
    if (!opts) {
        error_setg(errp, "Device '%s' is not a netdev", id);
        return;
    }

    qemu_del_net_client(nc);
    qemu_opts_del(opts);
}

static void netfilter_print_info(Monitor *mon, NetFilterState *nf)
{
    char *str;
    ObjectProperty *prop;
    ObjectPropertyIterator iter;
    Visitor *v;

    /* generate info str */
    object_property_iter_init(&iter, OBJECT(nf));
    while ((prop = object_property_iter_next(&iter))) {
        if (!strcmp(prop->name, "type")) {
            continue;
        }
        v = string_output_visitor_new(false, &str);
        object_property_get(OBJECT(nf), v, prop->name, NULL);
        visit_complete(v, &str);
        visit_free(v);
        monitor_printf(mon, ",%s=%s", prop->name, str);
        g_free(str);
    }
    monitor_printf(mon, "\n");
}

void print_net_client(Monitor *mon, NetClientState *nc)
{
    NetFilterState *nf;

    monitor_printf(mon, "%s: index=%d,type=%s,%s\n", nc->name,
                   nc->queue_index,
                   NetClientDriver_str(nc->info->type),
                   nc->info_str);
    if (!QTAILQ_EMPTY(&nc->filters)) {
        monitor_printf(mon, "filters:\n");
    }
    QTAILQ_FOREACH(nf, &nc->filters, next) {
        char *path = object_get_canonical_path_component(OBJECT(nf));

        monitor_printf(mon, "  - %s: type=%s", path,
                       object_get_typename(OBJECT(nf)));
        netfilter_print_info(mon, nf);
        g_free(path);
    }
}

RxFilterInfoList *qmp_query_rx_filter(bool has_name, const char *name,
                                      Error **errp)
{
    NetClientState *nc;
    RxFilterInfoList *filter_list = NULL, *last_entry = NULL;

    QTAILQ_FOREACH(nc, &net_clients, next) {
        RxFilterInfoList *entry;
        RxFilterInfo *info;

        if (has_name && strcmp(nc->name, name) != 0) {
            continue;
        }

        /* only query rx-filter information of NIC */
        if (nc->info->type != NET_CLIENT_DRIVER_NIC) {
            if (has_name) {
                error_setg(errp, "net client(%s) isn't a NIC", name);
                return NULL;
            }
            continue;
        }

        /* only query information on queue 0 since the info is per nic,
         * not per queue
         */
        if (nc->queue_index != 0)
            continue;

        if (nc->info->query_rx_filter) {
            info = nc->info->query_rx_filter(nc);
            entry = g_malloc0(sizeof(*entry));
            entry->value = info;

            if (!filter_list) {
                filter_list = entry;
            } else {
                last_entry->next = entry;
            }
            last_entry = entry;
        } else if (has_name) {
            error_setg(errp, "net client(%s) doesn't support"
                       " rx-filter querying", name);
            return NULL;
        }

        if (has_name) {
            break;
        }
    }

    if (filter_list == NULL && has_name) {
        error_setg(errp, "invalid net client name: %s", name);
    }

    return filter_list;
}

void hmp_info_network(Monitor *mon, const QDict *qdict)
{
    NetClientState *nc, *peer;
    NetClientDriver type;

    net_hub_info(mon);

    QTAILQ_FOREACH(nc, &net_clients, next) {
        peer = nc->peer;
        type = nc->info->type;

        /* Skip if already printed in hub info */
        if (net_hub_id_for_client(nc, NULL) == 0) {
            continue;
        }

        if (!peer || type == NET_CLIENT_DRIVER_NIC) {
            print_net_client(mon, nc);
        } /* else it's a netdev connected to a NIC, printed with the NIC */
        if (peer && type == NET_CLIENT_DRIVER_NIC) {
            monitor_printf(mon, " \\ ");
            print_net_client(mon, peer);
        }
    }
}

void colo_notify_filters_event(int event, Error **errp)
{
    NetClientState *nc;
    NetFilterState *nf;
    NetFilterClass *nfc = NULL;
    Error *local_err = NULL;

    QTAILQ_FOREACH(nc, &net_clients, next) {
        QTAILQ_FOREACH(nf, &nc->filters, next) {
            nfc = NETFILTER_GET_CLASS(OBJECT(nf));
            nfc->handle_event(nf, event, &local_err);
            if (local_err) {
                error_propagate(errp, local_err);
                return;
            }
        }
    }
}

void qmp_set_link(const char *name, bool up, Error **errp)
{
    NetClientState *ncs[MAX_QUEUE_NUM];
    NetClientState *nc;
    int queues, i;

    queues = qemu_find_net_clients_except(name, ncs,
                                          NET_CLIENT_DRIVER__MAX,
                                          MAX_QUEUE_NUM);

    if (queues == 0) {
        error_set(errp, ERROR_CLASS_DEVICE_NOT_FOUND,
                  "Device '%s' not found", name);
        return;
    }
    nc = ncs[0];

    for (i = 0; i < queues; i++) {
        ncs[i]->link_down = !up;
    }

    if (nc->info->link_status_changed) {
        nc->info->link_status_changed(nc);
    }

    if (nc->peer) {
        /* Change peer link only if the peer is NIC and then notify peer.
         * If the peer is a HUBPORT or a backend, we do not change the
         * link status.
         *
         * This behavior is compatible with qemu hubs where there could be
         * multiple clients that can still communicate with each other in
         * disconnected mode. For now maintain this compatibility.
         */
        if (nc->peer->info->type == NET_CLIENT_DRIVER_NIC) {
            for (i = 0; i < queues; i++) {
                ncs[i]->peer->link_down = !up;
            }
        }
        if (nc->peer->info->link_status_changed) {
            nc->peer->info->link_status_changed(nc->peer);
        }
    }
}

static void net_vm_change_state_handler(void *opaque, int running,
                                        RunState state)
{
    NetClientState *nc;
    NetClientState *tmp;

    QTAILQ_FOREACH_SAFE(nc, &net_clients, next, tmp) {
        if (running) {
            /* Flush queued packets and wake up backends. */
            if (nc->peer && qemu_can_send_packet(nc)) {
                qemu_flush_queued_packets(nc->peer);
            }
        } else {
            /* Complete all queued packets, to guarantee we don't modify
             * state later when VM is not running.
             */
            qemu_flush_or_purge_queued_packets(nc, true);
        }
    }
}

void net_cleanup(void)
{
    NetClientState *nc;

    /* We may del multiple entries during qemu_del_net_client(),
     * so QTAILQ_FOREACH_SAFE() is also not safe here.
     */
    while (!QTAILQ_EMPTY(&net_clients)) {
        nc = QTAILQ_FIRST(&net_clients);
        if (nc->info->type == NET_CLIENT_DRIVER_NIC) {
            qemu_del_nic(qemu_get_nic(nc));
        } else {
            qemu_del_net_client(nc);
        }
    }

    qemu_del_vm_change_state_handler(net_change_state_entry);
}

void net_check_clients(void)
{
    NetClientState *nc;
    int i;

    net_hub_check_clients();

    QTAILQ_FOREACH(nc, &net_clients, next) {
        if (!nc->peer) {
            warn_report("%s %s has no peer",
                        nc->info->type == NET_CLIENT_DRIVER_NIC
                        ? "nic" : "netdev",
                        nc->name);
        }
    }

    /* Check that all NICs requested via -net nic actually got created.
     * NICs created via -device don't need to be checked here because
     * they are always instantiated.
     */
    for (i = 0; i < MAX_NICS; i++) {
        NICInfo *nd = &nd_table[i];
        if (nd->used && !nd->instantiated) {
            warn_report("requested NIC (%s, model %s) "
                        "was not created (not supported by this machine?)",
                        nd->name ? nd->name : "anonymous",
                        nd->model ? nd->model : "unspecified");
        }
    }
}

static int net_init_client(void *dummy, QemuOpts *opts, Error **errp)
{
    return net_client_init(opts, false, errp);
}

static int net_init_netdev(void *dummy, QemuOpts *opts, Error **errp)
{
    return net_client_init(opts, true, errp);
}

/* For the convenience "--nic" parameter */
static int net_param_nic(void *dummy, QemuOpts *opts, Error **errp)
{
    char *mac, *nd_id;
    int idx, ret;
    NICInfo *ni;
    const char *type;

    type = qemu_opt_get(opts, "type");
    if (type && g_str_equal(type, "none")) {
        return 0;    /* Nothing to do, default_net is cleared in vl.c */
    }

    idx = nic_get_free_idx();
    if (idx == -1 || nb_nics >= MAX_NICS) {
        error_setg(errp, "no more on-board/default NIC slots available");
        return -1;
    }

    if (!type) {
        qemu_opt_set(opts, "type", "user", &error_abort);
    }

    ni = &nd_table[idx];
    memset(ni, 0, sizeof(*ni));
    ni->model = qemu_opt_get_del(opts, "model");

    /* Create an ID if the user did not specify one */
    nd_id = g_strdup(qemu_opts_id(opts));
    if (!nd_id) {
        nd_id = g_strdup_printf("__org.qemu.nic%i\n", idx);
        qemu_opts_set_id(opts, nd_id);
    }

    /* Handle MAC address */
    mac = qemu_opt_get_del(opts, "mac");
    if (mac) {
        ret = net_parse_macaddr(ni->macaddr.a, mac);
        g_free(mac);
        if (ret) {
            error_setg(errp, "invalid syntax for ethernet address");
            goto out;
        }
        if (is_multicast_ether_addr(ni->macaddr.a)) {
            error_setg(errp, "NIC cannot have multicast MAC address");
            ret = -1;
            goto out;
        }
    }
    qemu_macaddr_default_if_unset(&ni->macaddr);

    ret = net_client_init(opts, true, errp);
    if (ret == 0) {
        ni->netdev = qemu_find_netdev(nd_id);
        ni->used = true;
        nb_nics++;
    }

out:
    g_free(nd_id);
    return ret;
}

int net_init_clients(Error **errp)
{
    net_change_state_entry =
        qemu_add_vm_change_state_handler(net_vm_change_state_handler, NULL);

    QTAILQ_INIT(&net_clients);

    if (qemu_opts_foreach(qemu_find_opts("netdev"),
                          net_init_netdev, NULL, errp)) {
        return -1;
    }

    if (qemu_opts_foreach(qemu_find_opts("nic"), net_param_nic, NULL, errp)) {
        return -1;
    }

    if (qemu_opts_foreach(qemu_find_opts("net"), net_init_client, NULL, errp)) {
        return -1;
    }

    return 0;
}

int net_client_parse(QemuOptsList *opts_list, const char *optarg)
{
    if (!qemu_opts_parse_noisily(opts_list, optarg, true)) {
        return -1;
    }

    return 0;
}

/* From FreeBSD */
/* XXX: optimize */
uint32_t net_crc32(const uint8_t *p, int len)
{
    uint32_t crc;
    int carry, i, j;
    uint8_t b;

    crc = 0xffffffff;
    for (i = 0; i < len; i++) {
        b = *p++;
        for (j = 0; j < 8; j++) {
            carry = ((crc & 0x80000000L) ? 1 : 0) ^ (b & 0x01);
            crc <<= 1;
            b >>= 1;
            if (carry) {
                crc = ((crc ^ POLYNOMIAL_BE) | carry);
            }
        }
    }

    return crc;
}

uint32_t net_crc32_le(const uint8_t *p, int len)
{
    uint32_t crc;
    int carry, i, j;
    uint8_t b;

    crc = 0xffffffff;
    for (i = 0; i < len; i++) {
        b = *p++;
        for (j = 0; j < 8; j++) {
            carry = (crc & 0x1) ^ (b & 0x01);
            crc >>= 1;
            b >>= 1;
            if (carry) {
                crc ^= POLYNOMIAL_LE;
            }
        }
    }

    return crc;
}

QemuOptsList qemu_netdev_opts = {
    .name = "netdev",
    .implied_opt_name = "type",
    .head = QTAILQ_HEAD_INITIALIZER(qemu_netdev_opts.head),
    .desc = {
        /*
         * no elements => accept any params
         * validation will happen later
         */
        { /* end of list */ }
    },
};

QemuOptsList qemu_nic_opts = {
    .name = "nic",
    .implied_opt_name = "type",
    .head = QTAILQ_HEAD_INITIALIZER(qemu_nic_opts.head),
    .desc = {
        /*
         * no elements => accept any params
         * validation will happen later
         */
        { /* end of list */ }
    },
};

QemuOptsList qemu_net_opts = {
    .name = "net",
    .implied_opt_name = "type",
    .head = QTAILQ_HEAD_INITIALIZER(qemu_net_opts.head),
    .desc = {
        /*
         * no elements => accept any params
         * validation will happen later
         */
        { /* end of list */ }
    },
};

void net_socket_rs_init(SocketReadState *rs,
                        SocketReadStateFinalize *finalize,
                        bool vnet_hdr)
{
    rs->state = 0;
    rs->vnet_hdr = vnet_hdr;
    rs->index = 0;
    rs->packet_len = 0;
    rs->vnet_hdr_len = 0;
    memset(rs->buf, 0, sizeof(rs->buf));
    rs->finalize = finalize;
}

/*
 * Returns
 * 0: success
 * -1: error occurs
 */
int net_fill_rstate(SocketReadState *rs, const uint8_t *buf, int size)
{
    unsigned int l;

    while (size > 0) {
        /* Reassemble a packet from the network.
         * 0 = getting length.
         * 1 = getting vnet header length.
         * 2 = getting data.
         */
        switch (rs->state) {
        case 0:
            l = 4 - rs->index;
            if (l > size) {
                l = size;
            }
            memcpy(rs->buf + rs->index, buf, l);
            buf += l;
            size -= l;
            rs->index += l;
            if (rs->index == 4) {
                /* got length */
                rs->packet_len = ntohl(*(uint32_t *)rs->buf);
                rs->index = 0;
                if (rs->vnet_hdr) {
                    rs->state = 1;
                } else {
                    rs->state = 2;
                    rs->vnet_hdr_len = 0;
                }
            }
            break;
        case 1:
            l = 4 - rs->index;
            if (l > size) {
                l = size;
            }
            memcpy(rs->buf + rs->index, buf, l);
            buf += l;
            size -= l;
            rs->index += l;
            if (rs->index == 4) {
                /* got vnet header length */
                rs->vnet_hdr_len = ntohl(*(uint32_t *)rs->buf);
                rs->index = 0;
                rs->state = 2;
            }
            break;
        case 2:
            l = rs->packet_len - rs->index;
            if (l > size) {
                l = size;
            }
            if (rs->index + l <= sizeof(rs->buf)) {
                memcpy(rs->buf + rs->index, buf, l);
            } else {
                fprintf(stderr, "serious error: oversized packet received,"
                    "connection terminated.\n");
                rs->index = rs->state = 0;
                return -1;
            }

            rs->index += l;
            buf += l;
            size -= l;
            if (rs->index >= rs->packet_len) {
                rs->index = 0;
                rs->state = 0;
                assert(rs->finalize);
                rs->finalize(rs);
            }
            break;
        }
    }

    assert(size == 0);
    return 0;
}
