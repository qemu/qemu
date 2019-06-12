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

#ifndef QEMU_VNC_H
#define QEMU_VNC_H

#include "qapi/qapi-types-ui.h"
#include "qemu/queue.h"
#include "qemu/thread.h"
#include "ui/console.h"
#include "audio/audio.h"
#include "qemu/bitmap.h"
#include "crypto/tlssession.h"
#include "qemu/buffer.h"
#include "io/channel-socket.h"
#include "io/channel-tls.h"
#include "io/net-listener.h"
#include "authz/base.h"
#include <zlib.h>

#include "keymaps.h"
#include "vnc-palette.h"
#include "vnc-enc-zrle.h"
#include "ui/kbd-state.h"

// #define _VNC_DEBUG 1

#ifdef _VNC_DEBUG
#define VNC_DEBUG(fmt, ...) do { fprintf(stderr, fmt, ## __VA_ARGS__); } while (0)
#else
#define VNC_DEBUG(fmt, ...) do { } while (0)
#endif

/*****************************************************************************
 *
 * Core data structures
 *
 *****************************************************************************/

typedef struct VncState VncState;
typedef struct VncJob VncJob;
typedef struct VncRect VncRect;
typedef struct VncRectEntry VncRectEntry;

typedef int VncReadEvent(VncState *vs, uint8_t *data, size_t len);

typedef void VncWritePixels(VncState *vs, void *data, int size);

typedef void VncSendHextileTile(VncState *vs,
                                int x, int y, int w, int h,
                                void *last_bg,
                                void *last_fg,
                                int *has_bg, int *has_fg);

/* VNC_DIRTY_PIXELS_PER_BIT is the number of dirty pixels represented
 * by one bit in the dirty bitmap, should be a power of 2 */
#define VNC_DIRTY_PIXELS_PER_BIT 16

/* VNC_MAX_WIDTH must be a multiple of VNC_DIRTY_PIXELS_PER_BIT. */

#define VNC_MAX_WIDTH ROUND_UP(2560, VNC_DIRTY_PIXELS_PER_BIT)
#define VNC_MAX_HEIGHT 2048

/* VNC_DIRTY_BITS is the number of bits in the dirty bitmap. */
#define VNC_DIRTY_BITS (VNC_MAX_WIDTH / VNC_DIRTY_PIXELS_PER_BIT)

/* VNC_DIRTY_BPL (BPL = bits per line) might be greater than
 * VNC_DIRTY_BITS due to alignment */
#define VNC_DIRTY_BPL(x) (sizeof((x)->dirty) / VNC_MAX_HEIGHT * BITS_PER_BYTE)

#define VNC_STAT_RECT  64
#define VNC_STAT_COLS (VNC_MAX_WIDTH / VNC_STAT_RECT)
#define VNC_STAT_ROWS (VNC_MAX_HEIGHT / VNC_STAT_RECT)

#define VNC_AUTH_CHALLENGE_SIZE 16

typedef struct VncDisplay VncDisplay;

#include "vnc-auth-vencrypt.h"
#ifdef CONFIG_VNC_SASL
#include "vnc-auth-sasl.h"
#endif
#include "vnc-ws.h"

struct VncRectStat
{
    /* time of last 10 updates, to find update frequency */
    struct timeval times[10];
    int idx;

    double freq;        /* Update frequency (in Hz) */
    bool updated;       /* Already updated during this refresh */
};

typedef struct VncRectStat VncRectStat;

struct VncSurface
{
    struct timeval last_freq_check;
    DECLARE_BITMAP(dirty[VNC_MAX_HEIGHT],
                   VNC_MAX_WIDTH / VNC_DIRTY_PIXELS_PER_BIT);
    VncRectStat stats[VNC_STAT_ROWS][VNC_STAT_COLS];
    pixman_image_t *fb;
    pixman_format_code_t format;
};

typedef enum VncShareMode {
    VNC_SHARE_MODE_CONNECTING = 1,
    VNC_SHARE_MODE_SHARED,
    VNC_SHARE_MODE_EXCLUSIVE,
    VNC_SHARE_MODE_DISCONNECTED,
} VncShareMode;

typedef enum VncSharePolicy {
    VNC_SHARE_POLICY_IGNORE = 1,
    VNC_SHARE_POLICY_ALLOW_EXCLUSIVE,
    VNC_SHARE_POLICY_FORCE_SHARED,
} VncSharePolicy;

struct VncDisplay
{
    QTAILQ_HEAD(, VncState) clients;
    int num_connecting;
    int num_shared;
    int num_exclusive;
    int connections_limit;
    VncSharePolicy share_policy;
    QIONetListener *listener;
    QIONetListener *wslistener;
    DisplaySurface *ds;
    DisplayChangeListener dcl;
    kbd_layout_t *kbd_layout;
    int lock_key_sync;
    QEMUPutLEDEntry *led;
    int ledstate;
    QKbdState *kbd;
    QemuMutex mutex;

    QEMUCursor *cursor;
    int cursor_msize;
    uint8_t *cursor_mask;

    struct VncSurface guest;   /* guest visible surface (aka ds->surface) */
    pixman_image_t *server;    /* vnc server surface */

    const char *id;
    QTAILQ_ENTRY(VncDisplay) next;
    bool is_unix;
    char *password;
    time_t expires;
    int auth;
    int subauth; /* Used by VeNCrypt */
    int ws_auth; /* Used by websockets */
    int ws_subauth; /* Used by websockets */
    bool lossy;
    bool non_adaptive;
    QCryptoTLSCreds *tlscreds;
    QAuthZ *tlsauthz;
    char *tlsauthzid;
#ifdef CONFIG_VNC_SASL
    VncDisplaySASL sasl;
#endif
};

typedef struct VncTight {
    int type;
    uint8_t quality;
    uint8_t compression;
    uint8_t pixel24;
    Buffer tight;
    Buffer tmp;
    Buffer zlib;
    Buffer gradient;
#ifdef CONFIG_VNC_JPEG
    Buffer jpeg;
#endif
#ifdef CONFIG_VNC_PNG
    Buffer png;
#endif
    int levels[4];
    z_stream stream[4];
} VncTight;

typedef struct VncHextile {
    VncSendHextileTile *send_tile;
} VncHextile;

typedef struct VncZlib {
    Buffer zlib;
    Buffer tmp;
    z_stream stream;
    int level;
} VncZlib;

typedef struct VncZrle {
    int type;
    Buffer fb;
    Buffer zrle;
    Buffer tmp;
    Buffer zlib;
    z_stream stream;
    VncPalette palette;
} VncZrle;

typedef struct VncZywrle {
    int buf[VNC_ZRLE_TILE_WIDTH * VNC_ZRLE_TILE_HEIGHT];
} VncZywrle;

struct VncRect
{
    int x;
    int y;
    int w;
    int h;
};

struct VncRectEntry
{
    struct VncRect rect;
    QLIST_ENTRY(VncRectEntry) next;
};

struct VncJob
{
    VncState *vs;

    QLIST_HEAD(, VncRectEntry) rectangles;
    QTAILQ_ENTRY(VncJob) next;
};

typedef enum {
    VNC_STATE_UPDATE_NONE,
    VNC_STATE_UPDATE_INCREMENTAL,
    VNC_STATE_UPDATE_FORCE,
} VncStateUpdate;

#define VNC_MAGIC ((uint64_t)0x05b3f069b3d204bb)

struct VncState
{
    uint64_t magic;
    QIOChannelSocket *sioc; /* The underlying socket */
    QIOChannel *ioc; /* The channel currently used for I/O */
    guint ioc_tag;
    gboolean disconnecting;

    DECLARE_BITMAP(dirty[VNC_MAX_HEIGHT], VNC_DIRTY_BITS);
    uint8_t **lossy_rect; /* Not an Array to avoid costly memcpy in
                           * vnc-jobs-async.c */

    VncDisplay *vd;
    VncStateUpdate update; /* Most recent pending request from client */
    VncStateUpdate job_update; /* Currently processed by job thread */
    int has_dirty;
    uint32_t features;
    int absolute;
    int last_x;
    int last_y;
    uint32_t last_bmask;
    size_t client_width; /* limited to u16 by RFB proto */
    size_t client_height; /* limited to u16 by RFB proto */
    VncShareMode share_mode;

    uint32_t vnc_encoding;

    int major;
    int minor;

    int auth;
    int subauth; /* Used by VeNCrypt */
    char challenge[VNC_AUTH_CHALLENGE_SIZE];
    QCryptoTLSSession *tls; /* Borrowed pointer from channel, don't free */
#ifdef CONFIG_VNC_SASL
    VncStateSASL sasl;
#endif
    bool encode_ws;
    bool websocket;

#ifdef CONFIG_VNC
    VncClientInfo *info;
#endif

    /* Job thread bottom half has put data for a forced update
     * into the output buffer. This offset points to the end of
     * the update data in the output buffer. This lets us determine
     * when a force update is fully sent to the client, allowing
     * us to process further forced updates. */
    size_t force_update_offset;
    /* We allow multiple incremental updates or audio capture
     * samples to be queued in output buffer, provided the
     * buffer size doesn't exceed this threshold. The value
     * is calculating dynamically based on framebuffer size
     * and audio sample settings in vnc_update_throttle_offset() */
    size_t throttle_output_offset;
    Buffer output;
    Buffer input;
    /* current output mode information */
    VncWritePixels *write_pixels;
    PixelFormat client_pf;
    pixman_format_code_t client_format;
    bool client_be;

    CaptureVoiceOut *audio_cap;
    struct audsettings as;

    VncReadEvent *read_handler;
    size_t read_handler_expect;

    bool abort;
    QemuMutex output_mutex;
    QEMUBH *bh;
    Buffer jobs_buffer;

    /* Encoding specific, if you add something here, don't forget to
     *  update vnc_async_encoding_start()
     */
    VncTight tight;
    VncZlib zlib;
    VncHextile hextile;
    VncZrle zrle;
    VncZywrle zywrle;

    Notifier mouse_mode_notifier;

    QTAILQ_ENTRY(VncState) next;
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
    VNC_AUTH_TLS = 18,      /* Supported in GTK-VNC & VINO */
    VNC_AUTH_VENCRYPT = 19, /* Supported in GTK-VNC & VeNCrypt */
    VNC_AUTH_SASL = 20,     /* Supported in GTK-VNC & VINO */
};

enum {
    VNC_AUTH_VENCRYPT_PLAIN = 256,
    VNC_AUTH_VENCRYPT_TLSNONE = 257,
    VNC_AUTH_VENCRYPT_TLSVNC = 258,
    VNC_AUTH_VENCRYPT_TLSPLAIN = 259,
    VNC_AUTH_VENCRYPT_X509NONE = 260,
    VNC_AUTH_VENCRYPT_X509VNC = 261,
    VNC_AUTH_VENCRYPT_X509PLAIN = 262,
    VNC_AUTH_VENCRYPT_X509SASL = 263,
    VNC_AUTH_VENCRYPT_TLSSASL = 264,
};


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
#define VNC_ENCODING_TIGHT_PNG            0xFFFFFEFC /* -260 */
#define VNC_ENCODING_LED_STATE            0XFFFFFEFB /* -261 */
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
#define VNC_TIGHT_CCB_TYPE_PNG     (0x0A << 4)
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
#define VNC_FEATURE_RICH_CURSOR              7
#define VNC_FEATURE_TIGHT_PNG                8
#define VNC_FEATURE_ZRLE                     9
#define VNC_FEATURE_ZYWRLE                  10
#define VNC_FEATURE_LED_STATE               11

#define VNC_FEATURE_RESIZE_MASK              (1 << VNC_FEATURE_RESIZE)
#define VNC_FEATURE_HEXTILE_MASK             (1 << VNC_FEATURE_HEXTILE)
#define VNC_FEATURE_POINTER_TYPE_CHANGE_MASK (1 << VNC_FEATURE_POINTER_TYPE_CHANGE)
#define VNC_FEATURE_WMVI_MASK                (1 << VNC_FEATURE_WMVI)
#define VNC_FEATURE_TIGHT_MASK               (1 << VNC_FEATURE_TIGHT)
#define VNC_FEATURE_ZLIB_MASK                (1 << VNC_FEATURE_ZLIB)
#define VNC_FEATURE_COPYRECT_MASK            (1 << VNC_FEATURE_COPYRECT)
#define VNC_FEATURE_RICH_CURSOR_MASK         (1 << VNC_FEATURE_RICH_CURSOR)
#define VNC_FEATURE_TIGHT_PNG_MASK           (1 << VNC_FEATURE_TIGHT_PNG)
#define VNC_FEATURE_ZRLE_MASK                (1 << VNC_FEATURE_ZRLE)
#define VNC_FEATURE_ZYWRLE_MASK              (1 << VNC_FEATURE_ZYWRLE)
#define VNC_FEATURE_LED_STATE_MASK           (1 << VNC_FEATURE_LED_STATE)


/* Client -> Server message IDs */
#define VNC_MSG_CLIENT_SET_PIXEL_FORMAT           0
#define VNC_MSG_CLIENT_SET_ENCODINGS              2
#define VNC_MSG_CLIENT_FRAMEBUFFER_UPDATE_REQUEST 3
#define VNC_MSG_CLIENT_KEY_EVENT                  4
#define VNC_MSG_CLIENT_POINTER_EVENT              5
#define VNC_MSG_CLIENT_CUT_TEXT                   6
#define VNC_MSG_CLIENT_VMWARE_0                   127
#define VNC_MSG_CLIENT_CALL_CONTROL               249
#define VNC_MSG_CLIENT_XVP                        250
#define VNC_MSG_CLIENT_SET_DESKTOP_SIZE           251
#define VNC_MSG_CLIENT_TIGHT                      252
#define VNC_MSG_CLIENT_GII                        253
#define VNC_MSG_CLIENT_VMWARE_1                   254
#define VNC_MSG_CLIENT_QEMU                       255

/* Server -> Client message IDs */
#define VNC_MSG_SERVER_FRAMEBUFFER_UPDATE         0
#define VNC_MSG_SERVER_SET_COLOUR_MAP_ENTRIES     1
#define VNC_MSG_SERVER_BELL                       2
#define VNC_MSG_SERVER_CUT_TEXT                   3
#define VNC_MSG_SERVER_VMWARE_0                   127
#define VNC_MSG_SERVER_CALL_CONTROL               249
#define VNC_MSG_SERVER_XVP                        250
#define VNC_MSG_SERVER_TIGHT                      252
#define VNC_MSG_SERVER_GII                        253
#define VNC_MSG_SERVER_VMWARE_1                   254
#define VNC_MSG_SERVER_QEMU                       255



/* QEMU client -> server message IDs */
#define VNC_MSG_CLIENT_QEMU_EXT_KEY_EVENT         0
#define VNC_MSG_CLIENT_QEMU_AUDIO                 1

/* QEMU server -> client message IDs */
#define VNC_MSG_SERVER_QEMU_AUDIO                 1



/* QEMU client -> server audio message IDs */
#define VNC_MSG_CLIENT_QEMU_AUDIO_ENABLE          0
#define VNC_MSG_CLIENT_QEMU_AUDIO_DISABLE         1
#define VNC_MSG_CLIENT_QEMU_AUDIO_SET_FORMAT      2

/* QEMU server -> client audio message IDs */
#define VNC_MSG_SERVER_QEMU_AUDIO_END             0
#define VNC_MSG_SERVER_QEMU_AUDIO_BEGIN           1
#define VNC_MSG_SERVER_QEMU_AUDIO_DATA            2


/*****************************************************************************
 *
 * Internal APIs
 *
 *****************************************************************************/

/* Event loop functions */
gboolean vnc_client_io(QIOChannel *ioc,
                       GIOCondition condition,
                       void *opaque);

size_t vnc_client_read_buf(VncState *vs, uint8_t *data, size_t datalen);
size_t vnc_client_write_buf(VncState *vs, const uint8_t *data, size_t datalen);

/* Protocol I/O functions */
void vnc_write(VncState *vs, const void *data, size_t len);
void vnc_write_u32(VncState *vs, uint32_t value);
void vnc_write_s32(VncState *vs, int32_t value);
void vnc_write_u16(VncState *vs, uint16_t value);
void vnc_write_u8(VncState *vs, uint8_t value);
void vnc_flush(VncState *vs);
void vnc_read_when(VncState *vs, VncReadEvent *func, size_t expecting);
void vnc_disconnect_finish(VncState *vs);
void vnc_start_protocol(VncState *vs);


/* Buffer I/O functions */
uint32_t read_u32(uint8_t *data, size_t offset);

/* Protocol stage functions */
void vnc_client_error(VncState *vs);
size_t vnc_client_io_error(VncState *vs, ssize_t ret, Error **errp);

void start_client_init(VncState *vs);
void start_auth_vnc(VncState *vs);


/* Misc helpers */

static inline uint32_t vnc_has_feature(VncState *vs, int feature) {
    return (vs->features & (1 << feature));
}

/* Framebuffer */
void vnc_framebuffer_update(VncState *vs, int x, int y, int w, int h,
                            int32_t encoding);

/* server fb is in PIXMAN_x8r8g8b8 */
#define VNC_SERVER_FB_FORMAT PIXMAN_FORMAT(32, PIXMAN_TYPE_ARGB, 0, 8, 8, 8)
#define VNC_SERVER_FB_BITS   (PIXMAN_FORMAT_BPP(VNC_SERVER_FB_FORMAT))
#define VNC_SERVER_FB_BYTES  ((VNC_SERVER_FB_BITS+7)/8)

void *vnc_server_fb_ptr(VncDisplay *vd, int x, int y);
int vnc_server_fb_stride(VncDisplay *vd);

void vnc_convert_pixel(VncState *vs, uint8_t *buf, uint32_t v);
double vnc_update_freq(VncState *vs, int x, int y, int w, int h);
void vnc_sent_lossy_rect(VncState *vs, int x, int y, int w, int h);

/* Encodings */
int vnc_send_framebuffer_update(VncState *vs, int x, int y, int w, int h);

int vnc_raw_send_framebuffer_update(VncState *vs, int x, int y, int w, int h);

int vnc_hextile_send_framebuffer_update(VncState *vs, int x,
                                         int y, int w, int h);
void vnc_hextile_set_pixel_conversion(VncState *vs, int generic);

void *vnc_zlib_zalloc(void *x, unsigned items, unsigned size);
void vnc_zlib_zfree(void *x, void *addr);
int vnc_zlib_send_framebuffer_update(VncState *vs, int x, int y, int w, int h);
void vnc_zlib_clear(VncState *vs);

int vnc_tight_send_framebuffer_update(VncState *vs, int x, int y, int w, int h);
int vnc_tight_png_send_framebuffer_update(VncState *vs, int x, int y,
                                          int w, int h);
void vnc_tight_clear(VncState *vs);

int vnc_zrle_send_framebuffer_update(VncState *vs, int x, int y, int w, int h);
int vnc_zywrle_send_framebuffer_update(VncState *vs, int x, int y, int w, int h);
void vnc_zrle_clear(VncState *vs);

#endif /* QEMU_VNC_H */
