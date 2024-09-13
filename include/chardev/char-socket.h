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

#ifndef CHAR_SOCKET_H
#define CHAR_SOCKET_H

#include "io/channel-socket.h"
#include "io/channel-tls.h"
#include "io/net-listener.h"
#include "chardev/char.h"
#include "qom/object.h"

#define TCP_MAX_FDS 16

typedef struct {
    char buf[21];
    size_t buflen;
} TCPChardevTelnetInit;

typedef enum {
    TCP_CHARDEV_STATE_DISCONNECTED,
    TCP_CHARDEV_STATE_CONNECTING,
    TCP_CHARDEV_STATE_CONNECTED,
} TCPChardevState;

typedef ChardevClass SocketChardevClass;

struct SocketChardev {
    Chardev parent;
    QIOChannel *ioc; /* Client I/O channel */
    QIOChannelSocket *sioc; /* Client master channel */
    QIONetListener *listener;
    GSource *hup_source;
    QCryptoTLSCreds *tls_creds;
    char *tls_authz;
    TCPChardevState state;
    int max_size;
    int do_telnetopt;
    int do_nodelay;
    int *read_msgfds;
    size_t read_msgfds_num;
    int *write_msgfds;
    size_t write_msgfds_num;
    bool registered_yank;

    SocketAddress *addr;
    bool is_listen;
    bool is_telnet;
    bool is_tn3270;
    GSource *telnet_source;
    TCPChardevTelnetInit *telnet_init;

    bool is_websock;

    GSource *reconnect_timer;
    int64_t reconnect_time_ms;
    bool connect_err_reported;

    QIOTask *connect_task;
};
typedef struct SocketChardev SocketChardev;

DECLARE_INSTANCE_CHECKER(SocketChardev, SOCKET_CHARDEV,
                         TYPE_CHARDEV_SOCKET)

#endif /* CHAR_SOCKET_H */
