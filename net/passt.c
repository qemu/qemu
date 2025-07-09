/*
 * passt network backend
 *
 * Copyright Red Hat
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include <glib/gstdio.h>
#include <gio/gio.h>
#include "net/net.h"
#include "clients.h"
#include "qapi/error.h"
#include "io/net-listener.h"
#include "stream_data.h"

typedef struct NetPasstState {
    NetStreamData data;
    GPtrArray *args;
    gchar *pidfile;
    pid_t pid;
} NetPasstState;

static int net_passt_stream_start(NetPasstState *s, Error **errp);

static void net_passt_cleanup(NetClientState *nc)
{
    NetPasstState *s = DO_UPCAST(NetPasstState, data.nc, nc);

    kill(s->pid, SIGTERM);
    g_remove(s->pidfile);
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
        Error *error;

        /* we need to restart passt */
        kill(s->pid, SIGTERM);
        if (net_passt_stream_start(s, &error) == -1) {
            error_report_err(error);
        }

        return G_SOURCE_REMOVE;
    }

    return G_SOURCE_CONTINUE;
}

static NetClientInfo net_passt_info = {
    .type = NET_CLIENT_DRIVER_PASST,
    .size = sizeof(NetPasstState),
    .receive = net_passt_receive,
    .cleanup = net_passt_cleanup,
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

static GPtrArray *net_passt_decode_args(const NetDevPasstOptions *passt,
                                        gchar *pidfile, Error **errp)
{
    GPtrArray *args = g_ptr_array_new_with_free_func(g_free);

    if (passt->path) {
        g_ptr_array_add(args, g_strdup(passt->path));
    } else {
        g_ptr_array_add(args, g_strdup("passt"));
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

    if (net_passt_stream_start(s, errp) == -1) {
        qemu_del_net_client(nc);
        return -1;
    }

    return 0;
}
