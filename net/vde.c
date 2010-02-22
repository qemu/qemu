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
#include "net/vde.h"

#include "config-host.h"

#include <libvdeplug.h>

#include "net.h"
#include "qemu-char.h"
#include "qemu-common.h"
#include "qemu-option.h"
#include "sysemu.h"

typedef struct VDEState {
    VLANClientState nc;
    VDECONN *vde;
} VDEState;

static void vde_to_qemu(void *opaque)
{
    VDEState *s = opaque;
    uint8_t buf[4096];
    int size;

    size = vde_recv(s->vde, (char *)buf, sizeof(buf), 0);
    if (size > 0) {
        qemu_send_packet(&s->nc, buf, size);
    }
}

static ssize_t vde_receive(VLANClientState *nc, const uint8_t *buf, size_t size)
{
    VDEState *s = DO_UPCAST(VDEState, nc, nc);
    ssize_t ret;

    do {
      ret = vde_send(s->vde, (const char *)buf, size, 0);
    } while (ret < 0 && errno == EINTR);

    return ret;
}

static void vde_cleanup(VLANClientState *nc)
{
    VDEState *s = DO_UPCAST(VDEState, nc, nc);
    qemu_set_fd_handler(vde_datafd(s->vde), NULL, NULL, NULL);
    vde_close(s->vde);
}

static NetClientInfo net_vde_info = {
    .type = NET_CLIENT_TYPE_VDE,
    .size = sizeof(VDEState),
    .receive = vde_receive,
    .cleanup = vde_cleanup,
};

static int net_vde_init(VLANState *vlan, const char *model,
                        const char *name, const char *sock,
                        int port, const char *group, int mode)
{
    VLANClientState *nc;
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

    nc = qemu_new_net_client(&net_vde_info, vlan, NULL, model, name);

    snprintf(nc->info_str, sizeof(nc->info_str), "sock=%s,fd=%d",
             sock, vde_datafd(vde));

    s = DO_UPCAST(VDEState, nc, nc);

    s->vde = vde;

    qemu_set_fd_handler(vde_datafd(s->vde), vde_to_qemu, NULL, s);

    return 0;
}

int net_init_vde(QemuOpts *opts, Monitor *mon, const char *name, VLANState *vlan)
{
    const char *sock;
    const char *group;
    int port, mode;

    sock  = qemu_opt_get(opts, "sock");
    group = qemu_opt_get(opts, "group");

    port = qemu_opt_get_number(opts, "port", 0);
    mode = qemu_opt_get_number(opts, "mode", 0700);

    if (net_vde_init(vlan, "vde", name, sock, port, group, mode) == -1) {
        return -1;
    }

    return 0;
}
