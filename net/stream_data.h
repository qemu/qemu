/*
 * net stream generic functions
 *
 * Copyright Red Hat
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

typedef struct NetStreamData {
    NetClientState nc;
    QIOChannel *ioc;
    guint ioc_read_tag;
    guint ioc_write_tag;
    SocketReadState rs;
    unsigned int send_index;      /* number of bytes sent*/
    QIOChannelFunc send;
    /* server data */
    QIOChannel *listen_ioc;
    QIONetListener *listener;
    QIONetListenerClientFunc listen;
} NetStreamData;

ssize_t net_stream_data_receive(NetStreamData *d, const uint8_t *buf,
                                size_t size);
void net_stream_data_rs_finalize(SocketReadState *rs);
gboolean net_stream_data_send(QIOChannel *ioc, GIOCondition condition,
                              NetStreamData *d);
int net_stream_data_client_connected(QIOTask *task, NetStreamData *d);
void net_stream_data_listen(QIONetListener *listener,
                            QIOChannelSocket *cioc,
                            NetStreamData *d);
