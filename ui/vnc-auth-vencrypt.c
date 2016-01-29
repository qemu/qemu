/*
 * QEMU VNC display driver: VeNCrypt authentication setup
 *
 * Copyright (C) 2006 Anthony Liguori <anthony@codemonkey.ws>
 * Copyright (C) 2006 Fabrice Bellard
 * Copyright (C) 2009 Red Hat, Inc
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
#include "vnc.h"
#include "qemu/main-loop.h"

static void start_auth_vencrypt_subauth(VncState *vs)
{
    switch (vs->subauth) {
    case VNC_AUTH_VENCRYPT_TLSNONE:
    case VNC_AUTH_VENCRYPT_X509NONE:
       VNC_DEBUG("Accept TLS auth none\n");
       vnc_write_u32(vs, 0); /* Accept auth completion */
       start_client_init(vs);
       break;

    case VNC_AUTH_VENCRYPT_TLSVNC:
    case VNC_AUTH_VENCRYPT_X509VNC:
       VNC_DEBUG("Start TLS auth VNC\n");
       start_auth_vnc(vs);
       break;

#ifdef CONFIG_VNC_SASL
    case VNC_AUTH_VENCRYPT_TLSSASL:
    case VNC_AUTH_VENCRYPT_X509SASL:
      VNC_DEBUG("Start TLS auth SASL\n");
      start_auth_sasl(vs);
      break;
#endif /* CONFIG_VNC_SASL */

    default: /* Should not be possible, but just in case */
       VNC_DEBUG("Reject subauth %d server bug\n", vs->auth);
       vnc_write_u8(vs, 1);
       if (vs->minor >= 8) {
           static const char err[] = "Unsupported authentication type";
           vnc_write_u32(vs, sizeof(err));
           vnc_write(vs, err, sizeof(err));
       }
       vnc_client_error(vs);
    }
}

static void vnc_tls_handshake_done(Object *source,
                                   Error *err,
                                   gpointer user_data)
{
    VncState *vs = user_data;

    if (err) {
        VNC_DEBUG("Handshake failed %s\n",
                  error_get_pretty(err));
        vnc_client_error(vs);
    } else {
        vs->ioc_tag = qio_channel_add_watch(
            vs->ioc, G_IO_IN | G_IO_OUT, vnc_client_io, vs, NULL);
        start_auth_vencrypt_subauth(vs);
    }
}


static int protocol_client_vencrypt_auth(VncState *vs, uint8_t *data, size_t len)
{
    int auth = read_u32(data, 0);

    if (auth != vs->subauth) {
        VNC_DEBUG("Rejecting auth %d\n", auth);
        vnc_write_u8(vs, 0); /* Reject auth */
        vnc_flush(vs);
        vnc_client_error(vs);
    } else {
        Error *err = NULL;
        QIOChannelTLS *tls;
        VNC_DEBUG("Accepting auth %d, setting up TLS for handshake\n", auth);
        vnc_write_u8(vs, 1); /* Accept auth */
        vnc_flush(vs);

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
            return 0;
        }

        VNC_DEBUG("Start TLS VeNCrypt handshake process\n");
        object_unref(OBJECT(vs->ioc));
        vs->ioc = QIO_CHANNEL(tls);
        vs->tls = qio_channel_tls_get_session(tls);

        qio_channel_tls_handshake(tls,
                                  vnc_tls_handshake_done,
                                  vs,
                                  NULL);
    }
    return 0;
}

static int protocol_client_vencrypt_init(VncState *vs, uint8_t *data, size_t len)
{
    if (data[0] != 0 ||
        data[1] != 2) {
        VNC_DEBUG("Unsupported VeNCrypt protocol %d.%d\n", (int)data[0], (int)data[1]);
        vnc_write_u8(vs, 1); /* Reject version */
        vnc_flush(vs);
        vnc_client_error(vs);
    } else {
        VNC_DEBUG("Sending allowed auth %d\n", vs->subauth);
        vnc_write_u8(vs, 0); /* Accept version */
        vnc_write_u8(vs, 1); /* Number of sub-auths */
        vnc_write_u32(vs, vs->subauth); /* The supported auth */
        vnc_flush(vs);
        vnc_read_when(vs, protocol_client_vencrypt_auth, 4);
    }
    return 0;
}


void start_auth_vencrypt(VncState *vs)
{
    /* Send VeNCrypt version 0.2 */
    vnc_write_u8(vs, 0);
    vnc_write_u8(vs, 2);

    vnc_read_when(vs, protocol_client_vencrypt_init, 2);
}

