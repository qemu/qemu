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
#include "net/slirp.h"

#include "config-host.h"

#ifndef _WIN32
#include <sys/wait.h>
#endif
#include "net.h"
#include "monitor.h"
#include "sysemu.h"
#include "qemu_socket.h"
#include "slirp/libslirp.h"

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
#define SLIRP_CFG_LEGACY  2

struct slirp_config_str {
    struct slirp_config_str *next;
    int flags;
    char str[1024];
    int legacy_format;
};

typedef struct SlirpState {
    VLANClientState nc;
    QTAILQ_ENTRY(SlirpState) entry;
    Slirp *slirp;
#ifndef _WIN32
    char smb_dir[128];
#endif
} SlirpState;

static struct slirp_config_str *slirp_configs;
const char *legacy_tftp_prefix;
const char *legacy_bootp_filename;
static QTAILQ_HEAD(slirp_stacks, SlirpState) slirp_stacks =
    QTAILQ_HEAD_INITIALIZER(slirp_stacks);

static int slirp_hostfwd(SlirpState *s, const char *redir_str,
                         int legacy_format);
static int slirp_guestfwd(SlirpState *s, const char *config_str,
                          int legacy_format);

#ifndef _WIN32
static const char *legacy_smb_export;

static int slirp_smb(SlirpState *s, const char *exported_dir,
                     struct in_addr vserver_addr);
static void slirp_smb_cleanup(SlirpState *s);
#else
static inline void slirp_smb_cleanup(SlirpState *s) { }
#endif

int slirp_can_output(void *opaque)
{
    SlirpState *s = opaque;

    return qemu_can_send_packet(&s->nc);
}

void slirp_output(void *opaque, const uint8_t *pkt, int pkt_len)
{
    SlirpState *s = opaque;

    qemu_send_packet(&s->nc, pkt, pkt_len);
}

static ssize_t net_slirp_receive(VLANClientState *nc, const uint8_t *buf, size_t size)
{
    SlirpState *s = DO_UPCAST(SlirpState, nc, nc);

    slirp_input(s->slirp, buf, size);

    return size;
}

static void net_slirp_cleanup(VLANClientState *nc)
{
    SlirpState *s = DO_UPCAST(SlirpState, nc, nc);

    slirp_cleanup(s->slirp);
    slirp_smb_cleanup(s);
    QTAILQ_REMOVE(&slirp_stacks, s, entry);
}

static NetClientInfo net_slirp_info = {
    .type = NET_CLIENT_TYPE_SLIRP,
    .size = sizeof(SlirpState),
    .receive = net_slirp_receive,
    .cleanup = net_slirp_cleanup,
};

static int net_slirp_init(VLANState *vlan, const char *model,
                          const char *name, int restricted,
                          const char *vnetwork, const char *vhost,
                          const char *vhostname, const char *tftp_export,
                          const char *bootfile, const char *vdhcp_start,
                          const char *vnameserver, const char *smb_export,
                          const char *vsmbserver)
{
    /* default settings according to historic slirp */
    struct in_addr net  = { .s_addr = htonl(0x0a000200) }; /* 10.0.2.0 */
    struct in_addr mask = { .s_addr = htonl(0xffffff00) }; /* 255.255.255.0 */
    struct in_addr host = { .s_addr = htonl(0x0a000202) }; /* 10.0.2.2 */
    struct in_addr dhcp = { .s_addr = htonl(0x0a00020f) }; /* 10.0.2.15 */
    struct in_addr dns  = { .s_addr = htonl(0x0a000203) }; /* 10.0.2.3 */
#ifndef _WIN32
    struct in_addr smbsrv = { .s_addr = 0 };
#endif
    VLANClientState *nc;
    SlirpState *s;
    char buf[20];
    uint32_t addr;
    int shift;
    char *end;
    struct slirp_config_str *config;

    if (!tftp_export) {
        tftp_export = legacy_tftp_prefix;
    }
    if (!bootfile) {
        bootfile = legacy_bootp_filename;
    }

    if (vnetwork) {
        if (get_str_sep(buf, sizeof(buf), &vnetwork, '/') < 0) {
            if (!inet_aton(vnetwork, &net)) {
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
                return -1;
            }
            shift = strtol(vnetwork, &end, 10);
            if (*end != '\0') {
                if (!inet_aton(vnetwork, &mask)) {
                    return -1;
                }
            } else if (shift < 4 || shift > 32) {
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
        return -1;
    }
    if ((host.s_addr & mask.s_addr) != net.s_addr) {
        return -1;
    }

    if (vdhcp_start && !inet_aton(vdhcp_start, &dhcp)) {
        return -1;
    }
    if ((dhcp.s_addr & mask.s_addr) != net.s_addr ||
        dhcp.s_addr == host.s_addr || dhcp.s_addr == dns.s_addr) {
        return -1;
    }

    if (vnameserver && !inet_aton(vnameserver, &dns)) {
        return -1;
    }
    if ((dns.s_addr & mask.s_addr) != net.s_addr ||
        dns.s_addr == host.s_addr) {
        return -1;
    }

#ifndef _WIN32
    if (vsmbserver && !inet_aton(vsmbserver, &smbsrv)) {
        return -1;
    }
#endif

    nc = qemu_new_net_client(&net_slirp_info, vlan, NULL, model, name);

    snprintf(nc->info_str, sizeof(nc->info_str),
             "net=%s, restricted=%c", inet_ntoa(net), restricted ? 'y' : 'n');

    s = DO_UPCAST(SlirpState, nc, nc);

    s->slirp = slirp_init(restricted, net, mask, host, vhostname,
                          tftp_export, bootfile, dhcp, dns, s);
    QTAILQ_INSERT_TAIL(&slirp_stacks, s, entry);

    for (config = slirp_configs; config; config = config->next) {
        if (config->flags & SLIRP_CFG_HOSTFWD) {
            if (slirp_hostfwd(s, config->str,
                              config->flags & SLIRP_CFG_LEGACY) < 0)
                goto error;
        } else {
            if (slirp_guestfwd(s, config->str,
                               config->flags & SLIRP_CFG_LEGACY) < 0)
                goto error;
        }
    }
#ifndef _WIN32
    if (!smb_export) {
        smb_export = legacy_smb_export;
    }
    if (smb_export) {
        if (slirp_smb(s, smb_export, smbsrv) < 0)
            goto error;
    }
#endif

    return 0;

error:
    qemu_del_vlan_client(nc);
    return -1;
}

static SlirpState *slirp_lookup(Monitor *mon, const char *vlan,
                                const char *stack)
{

    if (vlan) {
        VLANClientState *nc;
        nc = qemu_find_vlan_client_by_name(mon, strtol(vlan, NULL, 0), stack);
        if (!nc) {
            return NULL;
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

void net_slirp_hostfwd_remove(Monitor *mon, const QDict *qdict)
{
    struct in_addr host_addr = { .s_addr = INADDR_ANY };
    int host_port;
    char buf[256] = "";
    const char *src_str, *p;
    SlirpState *s;
    int is_udp = 0;
    int err;
    const char *arg1 = qdict_get_str(qdict, "arg1");
    const char *arg2 = qdict_get_try_str(qdict, "arg2");
    const char *arg3 = qdict_get_try_str(qdict, "arg3");

    if (arg2) {
        s = slirp_lookup(mon, arg1, arg2);
        src_str = arg3;
    } else {
        s = slirp_lookup(mon, NULL, NULL);
        src_str = arg1;
    }
    if (!s) {
        return;
    }

    if (!src_str || !src_str[0])
        goto fail_syntax;

    p = src_str;
    get_str_sep(buf, sizeof(buf), &p, ':');

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

    host_port = atoi(p);

    err = slirp_remove_hostfwd(QTAILQ_FIRST(&slirp_stacks)->slirp, is_udp,
                               host_addr, host_port);

    monitor_printf(mon, "host forwarding rule for %s %s\n", src_str,
                   err ? "removed" : "not found");
    return;

 fail_syntax:
    monitor_printf(mon, "invalid format\n");
}

static int slirp_hostfwd(SlirpState *s, const char *redir_str,
                         int legacy_format)
{
    struct in_addr host_addr = { .s_addr = INADDR_ANY };
    struct in_addr guest_addr = { .s_addr = 0 };
    int host_port, guest_port;
    const char *p;
    char buf[256];
    int is_udp;
    char *end;

    p = redir_str;
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

    if (!legacy_format) {
        if (get_str_sep(buf, sizeof(buf), &p, ':') < 0) {
            goto fail_syntax;
        }
        if (buf[0] != '\0' && !inet_aton(buf, &host_addr)) {
            goto fail_syntax;
        }
    }

    if (get_str_sep(buf, sizeof(buf), &p, legacy_format ? ':' : '-') < 0) {
        goto fail_syntax;
    }
    host_port = strtol(buf, &end, 0);
    if (*end != '\0' || host_port < 1 || host_port > 65535) {
        goto fail_syntax;
    }

    if (get_str_sep(buf, sizeof(buf), &p, ':') < 0) {
        goto fail_syntax;
    }
    if (buf[0] != '\0' && !inet_aton(buf, &guest_addr)) {
        goto fail_syntax;
    }

    guest_port = strtol(p, &end, 0);
    if (*end != '\0' || guest_port < 1 || guest_port > 65535) {
        goto fail_syntax;
    }

    if (slirp_add_hostfwd(s->slirp, is_udp, host_addr, host_port, guest_addr,
                          guest_port) < 0) {
        error_report("could not set up host forwarding rule '%s'",
                     redir_str);
        return -1;
    }
    return 0;

 fail_syntax:
    error_report("invalid host forwarding rule '%s'", redir_str);
    return -1;
}

void net_slirp_hostfwd_add(Monitor *mon, const QDict *qdict)
{
    const char *redir_str;
    SlirpState *s;
    const char *arg1 = qdict_get_str(qdict, "arg1");
    const char *arg2 = qdict_get_try_str(qdict, "arg2");
    const char *arg3 = qdict_get_try_str(qdict, "arg3");

    if (arg2) {
        s = slirp_lookup(mon, arg1, arg2);
        redir_str = arg3;
    } else {
        s = slirp_lookup(mon, NULL, NULL);
        redir_str = arg1;
    }
    if (s) {
        slirp_hostfwd(s, redir_str, 0);
    }

}

int net_slirp_redir(const char *redir_str)
{
    struct slirp_config_str *config;

    if (QTAILQ_EMPTY(&slirp_stacks)) {
        config = qemu_malloc(sizeof(*config));
        pstrcpy(config->str, sizeof(config->str), redir_str);
        config->flags = SLIRP_CFG_HOSTFWD | SLIRP_CFG_LEGACY;
        config->next = slirp_configs;
        slirp_configs = config;
        return 0;
    }

    return slirp_hostfwd(QTAILQ_FIRST(&slirp_stacks), redir_str, 1);
}

#ifndef _WIN32

/* automatic user mode samba server configuration */
static void slirp_smb_cleanup(SlirpState *s)
{
    char cmd[128];
    int ret;

    if (s->smb_dir[0] != '\0') {
        snprintf(cmd, sizeof(cmd), "rm -rf %s", s->smb_dir);
        ret = system(cmd);
        if (ret == -1 || !WIFEXITED(ret)) {
            error_report("'%s' failed.", cmd);
        } else if (WEXITSTATUS(ret)) {
            error_report("'%s' failed. Error code: %d",
                         cmd, WEXITSTATUS(ret));
        }
        s->smb_dir[0] = '\0';
    }
}

static int slirp_smb(SlirpState* s, const char *exported_dir,
                     struct in_addr vserver_addr)
{
    static int instance;
    char smb_conf[128];
    char smb_cmdline[128];
    FILE *f;

    snprintf(s->smb_dir, sizeof(s->smb_dir), "/tmp/qemu-smb.%ld-%d",
             (long)getpid(), instance++);
    if (mkdir(s->smb_dir, 0700) < 0) {
        error_report("could not create samba server dir '%s'", s->smb_dir);
        return -1;
    }
    snprintf(smb_conf, sizeof(smb_conf), "%s/%s", s->smb_dir, "smb.conf");

    f = fopen(smb_conf, "w");
    if (!f) {
        slirp_smb_cleanup(s);
        error_report("could not create samba server configuration file '%s'",
                     smb_conf);
        return -1;
    }
    fprintf(f,
            "[global]\n"
            "private dir=%s\n"
            "smb ports=0\n"
            "socket address=127.0.0.1\n"
            "pid directory=%s\n"
            "lock directory=%s\n"
            "log file=%s/log.smbd\n"
            "smb passwd file=%s/smbpasswd\n"
            "security = share\n"
            "[qemu]\n"
            "path=%s\n"
            "read only=no\n"
            "guest ok=yes\n",
            s->smb_dir,
            s->smb_dir,
            s->smb_dir,
            s->smb_dir,
            s->smb_dir,
            exported_dir
            );
    fclose(f);

    snprintf(smb_cmdline, sizeof(smb_cmdline), "%s -s %s",
             SMBD_COMMAND, smb_conf);

    if (slirp_add_exec(s->slirp, 0, smb_cmdline, &vserver_addr, 139) < 0) {
        slirp_smb_cleanup(s);
        error_report("conflicting/invalid smbserver address");
        return -1;
    }
    return 0;
}

/* automatic user mode samba server configuration (legacy interface) */
int net_slirp_smb(const char *exported_dir)
{
    struct in_addr vserver_addr = { .s_addr = 0 };

    if (legacy_smb_export) {
        fprintf(stderr, "-smb given twice\n");
        return -1;
    }
    legacy_smb_export = exported_dir;
    if (!QTAILQ_EMPTY(&slirp_stacks)) {
        return slirp_smb(QTAILQ_FIRST(&slirp_stacks), exported_dir,
                         vserver_addr);
    }
    return 0;
}

#endif /* !defined(_WIN32) */

struct GuestFwd {
    CharDriverState *hd;
    struct in_addr server;
    int port;
    Slirp *slirp;
};

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

static int slirp_guestfwd(SlirpState *s, const char *config_str,
                          int legacy_format)
{
    struct in_addr server = { .s_addr = 0 };
    struct GuestFwd *fwd;
    const char *p;
    char buf[128];
    char *end;
    int port;

    p = config_str;
    if (legacy_format) {
        if (get_str_sep(buf, sizeof(buf), &p, ':') < 0) {
            goto fail_syntax;
        }
    } else {
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
    }
    port = strtol(buf, &end, 10);
    if (*end != '\0' || port < 1 || port > 65535) {
        goto fail_syntax;
    }

    fwd = qemu_malloc(sizeof(struct GuestFwd));
    snprintf(buf, sizeof(buf), "guestfwd.tcp:%d", port);
    fwd->hd = qemu_chr_open(buf, p, NULL);
    if (!fwd->hd) {
        error_report("could not open guest forwarding device '%s'", buf);
        qemu_free(fwd);
        return -1;
    }

    if (slirp_add_exec(s->slirp, 3, fwd->hd, &server, port) < 0) {
        error_report("conflicting/invalid host:port in guest forwarding "
                     "rule '%s'", config_str);
        qemu_free(fwd);
        return -1;
    }
    fwd->server = server;
    fwd->port = port;
    fwd->slirp = s->slirp;

    qemu_chr_add_handlers(fwd->hd, guestfwd_can_read, guestfwd_read,
                          NULL, fwd);
    return 0;

 fail_syntax:
    error_report("invalid guest forwarding rule '%s'", config_str);
    return -1;
}

void do_info_usernet(Monitor *mon)
{
    SlirpState *s;

    QTAILQ_FOREACH(s, &slirp_stacks, entry) {
        monitor_printf(mon, "VLAN %d (%s):\n",
                       s->nc.vlan ? s->nc.vlan->id : -1,
                       s->nc.name);
        slirp_connection_info(s->slirp, mon);
    }
}

static int net_init_slirp_configs(const char *name, const char *value, void *opaque)
{
    struct slirp_config_str *config;

    if (strcmp(name, "hostfwd") != 0 && strcmp(name, "guestfwd") != 0) {
        return 0;
    }

    config = qemu_mallocz(sizeof(*config));

    pstrcpy(config->str, sizeof(config->str), value);

    if (!strcmp(name, "hostfwd")) {
        config->flags = SLIRP_CFG_HOSTFWD;
    }

    config->next = slirp_configs;
    slirp_configs = config;

    return 0;
}

int net_init_slirp(QemuOpts *opts,
                   Monitor *mon,
                   const char *name,
                   VLANState *vlan)
{
    struct slirp_config_str *config;
    const char *vhost;
    const char *vhostname;
    const char *vdhcp_start;
    const char *vnamesrv;
    const char *tftp_export;
    const char *bootfile;
    const char *smb_export;
    const char *vsmbsrv;
    char *vnet = NULL;
    int restricted = 0;
    int ret;

    vhost       = qemu_opt_get(opts, "host");
    vhostname   = qemu_opt_get(opts, "hostname");
    vdhcp_start = qemu_opt_get(opts, "dhcpstart");
    vnamesrv    = qemu_opt_get(opts, "dns");
    tftp_export = qemu_opt_get(opts, "tftp");
    bootfile    = qemu_opt_get(opts, "bootfile");
    smb_export  = qemu_opt_get(opts, "smb");
    vsmbsrv     = qemu_opt_get(opts, "smbserver");

    if (qemu_opt_get(opts, "ip")) {
        const char *ip = qemu_opt_get(opts, "ip");
        int l = strlen(ip) + strlen("/24") + 1;

        vnet = qemu_malloc(l);

        /* emulate legacy ip= parameter */
        pstrcpy(vnet, l, ip);
        pstrcat(vnet, l, "/24");
    }

    if (qemu_opt_get(opts, "net")) {
        if (vnet) {
            qemu_free(vnet);
        }
        vnet = qemu_strdup(qemu_opt_get(opts, "net"));
    }

    if (qemu_opt_get(opts, "restrict") &&
        qemu_opt_get(opts, "restrict")[0] == 'y') {
        restricted = 1;
    }

    qemu_opt_foreach(opts, net_init_slirp_configs, NULL, 0);

    ret = net_slirp_init(vlan, "user", name, restricted, vnet, vhost,
                         vhostname, tftp_export, bootfile, vdhcp_start,
                         vnamesrv, smb_export, vsmbsrv);

    while (slirp_configs) {
        config = slirp_configs;
        slirp_configs = config->next;
        qemu_free(config);
    }

    qemu_free(vnet);

    return ret;
}

int net_slirp_parse_legacy(QemuOptsList *opts_list, const char *optarg, int *ret)
{
    if (strcmp(opts_list->name, "net") != 0 ||
        strncmp(optarg, "channel,", strlen("channel,")) != 0) {
        return 0;
    }

    /* handle legacy -net channel,port:chr */
    optarg += strlen("channel,");

    if (QTAILQ_EMPTY(&slirp_stacks)) {
        struct slirp_config_str *config;

        config = qemu_malloc(sizeof(*config));
        pstrcpy(config->str, sizeof(config->str), optarg);
        config->flags = SLIRP_CFG_LEGACY;
        config->next = slirp_configs;
        slirp_configs = config;
        *ret = 0;
    } else {
        *ret = slirp_guestfwd(QTAILQ_FIRST(&slirp_stacks), optarg, 1);
    }

    return 1;
}

