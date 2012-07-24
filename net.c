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
#include "net.h"

#include "config-host.h"

#include "net/tap.h"
#include "net/socket.h"
#include "net/dump.h"
#include "net/slirp.h"
#include "net/vde.h"
#include "net/hub.h"
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

void qemu_format_nic_info_str(NetClientState *vc, uint8_t macaddr[6])
{
    snprintf(vc->info_str, sizeof(vc->info_str),
             "model=%s,macaddr=%02x:%02x:%02x:%02x:%02x:%02x",
             vc->model,
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
static char *assign_name(NetClientState *vc1, const char *model)
{
    NetClientState *vc;
    char buf[256];
    int id = 0;

    QTAILQ_FOREACH(vc, &net_clients, next) {
        if (vc == vc1) {
            continue;
        }
        /* For compatibility only bump id for net clients on a vlan */
        if (strcmp(vc->model, model) == 0 &&
            net_hub_id_for_client(vc, NULL) == 0) {
            id++;
        }
    }

    snprintf(buf, sizeof(buf), "%s.%d", model, id);

    return g_strdup(buf);
}

static ssize_t qemu_deliver_packet(NetClientState *sender,
                                   unsigned flags,
                                   const uint8_t *data,
                                   size_t size,
                                   void *opaque);
static ssize_t qemu_deliver_packet_iov(NetClientState *sender,
                                       unsigned flags,
                                       const struct iovec *iov,
                                       int iovcnt,
                                       void *opaque);

NetClientState *qemu_new_net_client(NetClientInfo *info,
                                    NetClientState *peer,
                                    const char *model,
                                    const char *name)
{
    NetClientState *vc;

    assert(info->size >= sizeof(NetClientState));

    vc = g_malloc0(info->size);

    vc->info = info;
    vc->model = g_strdup(model);
    if (name) {
        vc->name = g_strdup(name);
    } else {
        vc->name = assign_name(vc, model);
    }

    if (peer) {
        assert(!peer->peer);
        vc->peer = peer;
        peer->peer = vc;
    }
    QTAILQ_INSERT_TAIL(&net_clients, vc, next);

    vc->send_queue = qemu_new_net_queue(qemu_deliver_packet,
                                        qemu_deliver_packet_iov,
                                        vc);

    return vc;
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

static void qemu_cleanup_vlan_client(NetClientState *vc)
{
    QTAILQ_REMOVE(&net_clients, vc, next);

    if (vc->info->cleanup) {
        vc->info->cleanup(vc);
    }
}

static void qemu_free_vlan_client(NetClientState *vc)
{
    if (vc->send_queue) {
        qemu_del_net_queue(vc->send_queue);
    }
    if (vc->peer) {
        vc->peer->peer = NULL;
    }
    g_free(vc->name);
    g_free(vc->model);
    g_free(vc);
}

void qemu_del_vlan_client(NetClientState *vc)
{
    /* If there is a peer NIC, delete and cleanup client, but do not free. */
    if (vc->peer && vc->peer->info->type == NET_CLIENT_OPTIONS_KIND_NIC) {
        NICState *nic = DO_UPCAST(NICState, nc, vc->peer);
        if (nic->peer_deleted) {
            return;
        }
        nic->peer_deleted = true;
        /* Let NIC know peer is gone. */
        vc->peer->link_down = true;
        if (vc->peer->info->link_status_changed) {
            vc->peer->info->link_status_changed(vc->peer);
        }
        qemu_cleanup_vlan_client(vc);
        return;
    }

    /* If this is a peer NIC and peer has already been deleted, free it now. */
    if (vc->peer && vc->info->type == NET_CLIENT_OPTIONS_KIND_NIC) {
        NICState *nic = DO_UPCAST(NICState, nc, vc);
        if (nic->peer_deleted) {
            qemu_free_vlan_client(vc->peer);
        }
    }

    qemu_cleanup_vlan_client(vc);
    qemu_free_vlan_client(vc);
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

static ssize_t qemu_deliver_packet(NetClientState *sender,
                                   unsigned flags,
                                   const uint8_t *data,
                                   size_t size,
                                   void *opaque)
{
    NetClientState *vc = opaque;
    ssize_t ret;

    if (vc->link_down) {
        return size;
    }

    if (vc->receive_disabled) {
        return 0;
    }

    if (flags & QEMU_NET_PACKET_FLAG_RAW && vc->info->receive_raw) {
        ret = vc->info->receive_raw(vc, data, size);
    } else {
        ret = vc->info->receive(vc, data, size);
    }

    if (ret == 0) {
        vc->receive_disabled = 1;
    };

    return ret;
}

void qemu_purge_queued_packets(NetClientState *vc)
{
    if (!vc->peer) {
        return;
    }

    qemu_net_queue_purge(vc->peer->send_queue, vc);
}

void qemu_flush_queued_packets(NetClientState *vc)
{
    vc->receive_disabled = 0;

    qemu_net_queue_flush(vc->send_queue);
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

void qemu_send_packet(NetClientState *vc, const uint8_t *buf, int size)
{
    qemu_send_packet_async(vc, buf, size, NULL);
}

ssize_t qemu_send_packet_raw(NetClientState *vc, const uint8_t *buf, int size)
{
    return qemu_send_packet_async_with_flags(vc, QEMU_NET_PACKET_FLAG_RAW,
                                             buf, size, NULL);
}

static ssize_t vc_sendv_compat(NetClientState *vc, const struct iovec *iov,
                               int iovcnt)
{
    uint8_t buffer[4096];
    size_t offset;

    offset = iov_to_buf(iov, iovcnt, 0, buffer, sizeof(buffer));

    return vc->info->receive(vc, buffer, offset);
}

static ssize_t qemu_deliver_packet_iov(NetClientState *sender,
                                       unsigned flags,
                                       const struct iovec *iov,
                                       int iovcnt,
                                       void *opaque)
{
    NetClientState *vc = opaque;

    if (vc->link_down) {
        return iov_size(iov, iovcnt);
    }

    if (vc->info->receive_iov) {
        return vc->info->receive_iov(vc, iov, iovcnt);
    } else {
        return vc_sendv_compat(vc, iov, iovcnt);
    }
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
qemu_sendv_packet(NetClientState *vc, const struct iovec *iov, int iovcnt)
{
    return qemu_sendv_packet_async(vc, iov, iovcnt, NULL);
}

NetClientState *qemu_find_netdev(const char *id)
{
    NetClientState *vc;

    QTAILQ_FOREACH(vc, &net_clients, next) {
        if (vc->info->type == NET_CLIENT_OPTIONS_KIND_NIC)
            continue;
        if (!strcmp(vc->name, id)) {
            return vc;
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

    if (!arg || strcmp(arg, "?"))
        return 0;

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

int net_handle_fd_param(Monitor *mon, const char *param)
{
    int fd;

    if (!qemu_isdigit(param[0]) && mon) {

        fd = monitor_get_fd(mon, param);
        if (fd == -1) {
            error_report("No file descriptor named %s found", param);
            return -1;
        }
    } else {
        fd = qemu_parse_fd(param);
    }

    return fd;
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
    NetClientState *vc;
    int vlan_id = qdict_get_int(qdict, "vlan_id");
    const char *device = qdict_get_str(qdict, "device");

    vc = net_hub_find_client_by_name(vlan_id, device);
    if (!vc) {
        return;
    }
    if (!net_host_check_device(vc->model)) {
        monitor_printf(mon, "invalid host network device %s\n", device);
        return;
    }
    qemu_del_vlan_client(vc);
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
    NetClientState *vc;

    vc = qemu_find_netdev(id);
    if (!vc) {
        error_set(errp, QERR_DEVICE_NOT_FOUND, id);
        return;
    }

    qemu_del_vlan_client(vc);
    qemu_opts_del(qemu_opts_find(qemu_find_opts_err("netdev", errp), id));
}

static void print_net_client(Monitor *mon, NetClientState *vc)
{
    monitor_printf(mon, "%s: type=%s,%s\n", vc->name,
                   NetClientOptionsKind_lookup[vc->info->type], vc->info_str);
}

void do_info_network(Monitor *mon)
{
    NetClientState *vc, *peer;
    NetClientOptionsKind type;

    monitor_printf(mon, "Devices not on any VLAN:\n");
    QTAILQ_FOREACH(vc, &net_clients, next) {
        peer = vc->peer;
        type = vc->info->type;
        if (!peer || type == NET_CLIENT_OPTIONS_KIND_NIC) {
            monitor_printf(mon, "  ");
            print_net_client(mon, vc);
        } /* else it's a netdev connected to a NIC, printed with the NIC */
        if (peer && type == NET_CLIENT_OPTIONS_KIND_NIC) {
            monitor_printf(mon, "   \\ ");
            print_net_client(mon, peer);
        }
    }
    net_hub_info(mon);
}

void qmp_set_link(const char *name, bool up, Error **errp)
{
    NetClientState *vc = NULL;

    QTAILQ_FOREACH(vc, &net_clients, next) {
        if (!strcmp(vc->name, name)) {
            goto done;
        }
    }
done:
    if (!vc) {
        error_set(errp, QERR_DEVICE_NOT_FOUND, name);
        return;
    }

    vc->link_down = !up;

    if (vc->info->link_status_changed) {
        vc->info->link_status_changed(vc);
    }

    /* Notify peer. Don't update peer link status: this makes it possible to
     * disconnect from host network without notifying the guest.
     * FIXME: is disconnected link status change operation useful?
     *
     * Current behaviour is compatible with qemu vlans where there could be
     * multiple clients that can still communicate with each other in
     * disconnected mode. For now maintain this compatibility. */
    if (vc->peer && vc->peer->info->link_status_changed) {
        vc->peer->info->link_status_changed(vc->peer);
    }
}

void net_cleanup(void)
{
    NetClientState *vc, *next_vc;

    QTAILQ_FOREACH_SAFE(vc, &net_clients, next, next_vc) {
        qemu_del_vlan_client(vc);
    }
}

void net_check_clients(void)
{
    NetClientState *vc;
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

    QTAILQ_FOREACH(vc, &net_clients, next) {
        if (!vc->peer) {
            fprintf(stderr, "Warning: %s %s has no peer\n",
                    vc->info->type == NET_CLIENT_OPTIONS_KIND_NIC ? "nic" : "netdev",
                    vc->name);
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
