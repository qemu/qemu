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
    bool started;
} VhostUserState;

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

static void vhost_user_stop(int queues, NetClientState *ncs[])
{
    VhostUserState *s;
    int i;

    for (i = 0; i < queues; i++) {
        assert (ncs[i]->info->type == NET_CLIENT_OPTIONS_KIND_VHOST_USER);

        s = DO_UPCAST(VhostUserState, nc, ncs[i]);

        if (s->vhost_net) {
            /* save acked features */
            uint64_t features = vhost_net_get_acked_features(s->vhost_net);
            if (features) {
                s->acked_features = features;
            }
            vhost_net_cleanup(s->vhost_net);
        }
    }
}

static int vhost_user_start(int queues, NetClientState *ncs[])
{
    VhostNetOptions options;
    struct vhost_net *net = NULL;
    VhostUserState *s;
    int max_queues;
    int i;

    options.backend_type = VHOST_BACKEND_TYPE_USER;

    for (i = 0; i < queues; i++) {
        assert (ncs[i]->info->type == NET_CLIENT_OPTIONS_KIND_VHOST_USER);

        s = DO_UPCAST(VhostUserState, nc, ncs[i]);

        options.net_backend = ncs[i];
        options.opaque      = s->chr;
        options.busyloop_timeout = 0;
        net = vhost_net_init(&options);
        if (!net) {
            error_report("failed to init vhost_net for queue %d", i);
            goto err;
        }

        if (i == 0) {
            max_queues = vhost_net_get_max_queues(net);
            if (queues > max_queues) {
                error_report("you are asking more queues than supported: %d",
                             max_queues);
                goto err;
            }
        }

        if (s->vhost_net) {
            vhost_net_cleanup(s->vhost_net);
            g_free(s->vhost_net);
        }
        s->vhost_net = net;
    }

    return 0;

err:
    if (net) {
        vhost_net_cleanup(net);
    }
    vhost_user_stop(i, ncs);
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
        g_free(s->vhost_net);
        s->vhost_net = NULL;
    }
    if (s->chr) {
        qemu_chr_add_handlers(s->chr, NULL, NULL, NULL, NULL);
        qemu_chr_fe_release(s->chr);
        s->chr = NULL;
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

    qemu_chr_disconnect(s->chr);

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
        s->started = true;
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
    NetClientState *nc, *nc0 = NULL;
    VhostUserState *s;
    int i;

    assert(name);
    assert(queues > 0);

    for (i = 0; i < queues; i++) {
        nc = qemu_new_net_client(&net_vhost_user_info, peer, device, name);
        if (!nc0) {
            nc0 = nc;
        }

        snprintf(nc->info_str, sizeof(nc->info_str), "vhost-user%d to %s",
                 i, chr->label);

        nc->queue_index = i;

        s = DO_UPCAST(VhostUserState, nc, nc);
        s->chr = chr;
    }

    s = DO_UPCAST(VhostUserState, nc, nc0);
    do {
        Error *err = NULL;
        if (qemu_chr_wait_connected(chr, &err) < 0) {
            error_report_err(err);
            return -1;
        }
        qemu_chr_add_handlers(chr, NULL, NULL,
                              net_vhost_user_event, nc0->name);
    } while (!s->started);

    assert(s->vhost_net);

    return 0;
}

static CharDriverState *net_vhost_claim_chardev(
    const NetdevVhostUserOptions *opts, Error **errp)
{
    CharDriverState *chr = qemu_chr_find(opts->chardev);

    if (chr == NULL) {
        error_setg(errp, "chardev \"%s\" not found", opts->chardev);
        return NULL;
    }

    if (!qemu_chr_has_feature(chr, QEMU_CHAR_FEATURE_RECONNECTABLE)) {
        error_setg(errp, "chardev \"%s\" is not reconnectable",
                   opts->chardev);
        return NULL;
    }
    if (!qemu_chr_has_feature(chr, QEMU_CHAR_FEATURE_FD_PASS)) {
        error_setg(errp, "chardev \"%s\" does not support FD passing",
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

    chr = net_vhost_claim_chardev(vhost_user_opts, errp);
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
