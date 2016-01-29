/*
 * QEMU VNC display driver: Websockets support
 *
 * Copyright (C) 2010 Joel Martin
 * Copyright (C) 2012 Tim Hardeck
 *
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this software; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "vnc.h"
#include "io/channel-websock.h"

static void vncws_tls_handshake_done(Object *source,
                                     Error *err,
                                     gpointer user_data)
{
    VncState *vs = user_data;

    if (err) {
        VNC_DEBUG("Handshake failed %s\n", error_get_pretty(err));
        vnc_client_error(vs);
    } else {
        VNC_DEBUG("TLS handshake complete, starting websocket handshake\n");
        vs->ioc_tag = qio_channel_add_watch(
            QIO_CHANNEL(vs->ioc), G_IO_IN, vncws_handshake_io, vs, NULL);
    }
}


gboolean vncws_tls_handshake_io(QIOChannel *ioc G_GNUC_UNUSED,
                                GIOCondition condition G_GNUC_UNUSED,
                                void *opaque)
{
    VncState *vs = opaque;
    QIOChannelTLS *tls;
    Error *err = NULL;

    VNC_DEBUG("TLS Websocket connection required\n");
    if (vs->ioc_tag) {
        g_source_remove(vs->ioc_tag);
        vs->ioc_tag = 0;
    }

    tls = qio_channel_tls_new_server(
        vs->ioc,
        vs->vd->tlscreds,
        vs->vd->tlsaclname,
        &err);
    if (!tls) {
        VNC_DEBUG("Failed to setup TLS %s\n", error_get_pretty(err));
        error_free(err);
        vnc_client_error(vs);
        return TRUE;
    }

    VNC_DEBUG("Start TLS WS handshake process\n");
    object_unref(OBJECT(vs->ioc));
    vs->ioc = QIO_CHANNEL(tls);
    vs->tls = qio_channel_tls_get_session(tls);

    qio_channel_tls_handshake(tls,
                              vncws_tls_handshake_done,
                              vs,
                              NULL);

    return TRUE;
}


static void vncws_handshake_done(Object *source,
                                 Error *err,
                                 gpointer user_data)
{
    VncState *vs = user_data;

    if (err) {
        VNC_DEBUG("Websock handshake failed %s\n", error_get_pretty(err));
        vnc_client_error(vs);
    } else {
        VNC_DEBUG("Websock handshake complete, starting VNC protocol\n");
        vnc_init_state(vs);
        vs->ioc_tag = qio_channel_add_watch(
            vs->ioc, G_IO_IN, vnc_client_io, vs, NULL);
    }
}


gboolean vncws_handshake_io(QIOChannel *ioc G_GNUC_UNUSED,
                            GIOCondition condition G_GNUC_UNUSED,
                            void *opaque)
{
    VncState *vs = opaque;
    QIOChannelWebsock *wioc;

    VNC_DEBUG("Websocket negotiate starting\n");
    if (vs->ioc_tag) {
        g_source_remove(vs->ioc_tag);
        vs->ioc_tag = 0;
    }

    wioc = qio_channel_websock_new_server(vs->ioc);

    object_unref(OBJECT(vs->ioc));
    vs->ioc = QIO_CHANNEL(wioc);

    qio_channel_websock_handshake(wioc,
                                  vncws_handshake_done,
                                  vs,
                                  NULL);

    return TRUE;
}
