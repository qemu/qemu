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
#include "qemu/log.h"
#include "net/slirp.h"


#ifndef _WIN32
#include <pwd.h>
#include <sys/wait.h>
#endif
#include "net/net.h"
#include "clients.h"
#include "hub.h"
#include "monitor/monitor.h"
#include "qemu/error-report.h"
#include "qemu/sockets.h"
#include <libslirp.h>
#include "chardev/char-fe.h"
#include "sysemu/sysemu.h"
#include "qemu/cutils.h"
#include "qapi/error.h"
#include "qapi/qmp/qdict.h"
#include "util.h"
#include "migration/register.h"
#include "migration/qemu-file-types.h"

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

/* slirp network adapter */

#define SLIRP_CFG_HOSTFWD 1

struct slirp_config_str {
    struct slirp_config_str *next;
    int flags;
    char str[1024];
};

struct GuestFwd {
    CharBackend hd;
    struct in_addr server;
    int port;
    Slirp *slirp;
};

typedef struct SlirpState {
    NetClientState nc;
    QTAILQ_ENTRY(SlirpState) entry;
    Slirp *slirp;
    Notifier poll_notifier;
    Notifier exit_notifier;
#ifndef _WIN32
    gchar *smb_dir;
#endif
    GSList *fwd;
} SlirpState;

static struct slirp_config_str *slirp_configs;
static QTAILQ_HEAD(, SlirpState) slirp_stacks =
    QTAILQ_HEAD_INITIALIZER(slirp_stacks);

static int slirp_hostfwd(SlirpState *s, const char *redir_str, Error **errp);
static int slirp_guestfwd(SlirpState *s, const char *config_str, Error **errp);

#ifndef _WIN32
static int slirp_smb(SlirpState *s, const char *exported_dir,
                     struct in_addr vserver_addr, Error **errp);
static void slirp_smb_cleanup(SlirpState *s);
#else
static inline void slirp_smb_cleanup(SlirpState *s) { }
#endif

static ssize_t net_slirp_send_packet(const void *pkt, size_t pkt_len,
                                     void *opaque)
{
    SlirpState *s = opaque;

    return qemu_send_packet(&s->nc, pkt, pkt_len);
}

static ssize_t net_slirp_receive(NetClientState *nc, const uint8_t *buf, size_t size)
{
    SlirpState *s = DO_UPCAST(SlirpState, nc, nc);

    slirp_input(s->slirp, buf, size);

    return size;
}

static void slirp_smb_exit(Notifier *n, void *data)
{
    SlirpState *s = container_of(n, SlirpState, exit_notifier);
    slirp_smb_cleanup(s);
}

static void slirp_free_fwd(gpointer data)
{
    struct GuestFwd *fwd = data;

    qemu_chr_fe_deinit(&fwd->hd, true);
    g_free(data);
}

static void net_slirp_cleanup(NetClientState *nc)
{
    SlirpState *s = DO_UPCAST(SlirpState, nc, nc);

    g_slist_free_full(s->fwd, slirp_free_fwd);
    main_loop_poll_remove_notifier(&s->poll_notifier);
    unregister_savevm(NULL, "slirp", s->slirp);
    slirp_cleanup(s->slirp);
    if (s->exit_notifier.notify) {
        qemu_remove_exit_notifier(&s->exit_notifier);
    }
    slirp_smb_cleanup(s);
    QTAILQ_REMOVE(&slirp_stacks, s, entry);
}

static NetClientInfo net_slirp_info = {
    .type = NET_CLIENT_DRIVER_USER,
    .size = sizeof(SlirpState),
    .receive = net_slirp_receive,
    .cleanup = net_slirp_cleanup,
};

static void net_slirp_guest_error(const char *msg, void *opaque)
{
    qemu_log_mask(LOG_GUEST_ERROR, "%s", msg);
}

static int64_t net_slirp_clock_get_ns(void *opaque)
{
    return qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
}

static void *net_slirp_timer_new(SlirpTimerCb cb,
                                 void *cb_opaque, void *opaque)
{
    return timer_new_full(NULL, QEMU_CLOCK_VIRTUAL,
                          SCALE_MS, QEMU_TIMER_ATTR_EXTERNAL,
                          cb, cb_opaque);
}

static void net_slirp_timer_free(void *timer, void *opaque)
{
    timer_del(timer);
    timer_free(timer);
}

static void net_slirp_timer_mod(void *timer, int64_t expire_timer,
                                void *opaque)
{
    timer_mod(timer, expire_timer);
}

static void net_slirp_register_poll_fd(int fd, void *opaque)
{
    qemu_fd_register(fd);
}

static void net_slirp_unregister_poll_fd(int fd, void *opaque)
{
    /* no qemu_fd_unregister */
}

static void net_slirp_notify(void *opaque)
{
    qemu_notify_event();
}

static const SlirpCb slirp_cb = {
    .send_packet = net_slirp_send_packet,
    .guest_error = net_slirp_guest_error,
    .clock_get_ns = net_slirp_clock_get_ns,
    .timer_new = net_slirp_timer_new,
    .timer_free = net_slirp_timer_free,
    .timer_mod = net_slirp_timer_mod,
    .register_poll_fd = net_slirp_register_poll_fd,
    .unregister_poll_fd = net_slirp_unregister_poll_fd,
    .notify = net_slirp_notify,
};

static int slirp_poll_to_gio(int events)
{
    int ret = 0;

    if (events & SLIRP_POLL_IN) {
        ret |= G_IO_IN;
    }
    if (events & SLIRP_POLL_OUT) {
        ret |= G_IO_OUT;
    }
    if (events & SLIRP_POLL_PRI) {
        ret |= G_IO_PRI;
    }
    if (events & SLIRP_POLL_ERR) {
        ret |= G_IO_ERR;
    }
    if (events & SLIRP_POLL_HUP) {
        ret |= G_IO_HUP;
    }

    return ret;
}

static int net_slirp_add_poll(int fd, int events, void *opaque)
{
    GArray *pollfds = opaque;
    GPollFD pfd = {
        .fd = fd,
        .events = slirp_poll_to_gio(events),
    };
    int idx = pollfds->len;
    g_array_append_val(pollfds, pfd);
    return idx;
}

static int slirp_gio_to_poll(int events)
{
    int ret = 0;

    if (events & G_IO_IN) {
        ret |= SLIRP_POLL_IN;
    }
    if (events & G_IO_OUT) {
        ret |= SLIRP_POLL_OUT;
    }
    if (events & G_IO_PRI) {
        ret |= SLIRP_POLL_PRI;
    }
    if (events & G_IO_ERR) {
        ret |= SLIRP_POLL_ERR;
    }
    if (events & G_IO_HUP) {
        ret |= SLIRP_POLL_HUP;
    }

    return ret;
}

static int net_slirp_get_revents(int idx, void *opaque)
{
    GArray *pollfds = opaque;

    return slirp_gio_to_poll(g_array_index(pollfds, GPollFD, idx).revents);
}

static void net_slirp_poll_notify(Notifier *notifier, void *data)
{
    MainLoopPoll *poll = data;
    SlirpState *s = container_of(notifier, SlirpState, poll_notifier);

    switch (poll->state) {
    case MAIN_LOOP_POLL_FILL:
        slirp_pollfds_fill(s->slirp, &poll->timeout,
                           net_slirp_add_poll, poll->pollfds);
        break;
    case MAIN_LOOP_POLL_OK:
    case MAIN_LOOP_POLL_ERR:
        slirp_pollfds_poll(s->slirp, poll->state == MAIN_LOOP_POLL_ERR,
                           net_slirp_get_revents, poll->pollfds);
        break;
    default:
        g_assert_not_reached();
    }
}

static ssize_t
net_slirp_stream_read(void *buf, size_t size, void *opaque)
{
    QEMUFile *f = opaque;

    return qemu_get_buffer(f, buf, size);
}

static ssize_t
net_slirp_stream_write(const void *buf, size_t size, void *opaque)
{
    QEMUFile *f = opaque;

    qemu_put_buffer(f, buf, size);
    if (qemu_file_get_error(f)) {
        return -1;
    }

    return size;
}

static int net_slirp_state_load(QEMUFile *f, void *opaque, int version_id)
{
    Slirp *slirp = opaque;

    return slirp_state_load(slirp, version_id, net_slirp_stream_read, f);
}

static void net_slirp_state_save(QEMUFile *f, void *opaque)
{
    Slirp *slirp = opaque;

    slirp_state_save(slirp, net_slirp_stream_write, f);
}

static SaveVMHandlers savevm_slirp_state = {
    .save_state = net_slirp_state_save,
    .load_state = net_slirp_state_load,
};

static int net_slirp_init(NetClientState *peer, const char *model,
                          const char *name, int restricted,
                          bool ipv4, const char *vnetwork, const char *vhost,
                          bool ipv6, const char *vprefix6, int vprefix6_len,
                          const char *vhost6,
                          const char *vhostname, const char *tftp_export,
                          const char *bootfile, const char *vdhcp_start,
                          const char *vnameserver, const char *vnameserver6,
                          const char *smb_export, const char *vsmbserver,
                          const char **dnssearch, const char *vdomainname,
                          const char *tftp_server_name,
                          Error **errp)
{
    /* default settings according to historic slirp */
    struct in_addr net  = { .s_addr = htonl(0x0a000200) }; /* 10.0.2.0 */
    struct in_addr mask = { .s_addr = htonl(0xffffff00) }; /* 255.255.255.0 */
    struct in_addr host = { .s_addr = htonl(0x0a000202) }; /* 10.0.2.2 */
    struct in_addr dhcp = { .s_addr = htonl(0x0a00020f) }; /* 10.0.2.15 */
    struct in_addr dns  = { .s_addr = htonl(0x0a000203) }; /* 10.0.2.3 */
    struct in6_addr ip6_prefix;
    struct in6_addr ip6_host;
    struct in6_addr ip6_dns;
#ifndef _WIN32
    struct in_addr smbsrv = { .s_addr = 0 };
#endif
    NetClientState *nc;
    SlirpState *s;
    char buf[20];
    uint32_t addr;
    int shift;
    char *end;
    struct slirp_config_str *config;

    if (!ipv4 && (vnetwork || vhost || vnameserver)) {
        error_setg(errp, "IPv4 disabled but netmask/host/dns provided");
        return -1;
    }

    if (!ipv6 && (vprefix6 || vhost6 || vnameserver6)) {
        error_setg(errp, "IPv6 disabled but prefix/host6/dns6 provided");
        return -1;
    }

    if (!ipv4 && !ipv6) {
        /* It doesn't make sense to disable both */
        error_setg(errp, "IPv4 and IPv6 disabled");
        return -1;
    }

    if (vnetwork) {
        if (get_str_sep(buf, sizeof(buf), &vnetwork, '/') < 0) {
            if (!inet_aton(vnetwork, &net)) {
                error_setg(errp, "Failed to parse netmask");
                return -1;
            }
            addr = ntohl(net.s_addr);
            if (!(addr & 0x80000000)) {
                mask.s_addr = htonl(0xff000000); /* class A */
            } else if ((addr & 0xfff00000) == 0xac100000) {
                mask.s_addr = htonl(0xfff00000); /* priv. 172.16.0.0/12 */
            } else if ((addr & 0xc0000000) == 0x80000000) {
                mask.s_addr = htonl(0xffff0000); /* class B */
            } else if ((addr & 0xffff0000) == 0xc0a80000) {
                mask.s_addr = htonl(0xffff0000); /* priv. 192.168.0.0/16 */
            } else if ((addr & 0xffff0000) == 0xc6120000) {
                mask.s_addr = htonl(0xfffe0000); /* tests 198.18.0.0/15 */
            } else if ((addr & 0xe0000000) == 0xe0000000) {
                mask.s_addr = htonl(0xffffff00); /* class C */
            } else {
                mask.s_addr = htonl(0xfffffff0); /* multicast/reserved */
            }
        } else {
            if (!inet_aton(buf, &net)) {
                error_setg(errp, "Failed to parse netmask");
                return -1;
            }
            shift = strtol(vnetwork, &end, 10);
            if (*end != '\0') {
                if (!inet_aton(vnetwork, &mask)) {
                    error_setg(errp,
                               "Failed to parse netmask (trailing chars)");
                    return -1;
                }
            } else if (shift < 4 || shift > 32) {
                error_setg(errp,
                           "Invalid netmask provided (must be in range 4-32)");
                return -1;
            } else {
                mask.s_addr = htonl(0xffffffff << (32 - shift));
            }
        }
        net.s_addr &= mask.s_addr;
        host.s_addr = net.s_addr | (htonl(0x0202) & ~mask.s_addr);
        dhcp.s_addr = net.s_addr | (htonl(0x020f) & ~mask.s_addr);
        dns.s_addr  = net.s_addr | (htonl(0x0203) & ~mask.s_addr);
    }

    if (vhost && !inet_aton(vhost, &host)) {
        error_setg(errp, "Failed to parse host");
        return -1;
    }
    if ((host.s_addr & mask.s_addr) != net.s_addr) {
        error_setg(errp, "Host doesn't belong to network");
        return -1;
    }

    if (vnameserver && !inet_aton(vnameserver, &dns)) {
        error_setg(errp, "Failed to parse DNS");
        return -1;
    }
    if ((dns.s_addr & mask.s_addr) != net.s_addr) {
        error_setg(errp, "DNS doesn't belong to network");
        return -1;
    }
    if (dns.s_addr == host.s_addr) {
        error_setg(errp, "DNS must be different from host");
        return -1;
    }

    if (vdhcp_start && !inet_aton(vdhcp_start, &dhcp)) {
        error_setg(errp, "Failed to parse DHCP start address");
        return -1;
    }
    if ((dhcp.s_addr & mask.s_addr) != net.s_addr) {
        error_setg(errp, "DHCP doesn't belong to network");
        return -1;
    }
    if (dhcp.s_addr == host.s_addr || dhcp.s_addr == dns.s_addr) {
        error_setg(errp, "DNS must be different from host and DNS");
        return -1;
    }

#ifndef _WIN32
    if (vsmbserver && !inet_aton(vsmbserver, &smbsrv)) {
        error_setg(errp, "Failed to parse SMB address");
        return -1;
    }
#endif

    if (!vprefix6) {
        vprefix6 = "fec0::";
    }
    if (!inet_pton(AF_INET6, vprefix6, &ip6_prefix)) {
        error_setg(errp, "Failed to parse IPv6 prefix");
        return -1;
    }

    if (!vprefix6_len) {
        vprefix6_len = 64;
    }
    if (vprefix6_len < 0 || vprefix6_len > 126) {
        error_setg(errp,
                   "Invalid IPv6 prefix provided "
                   "(IPv6 prefix length must be between 0 and 126)");
        return -1;
    }

    if (vhost6) {
        if (!inet_pton(AF_INET6, vhost6, &ip6_host)) {
            error_setg(errp, "Failed to parse IPv6 host");
            return -1;
        }
        if (!in6_equal_net(&ip6_prefix, &ip6_host, vprefix6_len)) {
            error_setg(errp, "IPv6 Host doesn't belong to network");
            return -1;
        }
    } else {
        ip6_host = ip6_prefix;
        ip6_host.s6_addr[15] |= 2;
    }

    if (vnameserver6) {
        if (!inet_pton(AF_INET6, vnameserver6, &ip6_dns)) {
            error_setg(errp, "Failed to parse IPv6 DNS");
            return -1;
        }
        if (!in6_equal_net(&ip6_prefix, &ip6_dns, vprefix6_len)) {
            error_setg(errp, "IPv6 DNS doesn't belong to network");
            return -1;
        }
    } else {
        ip6_dns = ip6_prefix;
        ip6_dns.s6_addr[15] |= 3;
    }

    if (vdomainname && !*vdomainname) {
        error_setg(errp, "'domainname' parameter cannot be empty");
        return -1;
    }

    if (vdomainname && strlen(vdomainname) > 255) {
        error_setg(errp, "'domainname' parameter cannot exceed 255 bytes");
        return -1;
    }

    if (vhostname && strlen(vhostname) > 255) {
        error_setg(errp, "'vhostname' parameter cannot exceed 255 bytes");
        return -1;
    }

    if (tftp_server_name && strlen(tftp_server_name) > 255) {
        error_setg(errp, "'tftp-server-name' parameter cannot exceed 255 bytes");
        return -1;
    }

    nc = qemu_new_net_client(&net_slirp_info, peer, model, name);

    snprintf(nc->info_str, sizeof(nc->info_str),
             "net=%s,restrict=%s", inet_ntoa(net),
             restricted ? "on" : "off");

    s = DO_UPCAST(SlirpState, nc, nc);

    s->slirp = slirp_init(restricted, ipv4, net, mask, host,
                          ipv6, ip6_prefix, vprefix6_len, ip6_host,
                          vhostname, tftp_server_name,
                          tftp_export, bootfile, dhcp,
                          dns, ip6_dns, dnssearch, vdomainname,
                          &slirp_cb, s);
    QTAILQ_INSERT_TAIL(&slirp_stacks, s, entry);

    /*
     * Make sure the current bitstream version of slirp is 4, to avoid
     * QEMU migration incompatibilities, if upstream slirp bumped the
     * version.
     *
     * FIXME: use bitfields of features? teach libslirp to save with
     * specific version?
     */
    g_assert(slirp_state_version() == 4);
    register_savevm_live(NULL, "slirp", 0, slirp_state_version(),
                         &savevm_slirp_state, s->slirp);

    s->poll_notifier.notify = net_slirp_poll_notify;
    main_loop_poll_add_notifier(&s->poll_notifier);

    for (config = slirp_configs; config; config = config->next) {
        if (config->flags & SLIRP_CFG_HOSTFWD) {
            if (slirp_hostfwd(s, config->str, errp) < 0) {
                goto error;
            }
        } else {
            if (slirp_guestfwd(s, config->str, errp) < 0) {
                goto error;
            }
        }
    }
#ifndef _WIN32
    if (smb_export) {
        if (slirp_smb(s, smb_export, smbsrv, errp) < 0) {
            goto error;
        }
    }
#endif

    s->exit_notifier.notify = slirp_smb_exit;
    qemu_add_exit_notifier(&s->exit_notifier);
    return 0;

error:
    qemu_del_net_client(nc);
    return -1;
}

static SlirpState *slirp_lookup(Monitor *mon, const char *hub_id,
                                const char *name)
{
    if (name) {
        NetClientState *nc;
        if (hub_id) {
            nc = net_hub_find_client_by_name(strtol(hub_id, NULL, 0), name);
            if (!nc) {
                monitor_printf(mon, "unrecognized (hub-id, stackname) pair\n");
                return NULL;
            }
            warn_report("Using 'hub-id' is deprecated, specify the netdev id "
                        "directly instead");
        } else {
            nc = qemu_find_netdev(name);
            if (!nc) {
                monitor_printf(mon, "unrecognized netdev id '%s'\n", name);
                return NULL;
            }
        }
        if (strcmp(nc->model, "user")) {
            monitor_printf(mon, "invalid device specified\n");
            return NULL;
        }
        return DO_UPCAST(SlirpState, nc, nc);
    } else {
        if (QTAILQ_EMPTY(&slirp_stacks)) {
            monitor_printf(mon, "user mode network stack not in use\n");
            return NULL;
        }
        return QTAILQ_FIRST(&slirp_stacks);
    }
}

void hmp_hostfwd_remove(Monitor *mon, const QDict *qdict)
{
    struct in_addr host_addr = { .s_addr = INADDR_ANY };
    int host_port;
    char buf[256];
    const char *src_str, *p;
    SlirpState *s;
    int is_udp = 0;
    int err;
    const char *arg1 = qdict_get_str(qdict, "arg1");
    const char *arg2 = qdict_get_try_str(qdict, "arg2");
    const char *arg3 = qdict_get_try_str(qdict, "arg3");

    if (arg3) {
        s = slirp_lookup(mon, arg1, arg2);
        src_str = arg3;
    } else if (arg2) {
        s = slirp_lookup(mon, NULL, arg1);
        src_str = arg2;
    } else {
        s = slirp_lookup(mon, NULL, NULL);
        src_str = arg1;
    }
    if (!s) {
        return;
    }

    p = src_str;
    if (!p || get_str_sep(buf, sizeof(buf), &p, ':') < 0) {
        goto fail_syntax;
    }

    if (!strcmp(buf, "tcp") || buf[0] == '\0') {
        is_udp = 0;
    } else if (!strcmp(buf, "udp")) {
        is_udp = 1;
    } else {
        goto fail_syntax;
    }

    if (get_str_sep(buf, sizeof(buf), &p, ':') < 0) {
        goto fail_syntax;
    }
    if (buf[0] != '\0' && !inet_aton(buf, &host_addr)) {
        goto fail_syntax;
    }

    if (qemu_strtoi(p, NULL, 10, &host_port)) {
        goto fail_syntax;
    }

    err = slirp_remove_hostfwd(s->slirp, is_udp, host_addr, host_port);

    monitor_printf(mon, "host forwarding rule for %s %s\n", src_str,
                   err ? "not found" : "removed");
    return;

 fail_syntax:
    monitor_printf(mon, "invalid format\n");
}

static int slirp_hostfwd(SlirpState *s, const char *redir_str, Error **errp)
{
    struct in_addr host_addr = { .s_addr = INADDR_ANY };
    struct in_addr guest_addr = { .s_addr = 0 };
    int host_port, guest_port;
    const char *p;
    char buf[256];
    int is_udp;
    char *end;
    const char *fail_reason = "Unknown reason";

    p = redir_str;
    if (!p || get_str_sep(buf, sizeof(buf), &p, ':') < 0) {
        fail_reason = "No : separators";
        goto fail_syntax;
    }
    if (!strcmp(buf, "tcp") || buf[0] == '\0') {
        is_udp = 0;
    } else if (!strcmp(buf, "udp")) {
        is_udp = 1;
    } else {
        fail_reason = "Bad protocol name";
        goto fail_syntax;
    }

    if (get_str_sep(buf, sizeof(buf), &p, ':') < 0) {
        fail_reason = "Missing : separator";
        goto fail_syntax;
    }
    if (buf[0] != '\0' && !inet_aton(buf, &host_addr)) {
        fail_reason = "Bad host address";
        goto fail_syntax;
    }

    if (get_str_sep(buf, sizeof(buf), &p, '-') < 0) {
        fail_reason = "Bad host port separator";
        goto fail_syntax;
    }
    host_port = strtol(buf, &end, 0);
    if (*end != '\0' || host_port < 0 || host_port > 65535) {
        fail_reason = "Bad host port";
        goto fail_syntax;
    }

    if (get_str_sep(buf, sizeof(buf), &p, ':') < 0) {
        fail_reason = "Missing guest address";
        goto fail_syntax;
    }
    if (buf[0] != '\0' && !inet_aton(buf, &guest_addr)) {
        fail_reason = "Bad guest address";
        goto fail_syntax;
    }

    guest_port = strtol(p, &end, 0);
    if (*end != '\0' || guest_port < 1 || guest_port > 65535) {
        fail_reason = "Bad guest port";
        goto fail_syntax;
    }

    if (slirp_add_hostfwd(s->slirp, is_udp, host_addr, host_port, guest_addr,
                          guest_port) < 0) {
        error_setg(errp, "Could not set up host forwarding rule '%s'",
                   redir_str);
        return -1;
    }
    return 0;

 fail_syntax:
    error_setg(errp, "Invalid host forwarding rule '%s' (%s)", redir_str,
               fail_reason);
    return -1;
}

void hmp_hostfwd_add(Monitor *mon, const QDict *qdict)
{
    const char *redir_str;
    SlirpState *s;
    const char *arg1 = qdict_get_str(qdict, "arg1");
    const char *arg2 = qdict_get_try_str(qdict, "arg2");
    const char *arg3 = qdict_get_try_str(qdict, "arg3");

    if (arg3) {
        s = slirp_lookup(mon, arg1, arg2);
        redir_str = arg3;
    } else if (arg2) {
        s = slirp_lookup(mon, NULL, arg1);
        redir_str = arg2;
    } else {
        s = slirp_lookup(mon, NULL, NULL);
        redir_str = arg1;
    }
    if (s) {
        Error *err = NULL;
        if (slirp_hostfwd(s, redir_str, &err) < 0) {
            error_report_err(err);
        }
    }

}

#ifndef _WIN32

/* automatic user mode samba server configuration */
static void slirp_smb_cleanup(SlirpState *s)
{
    int ret;

    if (s->smb_dir) {
        gchar *cmd = g_strdup_printf("rm -rf %s", s->smb_dir);
        ret = system(cmd);
        if (ret == -1 || !WIFEXITED(ret)) {
            error_report("'%s' failed.", cmd);
        } else if (WEXITSTATUS(ret)) {
            error_report("'%s' failed. Error code: %d",
                         cmd, WEXITSTATUS(ret));
        }
        g_free(cmd);
        g_free(s->smb_dir);
        s->smb_dir = NULL;
    }
}

static int slirp_smb(SlirpState* s, const char *exported_dir,
                     struct in_addr vserver_addr, Error **errp)
{
    char *smb_conf;
    char *smb_cmdline;
    struct passwd *passwd;
    FILE *f;

    passwd = getpwuid(geteuid());
    if (!passwd) {
        error_setg(errp, "Failed to retrieve user name");
        return -1;
    }

    if (access(CONFIG_SMBD_COMMAND, F_OK)) {
        error_setg(errp, "Could not find '%s', please install it",
                   CONFIG_SMBD_COMMAND);
        return -1;
    }

    if (access(exported_dir, R_OK | X_OK)) {
        error_setg(errp, "Error accessing shared directory '%s': %s",
                   exported_dir, strerror(errno));
        return -1;
    }

    s->smb_dir = g_dir_make_tmp("qemu-smb.XXXXXX", NULL);
    if (!s->smb_dir) {
        error_setg(errp, "Could not create samba server dir");
        return -1;
    }
    smb_conf = g_strdup_printf("%s/%s", s->smb_dir, "smb.conf");

    f = fopen(smb_conf, "w");
    if (!f) {
        slirp_smb_cleanup(s);
        error_setg(errp,
                   "Could not create samba server configuration file '%s'",
                    smb_conf);
        g_free(smb_conf);
        return -1;
    }
    fprintf(f,
            "[global]\n"
            "private dir=%s\n"
            "interfaces=127.0.0.1\n"
            "bind interfaces only=yes\n"
            "pid directory=%s\n"
            "lock directory=%s\n"
            "state directory=%s\n"
            "cache directory=%s\n"
            "ncalrpc dir=%s/ncalrpc\n"
            "log file=%s/log.smbd\n"
            "smb passwd file=%s/smbpasswd\n"
            "security = user\n"
            "map to guest = Bad User\n"
            "load printers = no\n"
            "printing = bsd\n"
            "disable spoolss = yes\n"
            "usershare max shares = 0\n"
            "[qemu]\n"
            "path=%s\n"
            "read only=no\n"
            "guest ok=yes\n"
            "force user=%s\n",
            s->smb_dir,
            s->smb_dir,
            s->smb_dir,
            s->smb_dir,
            s->smb_dir,
            s->smb_dir,
            s->smb_dir,
            s->smb_dir,
            exported_dir,
            passwd->pw_name
            );
    fclose(f);

    smb_cmdline = g_strdup_printf("%s -l %s -s %s",
             CONFIG_SMBD_COMMAND, s->smb_dir, smb_conf);
    g_free(smb_conf);

    if (slirp_add_exec(s->slirp, smb_cmdline, &vserver_addr, 139) < 0 ||
        slirp_add_exec(s->slirp, smb_cmdline, &vserver_addr, 445) < 0) {
        slirp_smb_cleanup(s);
        g_free(smb_cmdline);
        error_setg(errp, "Conflicting/invalid smbserver address");
        return -1;
    }
    g_free(smb_cmdline);
    return 0;
}

#endif /* !defined(_WIN32) */

static int guestfwd_can_read(void *opaque)
{
    struct GuestFwd *fwd = opaque;
    return slirp_socket_can_recv(fwd->slirp, fwd->server, fwd->port);
}

static void guestfwd_read(void *opaque, const uint8_t *buf, int size)
{
    struct GuestFwd *fwd = opaque;
    slirp_socket_recv(fwd->slirp, fwd->server, fwd->port, buf, size);
}

static ssize_t guestfwd_write(const void *buf, size_t len, void *chr)
{
    return qemu_chr_fe_write_all(chr, buf, len);
}

static int slirp_guestfwd(SlirpState *s, const char *config_str, Error **errp)
{
    /* TODO: IPv6 */
    struct in_addr server = { .s_addr = 0 };
    struct GuestFwd *fwd;
    const char *p;
    char buf[128];
    char *end;
    int port;

    p = config_str;
    if (get_str_sep(buf, sizeof(buf), &p, ':') < 0) {
        goto fail_syntax;
    }
    if (strcmp(buf, "tcp") && buf[0] != '\0') {
        goto fail_syntax;
    }
    if (get_str_sep(buf, sizeof(buf), &p, ':') < 0) {
        goto fail_syntax;
    }
    if (buf[0] != '\0' && !inet_aton(buf, &server)) {
        goto fail_syntax;
    }
    if (get_str_sep(buf, sizeof(buf), &p, '-') < 0) {
        goto fail_syntax;
    }
    port = strtol(buf, &end, 10);
    if (*end != '\0' || port < 1 || port > 65535) {
        goto fail_syntax;
    }

    snprintf(buf, sizeof(buf), "guestfwd.tcp.%d", port);

    if (g_str_has_prefix(p, "cmd:")) {
        if (slirp_add_exec(s->slirp, &p[4], &server, port) < 0) {
            error_setg(errp, "Conflicting/invalid host:port in guest "
                       "forwarding rule '%s'", config_str);
            return -1;
        }
    } else {
        Error *err = NULL;
        /*
         * FIXME: sure we want to support implicit
         * muxed monitors here?
         */
        Chardev *chr = qemu_chr_new_mux_mon(buf, p, NULL);

        if (!chr) {
            error_setg(errp, "Could not open guest forwarding device '%s'",
                       buf);
            return -1;
        }

        fwd = g_new(struct GuestFwd, 1);
        qemu_chr_fe_init(&fwd->hd, chr, &err);
        if (err) {
            error_propagate(errp, err);
            object_unparent(OBJECT(chr));
            g_free(fwd);
            return -1;
        }

        if (slirp_add_guestfwd(s->slirp, guestfwd_write, &fwd->hd,
                               &server, port) < 0) {
            error_setg(errp, "Conflicting/invalid host:port in guest "
                       "forwarding rule '%s'", config_str);
            qemu_chr_fe_deinit(&fwd->hd, true);
            g_free(fwd);
            return -1;
        }
        fwd->server = server;
        fwd->port = port;
        fwd->slirp = s->slirp;

        qemu_chr_fe_set_handlers(&fwd->hd, guestfwd_can_read, guestfwd_read,
                                 NULL, NULL, fwd, NULL, true);
        s->fwd = g_slist_append(s->fwd, fwd);
    }
    return 0;

 fail_syntax:
    error_setg(errp, "Invalid guest forwarding rule '%s'", config_str);
    return -1;
}

void hmp_info_usernet(Monitor *mon, const QDict *qdict)
{
    SlirpState *s;

    QTAILQ_FOREACH(s, &slirp_stacks, entry) {
        int id;
        bool got_hub_id = net_hub_id_for_client(&s->nc, &id) == 0;
        char *info = slirp_connection_info(s->slirp);
        monitor_printf(mon, "Hub %d (%s):\n%s",
                       got_hub_id ? id : -1,
                       s->nc.name, info);
        g_free(info);
    }
}

static void
net_init_slirp_configs(const StringList *fwd, int flags)
{
    while (fwd) {
        struct slirp_config_str *config;

        config = g_malloc0(sizeof(*config));
        pstrcpy(config->str, sizeof(config->str), fwd->value->str);
        config->flags = flags;
        config->next = slirp_configs;
        slirp_configs = config;

        fwd = fwd->next;
    }
}

static const char **slirp_dnssearch(const StringList *dnsname)
{
    const StringList *c = dnsname;
    size_t i = 0, num_opts = 0;
    const char **ret;

    while (c) {
        num_opts++;
        c = c->next;
    }

    if (num_opts == 0) {
        return NULL;
    }

    ret = g_malloc((num_opts + 1) * sizeof(*ret));
    c = dnsname;
    while (c) {
        ret[i++] = c->value->str;
        c = c->next;
    }
    ret[i] = NULL;
    return ret;
}

int net_init_slirp(const Netdev *netdev, const char *name,
                   NetClientState *peer, Error **errp)
{
    struct slirp_config_str *config;
    char *vnet;
    int ret;
    const NetdevUserOptions *user;
    const char **dnssearch;
    bool ipv4 = true, ipv6 = true;

    assert(netdev->type == NET_CLIENT_DRIVER_USER);
    user = &netdev->u.user;

    if ((user->has_ipv6 && user->ipv6 && !user->has_ipv4) ||
        (user->has_ipv4 && !user->ipv4)) {
        ipv4 = 0;
    }
    if ((user->has_ipv4 && user->ipv4 && !user->has_ipv6) ||
        (user->has_ipv6 && !user->ipv6)) {
        ipv6 = 0;
    }

    vnet = user->has_net ? g_strdup(user->net) :
           user->has_ip  ? g_strdup_printf("%s/24", user->ip) :
           NULL;

    dnssearch = slirp_dnssearch(user->dnssearch);

    /* all optional fields are initialized to "all bits zero" */

    net_init_slirp_configs(user->hostfwd, SLIRP_CFG_HOSTFWD);
    net_init_slirp_configs(user->guestfwd, 0);

    ret = net_slirp_init(peer, "user", name, user->q_restrict,
                         ipv4, vnet, user->host,
                         ipv6, user->ipv6_prefix, user->ipv6_prefixlen,
                         user->ipv6_host, user->hostname, user->tftp,
                         user->bootfile, user->dhcpstart,
                         user->dns, user->ipv6_dns, user->smb,
                         user->smbserver, dnssearch, user->domainname,
                         user->tftp_server_name, errp);

    while (slirp_configs) {
        config = slirp_configs;
        slirp_configs = config->next;
        g_free(config);
    }

    g_free(vnet);
    g_free(dnssearch);

    return ret;
}
