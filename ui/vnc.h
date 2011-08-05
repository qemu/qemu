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
#include "qemu-queue.h"
#ifdef CONFIG_VNC_THREAD
#include "qemu-thread.h"
#endif
#include "console.h"
#include "monitor.h"
#include "audio/audio.h"
#include "bitmap.h"
#include <zlib.h>
#include <stdbool.h>

#include "keymaps.h"
#include "vnc-palette.h"
#include "vnc-enc-zrle.h"

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

typedef struct Buffer
{
    size_t capacity;
    size_t offset;
    uint8_t *buffer;
} Buffer;

typedef struct VncState VncState;
typedef struct VncJob VncJob;
typedef struct VncRect VncRect;
typedef struct VncRectEntry VncRectEntry;

typedef int VncReadEvent(VncState *vs, uint8_t *data, size_t len);

typedef void VncWritePixels(VncState *vs, struct PixelFormat *pf, void *data, int size);

typedef void VncSendHextileTile(VncState *vs,
                                int x, int y, int w, int h,
                                void *last_bg,
                                void *last_fg,
                                int *has_bg, int *has_fg);

/* VNC_MAX_WIDTH must be a multiple of 16. */
#define VNC_MAX_WIDTH 2560
#define VNC_MAX_HEIGHT 2048

/* VNC_DIRTY_BITS is the number of bits in the dirty bitmap. */
#define VNC_DIRTY_BITS (VNC_MAX_WIDTH / 16)

#define VNC_STAT_RECT  64
#define VNC_STAT_COLS (VNC_MAX_WIDTH / VNC_STAT_RECT)
#define VNC_STAT_ROWS (VNC_MAX_HEIGHT / VNC_STAT_RECT)

#define VNC_AUTH_CHALLENGE_SIZE 16

typedef struct VncDisplay VncDisplay;

#ifdef CONFIG_VNC_TLS
#include "vnc-tls.h"
#include "vnc-auth-vencrypt.h"
#endif
#ifdef CONFIG_VNC_SASL
#include "vnc-auth-sasl.h"
#endif

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
    DECLARE_BITMAP(dirty[VNC_MAX_HEIGHT], VNC_MAX_WIDTH / 16);
    VncRectStat stats[VNC_STAT_ROWS][VNC_STAT_COLS];
    DisplaySurface *ds;
};

struct VncDisplay
{
    QTAILQ_HEAD(, VncState) clients;
    QEMUTimer *timer;
    int timer_interval;
    int lsock;
    DisplayState *ds;
    kbd_layout_t *kbd_layout;
    int lock_key_sync;
#ifdef CONFIG_VNC_THREAD
    QemuMutex mutex;
#endif

    QEMUCursor *cursor;
    int cursor_msize;
    uint8_t *cursor_mask;

    struct VncSurface guest;   /* guest visible surface (aka ds->surface) */
    DisplaySurface *server;  /* vnc server surface */

    char *display;
    char *password;
    time_t expires;
    int auth;
    bool lossy;
    bool non_adaptive;
#ifdef CONFIG_VNC_TLS
    int subauth; /* Used by VeNCrypt */
    VncDisplayTLS tls;
#endif
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

#ifdef CONFIG_VNC_THREAD
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
#else
struct VncJob
{
    VncState *vs;
    int rectangles;
    size_t saved_offset;
};
#endif

struct VncState
{
    int csock;

    DisplayState *ds;
    DECLARE_BITMAP(dirty[VNC_MAX_HEIGHT], VNC_DIRTY_BITS);
    uint8_t **lossy_rect; /* Not an Array to avoid costly memcpy in
                           * vnc-jobs-async.c */

    VncDisplay *vd;
    int need_update;
    int force_update;
    uint32_t features;
    int absolute;
    int last_x;
    int last_y;
    int client_width;
    int client_height;

    uint32_t vnc_encoding;

    int major;
    int minor;

    int auth;
    char challenge[VNC_AUTH_CHALLENGE_SIZE];
#ifdef CONFIG_VNC_TLS
    int subauth; /* Used by VeNCrypt */
    VncStateTLS tls;
#endif
#ifdef CONFIG_VNC_SASL
    VncStateSASL sasl;
#endif

    QObject *info;

    Buffer output;
    Buffer input;
    /* current output mode information */
    VncWritePixels *write_pixels;
    DisplaySurface clientds;

    CaptureVoiceOut *audio_cap;
    struct audsettings as;

    VncReadEvent *read_handler;
    size_t read_handler_expect;
    /* input */
    uint8_t modifiers_state[256];
    QEMUPutLEDEntry *led;

    bool abort;
#ifndef CONFIG_VNC_THREAD
    VncJob job;
#else
    QemuMutex output_mutex;
#endif

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
void vnc_client_read(void *opaque);
void vnc_client_write(void *opaque);

long vnc_client_read_buf(VncState *vs, uint8_t *data, size_t datalen);
long vnc_client_write_buf(VncState *vs, const uint8_t *data, size_t datalen);

/* Protocol I/O functions */
void vnc_write(VncState *vs, const void *data, size_t len);
void vnc_write_u32(VncState *vs, uint32_t value);
void vnc_write_s32(VncState *vs, int32_t value);
void vnc_write_u16(VncState *vs, uint16_t value);
void vnc_write_u8(VncState *vs, uint8_t value);
void vnc_flush(VncState *vs);
void vnc_read_when(VncState *vs, VncReadEvent *func, size_t expecting);


/* Buffer I/O functions */
uint8_t read_u8(uint8_t *data, size_t offset);
uint16_t read_u16(uint8_t *data, size_t offset);
int32_t read_s32(uint8_t *data, size_t offset);
uint32_t read_u32(uint8_t *data, size_t offset);

/* Protocol stage functions */
void vnc_client_error(VncState *vs);
int vnc_client_io_error(VncState *vs, int ret, int last_errno);

void start_client_init(VncState *vs);
void start_auth_vnc(VncState *vs);

/* Buffer management */
void buffer_reserve(Buffer *buffer, size_t len);
int buffer_empty(Buffer *buffer);
uint8_t *buffer_end(Buffer *buffer);
void buffer_reset(Buffer *buffer);
void buffer_free(Buffer *buffer);
void buffer_append(Buffer *buffer, const void *data, size_t len);


/* Misc helpers */

char *vnc_socket_local_addr(const char *format, int fd);
char *vnc_socket_remote_addr(const char *format, int fd);

static inline uint32_t vnc_has_feature(VncState *vs, int feature) {
    return (vs->features & (1 << feature));
}

/* Framebuffer */
void vnc_framebuffer_update(VncState *vs, int x, int y, int w, int h,
                            int32_t encoding);

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

#endif /* __QEMU_VNC_H */
