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

#include <libvdeplug.h>

#include "net/net.h"
#include "clients.h"
#include "qemu-common.h"
#include "qemu/option.h"
#include "qemu/main-loop.h"

typedef struct VDEState {
    NetClientState nc;
    VDECONN *vde;
} VDEState;

static void vde_to_qemu(void *opaque)
{
    VDEState *s = opaque;
    uint8_t buf[NET_BUFSIZE];
    int size;

    size = vde_recv(s->vde, (char *)buf, sizeof(buf), 0);
    if (size > 0) {
        qemu_send_packet(&s->nc, buf, size);
    }
}

static ssize_t vde_receive(NetClientState *nc, const uint8_t *buf, size_t size)
{
    VDEState *s = DO_UPCAST(VDEState, nc, nc);
    ssize_t ret;

    do {
      ret = vde_send(s->vde, (const char *)buf, size, 0);
    } while (ret < 0 && errno == EINTR);

    return ret;
}

static void vde_cleanup(NetClientState *nc)
{
    VDEState *s = DO_UPCAST(VDEState, nc, nc);
    qemu_set_fd_handler(vde_datafd(s->vde), NULL, NULL, NULL);
    vde_close(s->vde);
}

static NetClientInfo net_vde_info = {
    .type = NET_CLIENT_OPTIONS_KIND_VDE,
    .size = sizeof(VDEState),
    .receive = vde_receive,
    .cleanup = vde_cleanup,
};

static int net_vde_init(NetClientState *peer, const char *model,
                        const char *name, const char *sock,
                        int port, const char *group, int mode)
{
    NetClientState *nc;
    VDEState *s;
    VDECONN *vde;
    char *init_group = (char *)group;
    char *init_sock = (char *)sock;

    struct vde_open_args args = {
        .port = port,
        .group = init_group,
        .mode = mode,
    };

    vde = vde_open(init_sock, (char *)"QEMU", &args);
    if (!vde){
        return -1;
    }

    nc = qemu_new_net_client(&net_vde_info, peer, model, name);

    snprintf(nc->info_str, sizeof(nc->info_str), "sock=%s,fd=%d",
             sock, vde_datafd(vde));

    s = DO_UPCAST(VDEState, nc, nc);

    s->vde = vde;

    qemu_set_fd_handler(vde_datafd(s->vde), vde_to_qemu, NULL, s);

    return 0;
}

int net_init_vde(const NetClientOptions *opts, const char *name,
                 NetClientState *peer, Error **errp)
{
    /* FIXME error_setg(errp, ...) on failure */
    const NetdevVdeOptions *vde;

    assert(opts->type == NET_CLIENT_OPTIONS_KIND_VDE);
    vde = opts->u.vde;

    /* missing optional values have been initialized to "all bits zero" */
    if (net_vde_init(peer, "vde", name, vde->sock, vde->port, vde->group,
                     vde->has_mode ? vde->mode : 0700) == -1) {
        return -1;
    }

    return 0;
}
