/*
 * QEMU VNC display driver: SASL auth protocol
 *
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

/* Max amount of data we send/recv for SASL steps to prevent DOS */
#define SASL_DATA_MAX_LEN (1024 * 1024)


void vnc_sasl_client_cleanup(VncState *vs)
{
    if (vs->sasl.conn) {
        vs->sasl.runSSF = false;
        vs->sasl.wantSSF = false;
        vs->sasl.waitWriteSSF = 0;
        vs->sasl.encodedLength = vs->sasl.encodedOffset = 0;
        vs->sasl.encoded = NULL;
        g_free(vs->sasl.username);
        g_free(vs->sasl.mechlist);
        vs->sasl.username = vs->sasl.mechlist = NULL;
        sasl_dispose(&vs->sasl.conn);
        vs->sasl.conn = NULL;
    }
}


long vnc_client_write_sasl(VncState *vs)
{
    long ret;

    VNC_DEBUG("Write SASL: Pending output %p size %zd offset %zd "
              "Encoded: %p size %d offset %d\n",
              vs->output.buffer, vs->output.capacity, vs->output.offset,
              vs->sasl.encoded, vs->sasl.encodedLength, vs->sasl.encodedOffset);

    if (!vs->sasl.encoded) {
        int err;
        err = sasl_encode(vs->sasl.conn,
                          (char *)vs->output.buffer,
                          vs->output.offset,
                          (const char **)&vs->sasl.encoded,
                          &vs->sasl.encodedLength);
        if (err != SASL_OK)
            return vnc_client_io_error(vs, -1, NULL);

        vs->sasl.encodedOffset = 0;
    }

    ret = vnc_client_write_buf(vs,
                               vs->sasl.encoded + vs->sasl.encodedOffset,
                               vs->sasl.encodedLength - vs->sasl.encodedOffset);
    if (!ret)
        return 0;

    vs->sasl.encodedOffset += ret;
    if (vs->sasl.encodedOffset == vs->sasl.encodedLength) {
        vs->output.offset = 0;
        vs->sasl.encoded = NULL;
        vs->sasl.encodedOffset = vs->sasl.encodedLength = 0;
    }

    /* Can't merge this block with one above, because
     * someone might have written more unencrypted
     * data in vs->output while we were processing
     * SASL encoded output
     */
    if (vs->output.offset == 0) {
        if (vs->ioc_tag) {
            g_source_remove(vs->ioc_tag);
        }
        vs->ioc_tag = qio_channel_add_watch(
            vs->ioc, G_IO_IN, vnc_client_io, vs, NULL);
    }

    return ret;
}


long vnc_client_read_sasl(VncState *vs)
{
    long ret;
    uint8_t encoded[4096];
    const char *decoded;
    unsigned int decodedLen;
    int err;

    ret = vnc_client_read_buf(vs, encoded, sizeof(encoded));
    if (!ret)
        return 0;

    err = sasl_decode(vs->sasl.conn,
                      (char *)encoded, ret,
                      &decoded, &decodedLen);

    if (err != SASL_OK)
        return vnc_client_io_error(vs, -1, NULL);
    VNC_DEBUG("Read SASL Encoded %p size %ld Decoded %p size %d\n",
              encoded, ret, decoded, decodedLen);
    buffer_reserve(&vs->input, decodedLen);
    buffer_append(&vs->input, decoded, decodedLen);
    return decodedLen;
}


static int vnc_auth_sasl_check_access(VncState *vs)
{
    const void *val;
    int err;
    int allow;

    err = sasl_getprop(vs->sasl.conn, SASL_USERNAME, &val);
    if (err != SASL_OK) {
        VNC_DEBUG("cannot query SASL username on connection %d (%s), denying access\n",
                  err, sasl_errstring(err, NULL, NULL));
        return -1;
    }
    if (val == NULL) {
        VNC_DEBUG("no client username was found, denying access\n");
        return -1;
    }
    VNC_DEBUG("SASL client username %s\n", (const char *)val);

    vs->sasl.username = g_strdup((const char*)val);

    if (vs->vd->sasl.acl == NULL) {
        VNC_DEBUG("no ACL activated, allowing access\n");
        return 0;
    }

    allow = qemu_acl_party_is_allowed(vs->vd->sasl.acl, vs->sasl.username);

    VNC_DEBUG("SASL client %s %s by ACL\n", vs->sasl.username,
              allow ? "allowed" : "denied");
    return allow ? 0 : -1;
}

static int vnc_auth_sasl_check_ssf(VncState *vs)
{
    const void *val;
    int err, ssf;

    if (!vs->sasl.wantSSF)
        return 1;

    err = sasl_getprop(vs->sasl.conn, SASL_SSF, &val);
    if (err != SASL_OK)
        return 0;

    ssf = *(const int *)val;
    VNC_DEBUG("negotiated an SSF of %d\n", ssf);
    if (ssf < 56)
        return 0; /* 56 is good for Kerberos */

    /* Only setup for read initially, because we're about to send an RPC
     * reply which must be in plain text. When the next incoming RPC
     * arrives, we'll switch on writes too
     *
     * cf qemudClientReadSASL  in qemud.c
     */
    vs->sasl.runSSF = 1;

    /* We have a SSF that's good enough */
    return 1;
}

/*
 * Step Msg
 *
 * Input from client:
 *
 * u32 clientin-length
 * u8-array clientin-string
 *
 * Output to client:
 *
 * u32 serverout-length
 * u8-array serverout-strin
 * u8 continue
 */

static int protocol_client_auth_sasl_step_len(VncState *vs, uint8_t *data, size_t len);

static int protocol_client_auth_sasl_step(VncState *vs, uint8_t *data, size_t len)
{
    uint32_t datalen = len;
    const char *serverout;
    unsigned int serveroutlen;
    int err;
    char *clientdata = NULL;

    /* NB, distinction of NULL vs "" is *critical* in SASL */
    if (datalen) {
        clientdata = (char*)data;
        clientdata[datalen-1] = '\0'; /* Wire includes '\0', but make sure */
        datalen--; /* Don't count NULL byte when passing to _start() */
    }

    VNC_DEBUG("Step using SASL Data %p (%d bytes)\n",
              clientdata, datalen);
    err = sasl_server_step(vs->sasl.conn,
                           clientdata,
                           datalen,
                           &serverout,
                           &serveroutlen);
    if (err != SASL_OK &&
        err != SASL_CONTINUE) {
        VNC_DEBUG("sasl step failed %d (%s)\n",
                  err, sasl_errdetail(vs->sasl.conn));
        sasl_dispose(&vs->sasl.conn);
        vs->sasl.conn = NULL;
        goto authabort;
    }

    if (serveroutlen > SASL_DATA_MAX_LEN) {
        VNC_DEBUG("sasl step reply data too long %d\n",
                  serveroutlen);
        sasl_dispose(&vs->sasl.conn);
        vs->sasl.conn = NULL;
        goto authabort;
    }

    VNC_DEBUG("SASL return data %d bytes, nil; %d\n",
              serveroutlen, serverout ? 0 : 1);

    if (serveroutlen) {
        vnc_write_u32(vs, serveroutlen + 1);
        vnc_write(vs, serverout, serveroutlen + 1);
    } else {
        vnc_write_u32(vs, 0);
    }

    /* Whether auth is complete */
    vnc_write_u8(vs, err == SASL_CONTINUE ? 0 : 1);

    if (err == SASL_CONTINUE) {
        VNC_DEBUG("%s", "Authentication must continue\n");
        /* Wait for step length */
        vnc_read_when(vs, protocol_client_auth_sasl_step_len, 4);
    } else {
        if (!vnc_auth_sasl_check_ssf(vs)) {
            VNC_DEBUG("Authentication rejected for weak SSF %p\n", vs->ioc);
            goto authreject;
        }

        /* Check username whitelist ACL */
        if (vnc_auth_sasl_check_access(vs) < 0) {
            VNC_DEBUG("Authentication rejected for ACL %p\n", vs->ioc);
            goto authreject;
        }

        VNC_DEBUG("Authentication successful %p\n", vs->ioc);
        vnc_write_u32(vs, 0); /* Accept auth */
        /*
         * Delay writing in SSF encoded mode until pending output
         * buffer is written
         */
        if (vs->sasl.runSSF)
            vs->sasl.waitWriteSSF = vs->output.offset;
        start_client_init(vs);
    }

    return 0;

 authreject:
    vnc_write_u32(vs, 1); /* Reject auth */
    vnc_write_u32(vs, sizeof("Authentication failed"));
    vnc_write(vs, "Authentication failed", sizeof("Authentication failed"));
    vnc_flush(vs);
    vnc_client_error(vs);
    return -1;

 authabort:
    vnc_client_error(vs);
    return -1;
}

static int protocol_client_auth_sasl_step_len(VncState *vs, uint8_t *data, size_t len)
{
    uint32_t steplen = read_u32(data, 0);
    VNC_DEBUG("Got client step len %d\n", steplen);
    if (steplen > SASL_DATA_MAX_LEN) {
        VNC_DEBUG("Too much SASL data %d\n", steplen);
        vnc_client_error(vs);
        return -1;
    }

    if (steplen == 0)
        return protocol_client_auth_sasl_step(vs, NULL, 0);
    else
        vnc_read_when(vs, protocol_client_auth_sasl_step, steplen);
    return 0;
}

/*
 * Start Msg
 *
 * Input from client:
 *
 * u32 clientin-length
 * u8-array clientin-string
 *
 * Output to client:
 *
 * u32 serverout-length
 * u8-array serverout-strin
 * u8 continue
 */

#define SASL_DATA_MAX_LEN (1024 * 1024)

static int protocol_client_auth_sasl_start(VncState *vs, uint8_t *data, size_t len)
{
    uint32_t datalen = len;
    const char *serverout;
    unsigned int serveroutlen;
    int err;
    char *clientdata = NULL;

    /* NB, distinction of NULL vs "" is *critical* in SASL */
    if (datalen) {
        clientdata = (char*)data;
        clientdata[datalen-1] = '\0'; /* Should be on wire, but make sure */
        datalen--; /* Don't count NULL byte when passing to _start() */
    }

    VNC_DEBUG("Start SASL auth with mechanism %s. Data %p (%d bytes)\n",
              vs->sasl.mechlist, clientdata, datalen);
    err = sasl_server_start(vs->sasl.conn,
                            vs->sasl.mechlist,
                            clientdata,
                            datalen,
                            &serverout,
                            &serveroutlen);
    if (err != SASL_OK &&
        err != SASL_CONTINUE) {
        VNC_DEBUG("sasl start failed %d (%s)\n",
                  err, sasl_errdetail(vs->sasl.conn));
        sasl_dispose(&vs->sasl.conn);
        vs->sasl.conn = NULL;
        goto authabort;
    }
    if (serveroutlen > SASL_DATA_MAX_LEN) {
        VNC_DEBUG("sasl start reply data too long %d\n",
                  serveroutlen);
        sasl_dispose(&vs->sasl.conn);
        vs->sasl.conn = NULL;
        goto authabort;
    }

    VNC_DEBUG("SASL return data %d bytes, nil; %d\n",
              serveroutlen, serverout ? 0 : 1);

    if (serveroutlen) {
        vnc_write_u32(vs, serveroutlen + 1);
        vnc_write(vs, serverout, serveroutlen + 1);
    } else {
        vnc_write_u32(vs, 0);
    }

    /* Whether auth is complete */
    vnc_write_u8(vs, err == SASL_CONTINUE ? 0 : 1);

    if (err == SASL_CONTINUE) {
        VNC_DEBUG("%s", "Authentication must continue\n");
        /* Wait for step length */
        vnc_read_when(vs, protocol_client_auth_sasl_step_len, 4);
    } else {
        if (!vnc_auth_sasl_check_ssf(vs)) {
            VNC_DEBUG("Authentication rejected for weak SSF %p\n", vs->ioc);
            goto authreject;
        }

        /* Check username whitelist ACL */
        if (vnc_auth_sasl_check_access(vs) < 0) {
            VNC_DEBUG("Authentication rejected for ACL %p\n", vs->ioc);
            goto authreject;
        }

        VNC_DEBUG("Authentication successful %p\n", vs->ioc);
        vnc_write_u32(vs, 0); /* Accept auth */
        start_client_init(vs);
    }

    return 0;

 authreject:
    vnc_write_u32(vs, 1); /* Reject auth */
    vnc_write_u32(vs, sizeof("Authentication failed"));
    vnc_write(vs, "Authentication failed", sizeof("Authentication failed"));
    vnc_flush(vs);
    vnc_client_error(vs);
    return -1;

 authabort:
    vnc_client_error(vs);
    return -1;
}

static int protocol_client_auth_sasl_start_len(VncState *vs, uint8_t *data, size_t len)
{
    uint32_t startlen = read_u32(data, 0);
    VNC_DEBUG("Got client start len %d\n", startlen);
    if (startlen > SASL_DATA_MAX_LEN) {
        VNC_DEBUG("Too much SASL data %d\n", startlen);
        vnc_client_error(vs);
        return -1;
    }

    if (startlen == 0)
        return protocol_client_auth_sasl_start(vs, NULL, 0);

    vnc_read_when(vs, protocol_client_auth_sasl_start, startlen);
    return 0;
}

static int protocol_client_auth_sasl_mechname(VncState *vs, uint8_t *data, size_t len)
{
    char *mechname = g_strndup((const char *) data, len);
    VNC_DEBUG("Got client mechname '%s' check against '%s'\n",
              mechname, vs->sasl.mechlist);

    if (strncmp(vs->sasl.mechlist, mechname, len) == 0) {
        if (vs->sasl.mechlist[len] != '\0' &&
            vs->sasl.mechlist[len] != ',') {
            VNC_DEBUG("One %d", vs->sasl.mechlist[len]);
            goto fail;
        }
    } else {
        char *offset = strstr(vs->sasl.mechlist, mechname);
        VNC_DEBUG("Two %p\n", offset);
        if (!offset) {
            goto fail;
        }
        VNC_DEBUG("Two '%s'\n", offset);
        if (offset[-1] != ',' ||
            (offset[len] != '\0'&&
             offset[len] != ',')) {
            goto fail;
        }
    }

    g_free(vs->sasl.mechlist);
    vs->sasl.mechlist = mechname;

    VNC_DEBUG("Validated mechname '%s'\n", mechname);
    vnc_read_when(vs, protocol_client_auth_sasl_start_len, 4);
    return 0;

 fail:
    vnc_client_error(vs);
    g_free(mechname);
    return -1;
}

static int protocol_client_auth_sasl_mechname_len(VncState *vs, uint8_t *data, size_t len)
{
    uint32_t mechlen = read_u32(data, 0);
    VNC_DEBUG("Got client mechname len %d\n", mechlen);
    if (mechlen > 100) {
        VNC_DEBUG("Too long SASL mechname data %d\n", mechlen);
        vnc_client_error(vs);
        return -1;
    }
    if (mechlen < 1) {
        VNC_DEBUG("Too short SASL mechname %d\n", mechlen);
        vnc_client_error(vs);
        return -1;
    }
    vnc_read_when(vs, protocol_client_auth_sasl_mechname,mechlen);
    return 0;
}

static char *
vnc_socket_ip_addr_string(QIOChannelSocket *ioc,
                          bool local,
                          Error **errp)
{
    SocketAddress *addr;
    char *ret;

    if (local) {
        addr = qio_channel_socket_get_local_address(ioc, errp);
    } else {
        addr = qio_channel_socket_get_remote_address(ioc, errp);
    }
    if (!addr) {
        return NULL;
    }

    if (addr->type != SOCKET_ADDRESS_KIND_INET) {
        error_setg(errp, "Not an inet socket type");
        return NULL;
    }
    ret = g_strdup_printf("%s;%s", addr->u.inet->host, addr->u.inet->port);
    qapi_free_SocketAddress(addr);
    return ret;
}

void start_auth_sasl(VncState *vs)
{
    const char *mechlist = NULL;
    sasl_security_properties_t secprops;
    int err;
    char *localAddr, *remoteAddr;
    int mechlistlen;

    VNC_DEBUG("Initialize SASL auth %p\n", vs->ioc);

    /* Get local & remote client addresses in form  IPADDR;PORT */
    localAddr = vnc_socket_ip_addr_string(vs->sioc, true, NULL);
    if (!localAddr) {
        goto authabort;
    }

    remoteAddr = vnc_socket_ip_addr_string(vs->sioc, false, NULL);
    if (!remoteAddr) {
        g_free(localAddr);
        goto authabort;
    }

    err = sasl_server_new("vnc",
                          NULL, /* FQDN - just delegates to gethostname */
                          NULL, /* User realm */
                          localAddr,
                          remoteAddr,
                          NULL, /* Callbacks, not needed */
                          SASL_SUCCESS_DATA,
                          &vs->sasl.conn);
    g_free(localAddr);
    g_free(remoteAddr);
    localAddr = remoteAddr = NULL;

    if (err != SASL_OK) {
        VNC_DEBUG("sasl context setup failed %d (%s)",
                  err, sasl_errstring(err, NULL, NULL));
        vs->sasl.conn = NULL;
        goto authabort;
    }

    /* Inform SASL that we've got an external SSF layer from TLS/x509 */
    if (vs->auth == VNC_AUTH_VENCRYPT &&
        vs->subauth == VNC_AUTH_VENCRYPT_X509SASL) {
        Error *local_err = NULL;
        int keysize;
        sasl_ssf_t ssf;

        keysize = qcrypto_tls_session_get_key_size(vs->tls,
                                                   &local_err);
        if (keysize < 0) {
            VNC_DEBUG("cannot TLS get cipher size: %s\n",
                      error_get_pretty(local_err));
            error_free(local_err);
            sasl_dispose(&vs->sasl.conn);
            vs->sasl.conn = NULL;
            goto authabort;
        }
        ssf = keysize * CHAR_BIT; /* tls key size is bytes, sasl wants bits */

        err = sasl_setprop(vs->sasl.conn, SASL_SSF_EXTERNAL, &ssf);
        if (err != SASL_OK) {
            VNC_DEBUG("cannot set SASL external SSF %d (%s)\n",
                      err, sasl_errstring(err, NULL, NULL));
            sasl_dispose(&vs->sasl.conn);
            vs->sasl.conn = NULL;
            goto authabort;
        }
    } else {
        vs->sasl.wantSSF = 1;
    }

    memset (&secprops, 0, sizeof secprops);
    /* Inform SASL that we've got an external SSF layer from TLS.
     *
     * Disable SSF, if using TLS+x509+SASL only. TLS without x509
     * is not sufficiently strong
     */
    if (vs->vd->is_unix ||
        (vs->auth == VNC_AUTH_VENCRYPT &&
         vs->subauth == VNC_AUTH_VENCRYPT_X509SASL)) {
        /* If we've got TLS or UNIX domain sock, we don't care about SSF */
        secprops.min_ssf = 0;
        secprops.max_ssf = 0;
        secprops.maxbufsize = 8192;
        secprops.security_flags = 0;
    } else {
        /* Plain TCP, better get an SSF layer */
        secprops.min_ssf = 56; /* Good enough to require kerberos */
        secprops.max_ssf = 100000; /* Arbitrary big number */
        secprops.maxbufsize = 8192;
        /* Forbid any anonymous or trivially crackable auth */
        secprops.security_flags =
            SASL_SEC_NOANONYMOUS | SASL_SEC_NOPLAINTEXT;
    }

    err = sasl_setprop(vs->sasl.conn, SASL_SEC_PROPS, &secprops);
    if (err != SASL_OK) {
        VNC_DEBUG("cannot set SASL security props %d (%s)\n",
                  err, sasl_errstring(err, NULL, NULL));
        sasl_dispose(&vs->sasl.conn);
        vs->sasl.conn = NULL;
        goto authabort;
    }

    err = sasl_listmech(vs->sasl.conn,
                        NULL, /* Don't need to set user */
                        "", /* Prefix */
                        ",", /* Separator */
                        "", /* Suffix */
                        &mechlist,
                        NULL,
                        NULL);
    if (err != SASL_OK) {
        VNC_DEBUG("cannot list SASL mechanisms %d (%s)\n",
                  err, sasl_errdetail(vs->sasl.conn));
        sasl_dispose(&vs->sasl.conn);
        vs->sasl.conn = NULL;
        goto authabort;
    }
    VNC_DEBUG("Available mechanisms for client: '%s'\n", mechlist);

    vs->sasl.mechlist = g_strdup(mechlist);
    mechlistlen = strlen(mechlist);
    vnc_write_u32(vs, mechlistlen);
    vnc_write(vs, mechlist, mechlistlen);
    vnc_flush(vs);

    VNC_DEBUG("Wait for client mechname length\n");
    vnc_read_when(vs, protocol_client_auth_sasl_mechname_len, 4);

    return;

 authabort:
    vnc_client_error(vs);
}


