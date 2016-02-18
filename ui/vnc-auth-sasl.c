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
#include "qapi/error.h"
#include "authz/base.h"
#include "vnc.h"
#include "trace.h"

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


size_t vnc_client_write_sasl(VncState *vs)
{
    size_t ret;

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

        vs->sasl.encodedRawLength = vs->output.offset;
        vs->sasl.encodedOffset = 0;
    }

    ret = vnc_client_write_buf(vs,
                               vs->sasl.encoded + vs->sasl.encodedOffset,
                               vs->sasl.encodedLength - vs->sasl.encodedOffset);
    if (!ret)
        return 0;

    vs->sasl.encodedOffset += ret;
    if (vs->sasl.encodedOffset == vs->sasl.encodedLength) {
        bool throttled = vs->force_update_offset != 0;
        size_t offset;
        if (vs->sasl.encodedRawLength >= vs->force_update_offset) {
            vs->force_update_offset = 0;
        } else {
            vs->force_update_offset -= vs->sasl.encodedRawLength;
        }
        if (throttled && vs->force_update_offset == 0) {
            trace_vnc_client_unthrottle_forced(vs, vs->ioc);
        }
        offset = vs->output.offset;
        buffer_advance(&vs->output, vs->sasl.encodedRawLength);
        if (offset >= vs->throttle_output_offset &&
            vs->output.offset < vs->throttle_output_offset) {
            trace_vnc_client_unthrottle_incremental(vs, vs->ioc,
                                                    vs->output.offset);
        }
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


size_t vnc_client_read_sasl(VncState *vs)
{
    size_t ret;
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
    int rv;
    Error *err = NULL;
    bool allow;

    rv = sasl_getprop(vs->sasl.conn, SASL_USERNAME, &val);
    if (rv != SASL_OK) {
        trace_vnc_auth_fail(vs, vs->auth, "Cannot fetch SASL username",
                            sasl_errstring(rv, NULL, NULL));
        return -1;
    }
    if (val == NULL) {
        trace_vnc_auth_fail(vs, vs->auth, "No SASL username set", "");
        return -1;
    }

    vs->sasl.username = g_strdup((const char*)val);
    trace_vnc_auth_sasl_username(vs, vs->sasl.username);

    if (vs->vd->sasl.authzid == NULL) {
        trace_vnc_auth_sasl_acl(vs, 1);
        return 0;
    }

    allow = qauthz_is_allowed_by_id(vs->vd->sasl.authzid,
                                    vs->sasl.username, &err);
    if (err) {
        trace_vnc_auth_fail(vs, vs->auth, "Error from authz",
                            error_get_pretty(err));
        error_free(err);
        return -1;
    }

    trace_vnc_auth_sasl_acl(vs, allow);
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

    trace_vnc_auth_sasl_ssf(vs, ssf);

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

    err = sasl_server_step(vs->sasl.conn,
                           clientdata,
                           datalen,
                           &serverout,
                           &serveroutlen);
    trace_vnc_auth_sasl_step(vs, data, len, serverout, serveroutlen, err);
    if (err != SASL_OK &&
        err != SASL_CONTINUE) {
        trace_vnc_auth_fail(vs, vs->auth, "Cannot step SASL auth",
                            sasl_errdetail(vs->sasl.conn));
        sasl_dispose(&vs->sasl.conn);
        vs->sasl.conn = NULL;
        goto authabort;
    }

    if (serveroutlen > SASL_DATA_MAX_LEN) {
        trace_vnc_auth_fail(vs, vs->auth, "SASL data too long", "");
        sasl_dispose(&vs->sasl.conn);
        vs->sasl.conn = NULL;
        goto authabort;
    }

    if (serveroutlen) {
        vnc_write_u32(vs, serveroutlen + 1);
        vnc_write(vs, serverout, serveroutlen + 1);
    } else {
        vnc_write_u32(vs, 0);
    }

    /* Whether auth is complete */
    vnc_write_u8(vs, err == SASL_CONTINUE ? 0 : 1);

    if (err == SASL_CONTINUE) {
        /* Wait for step length */
        vnc_read_when(vs, protocol_client_auth_sasl_step_len, 4);
    } else {
        if (!vnc_auth_sasl_check_ssf(vs)) {
            trace_vnc_auth_fail(vs, vs->auth, "SASL SSF too weak", "");
            goto authreject;
        }

        /* Check username whitelist ACL */
        if (vnc_auth_sasl_check_access(vs) < 0) {
            goto authreject;
        }

        trace_vnc_auth_pass(vs, vs->auth);
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

    if (steplen > SASL_DATA_MAX_LEN) {
        trace_vnc_auth_fail(vs, vs->auth, "SASL step len too large", "");
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

    err = sasl_server_start(vs->sasl.conn,
                            vs->sasl.mechlist,
                            clientdata,
                            datalen,
                            &serverout,
                            &serveroutlen);
    trace_vnc_auth_sasl_start(vs, data, len, serverout, serveroutlen, err);
    if (err != SASL_OK &&
        err != SASL_CONTINUE) {
        trace_vnc_auth_fail(vs, vs->auth, "Cannot start SASL auth",
                            sasl_errdetail(vs->sasl.conn));
        sasl_dispose(&vs->sasl.conn);
        vs->sasl.conn = NULL;
        goto authabort;
    }
    if (serveroutlen > SASL_DATA_MAX_LEN) {
        trace_vnc_auth_fail(vs, vs->auth, "SASL data too long", "");
        sasl_dispose(&vs->sasl.conn);
        vs->sasl.conn = NULL;
        goto authabort;
    }

    if (serveroutlen) {
        vnc_write_u32(vs, serveroutlen + 1);
        vnc_write(vs, serverout, serveroutlen + 1);
    } else {
        vnc_write_u32(vs, 0);
    }

    /* Whether auth is complete */
    vnc_write_u8(vs, err == SASL_CONTINUE ? 0 : 1);

    if (err == SASL_CONTINUE) {
        /* Wait for step length */
        vnc_read_when(vs, protocol_client_auth_sasl_step_len, 4);
    } else {
        if (!vnc_auth_sasl_check_ssf(vs)) {
            trace_vnc_auth_fail(vs, vs->auth, "SASL SSF too weak", "");
            goto authreject;
        }

        /* Check username whitelist ACL */
        if (vnc_auth_sasl_check_access(vs) < 0) {
            goto authreject;
        }

        trace_vnc_auth_pass(vs, vs->auth);
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

    if (startlen > SASL_DATA_MAX_LEN) {
        trace_vnc_auth_fail(vs, vs->auth, "SASL start len too large", "");
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
    trace_vnc_auth_sasl_mech_choose(vs, mechname);

    if (strncmp(vs->sasl.mechlist, mechname, len) == 0) {
        if (vs->sasl.mechlist[len] != '\0' &&
            vs->sasl.mechlist[len] != ',') {
            goto fail;
        }
    } else {
        char *offset = strstr(vs->sasl.mechlist, mechname);
        if (!offset) {
            goto fail;
        }
        if (offset[-1] != ',' ||
            (offset[len] != '\0'&&
             offset[len] != ',')) {
            goto fail;
        }
    }

    g_free(vs->sasl.mechlist);
    vs->sasl.mechlist = mechname;

    vnc_read_when(vs, protocol_client_auth_sasl_start_len, 4);
    return 0;

 fail:
    trace_vnc_auth_fail(vs, vs->auth, "Unsupported mechname", mechname);
    vnc_client_error(vs);
    g_free(mechname);
    return -1;
}

static int protocol_client_auth_sasl_mechname_len(VncState *vs, uint8_t *data, size_t len)
{
    uint32_t mechlen = read_u32(data, 0);

    if (mechlen > 100) {
        trace_vnc_auth_fail(vs, vs->auth, "SASL mechname too long", "");
        vnc_client_error(vs);
        return -1;
    }
    if (mechlen < 1) {
        trace_vnc_auth_fail(vs, vs->auth, "SASL mechname too short", "");
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

    if (addr->type != SOCKET_ADDRESS_TYPE_INET) {
        error_setg(errp, "Not an inet socket type");
        return NULL;
    }
    ret = g_strdup_printf("%s;%s", addr->u.inet.host, addr->u.inet.port);
    qapi_free_SocketAddress(addr);
    return ret;
}

void start_auth_sasl(VncState *vs)
{
    const char *mechlist = NULL;
    sasl_security_properties_t secprops;
    int err;
    Error *local_err = NULL;
    char *localAddr, *remoteAddr;
    int mechlistlen;

    /* Get local & remote client addresses in form  IPADDR;PORT */
    localAddr = vnc_socket_ip_addr_string(vs->sioc, true, &local_err);
    if (!localAddr) {
        trace_vnc_auth_fail(vs, vs->auth, "Cannot format local IP",
                            error_get_pretty(local_err));
        goto authabort;
    }

    remoteAddr = vnc_socket_ip_addr_string(vs->sioc, false, &local_err);
    if (!remoteAddr) {
        trace_vnc_auth_fail(vs, vs->auth, "Cannot format remote IP",
                            error_get_pretty(local_err));
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
        trace_vnc_auth_fail(vs, vs->auth,  "SASL context setup failed",
                            sasl_errstring(err, NULL, NULL));
        vs->sasl.conn = NULL;
        goto authabort;
    }

    /* Inform SASL that we've got an external SSF layer from TLS/x509 */
    if (vs->auth == VNC_AUTH_VENCRYPT &&
        vs->subauth == VNC_AUTH_VENCRYPT_X509SASL) {
        int keysize;
        sasl_ssf_t ssf;

        keysize = qcrypto_tls_session_get_key_size(vs->tls,
                                                   &local_err);
        if (keysize < 0) {
            trace_vnc_auth_fail(vs, vs->auth, "cannot TLS get cipher size",
                                error_get_pretty(local_err));
            sasl_dispose(&vs->sasl.conn);
            vs->sasl.conn = NULL;
            goto authabort;
        }
        ssf = keysize * CHAR_BIT; /* tls key size is bytes, sasl wants bits */

        err = sasl_setprop(vs->sasl.conn, SASL_SSF_EXTERNAL, &ssf);
        if (err != SASL_OK) {
            trace_vnc_auth_fail(vs, vs->auth, "cannot set SASL external SSF",
                                sasl_errstring(err, NULL, NULL));
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
        trace_vnc_auth_fail(vs, vs->auth, "cannot set SASL security props",
                            sasl_errstring(err, NULL, NULL));
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
        trace_vnc_auth_fail(vs, vs->auth, "cannot list SASL mechanisms",
                            sasl_errdetail(vs->sasl.conn));
        sasl_dispose(&vs->sasl.conn);
        vs->sasl.conn = NULL;
        goto authabort;
    }
    trace_vnc_auth_sasl_mech_list(vs, mechlist);

    vs->sasl.mechlist = g_strdup(mechlist);
    mechlistlen = strlen(mechlist);
    vnc_write_u32(vs, mechlistlen);
    vnc_write(vs, mechlist, mechlistlen);
    vnc_flush(vs);

    vnc_read_when(vs, protocol_client_auth_sasl_mechname_len, 4);

    return;

 authabort:
    error_free(local_err);
    vnc_client_error(vs);
}


