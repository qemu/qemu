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
#include "io/channel-tls.h"
#include "io/channel-websock.h"
#include "io/net-listener.h"
#include "qemu/error-report.h"
#include "qemu/option.h"
#include "qapi/error.h"
#include "qapi/clone-visitor.h"
#include "qapi/qapi-visit-sockets.h"

#include "chardev/char-io.h"

/***********************************************************/
/* TCP Net console */

#define TCP_MAX_FDS 16

typedef struct {
    char buf[21];
    size_t buflen;
} TCPChardevTelnetInit;

typedef struct {
    Chardev parent;
    QIOChannel *ioc; /* Client I/O channel */
    QIOChannelSocket *sioc; /* Client master channel */
    QIONetListener *listener;
    GSource *hup_source;
    QCryptoTLSCreds *tls_creds;
    int connected;
    int max_size;
    int do_telnetopt;
    int do_nodelay;
    int *read_msgfds;
    size_t read_msgfds_num;
    int *write_msgfds;
    size_t write_msgfds_num;

    SocketAddress *addr;
    bool is_listen;
    bool is_telnet;
    bool is_tn3270;
    GSource *telnet_source;
    TCPChardevTelnetInit *telnet_init;

    bool is_websock;

    GSource *reconnect_timer;
    int64_t reconnect_time;
    bool connect_err_reported;
} SocketChardev;

#define SOCKET_CHARDEV(obj)                                     \
    OBJECT_CHECK(SocketChardev, (obj), TYPE_CHARDEV_SOCKET)

static gboolean socket_reconnect_timeout(gpointer opaque);
static void tcp_chr_telnet_init(Chardev *chr);

static void tcp_chr_reconn_timer_cancel(SocketChardev *s)
{
    if (s->reconnect_timer) {
        g_source_destroy(s->reconnect_timer);
        g_source_unref(s->reconnect_timer);
        s->reconnect_timer = NULL;
    }
}

static void qemu_chr_socket_restart_timer(Chardev *chr)
{
    SocketChardev *s = SOCKET_CHARDEV(chr);
    char *name;

    assert(s->connected == 0);
    name = g_strdup_printf("chardev-socket-reconnect-%s", chr->label);
    s->reconnect_timer = qemu_chr_timeout_add_ms(chr,
                                                 s->reconnect_time * 1000,
                                                 socket_reconnect_timeout,
                                                 chr);
    g_source_set_name(s->reconnect_timer, name);
    g_free(name);
}

static void check_report_connect_error(Chardev *chr,
                                       Error *err)
{
    SocketChardev *s = SOCKET_CHARDEV(chr);

    if (!s->connect_err_reported) {
        error_report("Unable to connect character device %s: %s",
                     chr->label, error_get_pretty(err));
        s->connect_err_reported = true;
    }
    qemu_chr_socket_restart_timer(chr);
}

static void tcp_chr_accept(QIONetListener *listener,
                           QIOChannelSocket *cioc,
                           void *opaque);

static int tcp_chr_read_poll(void *opaque);
static void tcp_chr_disconnect(Chardev *chr);

/* Called with chr_write_lock held.  */
static int tcp_chr_write(Chardev *chr, const uint8_t *buf, int len)
{
    SocketChardev *s = SOCKET_CHARDEV(chr);

    if (s->connected) {
        int ret =  io_channel_send_full(s->ioc, buf, len,
                                        s->write_msgfds,
                                        s->write_msgfds_num);

        /* free the written msgfds in any cases
         * other than ret < 0 && errno == EAGAIN
         */
        if (!(ret < 0 && EAGAIN == errno)
            && s->write_msgfds_num) {
            g_free(s->write_msgfds);
            s->write_msgfds = 0;
            s->write_msgfds_num = 0;
        }

        if (ret < 0 && errno != EAGAIN) {
            if (tcp_chr_read_poll(chr) <= 0) {
                tcp_chr_disconnect(chr);
                return len;
            } /* else let the read handler finish it properly */
        }

        return ret;
    } else {
        /* XXX: indicate an error ? */
        return len;
    }
}

static int tcp_chr_read_poll(void *opaque)
{
    Chardev *chr = CHARDEV(opaque);
    SocketChardev *s = SOCKET_CHARDEV(opaque);
    if (!s->connected) {
        return 0;
    }
    s->max_size = qemu_chr_be_can_write(chr);
    return s->max_size;
}

static void tcp_chr_process_IAC_bytes(Chardev *chr,
                                      SocketChardev *s,
                                      uint8_t *buf, int *size)
{
    /* Handle any telnet or tn3270 client's basic IAC options.
     * For telnet options, it satisfies char by char mode with no echo.
     * For tn3270 options, it satisfies binary mode with EOR.
     * All IAC options will be removed from the buf and the do_opt
     * pointer will be used to track the state of the width of the
     * IAC information.
     *
     * RFC854: "All TELNET commands consist of at least a two byte sequence.
     * The commands dealing with option negotiation are three byte sequences,
     * the third byte being the code for the option referenced."
     * "IAC BREAK", "IAC IP", "IAC NOP" and the double IAC are two bytes.
     * "IAC SB", "IAC SE" and "IAC EOR" are saved to split up data boundary
     * for tn3270.
     * NOP, Break and Interrupt Process(IP) might be encountered during a TN3270
     * session, and NOP and IP need to be done later.
     */

    int i;
    int j = 0;

    for (i = 0; i < *size; i++) {
        if (s->do_telnetopt > 1) {
            if ((unsigned char)buf[i] == IAC && s->do_telnetopt == 2) {
                /* Double IAC means send an IAC */
                if (j != i) {
                    buf[j] = buf[i];
                }
                j++;
                s->do_telnetopt = 1;
            } else {
                if ((unsigned char)buf[i] == IAC_BREAK
                    && s->do_telnetopt == 2) {
                    /* Handle IAC break commands by sending a serial break */
                    qemu_chr_be_event(chr, CHR_EVENT_BREAK);
                    s->do_telnetopt++;
                } else if (s->is_tn3270 && ((unsigned char)buf[i] == IAC_EOR
                           || (unsigned char)buf[i] == IAC_SB
                           || (unsigned char)buf[i] == IAC_SE)
                           && s->do_telnetopt == 2) {
                    buf[j++] = IAC;
                    buf[j++] = buf[i];
                    s->do_telnetopt++;
                } else if (s->is_tn3270 && ((unsigned char)buf[i] == IAC_IP
                           || (unsigned char)buf[i] == IAC_NOP)
                           && s->do_telnetopt == 2) {
                    /* TODO: IP and NOP need to be implemented later. */
                    s->do_telnetopt++;
                }
                s->do_telnetopt++;
            }
            if (s->do_telnetopt >= 4) {
                s->do_telnetopt = 1;
            }
        } else {
            if ((unsigned char)buf[i] == IAC) {
                s->do_telnetopt = 2;
            } else {
                if (j != i) {
                    buf[j] = buf[i];
                }
                j++;
            }
        }
    }
    *size = j;
}

static int tcp_get_msgfds(Chardev *chr, int *fds, int num)
{
    SocketChardev *s = SOCKET_CHARDEV(chr);

    int to_copy = (s->read_msgfds_num < num) ? s->read_msgfds_num : num;

    assert(num <= TCP_MAX_FDS);

    if (to_copy) {
        int i;

        memcpy(fds, s->read_msgfds, to_copy * sizeof(int));

        /* Close unused fds */
        for (i = to_copy; i < s->read_msgfds_num; i++) {
            close(s->read_msgfds[i]);
        }

        g_free(s->read_msgfds);
        s->read_msgfds = 0;
        s->read_msgfds_num = 0;
    }

    return to_copy;
}

static int tcp_set_msgfds(Chardev *chr, int *fds, int num)
{
    SocketChardev *s = SOCKET_CHARDEV(chr);

    /* clear old pending fd array */
    g_free(s->write_msgfds);
    s->write_msgfds = NULL;
    s->write_msgfds_num = 0;

    if (!s->connected ||
        !qio_channel_has_feature(s->ioc,
                                 QIO_CHANNEL_FEATURE_FD_PASS)) {
        return -1;
    }

    if (num) {
        s->write_msgfds = g_new(int, num);
        memcpy(s->write_msgfds, fds, num * sizeof(int));
    }

    s->write_msgfds_num = num;

    return 0;
}

static ssize_t tcp_chr_recv(Chardev *chr, char *buf, size_t len)
{
    SocketChardev *s = SOCKET_CHARDEV(chr);
    struct iovec iov = { .iov_base = buf, .iov_len = len };
    int ret;
    size_t i;
    int *msgfds = NULL;
    size_t msgfds_num = 0;

    if (qio_channel_has_feature(s->ioc, QIO_CHANNEL_FEATURE_FD_PASS)) {
        ret = qio_channel_readv_full(s->ioc, &iov, 1,
                                     &msgfds, &msgfds_num,
                                     NULL);
    } else {
        ret = qio_channel_readv_full(s->ioc, &iov, 1,
                                     NULL, NULL,
                                     NULL);
    }

    if (ret == QIO_CHANNEL_ERR_BLOCK) {
        errno = EAGAIN;
        ret = -1;
    } else if (ret == -1) {
        errno = EIO;
    }

    if (msgfds_num) {
        /* close and clean read_msgfds */
        for (i = 0; i < s->read_msgfds_num; i++) {
            close(s->read_msgfds[i]);
        }

        if (s->read_msgfds_num) {
            g_free(s->read_msgfds);
        }

        s->read_msgfds = msgfds;
        s->read_msgfds_num = msgfds_num;
    }

    for (i = 0; i < s->read_msgfds_num; i++) {
        int fd = s->read_msgfds[i];
        if (fd < 0) {
            continue;
        }

        /* O_NONBLOCK is preserved across SCM_RIGHTS so reset it */
        qemu_set_block(fd);

#ifndef MSG_CMSG_CLOEXEC
        qemu_set_cloexec(fd);
#endif
    }

    return ret;
}

static GSource *tcp_chr_add_watch(Chardev *chr, GIOCondition cond)
{
    SocketChardev *s = SOCKET_CHARDEV(chr);
    return qio_channel_create_watch(s->ioc, cond);
}

static void remove_hup_source(SocketChardev *s)
{
    if (s->hup_source != NULL) {
        g_source_destroy(s->hup_source);
        g_source_unref(s->hup_source);
        s->hup_source = NULL;
    }
}

static void tcp_chr_free_connection(Chardev *chr)
{
    SocketChardev *s = SOCKET_CHARDEV(chr);
    int i;

    if (s->read_msgfds_num) {
        for (i = 0; i < s->read_msgfds_num; i++) {
            close(s->read_msgfds[i]);
        }
        g_free(s->read_msgfds);
        s->read_msgfds = NULL;
        s->read_msgfds_num = 0;
    }

    remove_hup_source(s);

    tcp_set_msgfds(chr, NULL, 0);
    remove_fd_in_watch(chr);
    object_unref(OBJECT(s->sioc));
    s->sioc = NULL;
    object_unref(OBJECT(s->ioc));
    s->ioc = NULL;
    g_free(chr->filename);
    chr->filename = NULL;
    s->connected = 0;
}

static const char *qemu_chr_socket_protocol(SocketChardev *s)
{
    if (s->is_telnet) {
        return "telnet";
    }
    return s->is_websock ? "websocket" : "tcp";
}

static char *qemu_chr_socket_address(SocketChardev *s, const char *prefix)
{
    switch (s->addr->type) {
    case SOCKET_ADDRESS_TYPE_INET:
        return g_strdup_printf("%s%s:%s:%s%s", prefix,
                               qemu_chr_socket_protocol(s),
                               s->addr->u.inet.host,
                               s->addr->u.inet.port,
                               s->is_listen ? ",server" : "");
        break;
    case SOCKET_ADDRESS_TYPE_UNIX:
        return g_strdup_printf("%sunix:%s%s", prefix,
                               s->addr->u.q_unix.path,
                               s->is_listen ? ",server" : "");
        break;
    case SOCKET_ADDRESS_TYPE_FD:
        return g_strdup_printf("%sfd:%s%s", prefix, s->addr->u.fd.str,
                               s->is_listen ? ",server" : "");
        break;
    case SOCKET_ADDRESS_TYPE_VSOCK:
        return g_strdup_printf("%svsock:%s:%s", prefix,
                               s->addr->u.vsock.cid,
                               s->addr->u.vsock.port);
    default:
        abort();
    }
}

static void update_disconnected_filename(SocketChardev *s)
{
    Chardev *chr = CHARDEV(s);

    g_free(chr->filename);
    if (s->addr) {
        chr->filename = qemu_chr_socket_address(s, "disconnected:");
    } else {
        chr->filename = g_strdup("disconnected:socket");
    }
}

/* NB may be called even if tcp_chr_connect has not been
 * reached, due to TLS or telnet initialization failure,
 * so can *not* assume s->connected == true
 */
static void tcp_chr_disconnect(Chardev *chr)
{
    SocketChardev *s = SOCKET_CHARDEV(chr);
    bool emit_close = s->connected;

    tcp_chr_free_connection(chr);

    if (s->listener) {
        qio_net_listener_set_client_func_full(s->listener, tcp_chr_accept,
                                              chr, NULL, chr->gcontext);
    }
    update_disconnected_filename(s);
    if (emit_close) {
        qemu_chr_be_event(chr, CHR_EVENT_CLOSED);
    }
    if (s->reconnect_time) {
        qemu_chr_socket_restart_timer(chr);
    }
}

static gboolean tcp_chr_read(QIOChannel *chan, GIOCondition cond, void *opaque)
{
    Chardev *chr = CHARDEV(opaque);
    SocketChardev *s = SOCKET_CHARDEV(opaque);
    uint8_t buf[CHR_READ_BUF_LEN];
    int len, size;

    if (!s->connected || s->max_size <= 0) {
        return TRUE;
    }
    len = sizeof(buf);
    if (len > s->max_size) {
        len = s->max_size;
    }
    size = tcp_chr_recv(chr, (void *)buf, len);
    if (size == 0 || (size == -1 && errno != EAGAIN)) {
        /* connection closed */
        tcp_chr_disconnect(chr);
    } else if (size > 0) {
        if (s->do_telnetopt) {
            tcp_chr_process_IAC_bytes(chr, s, buf, &size);
        }
        if (size > 0) {
            qemu_chr_be_write(chr, buf, size);
        }
    }

    return TRUE;
}

static gboolean tcp_chr_hup(QIOChannel *channel,
                               GIOCondition cond,
                               void *opaque)
{
    Chardev *chr = CHARDEV(opaque);
    tcp_chr_disconnect(chr);
    return G_SOURCE_REMOVE;
}

static int tcp_chr_sync_read(Chardev *chr, const uint8_t *buf, int len)
{
    SocketChardev *s = SOCKET_CHARDEV(chr);
    int size;

    if (!s->connected) {
        return 0;
    }

    qio_channel_set_blocking(s->ioc, true, NULL);
    size = tcp_chr_recv(chr, (void *) buf, len);
    qio_channel_set_blocking(s->ioc, false, NULL);
    if (size == 0) {
        /* connection closed */
        tcp_chr_disconnect(chr);
    }

    return size;
}

static char *qemu_chr_compute_filename(SocketChardev *s)
{
    struct sockaddr_storage *ss = &s->sioc->localAddr;
    struct sockaddr_storage *ps = &s->sioc->remoteAddr;
    socklen_t ss_len = s->sioc->localAddrLen;
    socklen_t ps_len = s->sioc->remoteAddrLen;
    char shost[NI_MAXHOST], sserv[NI_MAXSERV];
    char phost[NI_MAXHOST], pserv[NI_MAXSERV];
    const char *left = "", *right = "";

    switch (ss->ss_family) {
#ifndef _WIN32
    case AF_UNIX:
        return g_strdup_printf("unix:%s%s",
                               ((struct sockaddr_un *)(ss))->sun_path,
                               s->is_listen ? ",server" : "");
#endif
    case AF_INET6:
        left  = "[";
        right = "]";
        /* fall through */
    case AF_INET:
        getnameinfo((struct sockaddr *) ss, ss_len, shost, sizeof(shost),
                    sserv, sizeof(sserv), NI_NUMERICHOST | NI_NUMERICSERV);
        getnameinfo((struct sockaddr *) ps, ps_len, phost, sizeof(phost),
                    pserv, sizeof(pserv), NI_NUMERICHOST | NI_NUMERICSERV);
        return g_strdup_printf("%s:%s%s%s:%s%s <-> %s%s%s:%s",
                               qemu_chr_socket_protocol(s),
                               left, shost, right, sserv,
                               s->is_listen ? ",server" : "",
                               left, phost, right, pserv);

    default:
        return g_strdup_printf("unknown");
    }
}

static void update_ioc_handlers(SocketChardev *s)
{
    Chardev *chr = CHARDEV(s);

    if (!s->connected) {
        return;
    }

    remove_fd_in_watch(chr);
    chr->gsource = io_add_watch_poll(chr, s->ioc,
                                     tcp_chr_read_poll,
                                     tcp_chr_read, chr,
                                     chr->gcontext);

    remove_hup_source(s);
    s->hup_source = qio_channel_create_watch(s->ioc, G_IO_HUP);
    g_source_set_callback(s->hup_source, (GSourceFunc)tcp_chr_hup,
                          chr, NULL);
    g_source_attach(s->hup_source, chr->gcontext);
}

static void tcp_chr_connect(void *opaque)
{
    Chardev *chr = CHARDEV(opaque);
    SocketChardev *s = SOCKET_CHARDEV(opaque);

    g_free(chr->filename);
    chr->filename = qemu_chr_compute_filename(s);

    s->connected = 1;
    update_ioc_handlers(s);
    qemu_chr_be_event(chr, CHR_EVENT_OPENED);
}

static void tcp_chr_telnet_destroy(SocketChardev *s)
{
    if (s->telnet_source) {
        g_source_destroy(s->telnet_source);
        g_source_unref(s->telnet_source);
        s->telnet_source = NULL;
    }
}

static void tcp_chr_update_read_handler(Chardev *chr)
{
    SocketChardev *s = SOCKET_CHARDEV(chr);

    if (s->listener) {
        /*
         * It's possible that chardev context is changed in
         * qemu_chr_be_update_read_handlers().  Reset it for QIO net
         * listener if there is.
         */
        qio_net_listener_set_client_func_full(s->listener, tcp_chr_accept,
                                              chr, NULL, chr->gcontext);
    }

    if (s->telnet_source) {
        tcp_chr_telnet_init(CHARDEV(s));
    }

    update_ioc_handlers(s);
}

static gboolean tcp_chr_telnet_init_io(QIOChannel *ioc,
                                       GIOCondition cond G_GNUC_UNUSED,
                                       gpointer user_data)
{
    SocketChardev *s = user_data;
    Chardev *chr = CHARDEV(s);
    TCPChardevTelnetInit *init = s->telnet_init;
    ssize_t ret;

    assert(init);

    ret = qio_channel_write(ioc, init->buf, init->buflen, NULL);
    if (ret < 0) {
        if (ret == QIO_CHANNEL_ERR_BLOCK) {
            ret = 0;
        } else {
            tcp_chr_disconnect(chr);
            goto end;
        }
    }
    init->buflen -= ret;

    if (init->buflen == 0) {
        tcp_chr_connect(chr);
        goto end;
    }

    memmove(init->buf, init->buf + ret, init->buflen);

    return G_SOURCE_CONTINUE;

end:
    g_free(s->telnet_init);
    s->telnet_init = NULL;
    g_source_unref(s->telnet_source);
    s->telnet_source = NULL;
    return G_SOURCE_REMOVE;
}

static void tcp_chr_telnet_init(Chardev *chr)
{
    SocketChardev *s = SOCKET_CHARDEV(chr);
    TCPChardevTelnetInit *init;
    size_t n = 0;

    /* Destroy existing task */
    tcp_chr_telnet_destroy(s);

    if (s->telnet_init) {
        /* We are possibly during a handshake already */
        goto cont;
    }

    s->telnet_init = g_new0(TCPChardevTelnetInit, 1);
    init = s->telnet_init;

#define IACSET(x, a, b, c)                      \
    do {                                        \
        x[n++] = a;                             \
        x[n++] = b;                             \
        x[n++] = c;                             \
    } while (0)

    if (!s->is_tn3270) {
        init->buflen = 12;
        /* Prep the telnet negotion to put telnet in binary,
         * no echo, single char mode */
        IACSET(init->buf, 0xff, 0xfb, 0x01);  /* IAC WILL ECHO */
        IACSET(init->buf, 0xff, 0xfb, 0x03);  /* IAC WILL Suppress go ahead */
        IACSET(init->buf, 0xff, 0xfb, 0x00);  /* IAC WILL Binary */
        IACSET(init->buf, 0xff, 0xfd, 0x00);  /* IAC DO Binary */
    } else {
        init->buflen = 21;
        /* Prep the TN3270 negotion based on RFC1576 */
        IACSET(init->buf, 0xff, 0xfd, 0x19);  /* IAC DO EOR */
        IACSET(init->buf, 0xff, 0xfb, 0x19);  /* IAC WILL EOR */
        IACSET(init->buf, 0xff, 0xfd, 0x00);  /* IAC DO BINARY */
        IACSET(init->buf, 0xff, 0xfb, 0x00);  /* IAC WILL BINARY */
        IACSET(init->buf, 0xff, 0xfd, 0x18);  /* IAC DO TERMINAL TYPE */
        IACSET(init->buf, 0xff, 0xfa, 0x18);  /* IAC SB TERMINAL TYPE */
        IACSET(init->buf, 0x01, 0xff, 0xf0);  /* SEND IAC SE */
    }

#undef IACSET

cont:
    s->telnet_source = qio_channel_add_watch_source(s->ioc, G_IO_OUT,
                                                    tcp_chr_telnet_init_io,
                                                    s, NULL,
                                                    chr->gcontext);
}


static void tcp_chr_websock_handshake(QIOTask *task, gpointer user_data)
{
    Chardev *chr = user_data;
    SocketChardev *s = user_data;

    if (qio_task_propagate_error(task, NULL)) {
        tcp_chr_disconnect(chr);
    } else {
        if (s->do_telnetopt) {
            tcp_chr_telnet_init(chr);
        } else {
            tcp_chr_connect(chr);
        }
    }
}


static void tcp_chr_websock_init(Chardev *chr)
{
    SocketChardev *s = SOCKET_CHARDEV(chr);
    QIOChannelWebsock *wioc = NULL;
    gchar *name;

    wioc = qio_channel_websock_new_server(s->ioc);

    name = g_strdup_printf("chardev-websocket-server-%s", chr->label);
    qio_channel_set_name(QIO_CHANNEL(wioc), name);
    g_free(name);
    object_unref(OBJECT(s->ioc));
    s->ioc = QIO_CHANNEL(wioc);

    qio_channel_websock_handshake(wioc, tcp_chr_websock_handshake, chr, NULL);
}


static void tcp_chr_tls_handshake(QIOTask *task,
                                  gpointer user_data)
{
    Chardev *chr = user_data;
    SocketChardev *s = user_data;

    if (qio_task_propagate_error(task, NULL)) {
        tcp_chr_disconnect(chr);
    } else {
        if (s->is_websock) {
            tcp_chr_websock_init(chr);
        } else if (s->do_telnetopt) {
            tcp_chr_telnet_init(chr);
        } else {
            tcp_chr_connect(chr);
        }
    }
}


static void tcp_chr_tls_init(Chardev *chr)
{
    SocketChardev *s = SOCKET_CHARDEV(chr);
    QIOChannelTLS *tioc;
    Error *err = NULL;
    gchar *name;

    if (s->is_listen) {
        tioc = qio_channel_tls_new_server(
            s->ioc, s->tls_creds,
            NULL, /* XXX Use an ACL */
            &err);
    } else {
        tioc = qio_channel_tls_new_client(
            s->ioc, s->tls_creds,
            s->addr->u.inet.host,
            &err);
    }
    if (tioc == NULL) {
        error_free(err);
        tcp_chr_disconnect(chr);
        return;
    }
    name = g_strdup_printf("chardev-tls-%s-%s",
                           s->is_listen ? "server" : "client",
                           chr->label);
    qio_channel_set_name(QIO_CHANNEL(tioc), name);
    g_free(name);
    object_unref(OBJECT(s->ioc));
    s->ioc = QIO_CHANNEL(tioc);

    qio_channel_tls_handshake(tioc,
                              tcp_chr_tls_handshake,
                              chr,
                              NULL,
                              chr->gcontext);
}


static void tcp_chr_set_client_ioc_name(Chardev *chr,
                                        QIOChannelSocket *sioc)
{
    SocketChardev *s = SOCKET_CHARDEV(chr);
    char *name;
    name = g_strdup_printf("chardev-tcp-%s-%s",
                           s->is_listen ? "server" : "client",
                           chr->label);
    qio_channel_set_name(QIO_CHANNEL(sioc), name);
    g_free(name);

}

static int tcp_chr_new_client(Chardev *chr, QIOChannelSocket *sioc)
{
    SocketChardev *s = SOCKET_CHARDEV(chr);

    if (s->ioc != NULL) {
        return -1;
    }

    s->ioc = QIO_CHANNEL(sioc);
    object_ref(OBJECT(sioc));
    s->sioc = sioc;
    object_ref(OBJECT(sioc));

    qio_channel_set_blocking(s->ioc, false, NULL);

    if (s->do_nodelay) {
        qio_channel_set_delay(s->ioc, false);
    }
    if (s->listener) {
        qio_net_listener_set_client_func_full(s->listener, NULL, NULL,
                                              NULL, chr->gcontext);
    }

    if (s->tls_creds) {
        tcp_chr_tls_init(chr);
    } else if (s->is_websock) {
        tcp_chr_websock_init(chr);
    } else if (s->do_telnetopt) {
        tcp_chr_telnet_init(chr);
    } else {
        tcp_chr_connect(chr);
    }

    return 0;
}


static int tcp_chr_add_client(Chardev *chr, int fd)
{
    int ret;
    QIOChannelSocket *sioc;

    sioc = qio_channel_socket_new_fd(fd, NULL);
    if (!sioc) {
        return -1;
    }
    tcp_chr_set_client_ioc_name(chr, sioc);
    ret = tcp_chr_new_client(chr, sioc);
    object_unref(OBJECT(sioc));
    return ret;
}

static void tcp_chr_accept(QIONetListener *listener,
                           QIOChannelSocket *cioc,
                           void *opaque)
{
    Chardev *chr = CHARDEV(opaque);

    tcp_chr_set_client_ioc_name(chr, cioc);
    tcp_chr_new_client(chr, cioc);
}

static int tcp_chr_wait_connected(Chardev *chr, Error **errp)
{
    SocketChardev *s = SOCKET_CHARDEV(chr);
    QIOChannelSocket *sioc;

    /* It can't wait on s->connected, since it is set asynchronously
     * in TLS and telnet cases, only wait for an accepted socket */
    while (!s->ioc) {
        if (s->is_listen) {
            info_report("QEMU waiting for connection on: %s",
                        chr->filename);
            sioc = qio_net_listener_wait_client(s->listener);
            tcp_chr_set_client_ioc_name(chr, sioc);
            tcp_chr_new_client(chr, sioc);
            object_unref(OBJECT(sioc));
        } else {
            sioc = qio_channel_socket_new();
            tcp_chr_set_client_ioc_name(chr, sioc);
            if (qio_channel_socket_connect_sync(sioc, s->addr, errp) < 0) {
                object_unref(OBJECT(sioc));
                return -1;
            }
            tcp_chr_new_client(chr, sioc);
            object_unref(OBJECT(sioc));
        }
    }

    return 0;
}

static void char_socket_finalize(Object *obj)
{
    Chardev *chr = CHARDEV(obj);
    SocketChardev *s = SOCKET_CHARDEV(obj);

    tcp_chr_free_connection(chr);
    tcp_chr_reconn_timer_cancel(s);
    qapi_free_SocketAddress(s->addr);
    tcp_chr_telnet_destroy(s);
    g_free(s->telnet_init);
    if (s->listener) {
        qio_net_listener_set_client_func_full(s->listener, NULL, NULL,
                                              NULL, chr->gcontext);
        object_unref(OBJECT(s->listener));
    }
    if (s->tls_creds) {
        object_unref(OBJECT(s->tls_creds));
    }

    qemu_chr_be_event(chr, CHR_EVENT_CLOSED);
}

static void qemu_chr_socket_connected(QIOTask *task, void *opaque)
{
    QIOChannelSocket *sioc = QIO_CHANNEL_SOCKET(qio_task_get_source(task));
    Chardev *chr = CHARDEV(opaque);
    SocketChardev *s = SOCKET_CHARDEV(chr);
    Error *err = NULL;

    if (qio_task_propagate_error(task, &err)) {
        check_report_connect_error(chr, err);
        error_free(err);
        goto cleanup;
    }

    s->connect_err_reported = false;
    tcp_chr_new_client(chr, sioc);

cleanup:
    object_unref(OBJECT(sioc));
}

static void tcp_chr_connect_async(Chardev *chr)
{
    SocketChardev *s = SOCKET_CHARDEV(chr);
    QIOChannelSocket *sioc;

    sioc = qio_channel_socket_new();
    tcp_chr_set_client_ioc_name(chr, sioc);
    qio_channel_socket_connect_async(sioc, s->addr,
                                     qemu_chr_socket_connected,
                                     chr, NULL, chr->gcontext);
}

static gboolean socket_reconnect_timeout(gpointer opaque)
{
    Chardev *chr = CHARDEV(opaque);
    SocketChardev *s = SOCKET_CHARDEV(opaque);

    g_source_unref(s->reconnect_timer);
    s->reconnect_timer = NULL;

    if (chr->be_open) {
        return false;
    }

    tcp_chr_connect_async(chr);

    return false;
}

static void qmp_chardev_open_socket(Chardev *chr,
                                    ChardevBackend *backend,
                                    bool *be_opened,
                                    Error **errp)
{
    SocketChardev *s = SOCKET_CHARDEV(chr);
    ChardevSocket *sock = backend->u.socket.data;
    bool do_nodelay     = sock->has_nodelay ? sock->nodelay : false;
    bool is_listen      = sock->has_server  ? sock->server  : true;
    bool is_telnet      = sock->has_telnet  ? sock->telnet  : false;
    bool is_tn3270      = sock->has_tn3270  ? sock->tn3270  : false;
    bool is_waitconnect = sock->has_wait    ? sock->wait    : false;
    bool is_websock     = sock->has_websocket ? sock->websocket : false;
    int64_t reconnect   = sock->has_reconnect ? sock->reconnect : 0;
    QIOChannelSocket *sioc = NULL;
    SocketAddress *addr;

    if (!is_listen && is_websock) {
        error_setg(errp, "%s", "Websocket client is not implemented");
        goto error;
    }

    s->is_listen = is_listen;
    s->is_telnet = is_telnet;
    s->is_tn3270 = is_tn3270;
    s->is_websock = is_websock;
    s->do_nodelay = do_nodelay;
    if (sock->tls_creds) {
        Object *creds;
        creds = object_resolve_path_component(
            object_get_objects_root(), sock->tls_creds);
        if (!creds) {
            error_setg(errp, "No TLS credentials with id '%s'",
                       sock->tls_creds);
            goto error;
        }
        s->tls_creds = (QCryptoTLSCreds *)
            object_dynamic_cast(creds,
                                TYPE_QCRYPTO_TLS_CREDS);
        if (!s->tls_creds) {
            error_setg(errp, "Object with id '%s' is not TLS credentials",
                       sock->tls_creds);
            goto error;
        }
        object_ref(OBJECT(s->tls_creds));
        if (is_listen) {
            if (s->tls_creds->endpoint != QCRYPTO_TLS_CREDS_ENDPOINT_SERVER) {
                error_setg(errp, "%s",
                           "Expected TLS credentials for server endpoint");
                goto error;
            }
        } else {
            if (s->tls_creds->endpoint != QCRYPTO_TLS_CREDS_ENDPOINT_CLIENT) {
                error_setg(errp, "%s",
                           "Expected TLS credentials for client endpoint");
                goto error;
            }
        }
    }

    s->addr = addr = socket_address_flatten(sock->addr);

    if (sock->has_reconnect && addr->type == SOCKET_ADDRESS_TYPE_FD) {
        error_setg(errp, "'reconnect' option is incompatible with 'fd'");
        goto error;
    }
    qemu_chr_set_feature(chr, QEMU_CHAR_FEATURE_RECONNECTABLE);
    /* TODO SOCKET_ADDRESS_FD where fd has AF_UNIX */
    if (addr->type == SOCKET_ADDRESS_TYPE_UNIX) {
        qemu_chr_set_feature(chr, QEMU_CHAR_FEATURE_FD_PASS);
    }

    /* be isn't opened until we get a connection */
    *be_opened = false;

    update_disconnected_filename(s);

    if (is_listen) {
        if (is_telnet || is_tn3270) {
            s->do_telnetopt = 1;
        }
    } else if (reconnect > 0) {
        s->reconnect_time = reconnect;
    }

    if (s->reconnect_time) {
        tcp_chr_connect_async(chr);
    } else {
        if (s->is_listen) {
            char *name;
            s->listener = qio_net_listener_new();

            name = g_strdup_printf("chardev-tcp-listener-%s", chr->label);
            qio_net_listener_set_name(s->listener, name);
            g_free(name);

            if (qio_net_listener_open_sync(s->listener, s->addr, errp) < 0) {
                object_unref(OBJECT(s->listener));
                s->listener = NULL;
                goto error;
            }

            qapi_free_SocketAddress(s->addr);
            s->addr = socket_local_address(s->listener->sioc[0]->fd, errp);
            update_disconnected_filename(s);

            if (is_waitconnect &&
                qemu_chr_wait_connected(chr, errp) < 0) {
                return;
            }
            if (!s->ioc) {
                qio_net_listener_set_client_func_full(s->listener,
                                                      tcp_chr_accept,
                                                      chr, NULL,
                                                      chr->gcontext);
            }
        } else if (qemu_chr_wait_connected(chr, errp) < 0) {
            goto error;
        }
    }

    return;

error:
    if (sioc) {
        object_unref(OBJECT(sioc));
    }
}

static void qemu_chr_parse_socket(QemuOpts *opts, ChardevBackend *backend,
                                  Error **errp)
{
    bool is_listen      = qemu_opt_get_bool(opts, "server", false);
    bool is_waitconnect = is_listen && qemu_opt_get_bool(opts, "wait", true);
    bool is_telnet      = qemu_opt_get_bool(opts, "telnet", false);
    bool is_tn3270      = qemu_opt_get_bool(opts, "tn3270", false);
    bool is_websock     = qemu_opt_get_bool(opts, "websocket", false);
    bool do_nodelay     = !qemu_opt_get_bool(opts, "delay", true);
    int64_t reconnect   = qemu_opt_get_number(opts, "reconnect", 0);
    const char *path = qemu_opt_get(opts, "path");
    const char *host = qemu_opt_get(opts, "host");
    const char *port = qemu_opt_get(opts, "port");
    const char *fd = qemu_opt_get(opts, "fd");
    const char *tls_creds = qemu_opt_get(opts, "tls-creds");
    SocketAddressLegacy *addr;
    ChardevSocket *sock;

    if ((!!path + !!fd + !!host) != 1) {
        error_setg(errp,
                   "Exactly one of 'path', 'fd' or 'host' required");
        return;
    }

    backend->type = CHARDEV_BACKEND_KIND_SOCKET;
    if (path) {
        if (tls_creds) {
            error_setg(errp, "TLS can only be used over TCP socket");
            return;
        }
    } else if (host) {
        if (!port) {
            error_setg(errp, "chardev: socket: no port given");
            return;
        }
    } else if (fd) {
        /* We don't know what host to validate against when in client mode */
        if (tls_creds && !is_listen) {
            error_setg(errp, "TLS can not be used with pre-opened client FD");
            return;
        }
    } else {
        g_assert_not_reached();
    }

    sock = backend->u.socket.data = g_new0(ChardevSocket, 1);
    qemu_chr_parse_common(opts, qapi_ChardevSocket_base(sock));

    sock->has_nodelay = true;
    sock->nodelay = do_nodelay;
    sock->has_server = true;
    sock->server = is_listen;
    sock->has_telnet = true;
    sock->telnet = is_telnet;
    sock->has_tn3270 = true;
    sock->tn3270 = is_tn3270;
    sock->has_websocket = true;
    sock->websocket = is_websock;
    sock->has_wait = true;
    sock->wait = is_waitconnect;
    sock->has_reconnect = qemu_opt_find(opts, "reconnect");
    sock->reconnect = reconnect;
    sock->tls_creds = g_strdup(tls_creds);

    addr = g_new0(SocketAddressLegacy, 1);
    if (path) {
        UnixSocketAddress *q_unix;
        addr->type = SOCKET_ADDRESS_LEGACY_KIND_UNIX;
        q_unix = addr->u.q_unix.data = g_new0(UnixSocketAddress, 1);
        q_unix->path = g_strdup(path);
    } else if (host) {
        addr->type = SOCKET_ADDRESS_LEGACY_KIND_INET;
        addr->u.inet.data = g_new(InetSocketAddress, 1);
        *addr->u.inet.data = (InetSocketAddress) {
            .host = g_strdup(host),
            .port = g_strdup(port),
            .has_to = qemu_opt_get(opts, "to"),
            .to = qemu_opt_get_number(opts, "to", 0),
            .has_ipv4 = qemu_opt_get(opts, "ipv4"),
            .ipv4 = qemu_opt_get_bool(opts, "ipv4", 0),
            .has_ipv6 = qemu_opt_get(opts, "ipv6"),
            .ipv6 = qemu_opt_get_bool(opts, "ipv6", 0),
        };
    } else if (fd) {
        addr->type = SOCKET_ADDRESS_LEGACY_KIND_FD;
        addr->u.fd.data = g_new(String, 1);
        addr->u.fd.data->str = g_strdup(fd);
    } else {
        g_assert_not_reached();
    }
    sock->addr = addr;
}

static void
char_socket_get_addr(Object *obj, Visitor *v, const char *name,
                     void *opaque, Error **errp)
{
    SocketChardev *s = SOCKET_CHARDEV(obj);

    visit_type_SocketAddress(v, name, &s->addr, errp);
}

static bool
char_socket_get_connected(Object *obj, Error **errp)
{
    SocketChardev *s = SOCKET_CHARDEV(obj);

    return s->connected;
}

static void char_socket_class_init(ObjectClass *oc, void *data)
{
    ChardevClass *cc = CHARDEV_CLASS(oc);

    cc->parse = qemu_chr_parse_socket;
    cc->open = qmp_chardev_open_socket;
    cc->chr_wait_connected = tcp_chr_wait_connected;
    cc->chr_write = tcp_chr_write;
    cc->chr_sync_read = tcp_chr_sync_read;
    cc->chr_disconnect = tcp_chr_disconnect;
    cc->get_msgfds = tcp_get_msgfds;
    cc->set_msgfds = tcp_set_msgfds;
    cc->chr_add_client = tcp_chr_add_client;
    cc->chr_add_watch = tcp_chr_add_watch;
    cc->chr_update_read_handler = tcp_chr_update_read_handler;

    object_class_property_add(oc, "addr", "SocketAddress",
                              char_socket_get_addr, NULL,
                              NULL, NULL, &error_abort);

    object_class_property_add_bool(oc, "connected", char_socket_get_connected,
                                   NULL, &error_abort);
}

static const TypeInfo char_socket_type_info = {
    .name = TYPE_CHARDEV_SOCKET,
    .parent = TYPE_CHARDEV,
    .instance_size = sizeof(SocketChardev),
    .instance_finalize = char_socket_finalize,
    .class_init = char_socket_class_init,
};

static void register_types(void)
{
    type_register_static(&char_socket_type_info);
}

type_init(register_types);
