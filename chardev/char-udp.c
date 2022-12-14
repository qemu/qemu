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
#include "chardev/char.h"
#include "io/channel-socket.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "qemu/option.h"

#include "chardev/char-io.h"
#include "qom/object.h"

/***********************************************************/
/* UDP Net console */

struct UdpChardev {
    Chardev parent;
    QIOChannel *ioc;
    uint8_t buf[CHR_READ_BUF_LEN];
    int bufcnt;
    int bufptr;
    int max_size;
};
typedef struct UdpChardev UdpChardev;

DECLARE_INSTANCE_CHECKER(UdpChardev, UDP_CHARDEV,
                         TYPE_CHARDEV_UDP)

/* Called with chr_write_lock held.  */
static int udp_chr_write(Chardev *chr, const uint8_t *buf, int len)
{
    UdpChardev *s = UDP_CHARDEV(chr);

    return qio_channel_write(
        s->ioc, (const char *)buf, len, NULL);
}

static void udp_chr_flush_buffer(UdpChardev *s)
{
    Chardev *chr = CHARDEV(s);

    while (s->max_size > 0 && s->bufptr < s->bufcnt) {
        int n = MIN(s->max_size, s->bufcnt - s->bufptr);
        qemu_chr_be_write(chr, &s->buf[s->bufptr], n);
        s->bufptr += n;
        s->max_size = qemu_chr_be_can_write(chr);
    }
}

static int udp_chr_read_poll(void *opaque)
{
    Chardev *chr = CHARDEV(opaque);
    UdpChardev *s = UDP_CHARDEV(opaque);

    s->max_size = qemu_chr_be_can_write(chr);

    /* If there were any stray characters in the queue process them
     * first
     */
    udp_chr_flush_buffer(s);

    return s->max_size;
}

static gboolean udp_chr_read(QIOChannel *chan, GIOCondition cond, void *opaque)
{
    Chardev *chr = CHARDEV(opaque);
    UdpChardev *s = UDP_CHARDEV(opaque);
    ssize_t ret;

    if (s->max_size == 0) {
        return TRUE;
    }
    ret = qio_channel_read(
        s->ioc, (char *)s->buf, sizeof(s->buf), NULL);
    if (ret <= 0) {
        remove_fd_in_watch(chr);
        return FALSE;
    }
    s->bufcnt = ret;
    s->bufptr = 0;
    udp_chr_flush_buffer(s);

    return TRUE;
}

static void udp_chr_update_read_handler(Chardev *chr)
{
    UdpChardev *s = UDP_CHARDEV(chr);

    remove_fd_in_watch(chr);
    if (s->ioc) {
        chr->gsource = io_add_watch_poll(chr, s->ioc,
                                           udp_chr_read_poll,
                                           udp_chr_read, chr,
                                           chr->gcontext);
    }
}

static void char_udp_finalize(Object *obj)
{
    Chardev *chr = CHARDEV(obj);
    UdpChardev *s = UDP_CHARDEV(obj);

    remove_fd_in_watch(chr);
    if (s->ioc) {
        object_unref(OBJECT(s->ioc));
    }
    qemu_chr_be_event(chr, CHR_EVENT_CLOSED);
}

static void qemu_chr_parse_udp(QemuOpts *opts, ChardevBackend *backend,
                               Error **errp)
{
    const char *host = qemu_opt_get(opts, "host");
    const char *port = qemu_opt_get(opts, "port");
    const char *localaddr = qemu_opt_get(opts, "localaddr");
    const char *localport = qemu_opt_get(opts, "localport");
    bool has_local = false;
    SocketAddressLegacy *addr;
    ChardevUdp *udp;

    backend->type = CHARDEV_BACKEND_KIND_UDP;
    if (host == NULL || strlen(host) == 0) {
        host = "localhost";
    }
    if (port == NULL || strlen(port) == 0) {
        error_setg(errp, "chardev: udp: remote port not specified");
        return;
    }
    if (localport == NULL || strlen(localport) == 0) {
        localport = "0";
    } else {
        has_local = true;
    }
    if (localaddr == NULL || strlen(localaddr) == 0) {
        localaddr = "";
    } else {
        has_local = true;
    }

    udp = backend->u.udp.data = g_new0(ChardevUdp, 1);
    qemu_chr_parse_common(opts, qapi_ChardevUdp_base(udp));

    addr = g_new0(SocketAddressLegacy, 1);
    addr->type = SOCKET_ADDRESS_TYPE_INET;
    addr->u.inet.data = g_new(InetSocketAddress, 1);
    *addr->u.inet.data = (InetSocketAddress) {
        .host = g_strdup(host),
        .port = g_strdup(port),
        .has_ipv4 = qemu_opt_get(opts, "ipv4"),
        .ipv4 = qemu_opt_get_bool(opts, "ipv4", 0),
        .has_ipv6 = qemu_opt_get(opts, "ipv6"),
        .ipv6 = qemu_opt_get_bool(opts, "ipv6", 0),
    };
    udp->remote = addr;

    if (has_local) {
        addr = g_new0(SocketAddressLegacy, 1);
        addr->type = SOCKET_ADDRESS_TYPE_INET;
        addr->u.inet.data = g_new(InetSocketAddress, 1);
        *addr->u.inet.data = (InetSocketAddress) {
            .host = g_strdup(localaddr),
            .port = g_strdup(localport),
        };
        udp->local = addr;
    }
}

static void qmp_chardev_open_udp(Chardev *chr,
                                 ChardevBackend *backend,
                                 bool *be_opened,
                                 Error **errp)
{
    ChardevUdp *udp = backend->u.udp.data;
    SocketAddress *local_addr = socket_address_flatten(udp->local);
    SocketAddress *remote_addr = socket_address_flatten(udp->remote);
    QIOChannelSocket *sioc = qio_channel_socket_new();
    char *name;
    UdpChardev *s = UDP_CHARDEV(chr);
    int ret;

    ret = qio_channel_socket_dgram_sync(sioc, local_addr, remote_addr, errp);
    qapi_free_SocketAddress(local_addr);
    qapi_free_SocketAddress(remote_addr);
    if (ret < 0) {
        object_unref(OBJECT(sioc));
        return;
    }

    name = g_strdup_printf("chardev-udp-%s", chr->label);
    qio_channel_set_name(QIO_CHANNEL(sioc), name);
    g_free(name);

    s->ioc = QIO_CHANNEL(sioc);
    /* be isn't opened until we get a connection */
    *be_opened = false;
}

static void char_udp_class_init(ObjectClass *oc, void *data)
{
    ChardevClass *cc = CHARDEV_CLASS(oc);

    cc->parse = qemu_chr_parse_udp;
    cc->open = qmp_chardev_open_udp;
    cc->chr_write = udp_chr_write;
    cc->chr_update_read_handler = udp_chr_update_read_handler;
}

static const TypeInfo char_udp_type_info = {
    .name = TYPE_CHARDEV_UDP,
    .parent = TYPE_CHARDEV,
    .instance_size = sizeof(UdpChardev),
    .instance_finalize = char_udp_finalize,
    .class_init = char_udp_class_init,
};

static void register_types(void)
{
    type_register_static(&char_udp_type_info);
}

type_init(register_types);
