/*
 * vhost-user.c
 *
 * Copyright (c) 2013 Virtual Open Systems Sarl.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "clients.h"
#include "net/vhost_net.h"
#include "net/vhost-user.h"
#include "sysemu/char.h"
#include "qemu/error-report.h"

typedef struct VhostUserState {
    NetClientState nc;
    CharDriverState *chr;
    bool vhostforce;
    VHostNetState *vhost_net;
} VhostUserState;

VHostNetState *vhost_user_get_vhost_net(NetClientState *nc)
{
    VhostUserState *s = DO_UPCAST(VhostUserState, nc, nc);
    return s->vhost_net;
}

static int vhost_user_running(VhostUserState *s)
{
    return (s->vhost_net) ? 1 : 0;
}

static int vhost_user_start(VhostUserState *s)
{
    VhostNetOptions options;

    if (vhost_user_running(s)) {
        return 0;
    }

    options.backend_type = VHOST_BACKEND_TYPE_USER;
    options.net_backend = &s->nc;
    options.opaque = s->chr;
    options.force = s->vhostforce;

    s->vhost_net = vhost_net_init(&options);

    return vhost_user_running(s) ? 0 : -1;
}

static void vhost_user_stop(VhostUserState *s)
{
    if (vhost_user_running(s)) {
        vhost_net_cleanup(s->vhost_net);
    }

    s->vhost_net = 0;
}

static void vhost_user_cleanup(NetClientState *nc)
{
    VhostUserState *s = DO_UPCAST(VhostUserState, nc, nc);

    vhost_user_stop(s);
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
        .type = 0,
        .size = sizeof(VhostUserState),
        .cleanup = vhost_user_cleanup,
        .has_vnet_hdr = vhost_user_has_vnet_hdr,
        .has_ufo = vhost_user_has_ufo,
};

static void net_vhost_link_down(VhostUserState *s, bool link_down)
{
    s->nc.link_down = link_down;

    if (s->nc.peer) {
        s->nc.peer->link_down = link_down;
    }

    if (s->nc.info->link_status_changed) {
        s->nc.info->link_status_changed(&s->nc);
    }

    if (s->nc.peer && s->nc.peer->info->link_status_changed) {
        s->nc.peer->info->link_status_changed(s->nc.peer);
    }
}

static void net_vhost_user_event(void *opaque, int event)
{
    VhostUserState *s = opaque;

    switch (event) {
    case CHR_EVENT_OPENED:
        vhost_user_start(s);
        net_vhost_link_down(s, false);
        error_report("chardev \"%s\" went up\n", s->chr->label);
        break;
    case CHR_EVENT_CLOSED:
        net_vhost_link_down(s, true);
        vhost_user_stop(s);
        error_report("chardev \"%s\" went down\n", s->chr->label);
        break;
    }
}

static int net_vhost_user_init(NetClientState *peer, const char *device,
                               const char *name, CharDriverState *chr,
                               bool vhostforce)
{
    NetClientState *nc;
    VhostUserState *s;

    nc = qemu_new_net_client(&net_vhost_user_info, peer, device, name);

    snprintf(nc->info_str, sizeof(nc->info_str), "vhost-user to %s",
             chr->label);

    s = DO_UPCAST(VhostUserState, nc, nc);

    /* We don't provide a receive callback */
    s->nc.receive_disabled = 1;
    s->chr = chr;
    s->vhostforce = vhostforce;

    qemu_chr_add_handlers(s->chr, NULL, NULL, net_vhost_user_event, s);

    return 0;
}

int net_init_vhost_user(const NetClientOptions *opts, const char *name,
                   NetClientState *peer)
{
    return net_vhost_user_init(peer, "vhost_user", 0, 0, 0);
}
