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
#include "config-host.h"

#include "net.h"
#include "net/clients.h"
#include "net/hub.h"
#include "net/slirp.h"
#include "net/util.h"

#include "monitor.h"
#include "qemu-common.h"
#include "qemu_socket.h"
#include "qmp-commands.h"
#include "hw/qdev.h"
#include "iov.h"
#include "qapi-visit.h"
#include "qapi/opts-visitor.h"
#include "qapi/qapi-dealloc-visitor.h"

/* Net bridge is currently not supported for W32. */
#if !defined(_WIN32)
# define CONFIG_NET_BRIDGE
#endif

static QTAILQ_HEAD(, NetClientState) net_clients;

int default_net = 1;

/***********************************************************/
/* network device redirectors */

#if defined(DEBUG_NET)
static void hex_dump(FILE *f, const uint8_t *buf, int size)
{
    int len, i, j, c;

    for(i=0;i<size;i+=16) {
        len = size - i;
        if (len > 16)
            len = 16;
        fprintf(f, "%08x ", i);
        for(j=0;j<16;j++) {
            if (j < len)
                fprintf(f, " %02x", buf[i+j]);
            else
                fprintf(f, "   ");
        }
        fprintf(f, " ");
        for(j=0;j<len;j++) {
            c = buf[i+j];
            if (c < ' ' || c > '~')
                c = '.';
            fprintf(f, "%c", c);
        }
        fprintf(f, "\n");
    }
}
#endif

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

int parse_host_port(struct sockaddr_in *saddr, const char *str)
{
    char buf[512];
    struct hostent *he;
    const char *p, *r;
    int port;

    p = str;
    if (get_str_sep(buf, sizeof(buf), &p, ':') < 0)
        return -1;
    saddr->sin_family = AF_INET;
    if (buf[0] == '\0') {
        saddr->sin_addr.s_addr = 0;
    } else {
        if (qemu_isdigit(buf[0])) {
            if (!inet_aton(buf, &saddr->sin_addr))
                return -1;
        } else {
            if ((he = gethostbyname(buf)) == NULL)
                return - 1;
            saddr->sin_addr = *(struct in_addr *)he->h_addr;
        }
    }
    port = strtol(p, (char **)&r, 0);
    if (r == p)
        return -1;
    saddr->sin_port = htons(port);
    return 0;
}

void qemu_format_nic_info_str(NetClientState *nc, uint8_t macaddr[6])
{
    snprintf(nc->info_str, sizeof(nc->info_str),
             "model=%s,macaddr=%02x:%02x:%02x:%02x:%02x:%02x",
             nc->model,
             macaddr[0], macaddr[1], macaddr[2],
             macaddr[3], macaddr[4], macaddr[5]);
}

void qemu_macaddr_default_if_unset(MACAddr *macaddr)
{
    static int index = 0;
    static const MACAddr zero = { .a = { 0,0,0,0,0,0 } };

    if (memcmp(macaddr, &zero, sizeof(zero)) != 0)
        return;
    macaddr->a[0] = 0x52;
    macaddr->a[1] = 0x54;
    macaddr->a[2] = 0x00;
    macaddr->a[3] = 0x12;
    macaddr->a[4] = 0x34;
    macaddr->a[5] = 0x56 + index++;
}

/**
 * Generate a name for net client
 *
 * Only net clients created with the legacy -net option need this.  Naming is
 * mandatory for net clients created with -netdev.
 */
static char *assign_name(NetClientState *nc1, const char *model)
{
    NetClientState *nc;
    char buf[256];
    int id = 0;

    QTAILQ_FOREACH(nc, &net_clients, next) {
        if (nc == nc1) {
            continue;
        }
        /* For compatibility only bump id for net clients on a vlan */
        if (strcmp(nc->model, model) == 0 &&
            net_hub_id_for_client(nc, NULL) == 0) {
            id++;
        }
    }

    snprintf(buf, sizeof(buf), "%s.%d", model, id);

    return g_strdup(buf);
}

NetClientState *qemu_new_net_client(NetClientInfo *info,
                                    NetClientState *peer,
                                    const char *model,
                                    const char *name)
{
    NetClientState *nc;

    assert(info->size >= sizeof(NetClientState));

    nc = g_malloc0(info->size);

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

    nc->send_queue = qemu_new_net_queue(nc);

    return nc;
}

NICState *qemu_new_nic(NetClientInfo *info,
                       NICConf *conf,
                       const char *model,
                       const char *name,
                       void *opaque)
{
    NetClientState *nc;
    NICState *nic;

    assert(info->type == NET_CLIENT_OPTIONS_KIND_NIC);
    assert(info->size >= sizeof(NICState));

    nc = qemu_new_net_client(info, conf->peer, model, name);

    nic = DO_UPCAST(NICState, nc, nc);
    nic->conf = conf;
    nic->opaque = opaque;

    return nic;
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
    if (nc->send_queue) {
        qemu_del_net_queue(nc->send_queue);
    }
    if (nc->peer) {
        nc->peer->peer = NULL;
    }
    g_free(nc->name);
    g_free(nc->model);
    g_free(nc);
}

void qemu_del_net_client(NetClientState *nc)
{
    /* If there is a peer NIC, delete and cleanup client, but do not free. */
    if (nc->peer && nc->peer->info->type == NET_CLIENT_OPTIONS_KIND_NIC) {
        NICState *nic = DO_UPCAST(NICState, nc, nc->peer);
        if (nic->peer_deleted) {
            return;
        }
        nic->peer_deleted = true;
        /* Let NIC know peer is gone. */
        nc->peer->link_down = true;
        if (nc->peer->info->link_status_changed) {
            nc->peer->info->link_status_changed(nc->peer);
        }
        qemu_cleanup_net_client(nc);
        return;
    }

    /* If this is a peer NIC and peer has already been deleted, free it now. */
    if (nc->peer && nc->info->type == NET_CLIENT_OPTIONS_KIND_NIC) {
        NICState *nic = DO_UPCAST(NICState, nc, nc);
        if (nic->peer_deleted) {
            qemu_free_net_client(nc->peer);
        }
    }

    qemu_cleanup_net_client(nc);
    qemu_free_net_client(nc);
}

void qemu_foreach_nic(qemu_nic_foreach func, void *opaque)
{
    NetClientState *nc;

    QTAILQ_FOREACH(nc, &net_clients, next) {
        if (nc->info->type == NET_CLIENT_OPTIONS_KIND_NIC) {
            func(DO_UPCAST(NICState, nc, nc), opaque);
        }
    }
}

int qemu_can_send_packet(NetClientState *sender)
{
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

ssize_t qemu_deliver_packet(NetClientState *sender,
                            unsigned flags,
                            const uint8_t *data,
                            size_t size,
                            void *opaque)
{
    NetClientState *nc = opaque;
    ssize_t ret;

    if (nc->link_down) {
        return size;
    }

    if (nc->receive_disabled) {
        return 0;
    }

    if (flags & QEMU_NET_PACKET_FLAG_RAW && nc->info->receive_raw) {
        ret = nc->info->receive_raw(nc, data, size);
    } else {
        ret = nc->info->receive(nc, data, size);
    }

    if (ret == 0) {
        nc->receive_disabled = 1;
    };

    return ret;
}

void qemu_purge_queued_packets(NetClientState *nc)
{
    if (!nc->peer) {
        return;
    }

    qemu_net_queue_purge(nc->peer->send_queue, nc);
}

void qemu_flush_queued_packets(NetClientState *nc)
{
    nc->receive_disabled = 0;

    if (qemu_net_queue_flush(nc->send_queue)) {
        /* We emptied the queue successfully, signal to the IO thread to repoll
         * the file descriptor (for tap, for example).
         */
        qemu_notify_event();
    }
}

static ssize_t qemu_send_packet_async_with_flags(NetClientState *sender,
                                                 unsigned flags,
                                                 const uint8_t *buf, int size,
                                                 NetPacketSent *sent_cb)
{
    NetQueue *queue;

#ifdef DEBUG_NET
    printf("qemu_send_packet_async:\n");
    hex_dump(stdout, buf, size);
#endif

    if (sender->link_down || !sender->peer) {
        return size;
    }

    queue = sender->peer->send_queue;

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
                               int iovcnt)
{
    uint8_t buffer[4096];
    size_t offset;

    offset = iov_to_buf(iov, iovcnt, 0, buffer, sizeof(buffer));

    return nc->info->receive(nc, buffer, offset);
}

ssize_t qemu_deliver_packet_iov(NetClientState *sender,
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

    if (nc->info->receive_iov) {
        ret = nc->info->receive_iov(nc, iov, iovcnt);
    } else {
        ret = nc_sendv_compat(nc, iov, iovcnt);
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

    if (sender->link_down || !sender->peer) {
        return iov_size(iov, iovcnt);
    }

    queue = sender->peer->send_queue;

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
        if (nc->info->type == NET_CLIENT_OPTIONS_KIND_NIC)
            continue;
        if (!strcmp(nc->name, id)) {
            return nc;
        }
    }

    return NULL;
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

static int net_init_nic(const NetClientOptions *opts, const char *name,
                        NetClientState *peer)
{
    int idx;
    NICInfo *nd;
    const NetLegacyNicOptions *nic;

    assert(opts->kind == NET_CLIENT_OPTIONS_KIND_NIC);
    nic = opts->nic;

    idx = nic_get_free_idx();
    if (idx == -1 || nb_nics >= MAX_NICS) {
        error_report("Too Many NICs");
        return -1;
    }

    nd = &nd_table[idx];

    memset(nd, 0, sizeof(*nd));

    if (nic->has_netdev) {
        nd->netdev = qemu_find_netdev(nic->netdev);
        if (!nd->netdev) {
            error_report("netdev '%s' not found", nic->netdev);
            return -1;
        }
    } else {
        assert(peer);
        nd->netdev = peer;
    }
    if (name) {
        nd->name = g_strdup(name);
    }
    if (nic->has_model) {
        nd->model = g_strdup(nic->model);
    }
    if (nic->has_addr) {
        nd->devaddr = g_strdup(nic->addr);
    }

    if (nic->has_macaddr &&
        net_parse_macaddr(nd->macaddr.a, nic->macaddr) < 0) {
        error_report("invalid syntax for ethernet address");
        return -1;
    }
    qemu_macaddr_default_if_unset(&nd->macaddr);

    if (nic->has_vectors) {
        if (nic->vectors > 0x7ffffff) {
            error_report("invalid # of vectors: %"PRIu32, nic->vectors);
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


static int (* const net_client_init_fun[NET_CLIENT_OPTIONS_KIND_MAX])(
    const NetClientOptions *opts,
    const char *name,
    NetClientState *peer) = {
        [NET_CLIENT_OPTIONS_KIND_NIC]       = net_init_nic,
#ifdef CONFIG_SLIRP
        [NET_CLIENT_OPTIONS_KIND_USER]      = net_init_slirp,
#endif
        [NET_CLIENT_OPTIONS_KIND_TAP]       = net_init_tap,
        [NET_CLIENT_OPTIONS_KIND_SOCKET]    = net_init_socket,
#ifdef CONFIG_VDE
        [NET_CLIENT_OPTIONS_KIND_VDE]       = net_init_vde,
#endif
        [NET_CLIENT_OPTIONS_KIND_DUMP]      = net_init_dump,
#ifdef CONFIG_NET_BRIDGE
        [NET_CLIENT_OPTIONS_KIND_BRIDGE]    = net_init_bridge,
#endif
        [NET_CLIENT_OPTIONS_KIND_HUBPORT]   = net_init_hubport,
};


static int net_client_init1(const void *object, int is_netdev, Error **errp)
{
    union {
        const Netdev    *netdev;
        const NetLegacy *net;
    } u;
    const NetClientOptions *opts;
    const char *name;

    if (is_netdev) {
        u.netdev = object;
        opts = u.netdev->opts;
        name = u.netdev->id;

        switch (opts->kind) {
#ifdef CONFIG_SLIRP
        case NET_CLIENT_OPTIONS_KIND_USER:
#endif
        case NET_CLIENT_OPTIONS_KIND_TAP:
        case NET_CLIENT_OPTIONS_KIND_SOCKET:
#ifdef CONFIG_VDE
        case NET_CLIENT_OPTIONS_KIND_VDE:
#endif
#ifdef CONFIG_NET_BRIDGE
        case NET_CLIENT_OPTIONS_KIND_BRIDGE:
#endif
        case NET_CLIENT_OPTIONS_KIND_HUBPORT:
            break;

        default:
            error_set(errp, QERR_INVALID_PARAMETER_VALUE, "type",
                      "a netdev backend type");
            return -1;
        }
    } else {
        u.net = object;
        opts = u.net->opts;
        /* missing optional values have been initialized to "all bits zero" */
        name = u.net->has_id ? u.net->id : u.net->name;
    }

    if (net_client_init_fun[opts->kind]) {
        NetClientState *peer = NULL;

        /* Do not add to a vlan if it's a -netdev or a nic with a netdev=
         * parameter. */
        if (!is_netdev &&
            (opts->kind != NET_CLIENT_OPTIONS_KIND_NIC ||
             !opts->nic->has_netdev)) {
            peer = net_hub_add_port(u.net->has_vlan ? u.net->vlan : 0, NULL);
        }

        if (net_client_init_fun[opts->kind](opts, name, peer) < 0) {
            /* TODO push error reporting into init() methods */
            error_set(errp, QERR_DEVICE_INIT_FAILED,
                      NetClientOptionsKind_lookup[opts->kind]);
            return -1;
        }
    }
    return 0;
}


static void net_visit(Visitor *v, int is_netdev, void **object, Error **errp)
{
    if (is_netdev) {
        visit_type_Netdev(v, (Netdev **)object, NULL, errp);
    } else {
        visit_type_NetLegacy(v, (NetLegacy **)object, NULL, errp);
    }
}


int net_client_init(QemuOpts *opts, int is_netdev, Error **errp)
{
    void *object = NULL;
    Error *err = NULL;
    int ret = -1;

    {
        OptsVisitor *ov = opts_visitor_new(opts);

        net_visit(opts_get_visitor(ov), is_netdev, &object, &err);
        opts_visitor_cleanup(ov);
    }

    if (!err) {
        ret = net_client_init1(object, is_netdev, &err);
    }

    if (object) {
        QapiDeallocVisitor *dv = qapi_dealloc_visitor_new();

        net_visit(qapi_dealloc_get_visitor(dv), is_netdev, &object, NULL);
        qapi_dealloc_visitor_cleanup(dv);
    }

    error_propagate(errp, err);
    return ret;
}


static int net_host_check_device(const char *device)
{
    int i;
    const char *valid_param_list[] = { "tap", "socket", "dump"
#ifdef CONFIG_NET_BRIDGE
                                       , "bridge"
#endif
#ifdef CONFIG_SLIRP
                                       ,"user"
#endif
#ifdef CONFIG_VDE
                                       ,"vde"
#endif
    };
    for (i = 0; i < sizeof(valid_param_list) / sizeof(char *); i++) {
        if (!strncmp(valid_param_list[i], device,
                     strlen(valid_param_list[i])))
            return 1;
    }

    return 0;
}

void net_host_device_add(Monitor *mon, const QDict *qdict)
{
    const char *device = qdict_get_str(qdict, "device");
    const char *opts_str = qdict_get_try_str(qdict, "opts");
    Error *local_err = NULL;
    QemuOpts *opts;

    if (!net_host_check_device(device)) {
        monitor_printf(mon, "invalid host network device %s\n", device);
        return;
    }

    opts = qemu_opts_parse(qemu_find_opts("net"), opts_str ? opts_str : "", 0);
    if (!opts) {
        return;
    }

    qemu_opt_set(opts, "type", device);

    net_client_init(opts, 0, &local_err);
    if (error_is_set(&local_err)) {
        qerror_report_err(local_err);
        error_free(local_err);
        monitor_printf(mon, "adding host network device %s failed\n", device);
    }
}

void net_host_device_remove(Monitor *mon, const QDict *qdict)
{
    NetClientState *nc;
    int vlan_id = qdict_get_int(qdict, "vlan_id");
    const char *device = qdict_get_str(qdict, "device");

    nc = net_hub_find_client_by_name(vlan_id, device);
    if (!nc) {
        return;
    }
    if (!net_host_check_device(nc->model)) {
        monitor_printf(mon, "invalid host network device %s\n", device);
        return;
    }
    qemu_del_net_client(nc);
}

void netdev_add(QemuOpts *opts, Error **errp)
{
    net_client_init(opts, 1, errp);
}

int qmp_netdev_add(Monitor *mon, const QDict *qdict, QObject **ret)
{
    Error *local_err = NULL;
    QemuOptsList *opts_list;
    QemuOpts *opts;

    opts_list = qemu_find_opts_err("netdev", &local_err);
    if (error_is_set(&local_err)) {
        goto exit_err;
    }

    opts = qemu_opts_from_qdict(opts_list, qdict, &local_err);
    if (error_is_set(&local_err)) {
        goto exit_err;
    }

    netdev_add(opts, &local_err);
    if (error_is_set(&local_err)) {
        qemu_opts_del(opts);
        goto exit_err;
    }

    return 0;

exit_err:
    qerror_report_err(local_err);
    error_free(local_err);
    return -1;
}

void qmp_netdev_del(const char *id, Error **errp)
{
    NetClientState *nc;
    QemuOpts *opts;

    nc = qemu_find_netdev(id);
    if (!nc) {
        error_set(errp, QERR_DEVICE_NOT_FOUND, id);
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

void print_net_client(Monitor *mon, NetClientState *nc)
{
    monitor_printf(mon, "%s: type=%s,%s\n", nc->name,
                   NetClientOptionsKind_lookup[nc->info->type], nc->info_str);
}

void do_info_network(Monitor *mon)
{
    NetClientState *nc, *peer;
    NetClientOptionsKind type;

    net_hub_info(mon);

    QTAILQ_FOREACH(nc, &net_clients, next) {
        peer = nc->peer;
        type = nc->info->type;

        /* Skip if already printed in hub info */
        if (net_hub_id_for_client(nc, NULL) == 0) {
            continue;
        }

        if (!peer || type == NET_CLIENT_OPTIONS_KIND_NIC) {
            print_net_client(mon, nc);
        } /* else it's a netdev connected to a NIC, printed with the NIC */
        if (peer && type == NET_CLIENT_OPTIONS_KIND_NIC) {
            monitor_printf(mon, " \\ ");
            print_net_client(mon, peer);
        }
    }
}

void qmp_set_link(const char *name, bool up, Error **errp)
{
    NetClientState *nc = NULL;

    QTAILQ_FOREACH(nc, &net_clients, next) {
        if (!strcmp(nc->name, name)) {
            goto done;
        }
    }
done:
    if (!nc) {
        error_set(errp, QERR_DEVICE_NOT_FOUND, name);
        return;
    }

    nc->link_down = !up;

    if (nc->info->link_status_changed) {
        nc->info->link_status_changed(nc);
    }

    /* Notify peer. Don't update peer link status: this makes it possible to
     * disconnect from host network without notifying the guest.
     * FIXME: is disconnected link status change operation useful?
     *
     * Current behaviour is compatible with qemu vlans where there could be
     * multiple clients that can still communicate with each other in
     * disconnected mode. For now maintain this compatibility. */
    if (nc->peer && nc->peer->info->link_status_changed) {
        nc->peer->info->link_status_changed(nc->peer);
    }
}

void net_cleanup(void)
{
    NetClientState *nc, *next_vc;

    QTAILQ_FOREACH_SAFE(nc, &net_clients, next, next_vc) {
        qemu_del_net_client(nc);
    }
}

void net_check_clients(void)
{
    NetClientState *nc;
    int i;

    /* Don't warn about the default network setup that you get if
     * no command line -net or -netdev options are specified. There
     * are two cases that we would otherwise complain about:
     * (1) board doesn't support a NIC but the implicit "-net nic"
     * requested one
     * (2) CONFIG_SLIRP not set, in which case the implicit "-net nic"
     * sets up a nic that isn't connected to anything.
     */
    if (default_net) {
        return;
    }

    net_hub_check_clients();

    QTAILQ_FOREACH(nc, &net_clients, next) {
        if (!nc->peer) {
            fprintf(stderr, "Warning: %s %s has no peer\n",
                    nc->info->type == NET_CLIENT_OPTIONS_KIND_NIC ?
                    "nic" : "netdev", nc->name);
        }
    }

    /* Check that all NICs requested via -net nic actually got created.
     * NICs created via -device don't need to be checked here because
     * they are always instantiated.
     */
    for (i = 0; i < MAX_NICS; i++) {
        NICInfo *nd = &nd_table[i];
        if (nd->used && !nd->instantiated) {
            fprintf(stderr, "Warning: requested NIC (%s, model %s) "
                    "was not created (not supported by this machine?)\n",
                    nd->name ? nd->name : "anonymous",
                    nd->model ? nd->model : "unspecified");
        }
    }
}

static int net_init_client(QemuOpts *opts, void *dummy)
{
    Error *local_err = NULL;

    net_client_init(opts, 0, &local_err);
    if (error_is_set(&local_err)) {
        qerror_report_err(local_err);
        error_free(local_err);
        return -1;
    }

    return 0;
}

static int net_init_netdev(QemuOpts *opts, void *dummy)
{
    Error *local_err = NULL;
    int ret;

    ret = net_client_init(opts, 1, &local_err);
    if (error_is_set(&local_err)) {
        qerror_report_err(local_err);
        error_free(local_err);
        return -1;
    }

    return ret;
}

int net_init_clients(void)
{
    QemuOptsList *net = qemu_find_opts("net");

    if (default_net) {
        /* if no clients, we use a default config */
        qemu_opts_set(net, NULL, "type", "nic");
#ifdef CONFIG_SLIRP
        qemu_opts_set(net, NULL, "type", "user");
#endif
    }

    QTAILQ_INIT(&net_clients);

    if (qemu_opts_foreach(qemu_find_opts("netdev"), net_init_netdev, NULL, 1) == -1)
        return -1;

    if (qemu_opts_foreach(net, net_init_client, NULL, 1) == -1) {
        return -1;
    }

    return 0;
}

int net_client_parse(QemuOptsList *opts_list, const char *optarg)
{
#if defined(CONFIG_SLIRP)
    int ret;
    if (net_slirp_parse_legacy(opts_list, optarg, &ret)) {
        return ret;
    }
#endif

    if (!qemu_opts_parse(opts_list, optarg, 1)) {
        return -1;
    }

    default_net = 0;
    return 0;
}

/* From FreeBSD */
/* XXX: optimize */
unsigned compute_mcast_idx(const uint8_t *ep)
{
    uint32_t crc;
    int carry, i, j;
    uint8_t b;

    crc = 0xffffffff;
    for (i = 0; i < 6; i++) {
        b = *ep++;
        for (j = 0; j < 8; j++) {
            carry = ((crc & 0x80000000L) ? 1 : 0) ^ (b & 0x01);
            crc <<= 1;
            b >>= 1;
            if (carry) {
                crc = ((crc ^ POLYNOMIAL) | carry);
            }
        }
    }
    return crc >> 26;
}
