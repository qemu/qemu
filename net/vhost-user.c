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
#include "hw/virtio/vhost-user.h"
#include "chardev/char-fe.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-net.h"
#include "qapi/qapi-events-net.h"
#include "qemu/config-file.h"
#include "qemu/error-report.h"
#include "qemu/option.h"
#include "trace.h"

typedef struct NetVhostUserState {
    NetClientState nc;
    CharBackend chr; /* only queue index 0 */
    VhostUserState *vhost_user;
    VHostNetState *vhost_net;
    guint watch;
    uint64_t acked_features;
    bool started;
} NetVhostUserState;

VHostNetState *vhost_user_get_vhost_net(NetClientState *nc)
{
    NetVhostUserState *s = DO_UPCAST(NetVhostUserState, nc, nc);
    assert(nc->info->type == NET_CLIENT_DRIVER_VHOST_USER);
    return s->vhost_net;
}

uint64_t vhost_user_get_acked_features(NetClientState *nc)
{
    NetVhostUserState *s = DO_UPCAST(NetVhostUserState, nc, nc);
    assert(nc->info->type == NET_CLIENT_DRIVER_VHOST_USER);
    return s->acked_features;
}

void vhost_user_save_acked_features(NetClientState *nc)
{
    NetVhostUserState *s;

    s = DO_UPCAST(NetVhostUserState, nc, nc);
    if (s->vhost_net) {
        uint64_t features = vhost_net_get_acked_features(s->vhost_net);
        if (features) {
            s->acked_features = features;
        }
    }
}

static void vhost_user_stop(int queues, NetClientState *ncs[])
{
    int i;
    NetVhostUserState *s;

    for (i = 0; i < queues; i++) {
        assert(ncs[i]->info->type == NET_CLIENT_DRIVER_VHOST_USER);

        s = DO_UPCAST(NetVhostUserState, nc, ncs[i]);

        if (s->vhost_net) {
            vhost_user_save_acked_features(ncs[i]);
            vhost_net_cleanup(s->vhost_net);
        }
    }
}

static int vhost_user_start(int queues, NetClientState *ncs[],
                            VhostUserState *be)
{
    VhostNetOptions options;
    struct vhost_net *net = NULL;
    NetVhostUserState *s;
    int max_queues;
    int i;

    options.backend_type = VHOST_BACKEND_TYPE_USER;

    for (i = 0; i < queues; i++) {
        assert(ncs[i]->info->type == NET_CLIENT_DRIVER_VHOST_USER);

        s = DO_UPCAST(NetVhostUserState, nc, ncs[i]);

        options.net_backend = ncs[i];
        options.opaque      = be;
        options.busyloop_timeout = 0;
        options.nvqs = 2;
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
        g_free(net);
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
        NetVhostUserState *s = DO_UPCAST(NetVhostUserState, nc, nc);
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

static void net_vhost_user_cleanup(NetClientState *nc)
{
    NetVhostUserState *s = DO_UPCAST(NetVhostUserState, nc, nc);

    if (s->vhost_net) {
        vhost_net_cleanup(s->vhost_net);
        g_free(s->vhost_net);
        s->vhost_net = NULL;
    }
    if (nc->queue_index == 0) {
        if (s->watch) {
            g_source_remove(s->watch);
            s->watch = 0;
        }
        qemu_chr_fe_deinit(&s->chr, true);
        if (s->vhost_user) {
            vhost_user_cleanup(s->vhost_user);
            g_free(s->vhost_user);
            s->vhost_user = NULL;
        }
    }

    qemu_purge_queued_packets(nc);
}

static int vhost_user_set_vnet_endianness(NetClientState *nc,
                                          bool enable)
{
    /* Nothing to do.  If the server supports
     * VHOST_USER_PROTOCOL_F_CROSS_ENDIAN, it will get the
     * vnet header endianness from there.  If it doesn't, negotiation
     * fails.
     */
    return 0;
}

static bool vhost_user_has_vnet_hdr(NetClientState *nc)
{
    assert(nc->info->type == NET_CLIENT_DRIVER_VHOST_USER);

    return true;
}

static bool vhost_user_has_ufo(NetClientState *nc)
{
    assert(nc->info->type == NET_CLIENT_DRIVER_VHOST_USER);

    return true;
}

static bool vhost_user_check_peer_type(NetClientState *nc, ObjectClass *oc,
                                       Error **errp)
{
    const char *driver = object_class_get_name(oc);

    if (!g_str_has_prefix(driver, "virtio-net-")) {
        error_setg(errp, "vhost-user requires frontend driver virtio-net-*");
        return false;
    }

    return true;
}

static NetClientInfo net_vhost_user_info = {
        .type = NET_CLIENT_DRIVER_VHOST_USER,
        .size = sizeof(NetVhostUserState),
        .receive = vhost_user_receive,
        .cleanup = net_vhost_user_cleanup,
        .has_vnet_hdr = vhost_user_has_vnet_hdr,
        .has_ufo = vhost_user_has_ufo,
        .set_vnet_be = vhost_user_set_vnet_endianness,
        .set_vnet_le = vhost_user_set_vnet_endianness,
        .check_peer_type = vhost_user_check_peer_type,
};

static gboolean net_vhost_user_watch(void *do_not_use, GIOCondition cond,
                                     void *opaque)
{
    NetVhostUserState *s = opaque;

    qemu_chr_fe_disconnect(&s->chr);

    return G_SOURCE_CONTINUE;
}

static void net_vhost_user_event(void *opaque, QEMUChrEvent event);

static void chr_closed_bh(void *opaque)
{
    const char *name = opaque;
    NetClientState *ncs[MAX_QUEUE_NUM];
    NetVhostUserState *s;
    Error *err = NULL;
    int queues, i;

    queues = qemu_find_net_clients_except(name, ncs,
                                          NET_CLIENT_DRIVER_NIC,
                                          MAX_QUEUE_NUM);
    assert(queues < MAX_QUEUE_NUM);

    s = DO_UPCAST(NetVhostUserState, nc, ncs[0]);

    for (i = queues -1; i >= 0; i--) {
        vhost_user_save_acked_features(ncs[i]);
    }

    qmp_set_link(name, false, &err);

    qemu_chr_fe_set_handlers(&s->chr, NULL, NULL, net_vhost_user_event,
                             NULL, opaque, NULL, true);

    if (err) {
        error_report_err(err);
    }
    qapi_event_send_netdev_vhost_user_disconnected(name);
}

static void net_vhost_user_event(void *opaque, QEMUChrEvent event)
{
    const char *name = opaque;
    NetClientState *ncs[MAX_QUEUE_NUM];
    NetVhostUserState *s;
    Chardev *chr;
    Error *err = NULL;
    int queues;

    queues = qemu_find_net_clients_except(name, ncs,
                                          NET_CLIENT_DRIVER_NIC,
                                          MAX_QUEUE_NUM);
    assert(queues < MAX_QUEUE_NUM);

    s = DO_UPCAST(NetVhostUserState, nc, ncs[0]);
    chr = qemu_chr_fe_get_driver(&s->chr);
    trace_vhost_user_event(chr->label, event);
    switch (event) {
    case CHR_EVENT_OPENED:
        if (vhost_user_start(queues, ncs, s->vhost_user) < 0) {
            qemu_chr_fe_disconnect(&s->chr);
            return;
        }
        s->watch = qemu_chr_fe_add_watch(&s->chr, G_IO_HUP,
                                         net_vhost_user_watch, s);
        qmp_set_link(name, true, &err);
        s->started = true;
        qapi_event_send_netdev_vhost_user_connected(name, chr->label);
        break;
    case CHR_EVENT_CLOSED:
        /* a close event may happen during a read/write, but vhost
         * code assumes the vhost_dev remains setup, so delay the
         * stop & clear to idle.
         * FIXME: better handle failure in vhost code, remove bh
         */
        if (s->watch) {
            AioContext *ctx = qemu_get_current_aio_context();

            g_source_remove(s->watch);
            s->watch = 0;
            qemu_chr_fe_set_handlers(&s->chr, NULL, NULL, NULL, NULL,
                                     NULL, NULL, false);

            aio_bh_schedule_oneshot(ctx, chr_closed_bh, opaque);
        }
        break;
    case CHR_EVENT_BREAK:
    case CHR_EVENT_MUX_IN:
    case CHR_EVENT_MUX_OUT:
        /* Ignore */
        break;
    }

    if (err) {
        error_report_err(err);
    }
}

static int net_vhost_user_init(NetClientState *peer, const char *device,
                               const char *name, Chardev *chr,
                               int queues)
{
    Error *err = NULL;
    NetClientState *nc, *nc0 = NULL;
    NetVhostUserState *s = NULL;
    VhostUserState *user;
    int i;

    assert(name);
    assert(queues > 0);

    user = g_new0(struct VhostUserState, 1);
    for (i = 0; i < queues; i++) {
        nc = qemu_new_net_client(&net_vhost_user_info, peer, device, name);
        qemu_set_info_str(nc, "vhost-user%d to %s", i, chr->label);
        nc->queue_index = i;
        if (!nc0) {
            nc0 = nc;
            s = DO_UPCAST(NetVhostUserState, nc, nc);
            if (!qemu_chr_fe_init(&s->chr, chr, &err) ||
                !vhost_user_init(user, &s->chr, &err)) {
                error_report_err(err);
                goto err;
            }
        }
        s = DO_UPCAST(NetVhostUserState, nc, nc);
        s->vhost_user = user;
    }

    s = DO_UPCAST(NetVhostUserState, nc, nc0);
    do {
        if (qemu_chr_fe_wait_connected(&s->chr, &err) < 0) {
            error_report_err(err);
            goto err;
        }
        qemu_chr_fe_set_handlers(&s->chr, NULL, NULL,
                                 net_vhost_user_event, NULL, nc0->name, NULL,
                                 true);
    } while (!s->started);

    assert(s->vhost_net);

    return 0;

err:
    if (user) {
        vhost_user_cleanup(user);
        g_free(user);
        if (s) {
            s->vhost_user = NULL;
        }
    }
    if (nc0) {
        qemu_del_net_client(nc0);
    }

    return -1;
}

static Chardev *net_vhost_claim_chardev(
    const NetdevVhostUserOptions *opts, Error **errp)
{
    Chardev *chr = qemu_chr_find(opts->chardev);

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

    return chr;
}

int net_init_vhost_user(const Netdev *netdev, const char *name,
                        NetClientState *peer, Error **errp)
{
    int queues;
    const NetdevVhostUserOptions *vhost_user_opts;
    Chardev *chr;

    assert(netdev->type == NET_CLIENT_DRIVER_VHOST_USER);
    vhost_user_opts = &netdev->u.vhost_user;

    chr = net_vhost_claim_chardev(vhost_user_opts, errp);
    if (!chr) {
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
