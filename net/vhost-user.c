/*
 * vhost-user.c
 *
 * Copyright (c) 2013 Virtual Open Systems Sarl.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "clients.h"
#include "net/vhost_net.h"
#include "net/vhost-user.h"
#include "sysemu/char.h"
#include "qemu/config-file.h"
#include "qemu/error-report.h"
#include "qmp-commands.h"
#include "trace.h"

typedef struct VhostUserState {
    NetClientState nc;
    CharDriverState *chr;
    VHostNetState *vhost_net;
    int watch;
    uint64_t acked_features;
} VhostUserState;

typedef struct VhostUserChardevProps {
    bool is_socket;
    bool is_unix;
} VhostUserChardevProps;

VHostNetState *vhost_user_get_vhost_net(NetClientState *nc)
{
    VhostUserState *s = DO_UPCAST(VhostUserState, nc, nc);
    assert(nc->info->type == NET_CLIENT_OPTIONS_KIND_VHOST_USER);
    return s->vhost_net;
}

uint64_t vhost_user_get_acked_features(NetClientState *nc)
{
    VhostUserState *s = DO_UPCAST(VhostUserState, nc, nc);
    assert(nc->info->type == NET_CLIENT_OPTIONS_KIND_VHOST_USER);
    return s->acked_features;
}

static int vhost_user_running(VhostUserState *s)
{
    return (s->vhost_net) ? 1 : 0;
}

static void vhost_user_stop(int queues, NetClientState *ncs[])
{
    VhostUserState *s;
    int i;

    for (i = 0; i < queues; i++) {
        assert (ncs[i]->info->type == NET_CLIENT_OPTIONS_KIND_VHOST_USER);

        s = DO_UPCAST(VhostUserState, nc, ncs[i]);
        if (!vhost_user_running(s)) {
            continue;
        }

        if (s->vhost_net) {
            /* save acked features */
            s->acked_features = vhost_net_get_acked_features(s->vhost_net);
            vhost_net_cleanup(s->vhost_net);
            s->vhost_net = NULL;
        }
    }
}

static int vhost_user_start(int queues, NetClientState *ncs[])
{
    VhostNetOptions options;
    VhostUserState *s;
    int max_queues;
    int i;

    options.backend_type = VHOST_BACKEND_TYPE_USER;

    for (i = 0; i < queues; i++) {
        assert (ncs[i]->info->type == NET_CLIENT_OPTIONS_KIND_VHOST_USER);

        s = DO_UPCAST(VhostUserState, nc, ncs[i]);
        if (vhost_user_running(s)) {
            continue;
        }

        options.net_backend = ncs[i];
        options.opaque      = s->chr;
        s->vhost_net = vhost_net_init(&options);
        if (!s->vhost_net) {
            error_report("failed to init vhost_net for queue %d", i);
            goto err;
        }

        if (i == 0) {
            max_queues = vhost_net_get_max_queues(s->vhost_net);
            if (queues > max_queues) {
                error_report("you are asking more queues than supported: %d",
                             max_queues);
                goto err;
            }
        }
    }

    return 0;

err:
    vhost_user_stop(i + 1, ncs);
    return -1;
}

static ssize_t vhost_user_receive(NetClientState *nc, const uint8_t *buf,
                                  size_t size)
{
    /* In case of RARP (message size is 60) notify backup to send a fake RARP.
       This fake RARP will be sent by backend only for guest
       without GUEST_ANNOUNCE capability.
     */
    if (size == 60) {
        VhostUserState *s = DO_UPCAST(VhostUserState, nc, nc);
        int r;
        static int display_rarp_failure = 1;
        char mac_addr[6];

        /* extract guest mac address from the RARP message */
        memcpy(mac_addr, &buf[6], 6);

        r = vhost_net_notify_migration_done(s->vhost_net, mac_addr);

        if ((r != 0) && (display_rarp_failure)) {
            fprintf(stderr,
                    "Vhost user backend fails to broadcast fake RARP\n");
            fflush(stderr);
            display_rarp_failure = 0;
        }
    }

    return size;
}

static void vhost_user_cleanup(NetClientState *nc)
{
    VhostUserState *s = DO_UPCAST(VhostUserState, nc, nc);

    if (s->vhost_net) {
        vhost_net_cleanup(s->vhost_net);
        s->vhost_net = NULL;
    }

    qemu_purge_queued_packets(nc);
}

static bool vhost_user_has_vnet_hdr(NetClientState *nc)
{
    assert(nc->info->type == NET_CLIENT_OPTIONS_KIND_VHOST_USER);

    return true;
}

static bool vhost_user_has_ufo(NetClientState *nc)
{
    assert(nc->info->type == NET_CLIENT_OPTIONS_KIND_VHOST_USER);

    return true;
}

static NetClientInfo net_vhost_user_info = {
        .type = NET_CLIENT_OPTIONS_KIND_VHOST_USER,
        .size = sizeof(VhostUserState),
        .receive = vhost_user_receive,
        .cleanup = vhost_user_cleanup,
        .has_vnet_hdr = vhost_user_has_vnet_hdr,
        .has_ufo = vhost_user_has_ufo,
};

static gboolean net_vhost_user_watch(GIOChannel *chan, GIOCondition cond,
                                           void *opaque)
{
    VhostUserState *s = opaque;
    uint8_t buf[1];

    /* We don't actually want to read anything, but CHR_EVENT_CLOSED will be
     * raised as a side-effect of the read.
     */
    qemu_chr_fe_read_all(s->chr, buf, sizeof(buf));

    return FALSE;
}

static void net_vhost_user_event(void *opaque, int event)
{
    const char *name = opaque;
    NetClientState *ncs[MAX_QUEUE_NUM];
    VhostUserState *s;
    Error *err = NULL;
    int queues;

    queues = qemu_find_net_clients_except(name, ncs,
                                          NET_CLIENT_OPTIONS_KIND_NIC,
                                          MAX_QUEUE_NUM);
    assert(queues < MAX_QUEUE_NUM);

    s = DO_UPCAST(VhostUserState, nc, ncs[0]);
    trace_vhost_user_event(s->chr->label, event);
    switch (event) {
    case CHR_EVENT_OPENED:
        s->watch = qemu_chr_fe_add_watch(s->chr, G_IO_HUP,
                                         net_vhost_user_watch, s);
        if (vhost_user_start(queues, ncs) < 0) {
            qemu_chr_disconnect(s->chr);
            return;
        }
        qmp_set_link(name, true, &err);
        break;
    case CHR_EVENT_CLOSED:
        qmp_set_link(name, false, &err);
        vhost_user_stop(queues, ncs);
        g_source_remove(s->watch);
        s->watch = 0;
        break;
    }

    if (err) {
        error_report_err(err);
    }
}

static int net_vhost_user_init(NetClientState *peer, const char *device,
                               const char *name, CharDriverState *chr,
                               int queues)
{
    NetClientState *nc;
    VhostUserState *s;
    int i;

    assert(name);
    assert(queues > 0);

    for (i = 0; i < queues; i++) {
        nc = qemu_new_net_client(&net_vhost_user_info, peer, device, name);

        snprintf(nc->info_str, sizeof(nc->info_str), "vhost-user%d to %s",
                 i, chr->label);

        nc->queue_index = i;

        s = DO_UPCAST(VhostUserState, nc, nc);
        s->chr = chr;
    }

    qemu_chr_add_handlers(chr, NULL, NULL, net_vhost_user_event, nc[0].name);

    return 0;
}

static int net_vhost_chardev_opts(void *opaque,
                                  const char *name, const char *value,
                                  Error **errp)
{
    VhostUserChardevProps *props = opaque;

    if (strcmp(name, "backend") == 0 && strcmp(value, "socket") == 0) {
        props->is_socket = true;
    } else if (strcmp(name, "path") == 0) {
        props->is_unix = true;
    } else if (strcmp(name, "server") == 0) {
    } else {
        error_setg(errp,
                   "vhost-user does not support a chardev with option %s=%s",
                   name, value);
        return -1;
    }
    return 0;
}

static CharDriverState *net_vhost_parse_chardev(
    const NetdevVhostUserOptions *opts, Error **errp)
{
    CharDriverState *chr = qemu_chr_find(opts->chardev);
    VhostUserChardevProps props;

    if (chr == NULL) {
        error_setg(errp, "chardev \"%s\" not found", opts->chardev);
        return NULL;
    }

    /* inspect chardev opts */
    memset(&props, 0, sizeof(props));
    if (qemu_opt_foreach(chr->opts, net_vhost_chardev_opts, &props, errp)) {
        return NULL;
    }

    if (!props.is_socket || !props.is_unix) {
        error_setg(errp, "chardev \"%s\" is not a unix socket",
                   opts->chardev);
        return NULL;
    }

    qemu_chr_fe_claim_no_fail(chr);

    return chr;
}

static int net_vhost_check_net(void *opaque, QemuOpts *opts, Error **errp)
{
    const char *name = opaque;
    const char *driver, *netdev;
    const char virtio_name[] = "virtio-net-";

    driver = qemu_opt_get(opts, "driver");
    netdev = qemu_opt_get(opts, "netdev");

    if (!driver || !netdev) {
        return 0;
    }

    if (strcmp(netdev, name) == 0 &&
        strncmp(driver, virtio_name, strlen(virtio_name)) != 0) {
        error_setg(errp, "vhost-user requires frontend driver virtio-net-*");
        return -1;
    }

    return 0;
}

int net_init_vhost_user(const NetClientOptions *opts, const char *name,
                        NetClientState *peer, Error **errp)
{
    int queues;
    const NetdevVhostUserOptions *vhost_user_opts;
    CharDriverState *chr;

    assert(opts->type == NET_CLIENT_OPTIONS_KIND_VHOST_USER);
    vhost_user_opts = opts->u.vhost_user.data;

    chr = net_vhost_parse_chardev(vhost_user_opts, errp);
    if (!chr) {
        return -1;
    }

    /* verify net frontend */
    if (qemu_opts_foreach(qemu_find_opts("device"), net_vhost_check_net,
                          (char *)name, errp)) {
        return -1;
    }

    queues = vhost_user_opts->has_queues ? vhost_user_opts->queues : 1;
    if (queues < 1 || queues > MAX_QUEUE_NUM) {
        error_setg(errp,
                   "vhost-user number of queues must be in range [1, %d]",
                   MAX_QUEUE_NUM);
        return -1;
    }

    return net_vhost_user_init(peer, "vhost_user", name, chr, queues);
}
