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

#include "vnc.h"


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

static void vnc_tls_handshake_io(void *opaque);

static int vnc_start_vencrypt_handshake(struct VncState *vs) {
    int ret;

    if ((ret = gnutls_handshake(vs->tls.session)) < 0) {
       if (!gnutls_error_is_fatal(ret)) {
           VNC_DEBUG("Handshake interrupted (blocking)\n");
           if (!gnutls_record_get_direction(vs->tls.session))
               qemu_set_fd_handler(vs->csock, vnc_tls_handshake_io, NULL, vs);
           else
               qemu_set_fd_handler(vs->csock, NULL, vnc_tls_handshake_io, vs);
           return 0;
       }
       VNC_DEBUG("Handshake failed %s\n", gnutls_strerror(ret));
       vnc_client_error(vs);
       return -1;
    }

    if (vs->vd->tls.x509verify) {
        if (vnc_tls_validate_certificate(vs) < 0) {
            VNC_DEBUG("Client verification failed\n");
            vnc_client_error(vs);
            return -1;
        } else {
            VNC_DEBUG("Client verification passed\n");
        }
    }

    VNC_DEBUG("Handshake done, switching to TLS data mode\n");
    vs->tls.wiremode = VNC_WIREMODE_TLS;
    qemu_set_fd_handler2(vs->csock, NULL, vnc_client_read, vnc_client_write, vs);

    start_auth_vencrypt_subauth(vs);

    return 0;
}

static void vnc_tls_handshake_io(void *opaque) {
    struct VncState *vs = (struct VncState *)opaque;

    VNC_DEBUG("Handshake IO continue\n");
    vnc_start_vencrypt_handshake(vs);
}



#define NEED_X509_AUTH(vs)                              \
    ((vs)->subauth == VNC_AUTH_VENCRYPT_X509NONE ||   \
     (vs)->subauth == VNC_AUTH_VENCRYPT_X509VNC ||    \
     (vs)->subauth == VNC_AUTH_VENCRYPT_X509PLAIN ||  \
     (vs)->subauth == VNC_AUTH_VENCRYPT_X509SASL)


static int protocol_client_vencrypt_auth(VncState *vs, uint8_t *data, size_t len)
{
    int auth = read_u32(data, 0);

    if (auth != vs->subauth) {
        VNC_DEBUG("Rejecting auth %d\n", auth);
        vnc_write_u8(vs, 0); /* Reject auth */
        vnc_flush(vs);
        vnc_client_error(vs);
    } else {
        VNC_DEBUG("Accepting auth %d, setting up TLS for handshake\n", auth);
        vnc_write_u8(vs, 1); /* Accept auth */
        vnc_flush(vs);

        if (vnc_tls_client_setup(vs, NEED_X509_AUTH(vs)) < 0) {
            VNC_DEBUG("Failed to setup TLS\n");
            return 0;
        }

        VNC_DEBUG("Start TLS VeNCrypt handshake process\n");
        if (vnc_start_vencrypt_handshake(vs) < 0) {
            VNC_DEBUG("Failed to start TLS handshake\n");
            return 0;
        }
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

