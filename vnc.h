/*
 * QEMU VNC display driver
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

#ifndef __QEMU_VNC_H
#define __QEMU_VNC_H

#include "qemu-common.h"
#include "console.h"
#include "monitor.h"
#include "audio/audio.h"
#include <zlib.h>

#ifdef CONFIG_VNC_TLS
#include <gnutls/gnutls.h>
#include <gnutls/x509.h>
#endif /* CONFIG_VNC_TLS */

#include "keymaps.h"

/*****************************************************************************
 *
 * Core data structures
 *
 *****************************************************************************/

typedef struct Buffer
{
    size_t capacity;
    size_t offset;
    uint8_t *buffer;
} Buffer;

typedef struct VncState VncState;

typedef int VncReadEvent(VncState *vs, uint8_t *data, size_t len);

typedef void VncWritePixels(VncState *vs, void *data, int size);

typedef void VncSendHextileTile(VncState *vs,
                                int x, int y, int w, int h,
                                void *last_bg,
                                void *last_fg,
                                int *has_bg, int *has_fg);

#define VNC_MAX_WIDTH 2048
#define VNC_MAX_HEIGHT 2048
#define VNC_DIRTY_WORDS (VNC_MAX_WIDTH / (16 * 32))

#define VNC_AUTH_CHALLENGE_SIZE 16

typedef struct VncDisplay VncDisplay;

struct VncDisplay
{
    int lsock;
    DisplayState *ds;
    VncState *clients;
    kbd_layout_t *kbd_layout;

    char *display;
    char *password;
    int auth;
#ifdef CONFIG_VNC_TLS
    int subauth;
    int x509verify;

    char *x509cacert;
    char *x509cacrl;
    char *x509cert;
    char *x509key;
#endif
};

struct VncState
{
    QEMUTimer *timer;
    int csock;
    DisplayState *ds;
    VncDisplay *vd;
    int need_update;
    uint32_t dirty_row[VNC_MAX_HEIGHT][VNC_DIRTY_WORDS];
    char *old_data;
    uint32_t features;
    int absolute;
    int last_x;
    int last_y;

    uint32_t vnc_encoding;
    uint8_t tight_quality;
    uint8_t tight_compression;

    int major;
    int minor;

    char challenge[VNC_AUTH_CHALLENGE_SIZE];

#ifdef CONFIG_VNC_TLS
    int wiremode;
    gnutls_session_t tls_session;
#endif

    Buffer output;
    Buffer input;
    /* current output mode information */
    VncWritePixels *write_pixels;
    VncSendHextileTile *send_hextile_tile;
    DisplaySurface clientds, serverds;

    CaptureVoiceOut *audio_cap;
    struct audsettings as;

    VncReadEvent *read_handler;
    size_t read_handler_expect;
    /* input */
    uint8_t modifiers_state[256];

    Buffer zlib;
    Buffer zlib_tmp;
    z_stream zlib_stream[4];

    VncState *next;
};


/*****************************************************************************
 *
 * Authentication modes
 *
 *****************************************************************************/

enum {
    VNC_AUTH_INVALID = 0,
    VNC_AUTH_NONE = 1,
    VNC_AUTH_VNC = 2,
    VNC_AUTH_RA2 = 5,
    VNC_AUTH_RA2NE = 6,
    VNC_AUTH_TIGHT = 16,
    VNC_AUTH_ULTRA = 17,
    VNC_AUTH_TLS = 18,
    VNC_AUTH_VENCRYPT = 19
};

#ifdef CONFIG_VNC_TLS
enum {
    VNC_WIREMODE_CLEAR,
    VNC_WIREMODE_TLS,
};

enum {
    VNC_AUTH_VENCRYPT_PLAIN = 256,
    VNC_AUTH_VENCRYPT_TLSNONE = 257,
    VNC_AUTH_VENCRYPT_TLSVNC = 258,
    VNC_AUTH_VENCRYPT_TLSPLAIN = 259,
    VNC_AUTH_VENCRYPT_X509NONE = 260,
    VNC_AUTH_VENCRYPT_X509VNC = 261,
    VNC_AUTH_VENCRYPT_X509PLAIN = 262,
};

#define X509_CA_CERT_FILE "ca-cert.pem"
#define X509_CA_CRL_FILE "ca-crl.pem"
#define X509_SERVER_KEY_FILE "server-key.pem"
#define X509_SERVER_CERT_FILE "server-cert.pem"

#endif /* CONFIG_VNC_TLS */

/*****************************************************************************
 *
 * Encoding types
 *
 *****************************************************************************/

#define VNC_ENCODING_RAW                  0x00000000
#define VNC_ENCODING_COPYRECT             0x00000001
#define VNC_ENCODING_RRE                  0x00000002
#define VNC_ENCODING_CORRE                0x00000004
#define VNC_ENCODING_HEXTILE              0x00000005
#define VNC_ENCODING_ZLIB                 0x00000006
#define VNC_ENCODING_TIGHT                0x00000007
#define VNC_ENCODING_ZLIBHEX              0x00000008
#define VNC_ENCODING_TRLE                 0x0000000f
#define VNC_ENCODING_ZRLE                 0x00000010
#define VNC_ENCODING_ZYWRLE               0x00000011
#define VNC_ENCODING_COMPRESSLEVEL0       0xFFFFFF00 /* -256 */
#define VNC_ENCODING_QUALITYLEVEL0        0xFFFFFFE0 /* -32  */
#define VNC_ENCODING_XCURSOR              0xFFFFFF10 /* -240 */
#define VNC_ENCODING_RICH_CURSOR          0xFFFFFF11 /* -239 */
#define VNC_ENCODING_POINTER_POS          0xFFFFFF18 /* -232 */
#define VNC_ENCODING_LASTRECT             0xFFFFFF20 /* -224 */
#define VNC_ENCODING_DESKTOPRESIZE        0xFFFFFF21 /* -223 */
#define VNC_ENCODING_POINTER_TYPE_CHANGE  0XFFFFFEFF /* -257 */
#define VNC_ENCODING_EXT_KEY_EVENT        0XFFFFFEFE /* -258 */
#define VNC_ENCODING_AUDIO                0XFFFFFEFD /* -259 */
#define VNC_ENCODING_WMVi                 0x574D5669

/*****************************************************************************
 *
 * Other tight constants
 *
 *****************************************************************************/

/*
 * Vendors known by TightVNC: standard VNC/RealVNC, TridiaVNC, and TightVNC.
 */

#define VNC_TIGHT_CCB_RESET_MASK   (0x0f)
#define VNC_TIGHT_CCB_TYPE_MASK    (0x0f << 4)
#define VNC_TIGHT_CCB_TYPE_FILL    (0x08 << 4)
#define VNC_TIGHT_CCB_TYPE_JPEG    (0x09 << 4)
#define VNC_TIGHT_CCB_BASIC_MAX    (0x07 << 4)
#define VNC_TIGHT_CCB_BASIC_ZLIB   (0x03 << 4)
#define VNC_TIGHT_CCB_BASIC_FILTER (0x04 << 4)

/*****************************************************************************
 *
 * Features
 *
 *****************************************************************************/

#define VNC_FEATURE_RESIZE                   0
#define VNC_FEATURE_HEXTILE                  1
#define VNC_FEATURE_POINTER_TYPE_CHANGE      2
#define VNC_FEATURE_WMVI                     3
#define VNC_FEATURE_TIGHT                    4
#define VNC_FEATURE_ZLIB                     5
#define VNC_FEATURE_COPYRECT                 6

#define VNC_FEATURE_RESIZE_MASK              (1 << VNC_FEATURE_RESIZE)
#define VNC_FEATURE_HEXTILE_MASK             (1 << VNC_FEATURE_HEXTILE)
#define VNC_FEATURE_POINTER_TYPE_CHANGE_MASK (1 << VNC_FEATURE_POINTER_TYPE_CHANGE)
#define VNC_FEATURE_WMVI_MASK                (1 << VNC_FEATURE_WMVI)
#define VNC_FEATURE_TIGHT_MASK               (1 << VNC_FEATURE_TIGHT)
#define VNC_FEATURE_ZLIB_MASK                (1 << VNC_FEATURE_ZLIB)
#define VNC_FEATURE_COPYRECT_MASK            (1 << VNC_FEATURE_COPYRECT)

#endif /* __QEMU_VNC_H */
