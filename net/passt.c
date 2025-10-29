/*
 * passt network backend
 *
 * Copyright Red Hat
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include <glib/gstdio.h>
#include "qemu/error-report.h"
#include <gio/gio.h>
#include "net/net.h"
#include "clients.h"
#include "qapi/error.h"
#include "io/net-listener.h"
#include "chardev/char-fe.h"
#include "net/vhost_net.h"
#include "hw/virtio/vhost.h"
#include "hw/virtio/vhost-user.h"
#include "standard-headers/linux/virtio_net.h"
#include "stream_data.h"

#ifdef CONFIG_VHOST_USER
static const int user_feature_bits[] = {
    VIRTIO_F_NOTIFY_ON_EMPTY,
    VIRTIO_F_NOTIFICATION_DATA,
    VIRTIO_RING_F_INDIRECT_DESC,
    VIRTIO_RING_F_EVENT_IDX,

    VIRTIO_F_ANY_LAYOUT,
    VIRTIO_F_VERSION_1,
    VIRTIO_NET_F_CSUM,
    VIRTIO_NET_F_GUEST_CSUM,
    VIRTIO_NET_F_GSO,
    VIRTIO_NET_F_GUEST_TSO4,
    VIRTIO_NET_F_GUEST_TSO6,
    VIRTIO_NET_F_GUEST_ECN,
    VIRTIO_NET_F_GUEST_UFO,
    VIRTIO_NET_F_HOST_TSO4,
    VIRTIO_NET_F_HOST_TSO6,
    VIRTIO_NET_F_HOST_ECN,
    VIRTIO_NET_F_HOST_UFO,
    VIRTIO_NET_F_MRG_RXBUF,
    VIRTIO_NET_F_MTU,
    VIRTIO_F_IOMMU_PLATFORM,
    VIRTIO_F_RING_PACKED,
    VIRTIO_F_RING_RESET,
    VIRTIO_F_IN_ORDER,
    VIRTIO_NET_F_RSS,
    VIRTIO_NET_F_RSC_EXT,
    VIRTIO_NET_F_HASH_REPORT,
    VIRTIO_NET_F_GUEST_USO4,
    VIRTIO_NET_F_GUEST_USO6,
    VIRTIO_NET_F_HOST_USO,

    /* This bit implies RARP isn't sent by QEMU out of band */
    VIRTIO_NET_F_GUEST_ANNOUNCE,

    VIRTIO_NET_F_MQ,

    VHOST_INVALID_FEATURE_BIT
};
#endif

typedef struct NetPasstState {
    NetStreamData data;
    GPtrArray *args;
    gchar *pidfile;
    pid_t pid;
#ifdef CONFIG_VHOST_USER
    /* vhost user */
    VhostUserState *vhost_user;
    VHostNetState *vhost_net;
    CharFrontend vhost_chr;
    guint vhost_watch;
    uint64_t acked_features;
    bool started;
#endif
} NetPasstState;

static int net_passt_stream_start(NetPasstState *s, Error **errp);

static void net_passt_cleanup(NetClientState *nc)
{
    NetPasstState *s = DO_UPCAST(NetPasstState, data.nc, nc);

#ifdef CONFIG_VHOST_USER
    if (s->vhost_net) {
        vhost_net_cleanup(s->vhost_net);
        g_free(s->vhost_net);
        s->vhost_net = NULL;
    }
    if (s->vhost_watch) {
        g_source_remove(s->vhost_watch);
        s->vhost_watch = 0;
    }
    qemu_chr_fe_deinit(&s->vhost_chr, true);
    if (s->vhost_user) {
        vhost_user_cleanup(s->vhost_user);
        g_free(s->vhost_user);
        s->vhost_user = NULL;
    }
#endif

    kill(s->pid, SIGTERM);
    if (g_remove(s->pidfile) != 0) {
        warn_report("Failed to remove passt pidfile %s: %s",
                    s->pidfile, strerror(errno));
    }
    g_free(s->pidfile);
    g_ptr_array_free(s->args, TRUE);
}

static ssize_t net_passt_receive(NetClientState *nc, const uint8_t *buf,
                                  size_t size)
{
    NetStreamData *d = DO_UPCAST(NetStreamData, nc, nc);

    return net_stream_data_receive(d, buf, size);
}

static gboolean net_passt_send(QIOChannel *ioc, GIOCondition condition,
                                gpointer data)
{
    if (net_stream_data_send(ioc, condition, data) == G_SOURCE_REMOVE) {
        NetPasstState *s = DO_UPCAST(NetPasstState, data, data);
        Error *error = NULL;

        /* we need to restart passt */
        kill(s->pid, SIGTERM);
        if (net_passt_stream_start(s, &error) == -1) {
            error_report_err(error);
        }

        return G_SOURCE_REMOVE;
    }

    return G_SOURCE_CONTINUE;
}

#ifdef CONFIG_VHOST_USER
static int passt_set_vnet_endianness(NetClientState *nc, bool enable)
{
    assert(nc->info->type == NET_CLIENT_DRIVER_PASST);

    return 0;
}

static bool passt_has_vnet_hdr(NetClientState *nc)
{
    NetPasstState *s = DO_UPCAST(NetPasstState, data.nc, nc);

    assert(nc->info->type == NET_CLIENT_DRIVER_PASST);

    return s->vhost_user != NULL;
}

static bool passt_has_ufo(NetClientState *nc)
{
    NetPasstState *s = DO_UPCAST(NetPasstState, data.nc, nc);

    assert(nc->info->type == NET_CLIENT_DRIVER_PASST);

    return s->vhost_user != NULL;
}

static bool passt_check_peer_type(NetClientState *nc, ObjectClass *oc,
                                             Error **errp)
{
    NetPasstState *s = DO_UPCAST(NetPasstState, data.nc, nc);
    const char *driver = object_class_get_name(oc);

    assert(nc->info->type == NET_CLIENT_DRIVER_PASST);

    if (s->vhost_user == NULL) {
        return true;
    }

    if (!g_str_has_prefix(driver, "virtio-net-")) {
        error_setg(errp, "vhost-user requires frontend driver virtio-net-*");
        return false;
    }

    return true;
}

static struct vhost_net *passt_get_vhost_net(NetClientState *nc)
{
    NetPasstState *s = DO_UPCAST(NetPasstState, data.nc, nc);

    assert(nc->info->type == NET_CLIENT_DRIVER_PASST);

    return s->vhost_net;
}

static uint64_t passt_get_acked_features(NetClientState *nc)
{
    NetPasstState *s = DO_UPCAST(NetPasstState, data.nc, nc);

    assert(nc->info->type == NET_CLIENT_DRIVER_PASST);

    return s->acked_features;
}

static void passt_save_acked_features(NetClientState *nc)
{
    NetPasstState *s = DO_UPCAST(NetPasstState, data.nc, nc);

    assert(nc->info->type == NET_CLIENT_DRIVER_PASST);

    if (s->vhost_net) {
        uint64_t features = vhost_net_get_acked_features(s->vhost_net);
        if (features) {
            s->acked_features = features;
        }
    }
}
#endif

static NetClientInfo net_passt_info = {
    .type = NET_CLIENT_DRIVER_PASST,
    .size = sizeof(NetPasstState),
    .receive = net_passt_receive,
    .cleanup = net_passt_cleanup,
#ifdef CONFIG_VHOST_USER
    .has_vnet_hdr = passt_has_vnet_hdr,
    .has_ufo = passt_has_ufo,
    .set_vnet_be = passt_set_vnet_endianness,
    .set_vnet_le = passt_set_vnet_endianness,
    .check_peer_type = passt_check_peer_type,
    .get_vhost_net = passt_get_vhost_net,
#endif
};

static void net_passt_client_connected(QIOTask *task, gpointer opaque)
{
    NetPasstState *s = opaque;

    if (net_stream_data_client_connected(task, &s->data) == 0) {
        qemu_set_info_str(&s->data.nc, "stream,connected to pid %d", s->pid);
    }
}

static int net_passt_start_daemon(NetPasstState *s, int sock, Error **errp)
{
    g_autoptr(GSubprocess) daemon = NULL;
    g_autofree gchar *contents = NULL;
    g_autoptr(GError) error = NULL;
    GSubprocessLauncher *launcher;

    qemu_set_info_str(&s->data.nc, "launching passt");

    launcher = g_subprocess_launcher_new(G_SUBPROCESS_FLAGS_NONE);
    g_subprocess_launcher_take_fd(launcher, sock, 3);

    daemon =  g_subprocess_launcher_spawnv(launcher,
                                           (const gchar *const *)s->args->pdata,
                                           &error);
    g_object_unref(launcher);

    if (!daemon) {
        error_setg(errp, "Error creating daemon: %s", error->message);
        return -1;
    }

    if (!g_subprocess_wait(daemon, NULL, &error)) {
        error_setg(errp, "Error waiting for daemon: %s", error->message);
        return -1;
    }

    if (g_subprocess_get_if_exited(daemon) &&
        g_subprocess_get_exit_status(daemon)) {
        return -1;
    }

    if (!g_file_get_contents(s->pidfile, &contents, NULL, &error)) {
        error_setg(errp, "Cannot read passt pid: %s", error->message);
        return -1;
    }

    s->pid = (pid_t)g_ascii_strtoll(contents, NULL, 10);
    if (s->pid <= 0) {
        error_setg(errp, "File '%s' did not contain a valid PID.", s->pidfile);
        return -1;
    }

    return 0;
}

static int net_passt_stream_start(NetPasstState *s, Error **errp)
{
    QIOChannelSocket *sioc;
    SocketAddress *addr;
    int sv[2];

    if (socketpair(PF_UNIX, SOCK_STREAM, 0, sv) == -1) {
        error_setg_errno(errp, errno, "socketpair() failed");
        return -1;
    }

    /* connect to passt */
    qemu_set_info_str(&s->data.nc, "connecting to passt");

    /* create socket channel */
    sioc = qio_channel_socket_new();
    s->data.ioc = QIO_CHANNEL(sioc);
    s->data.nc.link_down = true;
    s->data.send = net_passt_send;

    addr = g_new0(SocketAddress, 1);
    addr->type = SOCKET_ADDRESS_TYPE_FD;
    addr->u.fd.str = g_strdup_printf("%d", sv[0]);

    qio_channel_socket_connect_async(sioc, addr,
                                     net_passt_client_connected, s,
                                     NULL, NULL);

    qapi_free_SocketAddress(addr);

    /* start passt */
    if (net_passt_start_daemon(s, sv[1], errp) == -1) {
        close(sv[0]);
        close(sv[1]);
        return -1;
    }
    close(sv[1]);

    return 0;
}

#ifdef CONFIG_VHOST_USER
static gboolean passt_vhost_user_watch(void *do_not_use, GIOCondition cond,
                                       void *opaque)
{
    NetPasstState *s = opaque;

    qemu_chr_fe_disconnect(&s->vhost_chr);

    return G_SOURCE_CONTINUE;
}

static void passt_vhost_user_event(void *opaque, QEMUChrEvent event);

static void chr_closed_bh(void *opaque)
{
    NetPasstState *s = opaque;

    passt_save_acked_features(&s->data.nc);

    net_client_set_link(&(NetClientState *){ &s->data.nc }, 1, false);

    qemu_chr_fe_set_handlers(&s->vhost_chr, NULL, NULL, passt_vhost_user_event,
                             NULL, s, NULL, true);
}

static void passt_vhost_user_stop(NetPasstState *s)
{
    passt_save_acked_features(&s->data.nc);
    vhost_net_cleanup(s->vhost_net);
}

static int passt_vhost_user_start(NetPasstState *s, VhostUserState *be)
{
    struct vhost_net *net = NULL;
    VhostNetOptions options;

    options.backend_type = VHOST_BACKEND_TYPE_USER;
    options.net_backend = &s->data.nc;
    options.opaque = be;
    options.busyloop_timeout = 0;
    options.nvqs = 2;
    options.feature_bits = user_feature_bits;
    options.max_tx_queue_size = VIRTQUEUE_MAX_SIZE;
    options.get_acked_features = passt_get_acked_features;
    options.save_acked_features = passt_save_acked_features;
    options.is_vhost_user = true;

    net = vhost_net_init(&options);
    if (!net) {
        error_report("failed to init passt vhost_net");
        passt_vhost_user_stop(s);
        return -1;
    }

    if (s->vhost_net) {
        vhost_net_cleanup(s->vhost_net);
        g_free(s->vhost_net);
    }
    s->vhost_net = net;

    return 0;
}

static void passt_vhost_user_event(void *opaque, QEMUChrEvent event)
{
    NetPasstState *s = opaque;

    switch (event) {
    case CHR_EVENT_OPENED:
        if (passt_vhost_user_start(s, s->vhost_user) < 0) {
            qemu_chr_fe_disconnect(&s->vhost_chr);
            return;
        }
        s->vhost_watch = qemu_chr_fe_add_watch(&s->vhost_chr, G_IO_HUP,
                                               passt_vhost_user_watch, s);
        net_client_set_link(&(NetClientState *){ &s->data.nc }, 1, true);
        s->started = true;
        break;
    case CHR_EVENT_CLOSED:
        if (s->vhost_watch) {
            AioContext *ctx = qemu_get_current_aio_context();

            g_source_remove(s->vhost_watch);
            s->vhost_watch = 0;
            qemu_chr_fe_set_handlers(&s->vhost_chr, NULL, NULL,  NULL, NULL,
                                     NULL, NULL, false);

            aio_bh_schedule_oneshot(ctx, chr_closed_bh, s);
        }
        break;
    case CHR_EVENT_BREAK:
    case CHR_EVENT_MUX_IN:
    case CHR_EVENT_MUX_OUT:
        /* Ignore */
        break;
    }
}

static int net_passt_vhost_user_init(NetPasstState *s, Error **errp)
{
    Chardev *chr;
    int sv[2];

    if (socketpair(PF_UNIX, SOCK_STREAM, 0, sv) == -1) {
        error_setg_errno(errp, errno, "socketpair() failed");
        return -1;
    }

    /* connect to passt */
    qemu_set_info_str(&s->data.nc, "connecting to passt");

    /* create chardev */

    chr = CHARDEV(object_new(TYPE_CHARDEV_SOCKET));
    if (!chr || qemu_chr_add_client(chr, sv[0]) == -1) {
        object_unref(OBJECT(chr));
        error_setg(errp, "Failed to make socket chardev");
        goto err;
    }

    s->vhost_user = g_new0(struct VhostUserState, 1);
    if (!qemu_chr_fe_init(&s->vhost_chr, chr, errp) ||
        !vhost_user_init(s->vhost_user, &s->vhost_chr, errp)) {
        goto err;
    }

    /* start passt */
    if (net_passt_start_daemon(s, sv[1], errp) == -1) {
        goto err;
    }

    do {
        if (qemu_chr_fe_wait_connected(&s->vhost_chr, errp) < 0) {
            goto err;
        }

        qemu_chr_fe_set_handlers(&s->vhost_chr, NULL, NULL,
                                 passt_vhost_user_event, NULL, s, NULL,
                                 true);
    } while (!s->started);

    qemu_set_info_str(&s->data.nc, "vhost-user,connected to pid %d", s->pid);

    close(sv[1]);
    return 0;
err:
    close(sv[0]);
    close(sv[1]);

    return -1;
}
#else
static int net_passt_vhost_user_init(NetPasstState *s, Error **errp)
{
    error_setg(errp, "vhost-user support has not been built");

    return -1;
}
#endif

static GPtrArray *net_passt_decode_args(const NetDevPasstOptions *passt,
                                        gchar *pidfile, Error **errp)
{
    GPtrArray *args = g_ptr_array_new_with_free_func(g_free);

    if (passt->path) {
        g_ptr_array_add(args, g_strdup(passt->path));
    } else {
        g_ptr_array_add(args, g_strdup("passt"));
    }

    if (passt->has_vhost_user && passt->vhost_user) {
        g_ptr_array_add(args, g_strdup("--vhost-user"));
    }

    /* by default, be quiet */
    if (!passt->has_quiet || passt->quiet) {
        g_ptr_array_add(args, g_strdup("--quiet"));
    }

    if (passt->has_mtu) {
        g_ptr_array_add(args, g_strdup("--mtu"));
        g_ptr_array_add(args, g_strdup_printf("%"PRId64, passt->mtu));
    }

    if (passt->address) {
        g_ptr_array_add(args, g_strdup("--address"));
        g_ptr_array_add(args, g_strdup(passt->address));
    }

    if (passt->netmask) {
        g_ptr_array_add(args, g_strdup("--netmask"));
        g_ptr_array_add(args, g_strdup(passt->netmask));
    }

    if (passt->mac) {
        g_ptr_array_add(args, g_strdup("--mac-addr"));
        g_ptr_array_add(args, g_strdup(passt->mac));
    }

    if (passt->gateway) {
        g_ptr_array_add(args, g_strdup("--gateway"));
        g_ptr_array_add(args, g_strdup(passt->gateway));
    }

    if (passt->interface) {
        g_ptr_array_add(args, g_strdup("--interface"));
        g_ptr_array_add(args, g_strdup(passt->interface));
    }

    if (passt->outbound) {
        g_ptr_array_add(args, g_strdup("--outbound"));
        g_ptr_array_add(args, g_strdup(passt->outbound));
    }

    if (passt->outbound_if4) {
        g_ptr_array_add(args, g_strdup("--outbound-if4"));
        g_ptr_array_add(args, g_strdup(passt->outbound_if4));
    }

    if (passt->outbound_if6) {
        g_ptr_array_add(args, g_strdup("--outbound-if6"));
        g_ptr_array_add(args, g_strdup(passt->outbound_if6));
    }

    if (passt->dns) {
        g_ptr_array_add(args, g_strdup("--dns"));
        g_ptr_array_add(args, g_strdup(passt->dns));
    }
    if (passt->fqdn) {
        g_ptr_array_add(args, g_strdup("--fqdn"));
        g_ptr_array_add(args, g_strdup(passt->fqdn));
    }

    if (passt->has_dhcp_dns && !passt->dhcp_dns) {
        g_ptr_array_add(args, g_strdup("--no-dhcp-dns"));
    }

    if (passt->has_dhcp_search && !passt->dhcp_search) {
        g_ptr_array_add(args, g_strdup("--no-dhcp-search"));
    }

    if (passt->map_host_loopback) {
        g_ptr_array_add(args, g_strdup("--map-host-loopback"));
        g_ptr_array_add(args, g_strdup(passt->map_host_loopback));
    }

    if (passt->map_guest_addr) {
        g_ptr_array_add(args, g_strdup("--map-guest-addr"));
        g_ptr_array_add(args, g_strdup(passt->map_guest_addr));
    }

    if (passt->dns_forward) {
        g_ptr_array_add(args, g_strdup("--dns-forward"));
        g_ptr_array_add(args, g_strdup(passt->dns_forward));
    }

    if (passt->dns_host) {
        g_ptr_array_add(args, g_strdup("--dns-host"));
        g_ptr_array_add(args, g_strdup(passt->dns_host));
    }

    if (passt->has_tcp && !passt->tcp) {
        g_ptr_array_add(args, g_strdup("--no-tcp"));
    }

    if (passt->has_udp && !passt->udp) {
        g_ptr_array_add(args, g_strdup("--no-udp"));
    }

    if (passt->has_icmp && !passt->icmp) {
        g_ptr_array_add(args, g_strdup("--no-icmp"));
    }

    if (passt->has_dhcp && !passt->dhcp) {
        g_ptr_array_add(args, g_strdup("--no-dhcp"));
    }

    if (passt->has_ndp && !passt->ndp) {
        g_ptr_array_add(args, g_strdup("--no-ndp"));
    }
    if (passt->has_dhcpv6 && !passt->dhcpv6) {
        g_ptr_array_add(args, g_strdup("--no-dhcpv6"));
    }

    if (passt->has_ra && !passt->ra) {
        g_ptr_array_add(args, g_strdup("--no-ra"));
    }

    if (passt->has_freebind && passt->freebind) {
        g_ptr_array_add(args, g_strdup("--freebind"));
    }

    if (passt->has_ipv4 && !passt->ipv4) {
        g_ptr_array_add(args, g_strdup("--ipv6-only"));
    }

    if (passt->has_ipv6 && !passt->ipv6) {
        g_ptr_array_add(args, g_strdup("--ipv4-only"));
    }

    if (passt->has_search && passt->search) {
        const StringList *list = passt->search;
        GString *domains = g_string_new(list->value->str);

        list = list->next;
        while (list) {
            g_string_append(domains, " ");
            g_string_append(domains, list->value->str);
            list = list->next;
        }

        g_ptr_array_add(args, g_strdup("--search"));
        g_ptr_array_add(args, g_string_free(domains, FALSE));
    }

    if (passt->has_tcp_ports && passt->tcp_ports) {
        const StringList *list = passt->tcp_ports;
        GString *tcp_ports = g_string_new(list->value->str);

        list = list->next;
        while (list) {
            g_string_append(tcp_ports, ",");
            g_string_append(tcp_ports, list->value->str);
            list = list->next;
        }

        g_ptr_array_add(args, g_strdup("--tcp-ports"));
        g_ptr_array_add(args, g_string_free(tcp_ports, FALSE));
    }

    if (passt->has_udp_ports && passt->udp_ports) {
        const StringList *list = passt->udp_ports;
        GString *udp_ports = g_string_new(list->value->str);

        list = list->next;
        while (list) {
            g_string_append(udp_ports, ",");
            g_string_append(udp_ports, list->value->str);
            list = list->next;
        }

        g_ptr_array_add(args, g_strdup("--udp-ports"));
        g_ptr_array_add(args, g_string_free(udp_ports, FALSE));
    }

    if (passt->has_param && passt->param) {
        const StringList *list = passt->param;

        while (list) {
            g_ptr_array_add(args, g_strdup(list->value->str));
            list = list->next;
        }
    }

    /* provide a pid file to be able to kil passt on exit */
    g_ptr_array_add(args, g_strdup("--pid"));
    g_ptr_array_add(args, g_strdup(pidfile));

    /* g_subprocess_launcher_take_fd() will set the socket on fd 3 */
    g_ptr_array_add(args, g_strdup("--fd"));
    g_ptr_array_add(args, g_strdup("3"));

    g_ptr_array_add(args, NULL);

    return args;
}

int net_init_passt(const Netdev *netdev, const char *name,
                   NetClientState *peer, Error **errp)
{
    g_autoptr(GError) error = NULL;
    NetClientState *nc;
    NetPasstState *s;
    GPtrArray *args;
    gchar *pidfile;
    int pidfd;

    assert(netdev->type == NET_CLIENT_DRIVER_PASST);

    pidfd = g_file_open_tmp("passt-XXXXXX.pid", &pidfile, &error);
    if (pidfd == -1) {
        error_setg(errp, "Failed to create temporary file: %s", error->message);
        return -1;
    }
    close(pidfd);

    args = net_passt_decode_args(&netdev->u.passt, pidfile, errp);
    if (args == NULL) {
        g_free(pidfile);
        return -1;
    }

    nc = qemu_new_net_client(&net_passt_info, peer, "passt", name);
    s = DO_UPCAST(NetPasstState, data.nc, nc);

    s->args = args;
    s->pidfile = pidfile;

    if (netdev->u.passt.has_vhost_user && netdev->u.passt.vhost_user) {
        if (net_passt_vhost_user_init(s, errp) == -1) {
            qemu_del_net_client(nc);
            return -1;
        }

        return 0;
    }

    if (net_passt_stream_start(s, errp) == -1) {
        qemu_del_net_client(nc);
        return -1;
    }

    return 0;
}
