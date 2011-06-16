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
#include "net/util.h"
#include "monitor.h"
#include "qemu-common.h"
#include "qemu_socket.h"
#include "hw/qdev.h"
#include "iov.h"

static QTAILQ_HEAD(, VLANState) vlans;
static QTAILQ_HEAD(, VLANClientState) non_vlan_clients;

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

void qemu_format_nic_info_str(VLANClientState *vc, uint8_t macaddr[6])
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

static char *assign_name(VLANClientState *vc1, const char *model)
{
    VLANState *vlan;
    VLANClientState *vc;
    char buf[256];
    int id = 0;

    QTAILQ_FOREACH(vlan, &vlans, next) {
        QTAILQ_FOREACH(vc, &vlan->clients, next) {
            if (vc != vc1 && strcmp(vc->model, model) == 0) {
                id++;
            }
        }
    }

    QTAILQ_FOREACH(vc, &non_vlan_clients, next) {
        if (vc != vc1 && strcmp(vc->model, model) == 0) {
            id++;
        }
    }

    snprintf(buf, sizeof(buf), "%s.%d", model, id);

    return qemu_strdup(buf);
}

static ssize_t qemu_deliver_packet(VLANClientState *sender,
                                   unsigned flags,
                                   const uint8_t *data,
                                   size_t size,
                                   void *opaque);
static ssize_t qemu_deliver_packet_iov(VLANClientState *sender,
                                       unsigned flags,
                                       const struct iovec *iov,
                                       int iovcnt,
                                       void *opaque);

VLANClientState *qemu_new_net_client(NetClientInfo *info,
                                     VLANState *vlan,
                                     VLANClientState *peer,
                                     const char *model,
                                     const char *name)
{
    VLANClientState *vc;

    assert(info->size >= sizeof(VLANClientState));

    vc = qemu_mallocz(info->size);

    vc->info = info;
    vc->model = qemu_strdup(model);
    if (name) {
        vc->name = qemu_strdup(name);
    } else {
        vc->name = assign_name(vc, model);
    }

    if (vlan) {
        assert(!peer);
        vc->vlan = vlan;
        QTAILQ_INSERT_TAIL(&vc->vlan->clients, vc, next);
    } else {
        if (peer) {
            assert(!peer->peer);
            vc->peer = peer;
            peer->peer = vc;
        }
        QTAILQ_INSERT_TAIL(&non_vlan_clients, vc, next);

        vc->send_queue = qemu_new_net_queue(qemu_deliver_packet,
                                            qemu_deliver_packet_iov,
                                            vc);
    }

    return vc;
}

NICState *qemu_new_nic(NetClientInfo *info,
                       NICConf *conf,
                       const char *model,
                       const char *name,
                       void *opaque)
{
    VLANClientState *nc;
    NICState *nic;

    assert(info->type == NET_CLIENT_TYPE_NIC);
    assert(info->size >= sizeof(NICState));

    nc = qemu_new_net_client(info, conf->vlan, conf->peer, model, name);

    nic = DO_UPCAST(NICState, nc, nc);
    nic->conf = conf;
    nic->opaque = opaque;

    return nic;
}

static void qemu_cleanup_vlan_client(VLANClientState *vc)
{
    if (vc->vlan) {
        QTAILQ_REMOVE(&vc->vlan->clients, vc, next);
    } else {
        QTAILQ_REMOVE(&non_vlan_clients, vc, next);
    }

    if (vc->info->cleanup) {
        vc->info->cleanup(vc);
    }
}

static void qemu_free_vlan_client(VLANClientState *vc)
{
    if (!vc->vlan) {
        if (vc->send_queue) {
            qemu_del_net_queue(vc->send_queue);
        }
        if (vc->peer) {
            vc->peer->peer = NULL;
        }
    }
    qemu_free(vc->name);
    qemu_free(vc->model);
    qemu_free(vc);
}

void qemu_del_vlan_client(VLANClientState *vc)
{
    /* If there is a peer NIC, delete and cleanup client, but do not free. */
    if (!vc->vlan && vc->peer && vc->peer->info->type == NET_CLIENT_TYPE_NIC) {
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
    if (!vc->vlan && vc->peer && vc->info->type == NET_CLIENT_TYPE_NIC) {
        NICState *nic = DO_UPCAST(NICState, nc, vc);
        if (nic->peer_deleted) {
            qemu_free_vlan_client(vc->peer);
        }
    }

    qemu_cleanup_vlan_client(vc);
    qemu_free_vlan_client(vc);
}

VLANClientState *
qemu_find_vlan_client_by_name(Monitor *mon, int vlan_id,
                              const char *client_str)
{
    VLANState *vlan;
    VLANClientState *vc;

    vlan = qemu_find_vlan(vlan_id, 0);
    if (!vlan) {
        monitor_printf(mon, "unknown VLAN %d\n", vlan_id);
        return NULL;
    }

    QTAILQ_FOREACH(vc, &vlan->clients, next) {
        if (!strcmp(vc->name, client_str)) {
            break;
        }
    }
    if (!vc) {
        monitor_printf(mon, "can't find device %s on VLAN %d\n",
                       client_str, vlan_id);
    }

    return vc;
}

void qemu_foreach_nic(qemu_nic_foreach func, void *opaque)
{
    VLANClientState *nc;
    VLANState *vlan;

    QTAILQ_FOREACH(nc, &non_vlan_clients, next) {
        if (nc->info->type == NET_CLIENT_TYPE_NIC) {
            func(DO_UPCAST(NICState, nc, nc), opaque);
        }
    }

    QTAILQ_FOREACH(vlan, &vlans, next) {
        QTAILQ_FOREACH(nc, &vlan->clients, next) {
            if (nc->info->type == NET_CLIENT_TYPE_NIC) {
                func(DO_UPCAST(NICState, nc, nc), opaque);
            }
        }
    }
}

int qemu_can_send_packet(VLANClientState *sender)
{
    VLANState *vlan = sender->vlan;
    VLANClientState *vc;

    if (sender->peer) {
        if (sender->peer->receive_disabled) {
            return 0;
        } else if (sender->peer->info->can_receive &&
                   !sender->peer->info->can_receive(sender->peer)) {
            return 0;
        } else {
            return 1;
        }
    }

    if (!sender->vlan) {
        return 1;
    }

    QTAILQ_FOREACH(vc, &vlan->clients, next) {
        if (vc == sender) {
            continue;
        }

        /* no can_receive() handler, they can always receive */
        if (vc->info->can_receive && !vc->info->can_receive(vc)) {
            return 0;
        }
    }
    return 1;
}

static ssize_t qemu_deliver_packet(VLANClientState *sender,
                                   unsigned flags,
                                   const uint8_t *data,
                                   size_t size,
                                   void *opaque)
{
    VLANClientState *vc = opaque;
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

static ssize_t qemu_vlan_deliver_packet(VLANClientState *sender,
                                        unsigned flags,
                                        const uint8_t *buf,
                                        size_t size,
                                        void *opaque)
{
    VLANState *vlan = opaque;
    VLANClientState *vc;
    ssize_t ret = -1;

    QTAILQ_FOREACH(vc, &vlan->clients, next) {
        ssize_t len;

        if (vc == sender) {
            continue;
        }

        if (vc->link_down) {
            ret = size;
            continue;
        }

        if (vc->receive_disabled) {
            ret = 0;
            continue;
        }

        if (flags & QEMU_NET_PACKET_FLAG_RAW && vc->info->receive_raw) {
            len = vc->info->receive_raw(vc, buf, size);
        } else {
            len = vc->info->receive(vc, buf, size);
        }

        if (len == 0) {
            vc->receive_disabled = 1;
        }

        ret = (ret >= 0) ? ret : len;

    }

    return ret;
}

void qemu_purge_queued_packets(VLANClientState *vc)
{
    NetQueue *queue;

    if (!vc->peer && !vc->vlan) {
        return;
    }

    if (vc->peer) {
        queue = vc->peer->send_queue;
    } else {
        queue = vc->vlan->send_queue;
    }

    qemu_net_queue_purge(queue, vc);
}

void qemu_flush_queued_packets(VLANClientState *vc)
{
    NetQueue *queue;

    vc->receive_disabled = 0;

    if (vc->vlan) {
        queue = vc->vlan->send_queue;
    } else {
        queue = vc->send_queue;
    }

    qemu_net_queue_flush(queue);
}

static ssize_t qemu_send_packet_async_with_flags(VLANClientState *sender,
                                                 unsigned flags,
                                                 const uint8_t *buf, int size,
                                                 NetPacketSent *sent_cb)
{
    NetQueue *queue;

#ifdef DEBUG_NET
    printf("qemu_send_packet_async:\n");
    hex_dump(stdout, buf, size);
#endif

    if (sender->link_down || (!sender->peer && !sender->vlan)) {
        return size;
    }

    if (sender->peer) {
        queue = sender->peer->send_queue;
    } else {
        queue = sender->vlan->send_queue;
    }

    return qemu_net_queue_send(queue, sender, flags, buf, size, sent_cb);
}

ssize_t qemu_send_packet_async(VLANClientState *sender,
                               const uint8_t *buf, int size,
                               NetPacketSent *sent_cb)
{
    return qemu_send_packet_async_with_flags(sender, QEMU_NET_PACKET_FLAG_NONE,
                                             buf, size, sent_cb);
}

void qemu_send_packet(VLANClientState *vc, const uint8_t *buf, int size)
{
    qemu_send_packet_async(vc, buf, size, NULL);
}

ssize_t qemu_send_packet_raw(VLANClientState *vc, const uint8_t *buf, int size)
{
    return qemu_send_packet_async_with_flags(vc, QEMU_NET_PACKET_FLAG_RAW,
                                             buf, size, NULL);
}

static ssize_t vc_sendv_compat(VLANClientState *vc, const struct iovec *iov,
                               int iovcnt)
{
    uint8_t buffer[4096];
    size_t offset;

    offset = iov_to_buf(iov, iovcnt, buffer, 0, sizeof(buffer));

    return vc->info->receive(vc, buffer, offset);
}

static ssize_t qemu_deliver_packet_iov(VLANClientState *sender,
                                       unsigned flags,
                                       const struct iovec *iov,
                                       int iovcnt,
                                       void *opaque)
{
    VLANClientState *vc = opaque;

    if (vc->link_down) {
        return iov_size(iov, iovcnt);
    }

    if (vc->info->receive_iov) {
        return vc->info->receive_iov(vc, iov, iovcnt);
    } else {
        return vc_sendv_compat(vc, iov, iovcnt);
    }
}

static ssize_t qemu_vlan_deliver_packet_iov(VLANClientState *sender,
                                            unsigned flags,
                                            const struct iovec *iov,
                                            int iovcnt,
                                            void *opaque)
{
    VLANState *vlan = opaque;
    VLANClientState *vc;
    ssize_t ret = -1;

    QTAILQ_FOREACH(vc, &vlan->clients, next) {
        ssize_t len;

        if (vc == sender) {
            continue;
        }

        if (vc->link_down) {
            ret = iov_size(iov, iovcnt);
            continue;
        }

        assert(!(flags & QEMU_NET_PACKET_FLAG_RAW));

        if (vc->info->receive_iov) {
            len = vc->info->receive_iov(vc, iov, iovcnt);
        } else {
            len = vc_sendv_compat(vc, iov, iovcnt);
        }

        ret = (ret >= 0) ? ret : len;
    }

    return ret;
}

ssize_t qemu_sendv_packet_async(VLANClientState *sender,
                                const struct iovec *iov, int iovcnt,
                                NetPacketSent *sent_cb)
{
    NetQueue *queue;

    if (sender->link_down || (!sender->peer && !sender->vlan)) {
        return iov_size(iov, iovcnt);
    }

    if (sender->peer) {
        queue = sender->peer->send_queue;
    } else {
        queue = sender->vlan->send_queue;
    }

    return qemu_net_queue_send_iov(queue, sender,
                                   QEMU_NET_PACKET_FLAG_NONE,
                                   iov, iovcnt, sent_cb);
}

ssize_t
qemu_sendv_packet(VLANClientState *vc, const struct iovec *iov, int iovcnt)
{
    return qemu_sendv_packet_async(vc, iov, iovcnt, NULL);
}

/* find or alloc a new VLAN */
VLANState *qemu_find_vlan(int id, int allocate)
{
    VLANState *vlan;

    QTAILQ_FOREACH(vlan, &vlans, next) {
        if (vlan->id == id) {
            return vlan;
        }
    }

    if (!allocate) {
        return NULL;
    }

    vlan = qemu_mallocz(sizeof(VLANState));
    vlan->id = id;
    QTAILQ_INIT(&vlan->clients);

    vlan->send_queue = qemu_new_net_queue(qemu_vlan_deliver_packet,
                                          qemu_vlan_deliver_packet_iov,
                                          vlan);

    QTAILQ_INSERT_TAIL(&vlans, vlan, next);

    return vlan;
}

VLANClientState *qemu_find_netdev(const char *id)
{
    VLANClientState *vc;

    QTAILQ_FOREACH(vc, &non_vlan_clients, next) {
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
        nd->model = qemu_strdup(default_model);

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
        char *endptr = NULL;

        fd = strtol(param, &endptr, 10);
        if (*endptr || (fd == 0 && param == endptr)) {
            return -1;
        }
    }

    return fd;
}

static int net_init_nic(QemuOpts *opts,
                        Monitor *mon,
                        const char *name,
                        VLANState *vlan)
{
    int idx;
    NICInfo *nd;
    const char *netdev;

    idx = nic_get_free_idx();
    if (idx == -1 || nb_nics >= MAX_NICS) {
        error_report("Too Many NICs");
        return -1;
    }

    nd = &nd_table[idx];

    memset(nd, 0, sizeof(*nd));

    if ((netdev = qemu_opt_get(opts, "netdev"))) {
        nd->netdev = qemu_find_netdev(netdev);
        if (!nd->netdev) {
            error_report("netdev '%s' not found", netdev);
            return -1;
        }
    } else {
        assert(vlan);
        nd->vlan = vlan;
    }
    if (name) {
        nd->name = qemu_strdup(name);
    }
    if (qemu_opt_get(opts, "model")) {
        nd->model = qemu_strdup(qemu_opt_get(opts, "model"));
    }
    if (qemu_opt_get(opts, "addr")) {
        nd->devaddr = qemu_strdup(qemu_opt_get(opts, "addr"));
    }

    nd->macaddr[0] = 0x52;
    nd->macaddr[1] = 0x54;
    nd->macaddr[2] = 0x00;
    nd->macaddr[3] = 0x12;
    nd->macaddr[4] = 0x34;
    nd->macaddr[5] = 0x56 + idx;

    if (qemu_opt_get(opts, "macaddr") &&
        net_parse_macaddr(nd->macaddr, qemu_opt_get(opts, "macaddr")) < 0) {
        error_report("invalid syntax for ethernet address");
        return -1;
    }

    nd->nvectors = qemu_opt_get_number(opts, "vectors",
                                       DEV_NVECTORS_UNSPECIFIED);
    if (nd->nvectors != DEV_NVECTORS_UNSPECIFIED &&
        (nd->nvectors < 0 || nd->nvectors > 0x7ffffff)) {
        error_report("invalid # of vectors: %d", nd->nvectors);
        return -1;
    }

    nd->used = 1;
    nb_nics++;

    return idx;
}

#define NET_COMMON_PARAMS_DESC                     \
    {                                              \
        .name = "type",                            \
        .type = QEMU_OPT_STRING,                   \
        .help = "net client type (nic, tap etc.)", \
     }, {                                          \
        .name = "vlan",                            \
        .type = QEMU_OPT_NUMBER,                   \
        .help = "vlan number",                     \
     }, {                                          \
        .name = "name",                            \
        .type = QEMU_OPT_STRING,                   \
        .help = "identifier for monitor commands", \
     }

typedef int (*net_client_init_func)(QemuOpts *opts,
                                    Monitor *mon,
                                    const char *name,
                                    VLANState *vlan);

/* magic number, but compiler will warn if too small */
#define NET_MAX_DESC 20

static const struct {
    const char *type;
    net_client_init_func init;
    QemuOptDesc desc[NET_MAX_DESC];
} net_client_types[] = {
    {
        .type = "none",
        .desc = {
            NET_COMMON_PARAMS_DESC,
            { /* end of list */ }
        },
    }, {
        .type = "nic",
        .init = net_init_nic,
        .desc = {
            NET_COMMON_PARAMS_DESC,
            {
                .name = "netdev",
                .type = QEMU_OPT_STRING,
                .help = "id of -netdev to connect to",
            },
            {
                .name = "macaddr",
                .type = QEMU_OPT_STRING,
                .help = "MAC address",
            }, {
                .name = "model",
                .type = QEMU_OPT_STRING,
                .help = "device model (e1000, rtl8139, virtio etc.)",
            }, {
                .name = "addr",
                .type = QEMU_OPT_STRING,
                .help = "PCI device address",
            }, {
                .name = "vectors",
                .type = QEMU_OPT_NUMBER,
                .help = "number of MSI-x vectors, 0 to disable MSI-X",
            },
            { /* end of list */ }
        },
#ifdef CONFIG_SLIRP
    }, {
        .type = "user",
        .init = net_init_slirp,
        .desc = {
            NET_COMMON_PARAMS_DESC,
            {
                .name = "hostname",
                .type = QEMU_OPT_STRING,
                .help = "client hostname reported by the builtin DHCP server",
            }, {
                .name = "restrict",
                .type = QEMU_OPT_STRING,
                .help = "isolate the guest from the host (y|yes|n|no)",
            }, {
                .name = "ip",
                .type = QEMU_OPT_STRING,
                .help = "legacy parameter, use net= instead",
            }, {
                .name = "net",
                .type = QEMU_OPT_STRING,
                .help = "IP address and optional netmask",
            }, {
                .name = "host",
                .type = QEMU_OPT_STRING,
                .help = "guest-visible address of the host",
            }, {
                .name = "tftp",
                .type = QEMU_OPT_STRING,
                .help = "root directory of the built-in TFTP server",
            }, {
                .name = "bootfile",
                .type = QEMU_OPT_STRING,
                .help = "BOOTP filename, for use with tftp=",
            }, {
                .name = "dhcpstart",
                .type = QEMU_OPT_STRING,
                .help = "the first of the 16 IPs the built-in DHCP server can assign",
            }, {
                .name = "dns",
                .type = QEMU_OPT_STRING,
                .help = "guest-visible address of the virtual nameserver",
            }, {
                .name = "smb",
                .type = QEMU_OPT_STRING,
                .help = "root directory of the built-in SMB server",
            }, {
                .name = "smbserver",
                .type = QEMU_OPT_STRING,
                .help = "IP address of the built-in SMB server",
            }, {
                .name = "hostfwd",
                .type = QEMU_OPT_STRING,
                .help = "guest port number to forward incoming TCP or UDP connections",
            }, {
                .name = "guestfwd",
                .type = QEMU_OPT_STRING,
                .help = "IP address and port to forward guest TCP connections",
            },
            { /* end of list */ }
        },
#endif
    }, {
        .type = "tap",
        .init = net_init_tap,
        .desc = {
            NET_COMMON_PARAMS_DESC,
            {
                .name = "ifname",
                .type = QEMU_OPT_STRING,
                .help = "interface name",
            },
#ifndef _WIN32
            {
                .name = "fd",
                .type = QEMU_OPT_STRING,
                .help = "file descriptor of an already opened tap",
            }, {
                .name = "script",
                .type = QEMU_OPT_STRING,
                .help = "script to initialize the interface",
            }, {
                .name = "downscript",
                .type = QEMU_OPT_STRING,
                .help = "script to shut down the interface",
            }, {
                .name = "sndbuf",
                .type = QEMU_OPT_SIZE,
                .help = "send buffer limit"
            }, {
                .name = "vnet_hdr",
                .type = QEMU_OPT_BOOL,
                .help = "enable the IFF_VNET_HDR flag on the tap interface"
            }, {
                .name = "vhost",
                .type = QEMU_OPT_BOOL,
                .help = "enable vhost-net network accelerator",
            }, {
                .name = "vhostfd",
                .type = QEMU_OPT_STRING,
                .help = "file descriptor of an already opened vhost net device",
            }, {
                .name = "vhostforce",
                .type = QEMU_OPT_BOOL,
                .help = "force vhost on for non-MSIX virtio guests",
        },
#endif /* _WIN32 */
            { /* end of list */ }
        },
    }, {
        .type = "socket",
        .init = net_init_socket,
        .desc = {
            NET_COMMON_PARAMS_DESC,
            {
                .name = "fd",
                .type = QEMU_OPT_STRING,
                .help = "file descriptor of an already opened socket",
            }, {
                .name = "listen",
                .type = QEMU_OPT_STRING,
                .help = "port number, and optional hostname, to listen on",
            }, {
                .name = "connect",
                .type = QEMU_OPT_STRING,
                .help = "port number, and optional hostname, to connect to",
            }, {
                .name = "mcast",
                .type = QEMU_OPT_STRING,
                .help = "UDP multicast address and port number",
            }, {
                .name = "localaddr",
                .type = QEMU_OPT_STRING,
                .help = "source address for multicast packets",
            },
            { /* end of list */ }
        },
#ifdef CONFIG_VDE
    }, {
        .type = "vde",
        .init = net_init_vde,
        .desc = {
            NET_COMMON_PARAMS_DESC,
            {
                .name = "sock",
                .type = QEMU_OPT_STRING,
                .help = "socket path",
            }, {
                .name = "port",
                .type = QEMU_OPT_NUMBER,
                .help = "port number",
            }, {
                .name = "group",
                .type = QEMU_OPT_STRING,
                .help = "group owner of socket",
            }, {
                .name = "mode",
                .type = QEMU_OPT_NUMBER,
                .help = "permissions for socket",
            },
            { /* end of list */ }
        },
#endif
    }, {
        .type = "dump",
        .init = net_init_dump,
        .desc = {
            NET_COMMON_PARAMS_DESC,
            {
                .name = "len",
                .type = QEMU_OPT_SIZE,
                .help = "per-packet size limit (64k default)",
            }, {
                .name = "file",
                .type = QEMU_OPT_STRING,
                .help = "dump file path (default is qemu-vlan0.pcap)",
            },
            { /* end of list */ }
        },
    },
    { /* end of list */ }
};

int net_client_init(Monitor *mon, QemuOpts *opts, int is_netdev)
{
    const char *name;
    const char *type;
    int i;

    type = qemu_opt_get(opts, "type");
    if (!type) {
        qerror_report(QERR_MISSING_PARAMETER, "type");
        return -1;
    }

    if (is_netdev) {
        if (strcmp(type, "tap") != 0 &&
#ifdef CONFIG_SLIRP
            strcmp(type, "user") != 0 &&
#endif
#ifdef CONFIG_VDE
            strcmp(type, "vde") != 0 &&
#endif
            strcmp(type, "socket") != 0) {
            qerror_report(QERR_INVALID_PARAMETER_VALUE, "type",
                          "a netdev backend type");
            return -1;
        }

        if (qemu_opt_get(opts, "vlan")) {
            qerror_report(QERR_INVALID_PARAMETER, "vlan");
            return -1;
        }
        if (qemu_opt_get(opts, "name")) {
            qerror_report(QERR_INVALID_PARAMETER, "name");
            return -1;
        }
        if (!qemu_opts_id(opts)) {
            qerror_report(QERR_MISSING_PARAMETER, "id");
            return -1;
        }
    }

    name = qemu_opts_id(opts);
    if (!name) {
        name = qemu_opt_get(opts, "name");
    }

    for (i = 0; net_client_types[i].type != NULL; i++) {
        if (!strcmp(net_client_types[i].type, type)) {
            VLANState *vlan = NULL;
            int ret;

            if (qemu_opts_validate(opts, &net_client_types[i].desc[0]) == -1) {
                return -1;
            }

            /* Do not add to a vlan if it's a -netdev or a nic with a
             * netdev= parameter. */
            if (!(is_netdev ||
                  (strcmp(type, "nic") == 0 && qemu_opt_get(opts, "netdev")))) {
                vlan = qemu_find_vlan(qemu_opt_get_number(opts, "vlan", 0), 1);
            }

            ret = 0;
            if (net_client_types[i].init) {
                ret = net_client_types[i].init(opts, mon, name, vlan);
                if (ret < 0) {
                    /* TODO push error reporting into init() methods */
                    qerror_report(QERR_DEVICE_INIT_FAILED, type);
                    return -1;
                }
            }
            return ret;
        }
    }

    qerror_report(QERR_INVALID_PARAMETER_VALUE, "type",
                  "a network client type");
    return -1;
}

static int net_host_check_device(const char *device)
{
    int i;
    const char *valid_param_list[] = { "tap", "socket", "dump"
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

    if (net_client_init(mon, opts, 0) < 0) {
        monitor_printf(mon, "adding host network device %s failed\n", device);
    }
}

void net_host_device_remove(Monitor *mon, const QDict *qdict)
{
    VLANClientState *vc;
    int vlan_id = qdict_get_int(qdict, "vlan_id");
    const char *device = qdict_get_str(qdict, "device");

    vc = qemu_find_vlan_client_by_name(mon, vlan_id, device);
    if (!vc) {
        return;
    }
    if (!net_host_check_device(vc->model)) {
        monitor_printf(mon, "invalid host network device %s\n", device);
        return;
    }
    qemu_del_vlan_client(vc);
}

int do_netdev_add(Monitor *mon, const QDict *qdict, QObject **ret_data)
{
    QemuOpts *opts;
    int res;

    opts = qemu_opts_from_qdict(qemu_find_opts("netdev"), qdict);
    if (!opts) {
        return -1;
    }

    res = net_client_init(mon, opts, 1);
    if (res < 0) {
        qemu_opts_del(opts);
    }

    return res;
}

int do_netdev_del(Monitor *mon, const QDict *qdict, QObject **ret_data)
{
    const char *id = qdict_get_str(qdict, "id");
    VLANClientState *vc;

    vc = qemu_find_netdev(id);
    if (!vc || vc->info->type == NET_CLIENT_TYPE_NIC) {
        qerror_report(QERR_DEVICE_NOT_FOUND, id);
        return -1;
    }
    qemu_del_vlan_client(vc);
    qemu_opts_del(qemu_opts_find(qemu_find_opts("netdev"), id));
    return 0;
}

void do_info_network(Monitor *mon)
{
    VLANState *vlan;
    VLANClientState *vc;

    QTAILQ_FOREACH(vlan, &vlans, next) {
        monitor_printf(mon, "VLAN %d devices:\n", vlan->id);

        QTAILQ_FOREACH(vc, &vlan->clients, next) {
            monitor_printf(mon, "  %s: %s\n", vc->name, vc->info_str);
        }
    }
    monitor_printf(mon, "Devices not on any VLAN:\n");
    QTAILQ_FOREACH(vc, &non_vlan_clients, next) {
        monitor_printf(mon, "  %s: %s", vc->name, vc->info_str);
        if (vc->peer) {
            monitor_printf(mon, " peer=%s", vc->peer->name);
        }
        monitor_printf(mon, "\n");
    }
}

int do_set_link(Monitor *mon, const QDict *qdict, QObject **ret_data)
{
    VLANState *vlan;
    VLANClientState *vc = NULL;
    const char *name = qdict_get_str(qdict, "name");
    int up = qdict_get_bool(qdict, "up");

    QTAILQ_FOREACH(vlan, &vlans, next) {
        QTAILQ_FOREACH(vc, &vlan->clients, next) {
            if (strcmp(vc->name, name) == 0) {
                goto done;
            }
        }
    }
    vc = qemu_find_netdev(name);
done:

    if (!vc) {
        qerror_report(QERR_DEVICE_NOT_FOUND, name);
        return -1;
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
    return 0;
}

void net_cleanup(void)
{
    VLANState *vlan;
    VLANClientState *vc, *next_vc;

    QTAILQ_FOREACH(vlan, &vlans, next) {
        QTAILQ_FOREACH_SAFE(vc, &vlan->clients, next, next_vc) {
            qemu_del_vlan_client(vc);
        }
    }

    QTAILQ_FOREACH_SAFE(vc, &non_vlan_clients, next, next_vc) {
        qemu_del_vlan_client(vc);
    }
}

void net_check_clients(void)
{
    VLANState *vlan;
    VLANClientState *vc;
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

    QTAILQ_FOREACH(vlan, &vlans, next) {
        int has_nic = 0, has_host_dev = 0;

        QTAILQ_FOREACH(vc, &vlan->clients, next) {
            switch (vc->info->type) {
            case NET_CLIENT_TYPE_NIC:
                has_nic = 1;
                break;
            case NET_CLIENT_TYPE_SLIRP:
            case NET_CLIENT_TYPE_TAP:
            case NET_CLIENT_TYPE_SOCKET:
            case NET_CLIENT_TYPE_VDE:
                has_host_dev = 1;
                break;
            default: ;
            }
        }
        if (has_host_dev && !has_nic)
            fprintf(stderr, "Warning: vlan %d with no nics\n", vlan->id);
        if (has_nic && !has_host_dev)
            fprintf(stderr,
                    "Warning: vlan %d is not connected to host network\n",
                    vlan->id);
    }
    QTAILQ_FOREACH(vc, &non_vlan_clients, next) {
        if (!vc->peer) {
            fprintf(stderr, "Warning: %s %s has no peer\n",
                    vc->info->type == NET_CLIENT_TYPE_NIC ? "nic" : "netdev",
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
    if (net_client_init(NULL, opts, 0) < 0)
        return -1;
    return 0;
}

static int net_init_netdev(QemuOpts *opts, void *dummy)
{
    return net_client_init(NULL, opts, 1);
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

    QTAILQ_INIT(&vlans);
    QTAILQ_INIT(&non_vlan_clients);

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
