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


#ifndef __QEMU_VNC_AUTH_SASL_H__
#define __QEMU_VNC_AUTH_SASL_H__


#include <sasl/sasl.h>

typedef struct VncStateSASL VncStateSASL;
typedef struct VncDisplaySASL VncDisplaySASL;

#include "acl.h"

struct VncStateSASL {
    sasl_conn_t *conn;
    /* If we want to negotiate an SSF layer with client */
    bool wantSSF;
    /* If we are now running the SSF layer */
    bool runSSF;
    /*
     * If this is non-zero, then wait for that many bytes
     * to be written plain, before switching to SSF encoding
     * This allows the VNC auth result to finish being
     * written in plain.
     */
    unsigned int waitWriteSSF;

    /*
     * Buffering encoded data to allow more clear data
     * to be stuffed onto the output buffer
     */
    const uint8_t *encoded;
    unsigned int encodedLength;
    unsigned int encodedOffset;
    char *username;
    char *mechlist;
};

struct VncDisplaySASL {
    qemu_acl *acl;
};

void vnc_sasl_client_cleanup(VncState *vs);

long vnc_client_read_sasl(VncState *vs);
long vnc_client_write_sasl(VncState *vs);

void start_auth_sasl(VncState *vs);

#endif /* __QEMU_VNC_AUTH_SASL_H__ */

