/*
 * QEMU VNC display driver
 *
 * Copyright (C) 2006 Anthony Liguori <anthony@codemonkey.ws>
 * Copyright (C) 2006 Fabrice Bellard
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

#include "qemu-common.h"
#include "console.h"
#include "sysemu.h"
#include "qemu_socket.h"
#include "qemu-timer.h"

#define VNC_REFRESH_INTERVAL (1000 / 30)

#include "vnc_keysym.h"
#include "keymaps.c"
#include "d3des.h"

#if CONFIG_VNC_TLS
#include <gnutls/gnutls.h>
#include <gnutls/x509.h>
#endif /* CONFIG_VNC_TLS */

// #define _VNC_DEBUG 1

#if _VNC_DEBUG
#define VNC_DEBUG(fmt, ...) do { fprintf(stderr, fmt, ## __VA_ARGS__); } while (0)

#if CONFIG_VNC_TLS && _VNC_DEBUG >= 2
/* Very verbose, so only enabled for _VNC_DEBUG >= 2 */
static void vnc_debug_gnutls_log(int level, const char* str) {
    VNC_DEBUG("%d %s", level, str);
}
#endif /* CONFIG_VNC_TLS && _VNC_DEBUG */
#else
#define VNC_DEBUG(fmt, ...) do { } while (0)
#endif


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
                                uint32_t *last_bg,
                                uint32_t *last_fg,
                                int *has_bg, int *has_fg);

#define VNC_MAX_WIDTH 2048
#define VNC_MAX_HEIGHT 2048
#define VNC_DIRTY_WORDS (VNC_MAX_WIDTH / (16 * 32))

#define VNC_AUTH_CHALLENGE_SIZE 16

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

#if CONFIG_VNC_TLS
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

#if CONFIG_VNC_TLS
#define X509_CA_CERT_FILE "ca-cert.pem"
#define X509_CA_CRL_FILE "ca-crl.pem"
#define X509_SERVER_KEY_FILE "server-key.pem"
#define X509_SERVER_CERT_FILE "server-cert.pem"
#endif

#endif /* CONFIG_VNC_TLS */

struct VncState
{
    QEMUTimer *timer;
    int lsock;
    int csock;
    DisplayState *ds;
    int need_update;
    int width;
    int height;
    uint32_t dirty_row[VNC_MAX_HEIGHT][VNC_DIRTY_WORDS];
    char *old_data;
    int depth; /* internal VNC frame buffer byte per pixel */
    int has_resize;
    int has_hextile;
    int has_pointer_type_change;
    int absolute;
    int last_x;
    int last_y;

    int major;
    int minor;

    char *display;
    char *password;
    int auth;
#if CONFIG_VNC_TLS
    int subauth;
    int x509verify;

    char *x509cacert;
    char *x509cacrl;
    char *x509cert;
    char *x509key;
#endif
    char challenge[VNC_AUTH_CHALLENGE_SIZE];

#if CONFIG_VNC_TLS
    int wiremode;
    gnutls_session_t tls_session;
#endif

    Buffer output;
    Buffer input;
    kbd_layout_t *kbd_layout;
    /* current output mode information */
    VncWritePixels *write_pixels;
    VncSendHextileTile *send_hextile_tile;
    int pix_bpp, pix_big_endian;
    int red_shift, red_max, red_shift1;
    int green_shift, green_max, green_shift1;
    int blue_shift, blue_max, blue_shift1;

    VncReadEvent *read_handler;
    size_t read_handler_expect;
    /* input */
    uint8_t modifiers_state[256];
};

static VncState *vnc_state; /* needed for info vnc */

void do_info_vnc(void)
{
    if (vnc_state == NULL)
	term_printf("VNC server disabled\n");
    else {
	term_printf("VNC server active on: ");
	term_print_filename(vnc_state->display);
	term_printf("\n");

	if (vnc_state->csock == -1)
	    term_printf("No client connected\n");
	else
	    term_printf("Client connected\n");
    }
}

/* TODO
   1) Get the queue working for IO.
   2) there is some weirdness when using the -S option (the screen is grey
      and not totally invalidated
   3) resolutions > 1024
*/

static void vnc_write(VncState *vs, const void *data, size_t len);
static void vnc_write_u32(VncState *vs, uint32_t value);
static void vnc_write_s32(VncState *vs, int32_t value);
static void vnc_write_u16(VncState *vs, uint16_t value);
static void vnc_write_u8(VncState *vs, uint8_t value);
static void vnc_flush(VncState *vs);
static void vnc_update_client(void *opaque);
static void vnc_client_read(void *opaque);

static inline void vnc_set_bit(uint32_t *d, int k)
{
    d[k >> 5] |= 1 << (k & 0x1f);
}

static inline void vnc_clear_bit(uint32_t *d, int k)
{
    d[k >> 5] &= ~(1 << (k & 0x1f));
}

static inline void vnc_set_bits(uint32_t *d, int n, int nb_words)
{
    int j;

    j = 0;
    while (n >= 32) {
        d[j++] = -1;
        n -= 32;
    }
    if (n > 0)
        d[j++] = (1 << n) - 1;
    while (j < nb_words)
        d[j++] = 0;
}

static inline int vnc_get_bit(const uint32_t *d, int k)
{
    return (d[k >> 5] >> (k & 0x1f)) & 1;
}

static inline int vnc_and_bits(const uint32_t *d1, const uint32_t *d2,
                               int nb_words)
{
    int i;
    for(i = 0; i < nb_words; i++) {
        if ((d1[i] & d2[i]) != 0)
            return 1;
    }
    return 0;
}

static void vnc_dpy_update(DisplayState *ds, int x, int y, int w, int h)
{
    VncState *vs = ds->opaque;
    int i;

    h += y;

    /* round x down to ensure the loop only spans one 16-pixel block per,
       iteration.  otherwise, if (x % 16) != 0, the last iteration may span
       two 16-pixel blocks but we only mark the first as dirty
    */
    w += (x % 16);
    x -= (x % 16);

    x = MIN(x, vs->width);
    y = MIN(y, vs->height);
    w = MIN(x + w, vs->width) - x;
    h = MIN(h, vs->height);

    for (; y < h; y++)
	for (i = 0; i < w; i += 16)
	    vnc_set_bit(vs->dirty_row[y], (x + i) / 16);
}

static void vnc_framebuffer_update(VncState *vs, int x, int y, int w, int h,
				   int32_t encoding)
{
    vnc_write_u16(vs, x);
    vnc_write_u16(vs, y);
    vnc_write_u16(vs, w);
    vnc_write_u16(vs, h);

    vnc_write_s32(vs, encoding);
}

static void vnc_dpy_resize(DisplayState *ds, int w, int h)
{
    int size_changed;
    VncState *vs = ds->opaque;

    ds->data = qemu_realloc(ds->data, w * h * vs->depth);
    vs->old_data = qemu_realloc(vs->old_data, w * h * vs->depth);

    if (ds->data == NULL || vs->old_data == NULL) {
	fprintf(stderr, "vnc: memory allocation failed\n");
	exit(1);
    }

    if (ds->depth != vs->depth * 8) {
        ds->depth = vs->depth * 8;
        console_color_init(ds);
    }
    size_changed = ds->width != w || ds->height != h;
    ds->width = w;
    ds->height = h;
    ds->linesize = w * vs->depth;
    if (size_changed) {
        vs->width = ds->width;
        vs->height = ds->height;
        if (vs->csock != -1 && vs->has_resize) {
            vnc_write_u8(vs, 0);  /* msg id */
            vnc_write_u8(vs, 0);
            vnc_write_u16(vs, 1); /* number of rects */
            vnc_framebuffer_update(vs, 0, 0, ds->width, ds->height, -223);
            vnc_flush(vs);
        }
    }

    memset(vs->dirty_row, 0xFF, sizeof(vs->dirty_row));
    memset(vs->old_data, 42, vs->ds->linesize * vs->ds->height);
}

/* fastest code */
static void vnc_write_pixels_copy(VncState *vs, void *pixels, int size)
{
    vnc_write(vs, pixels, size);
}

/* slowest but generic code. */
static void vnc_convert_pixel(VncState *vs, uint8_t *buf, uint32_t v)
{
    unsigned int r, g, b;

    r = (v >> vs->red_shift1) & vs->red_max;
    g = (v >> vs->green_shift1) & vs->green_max;
    b = (v >> vs->blue_shift1) & vs->blue_max;
    v = (r << vs->red_shift) |
        (g << vs->green_shift) |
        (b << vs->blue_shift);
    switch(vs->pix_bpp) {
    case 1:
        buf[0] = v;
        break;
    case 2:
        if (vs->pix_big_endian) {
            buf[0] = v >> 8;
            buf[1] = v;
        } else {
            buf[1] = v >> 8;
            buf[0] = v;
        }
        break;
    default:
    case 4:
        if (vs->pix_big_endian) {
            buf[0] = v >> 24;
            buf[1] = v >> 16;
            buf[2] = v >> 8;
            buf[3] = v;
        } else {
            buf[3] = v >> 24;
            buf[2] = v >> 16;
            buf[1] = v >> 8;
            buf[0] = v;
        }
        break;
    }
}

static void vnc_write_pixels_generic(VncState *vs, void *pixels1, int size)
{
    uint32_t *pixels = pixels1;
    uint8_t buf[4];
    int n, i;

    n = size >> 2;
    for(i = 0; i < n; i++) {
        vnc_convert_pixel(vs, buf, pixels[i]);
        vnc_write(vs, buf, vs->pix_bpp);
    }
}

static void send_framebuffer_update_raw(VncState *vs, int x, int y, int w, int h)
{
    int i;
    uint8_t *row;

    vnc_framebuffer_update(vs, x, y, w, h, 0);

    row = vs->ds->data + y * vs->ds->linesize + x * vs->depth;
    for (i = 0; i < h; i++) {
	vs->write_pixels(vs, row, w * vs->depth);
	row += vs->ds->linesize;
    }
}

static void hextile_enc_cord(uint8_t *ptr, int x, int y, int w, int h)
{
    ptr[0] = ((x & 0x0F) << 4) | (y & 0x0F);
    ptr[1] = (((w - 1) & 0x0F) << 4) | ((h - 1) & 0x0F);
}

#define BPP 8
#include "vnchextile.h"
#undef BPP

#define BPP 16
#include "vnchextile.h"
#undef BPP

#define BPP 32
#include "vnchextile.h"
#undef BPP

#define GENERIC
#define BPP 32
#include "vnchextile.h"
#undef BPP
#undef GENERIC

static void send_framebuffer_update_hextile(VncState *vs, int x, int y, int w, int h)
{
    int i, j;
    int has_fg, has_bg;
    uint32_t last_fg32, last_bg32;

    vnc_framebuffer_update(vs, x, y, w, h, 5);

    has_fg = has_bg = 0;
    for (j = y; j < (y + h); j += 16) {
	for (i = x; i < (x + w); i += 16) {
            vs->send_hextile_tile(vs, i, j,
                                  MIN(16, x + w - i), MIN(16, y + h - j),
                                  &last_bg32, &last_fg32, &has_bg, &has_fg);
	}
    }
}

static void send_framebuffer_update(VncState *vs, int x, int y, int w, int h)
{
	if (vs->has_hextile)
	    send_framebuffer_update_hextile(vs, x, y, w, h);
	else
	    send_framebuffer_update_raw(vs, x, y, w, h);
}

static void vnc_copy(DisplayState *ds, int src_x, int src_y, int dst_x, int dst_y, int w, int h)
{
    int src, dst;
    uint8_t *src_row;
    uint8_t *dst_row;
    char *old_row;
    int y = 0;
    int pitch = ds->linesize;
    VncState *vs = ds->opaque;

    vnc_update_client(vs);

    if (dst_y > src_y) {
	y = h - 1;
	pitch = -pitch;
    }

    src = (ds->linesize * (src_y + y) + vs->depth * src_x);
    dst = (ds->linesize * (dst_y + y) + vs->depth * dst_x);

    src_row = ds->data + src;
    dst_row = ds->data + dst;
    old_row = vs->old_data + dst;

    for (y = 0; y < h; y++) {
	memmove(old_row, src_row, w * vs->depth);
	memmove(dst_row, src_row, w * vs->depth);
	src_row += pitch;
	dst_row += pitch;
	old_row += pitch;
    }

    vnc_write_u8(vs, 0);  /* msg id */
    vnc_write_u8(vs, 0);
    vnc_write_u16(vs, 1); /* number of rects */
    vnc_framebuffer_update(vs, dst_x, dst_y, w, h, 1);
    vnc_write_u16(vs, src_x);
    vnc_write_u16(vs, src_y);
    vnc_flush(vs);
}

static int find_dirty_height(VncState *vs, int y, int last_x, int x)
{
    int h;

    for (h = 1; h < (vs->height - y); h++) {
	int tmp_x;
	if (!vnc_get_bit(vs->dirty_row[y + h], last_x))
	    break;
	for (tmp_x = last_x; tmp_x < x; tmp_x++)
	    vnc_clear_bit(vs->dirty_row[y + h], tmp_x);
    }

    return h;
}

static void vnc_update_client(void *opaque)
{
    VncState *vs = opaque;

    if (vs->need_update && vs->csock != -1) {
	int y;
	uint8_t *row;
	char *old_row;
	uint32_t width_mask[VNC_DIRTY_WORDS];
	int n_rectangles;
	int saved_offset;
	int has_dirty = 0;

        vga_hw_update();

        vnc_set_bits(width_mask, (vs->width / 16), VNC_DIRTY_WORDS);

	/* Walk through the dirty map and eliminate tiles that
	   really aren't dirty */
	row = vs->ds->data;
	old_row = vs->old_data;

	for (y = 0; y < vs->height; y++) {
	    if (vnc_and_bits(vs->dirty_row[y], width_mask, VNC_DIRTY_WORDS)) {
		int x;
		uint8_t *ptr;
		char *old_ptr;

		ptr = row;
		old_ptr = (char*)old_row;

		for (x = 0; x < vs->ds->width; x += 16) {
		    if (memcmp(old_ptr, ptr, 16 * vs->depth) == 0) {
			vnc_clear_bit(vs->dirty_row[y], (x / 16));
		    } else {
			has_dirty = 1;
			memcpy(old_ptr, ptr, 16 * vs->depth);
		    }

		    ptr += 16 * vs->depth;
		    old_ptr += 16 * vs->depth;
		}
	    }

	    row += vs->ds->linesize;
	    old_row += vs->ds->linesize;
	}

	if (!has_dirty) {
	    qemu_mod_timer(vs->timer, qemu_get_clock(rt_clock) + VNC_REFRESH_INTERVAL);
	    return;
	}

	/* Count rectangles */
	n_rectangles = 0;
	vnc_write_u8(vs, 0);  /* msg id */
	vnc_write_u8(vs, 0);
	saved_offset = vs->output.offset;
	vnc_write_u16(vs, 0);

	for (y = 0; y < vs->height; y++) {
	    int x;
	    int last_x = -1;
	    for (x = 0; x < vs->width / 16; x++) {
		if (vnc_get_bit(vs->dirty_row[y], x)) {
		    if (last_x == -1) {
			last_x = x;
		    }
		    vnc_clear_bit(vs->dirty_row[y], x);
		} else {
		    if (last_x != -1) {
			int h = find_dirty_height(vs, y, last_x, x);
			send_framebuffer_update(vs, last_x * 16, y, (x - last_x) * 16, h);
			n_rectangles++;
		    }
		    last_x = -1;
		}
	    }
	    if (last_x != -1) {
		int h = find_dirty_height(vs, y, last_x, x);
		send_framebuffer_update(vs, last_x * 16, y, (x - last_x) * 16, h);
		n_rectangles++;
	    }
	}
	vs->output.buffer[saved_offset] = (n_rectangles >> 8) & 0xFF;
	vs->output.buffer[saved_offset + 1] = n_rectangles & 0xFF;
	vnc_flush(vs);

    }

    if (vs->csock != -1) {
        qemu_mod_timer(vs->timer, qemu_get_clock(rt_clock) + VNC_REFRESH_INTERVAL);
    }

}

static int vnc_listen_poll(void *opaque)
{
    VncState *vs = opaque;
    if (vs->csock == -1)
	return 1;
    return 0;
}

static void buffer_reserve(Buffer *buffer, size_t len)
{
    if ((buffer->capacity - buffer->offset) < len) {
	buffer->capacity += (len + 1024);
	buffer->buffer = qemu_realloc(buffer->buffer, buffer->capacity);
	if (buffer->buffer == NULL) {
	    fprintf(stderr, "vnc: out of memory\n");
	    exit(1);
	}
    }
}

static int buffer_empty(Buffer *buffer)
{
    return buffer->offset == 0;
}

static uint8_t *buffer_end(Buffer *buffer)
{
    return buffer->buffer + buffer->offset;
}

static void buffer_reset(Buffer *buffer)
{
	buffer->offset = 0;
}

static void buffer_append(Buffer *buffer, const void *data, size_t len)
{
    memcpy(buffer->buffer + buffer->offset, data, len);
    buffer->offset += len;
}

static int vnc_client_io_error(VncState *vs, int ret, int last_errno)
{
    if (ret == 0 || ret == -1) {
        if (ret == -1) {
            switch (last_errno) {
                case EINTR:
                case EAGAIN:
#ifdef _WIN32
                case WSAEWOULDBLOCK:
#endif
                    return 0;
                default:
                    break;
            }
        }

	VNC_DEBUG("Closing down client sock %d %d\n", ret, ret < 0 ? last_errno : 0);
	qemu_set_fd_handler2(vs->csock, NULL, NULL, NULL, NULL);
	closesocket(vs->csock);
	vs->csock = -1;
	vs->ds->idle = 1;
	buffer_reset(&vs->input);
	buffer_reset(&vs->output);
	vs->need_update = 0;
#if CONFIG_VNC_TLS
	if (vs->tls_session) {
	    gnutls_deinit(vs->tls_session);
	    vs->tls_session = NULL;
	}
	vs->wiremode = VNC_WIREMODE_CLEAR;
#endif /* CONFIG_VNC_TLS */
	return 0;
    }
    return ret;
}

static void vnc_client_error(VncState *vs)
{
    vnc_client_io_error(vs, -1, EINVAL);
}

static void vnc_client_write(void *opaque)
{
    long ret;
    VncState *vs = opaque;

#if CONFIG_VNC_TLS
    if (vs->tls_session) {
	ret = gnutls_write(vs->tls_session, vs->output.buffer, vs->output.offset);
	if (ret < 0) {
	    if (ret == GNUTLS_E_AGAIN)
		errno = EAGAIN;
	    else
		errno = EIO;
	    ret = -1;
	}
    } else
#endif /* CONFIG_VNC_TLS */
	ret = send(vs->csock, vs->output.buffer, vs->output.offset, 0);
    ret = vnc_client_io_error(vs, ret, socket_error());
    if (!ret)
	return;

    memmove(vs->output.buffer, vs->output.buffer + ret, (vs->output.offset - ret));
    vs->output.offset -= ret;

    if (vs->output.offset == 0) {
	qemu_set_fd_handler2(vs->csock, NULL, vnc_client_read, NULL, vs);
    }
}

static void vnc_read_when(VncState *vs, VncReadEvent *func, size_t expecting)
{
    vs->read_handler = func;
    vs->read_handler_expect = expecting;
}

static void vnc_client_read(void *opaque)
{
    VncState *vs = opaque;
    long ret;

    buffer_reserve(&vs->input, 4096);

#if CONFIG_VNC_TLS
    if (vs->tls_session) {
	ret = gnutls_read(vs->tls_session, buffer_end(&vs->input), 4096);
	if (ret < 0) {
	    if (ret == GNUTLS_E_AGAIN)
		errno = EAGAIN;
	    else
		errno = EIO;
	    ret = -1;
	}
    } else
#endif /* CONFIG_VNC_TLS */
	ret = recv(vs->csock, buffer_end(&vs->input), 4096, 0);
    ret = vnc_client_io_error(vs, ret, socket_error());
    if (!ret)
	return;

    vs->input.offset += ret;

    while (vs->read_handler && vs->input.offset >= vs->read_handler_expect) {
	size_t len = vs->read_handler_expect;
	int ret;

	ret = vs->read_handler(vs, vs->input.buffer, len);
	if (vs->csock == -1)
	    return;

	if (!ret) {
	    memmove(vs->input.buffer, vs->input.buffer + len, (vs->input.offset - len));
	    vs->input.offset -= len;
	} else {
	    vs->read_handler_expect = ret;
	}
    }
}

static void vnc_write(VncState *vs, const void *data, size_t len)
{
    buffer_reserve(&vs->output, len);

    if (buffer_empty(&vs->output)) {
	qemu_set_fd_handler2(vs->csock, NULL, vnc_client_read, vnc_client_write, vs);
    }

    buffer_append(&vs->output, data, len);
}

static void vnc_write_s32(VncState *vs, int32_t value)
{
    vnc_write_u32(vs, *(uint32_t *)&value);
}

static void vnc_write_u32(VncState *vs, uint32_t value)
{
    uint8_t buf[4];

    buf[0] = (value >> 24) & 0xFF;
    buf[1] = (value >> 16) & 0xFF;
    buf[2] = (value >>  8) & 0xFF;
    buf[3] = value & 0xFF;

    vnc_write(vs, buf, 4);
}

static void vnc_write_u16(VncState *vs, uint16_t value)
{
    uint8_t buf[2];

    buf[0] = (value >> 8) & 0xFF;
    buf[1] = value & 0xFF;

    vnc_write(vs, buf, 2);
}

static void vnc_write_u8(VncState *vs, uint8_t value)
{
    vnc_write(vs, (char *)&value, 1);
}

static void vnc_flush(VncState *vs)
{
    if (vs->output.offset)
	vnc_client_write(vs);
}

static uint8_t read_u8(uint8_t *data, size_t offset)
{
    return data[offset];
}

static uint16_t read_u16(uint8_t *data, size_t offset)
{
    return ((data[offset] & 0xFF) << 8) | (data[offset + 1] & 0xFF);
}

static int32_t read_s32(uint8_t *data, size_t offset)
{
    return (int32_t)((data[offset] << 24) | (data[offset + 1] << 16) |
		     (data[offset + 2] << 8) | data[offset + 3]);
}

static uint32_t read_u32(uint8_t *data, size_t offset)
{
    return ((data[offset] << 24) | (data[offset + 1] << 16) |
	    (data[offset + 2] << 8) | data[offset + 3]);
}

#if CONFIG_VNC_TLS
static ssize_t vnc_tls_push(gnutls_transport_ptr_t transport,
                            const void *data,
                            size_t len) {
    struct VncState *vs = (struct VncState *)transport;
    int ret;

 retry:
    ret = send(vs->csock, data, len, 0);
    if (ret < 0) {
	if (errno == EINTR)
	    goto retry;
	return -1;
    }
    return ret;
}


static ssize_t vnc_tls_pull(gnutls_transport_ptr_t transport,
                            void *data,
                            size_t len) {
    struct VncState *vs = (struct VncState *)transport;
    int ret;

 retry:
    ret = recv(vs->csock, data, len, 0);
    if (ret < 0) {
	if (errno == EINTR)
	    goto retry;
	return -1;
    }
    return ret;
}
#endif /* CONFIG_VNC_TLS */

static void client_cut_text(VncState *vs, size_t len, uint8_t *text)
{
}

static void check_pointer_type_change(VncState *vs, int absolute)
{
    if (vs->has_pointer_type_change && vs->absolute != absolute) {
	vnc_write_u8(vs, 0);
	vnc_write_u8(vs, 0);
	vnc_write_u16(vs, 1);
	vnc_framebuffer_update(vs, absolute, 0,
			       vs->ds->width, vs->ds->height, -257);
	vnc_flush(vs);
    }
    vs->absolute = absolute;
}

static void pointer_event(VncState *vs, int button_mask, int x, int y)
{
    int buttons = 0;
    int dz = 0;

    if (button_mask & 0x01)
	buttons |= MOUSE_EVENT_LBUTTON;
    if (button_mask & 0x02)
	buttons |= MOUSE_EVENT_MBUTTON;
    if (button_mask & 0x04)
	buttons |= MOUSE_EVENT_RBUTTON;
    if (button_mask & 0x08)
	dz = -1;
    if (button_mask & 0x10)
	dz = 1;

    if (vs->absolute) {
	kbd_mouse_event(x * 0x7FFF / (vs->ds->width - 1),
			y * 0x7FFF / (vs->ds->height - 1),
			dz, buttons);
    } else if (vs->has_pointer_type_change) {
	x -= 0x7FFF;
	y -= 0x7FFF;

	kbd_mouse_event(x, y, dz, buttons);
    } else {
	if (vs->last_x != -1)
	    kbd_mouse_event(x - vs->last_x,
			    y - vs->last_y,
			    dz, buttons);
	vs->last_x = x;
	vs->last_y = y;
    }

    check_pointer_type_change(vs, kbd_mouse_is_absolute());
}

static void reset_keys(VncState *vs)
{
    int i;
    for(i = 0; i < 256; i++) {
        if (vs->modifiers_state[i]) {
            if (i & 0x80)
                kbd_put_keycode(0xe0);
            kbd_put_keycode(i | 0x80);
            vs->modifiers_state[i] = 0;
        }
    }
}

static void press_key(VncState *vs, int keysym)
{
    kbd_put_keycode(keysym2scancode(vs->kbd_layout, keysym) & 0x7f);
    kbd_put_keycode(keysym2scancode(vs->kbd_layout, keysym) | 0x80);
}

static void do_key_event(VncState *vs, int down, uint32_t sym)
{
    int keycode;

    keycode = keysym2scancode(vs->kbd_layout, sym & 0xFFFF);

    /* QEMU console switch */
    switch(keycode) {
    case 0x2a:                          /* Left Shift */
    case 0x36:                          /* Right Shift */
    case 0x1d:                          /* Left CTRL */
    case 0x9d:                          /* Right CTRL */
    case 0x38:                          /* Left ALT */
    case 0xb8:                          /* Right ALT */
        if (down)
            vs->modifiers_state[keycode] = 1;
        else
            vs->modifiers_state[keycode] = 0;
        break;
    case 0x02 ... 0x0a: /* '1' to '9' keys */
        if (down && vs->modifiers_state[0x1d] && vs->modifiers_state[0x38]) {
            /* Reset the modifiers sent to the current console */
            reset_keys(vs);
            console_select(keycode - 0x02);
            return;
        }
        break;
    case 0x3a:			/* CapsLock */
    case 0x45:			/* NumLock */
        if (!down)
            vs->modifiers_state[keycode] ^= 1;
        break;
    }

    if (keycode_is_keypad(vs->kbd_layout, keycode)) {
        /* If the numlock state needs to change then simulate an additional
           keypress before sending this one.  This will happen if the user
           toggles numlock away from the VNC window.
        */
        if (keysym_is_numlock(vs->kbd_layout, sym & 0xFFFF)) {
            if (!vs->modifiers_state[0x45]) {
                vs->modifiers_state[0x45] = 1;
                press_key(vs, 0xff7f);
            }
        } else {
            if (vs->modifiers_state[0x45]) {
                vs->modifiers_state[0x45] = 0;
                press_key(vs, 0xff7f);
            }
        }
    }

    if (is_graphic_console()) {
        if (keycode & 0x80)
            kbd_put_keycode(0xe0);
        if (down)
            kbd_put_keycode(keycode & 0x7f);
        else
            kbd_put_keycode(keycode | 0x80);
    } else {
        /* QEMU console emulation */
        if (down) {
            switch (keycode) {
            case 0x2a:                          /* Left Shift */
            case 0x36:                          /* Right Shift */
            case 0x1d:                          /* Left CTRL */
            case 0x9d:                          /* Right CTRL */
            case 0x38:                          /* Left ALT */
            case 0xb8:                          /* Right ALT */
                break;
            case 0xc8:
                kbd_put_keysym(QEMU_KEY_UP);
                break;
            case 0xd0:
                kbd_put_keysym(QEMU_KEY_DOWN);
                break;
            case 0xcb:
                kbd_put_keysym(QEMU_KEY_LEFT);
                break;
            case 0xcd:
                kbd_put_keysym(QEMU_KEY_RIGHT);
                break;
            case 0xd3:
                kbd_put_keysym(QEMU_KEY_DELETE);
                break;
            case 0xc7:
                kbd_put_keysym(QEMU_KEY_HOME);
                break;
            case 0xcf:
                kbd_put_keysym(QEMU_KEY_END);
                break;
            case 0xc9:
                kbd_put_keysym(QEMU_KEY_PAGEUP);
                break;
            case 0xd1:
                kbd_put_keysym(QEMU_KEY_PAGEDOWN);
                break;
            default:
                kbd_put_keysym(sym);
                break;
            }
        }
    }
}

static void key_event(VncState *vs, int down, uint32_t sym)
{
    if (sym >= 'A' && sym <= 'Z' && is_graphic_console())
	sym = sym - 'A' + 'a';
    do_key_event(vs, down, sym);
}

static void framebuffer_update_request(VncState *vs, int incremental,
				       int x_position, int y_position,
				       int w, int h)
{
    if (x_position > vs->ds->width)
        x_position = vs->ds->width;
    if (y_position > vs->ds->height)
        y_position = vs->ds->height;
    if (x_position + w >= vs->ds->width)
        w = vs->ds->width  - x_position;
    if (y_position + h >= vs->ds->height)
        h = vs->ds->height - y_position;

    int i;
    vs->need_update = 1;
    if (!incremental) {
	char *old_row = vs->old_data + y_position * vs->ds->linesize;

	for (i = 0; i < h; i++) {
            vnc_set_bits(vs->dirty_row[y_position + i],
                         (vs->ds->width / 16), VNC_DIRTY_WORDS);
	    memset(old_row, 42, vs->ds->width * vs->depth);
	    old_row += vs->ds->linesize;
	}
    }
}

static void set_encodings(VncState *vs, int32_t *encodings, size_t n_encodings)
{
    int i;

    vs->has_hextile = 0;
    vs->has_resize = 0;
    vs->has_pointer_type_change = 0;
    vs->absolute = -1;
    vs->ds->dpy_copy = NULL;

    for (i = n_encodings - 1; i >= 0; i--) {
	switch (encodings[i]) {
	case 0: /* Raw */
	    vs->has_hextile = 0;
	    break;
	case 1: /* CopyRect */
	    vs->ds->dpy_copy = vnc_copy;
	    break;
	case 5: /* Hextile */
	    vs->has_hextile = 1;
	    break;
	case -223: /* DesktopResize */
	    vs->has_resize = 1;
	    break;
	case -257:
	    vs->has_pointer_type_change = 1;
	    break;
	default:
	    break;
	}
    }

    check_pointer_type_change(vs, kbd_mouse_is_absolute());
}

static int compute_nbits(unsigned int val)
{
    int n;
    n = 0;
    while (val != 0) {
        n++;
        val >>= 1;
    }
    return n;
}

static void set_pixel_format(VncState *vs,
			     int bits_per_pixel, int depth,
			     int big_endian_flag, int true_color_flag,
			     int red_max, int green_max, int blue_max,
			     int red_shift, int green_shift, int blue_shift)
{
    int host_big_endian_flag;

#ifdef WORDS_BIGENDIAN
    host_big_endian_flag = 1;
#else
    host_big_endian_flag = 0;
#endif
    if (!true_color_flag) {
    fail:
	vnc_client_error(vs);
        return;
    }
    if (bits_per_pixel == 32 &&
        host_big_endian_flag == big_endian_flag &&
        red_max == 0xff && green_max == 0xff && blue_max == 0xff &&
        red_shift == 16 && green_shift == 8 && blue_shift == 0) {
        vs->depth = 4;
        vs->write_pixels = vnc_write_pixels_copy;
        vs->send_hextile_tile = send_hextile_tile_32;
    } else
    if (bits_per_pixel == 16 &&
        host_big_endian_flag == big_endian_flag &&
        red_max == 31 && green_max == 63 && blue_max == 31 &&
        red_shift == 11 && green_shift == 5 && blue_shift == 0) {
        vs->depth = 2;
        vs->write_pixels = vnc_write_pixels_copy;
        vs->send_hextile_tile = send_hextile_tile_16;
    } else
    if (bits_per_pixel == 8 &&
        red_max == 7 && green_max == 7 && blue_max == 3 &&
        red_shift == 5 && green_shift == 2 && blue_shift == 0) {
        vs->depth = 1;
        vs->write_pixels = vnc_write_pixels_copy;
        vs->send_hextile_tile = send_hextile_tile_8;
    } else
    {
        /* generic and slower case */
        if (bits_per_pixel != 8 &&
            bits_per_pixel != 16 &&
            bits_per_pixel != 32)
            goto fail;
        vs->depth = 4;
        vs->red_shift = red_shift;
        vs->red_max = red_max;
        vs->red_shift1 = 24 - compute_nbits(red_max);
        vs->green_shift = green_shift;
        vs->green_max = green_max;
        vs->green_shift1 = 16 - compute_nbits(green_max);
        vs->blue_shift = blue_shift;
        vs->blue_max = blue_max;
        vs->blue_shift1 = 8 - compute_nbits(blue_max);
        vs->pix_bpp = bits_per_pixel / 8;
        vs->pix_big_endian = big_endian_flag;
        vs->write_pixels = vnc_write_pixels_generic;
        vs->send_hextile_tile = send_hextile_tile_generic;
    }

    vnc_dpy_resize(vs->ds, vs->ds->width, vs->ds->height);

    vga_hw_invalidate();
    vga_hw_update();
}

static int protocol_client_msg(VncState *vs, uint8_t *data, size_t len)
{
    int i;
    uint16_t limit;

    switch (data[0]) {
    case 0:
	if (len == 1)
	    return 20;

	set_pixel_format(vs, read_u8(data, 4), read_u8(data, 5),
			 read_u8(data, 6), read_u8(data, 7),
			 read_u16(data, 8), read_u16(data, 10),
			 read_u16(data, 12), read_u8(data, 14),
			 read_u8(data, 15), read_u8(data, 16));
	break;
    case 2:
	if (len == 1)
	    return 4;

	if (len == 4)
	    return 4 + (read_u16(data, 2) * 4);

	limit = read_u16(data, 2);
	for (i = 0; i < limit; i++) {
	    int32_t val = read_s32(data, 4 + (i * 4));
	    memcpy(data + 4 + (i * 4), &val, sizeof(val));
	}

	set_encodings(vs, (int32_t *)(data + 4), limit);
	break;
    case 3:
	if (len == 1)
	    return 10;

	framebuffer_update_request(vs,
				   read_u8(data, 1), read_u16(data, 2), read_u16(data, 4),
				   read_u16(data, 6), read_u16(data, 8));
	break;
    case 4:
	if (len == 1)
	    return 8;

	key_event(vs, read_u8(data, 1), read_u32(data, 4));
	break;
    case 5:
	if (len == 1)
	    return 6;

	pointer_event(vs, read_u8(data, 1), read_u16(data, 2), read_u16(data, 4));
	break;
    case 6:
	if (len == 1)
	    return 8;

	if (len == 8) {
            uint32_t dlen = read_u32(data, 4);
            if (dlen > 0)
                return 8 + dlen;
        }

	client_cut_text(vs, read_u32(data, 4), data + 8);
	break;
    default:
	printf("Msg: %d\n", data[0]);
	vnc_client_error(vs);
	break;
    }

    vnc_read_when(vs, protocol_client_msg, 1);
    return 0;
}

static int protocol_client_init(VncState *vs, uint8_t *data, size_t len)
{
    char pad[3] = { 0, 0, 0 };
    char buf[1024];
    int size;

    vs->width = vs->ds->width;
    vs->height = vs->ds->height;
    vnc_write_u16(vs, vs->ds->width);
    vnc_write_u16(vs, vs->ds->height);

    vnc_write_u8(vs, vs->depth * 8); /* bits-per-pixel */
    vnc_write_u8(vs, vs->depth * 8); /* depth */
#ifdef WORDS_BIGENDIAN
    vnc_write_u8(vs, 1);             /* big-endian-flag */
#else
    vnc_write_u8(vs, 0);             /* big-endian-flag */
#endif
    vnc_write_u8(vs, 1);             /* true-color-flag */
    if (vs->depth == 4) {
	vnc_write_u16(vs, 0xFF);     /* red-max */
	vnc_write_u16(vs, 0xFF);     /* green-max */
	vnc_write_u16(vs, 0xFF);     /* blue-max */
	vnc_write_u8(vs, 16);        /* red-shift */
	vnc_write_u8(vs, 8);         /* green-shift */
	vnc_write_u8(vs, 0);         /* blue-shift */
        vs->send_hextile_tile = send_hextile_tile_32;
    } else if (vs->depth == 2) {
	vnc_write_u16(vs, 31);       /* red-max */
	vnc_write_u16(vs, 63);       /* green-max */
	vnc_write_u16(vs, 31);       /* blue-max */
	vnc_write_u8(vs, 11);        /* red-shift */
	vnc_write_u8(vs, 5);         /* green-shift */
	vnc_write_u8(vs, 0);         /* blue-shift */
        vs->send_hextile_tile = send_hextile_tile_16;
    } else if (vs->depth == 1) {
        /* XXX: change QEMU pixel 8 bit pixel format to match the VNC one ? */
	vnc_write_u16(vs, 7);        /* red-max */
	vnc_write_u16(vs, 7);        /* green-max */
	vnc_write_u16(vs, 3);        /* blue-max */
	vnc_write_u8(vs, 5);         /* red-shift */
	vnc_write_u8(vs, 2);         /* green-shift */
	vnc_write_u8(vs, 0);         /* blue-shift */
        vs->send_hextile_tile = send_hextile_tile_8;
    }
    vs->write_pixels = vnc_write_pixels_copy;

    vnc_write(vs, pad, 3);           /* padding */

    if (qemu_name)
        size = snprintf(buf, sizeof(buf), "QEMU (%s)", qemu_name);
    else
        size = snprintf(buf, sizeof(buf), "QEMU");

    vnc_write_u32(vs, size);
    vnc_write(vs, buf, size);
    vnc_flush(vs);

    vnc_read_when(vs, protocol_client_msg, 1);

    return 0;
}

static void make_challenge(VncState *vs)
{
    int i;

    srand(time(NULL)+getpid()+getpid()*987654+rand());

    for (i = 0 ; i < sizeof(vs->challenge) ; i++)
        vs->challenge[i] = (int) (256.0*rand()/(RAND_MAX+1.0));
}

static int protocol_client_auth_vnc(VncState *vs, uint8_t *data, size_t len)
{
    unsigned char response[VNC_AUTH_CHALLENGE_SIZE];
    int i, j, pwlen;
    unsigned char key[8];

    if (!vs->password || !vs->password[0]) {
	VNC_DEBUG("No password configured on server");
	vnc_write_u32(vs, 1); /* Reject auth */
	if (vs->minor >= 8) {
	    static const char err[] = "Authentication failed";
	    vnc_write_u32(vs, sizeof(err));
	    vnc_write(vs, err, sizeof(err));
	}
	vnc_flush(vs);
	vnc_client_error(vs);
	return 0;
    }

    memcpy(response, vs->challenge, VNC_AUTH_CHALLENGE_SIZE);

    /* Calculate the expected challenge response */
    pwlen = strlen(vs->password);
    for (i=0; i<sizeof(key); i++)
        key[i] = i<pwlen ? vs->password[i] : 0;
    deskey(key, EN0);
    for (j = 0; j < VNC_AUTH_CHALLENGE_SIZE; j += 8)
        des(response+j, response+j);

    /* Compare expected vs actual challenge response */
    if (memcmp(response, data, VNC_AUTH_CHALLENGE_SIZE) != 0) {
	VNC_DEBUG("Client challenge reponse did not match\n");
	vnc_write_u32(vs, 1); /* Reject auth */
	if (vs->minor >= 8) {
	    static const char err[] = "Authentication failed";
	    vnc_write_u32(vs, sizeof(err));
	    vnc_write(vs, err, sizeof(err));
	}
	vnc_flush(vs);
	vnc_client_error(vs);
    } else {
	VNC_DEBUG("Accepting VNC challenge response\n");
	vnc_write_u32(vs, 0); /* Accept auth */
	vnc_flush(vs);

	vnc_read_when(vs, protocol_client_init, 1);
    }
    return 0;
}

static int start_auth_vnc(VncState *vs)
{
    make_challenge(vs);
    /* Send client a 'random' challenge */
    vnc_write(vs, vs->challenge, sizeof(vs->challenge));
    vnc_flush(vs);

    vnc_read_when(vs, protocol_client_auth_vnc, sizeof(vs->challenge));
    return 0;
}


#if CONFIG_VNC_TLS
#define DH_BITS 1024
static gnutls_dh_params_t dh_params;

static int vnc_tls_initialize(void)
{
    static int tlsinitialized = 0;

    if (tlsinitialized)
	return 1;

    if (gnutls_global_init () < 0)
	return 0;

    /* XXX ought to re-generate diffie-hellmen params periodically */
    if (gnutls_dh_params_init (&dh_params) < 0)
	return 0;
    if (gnutls_dh_params_generate2 (dh_params, DH_BITS) < 0)
	return 0;

#if _VNC_DEBUG == 2
    gnutls_global_set_log_level(10);
    gnutls_global_set_log_function(vnc_debug_gnutls_log);
#endif

    tlsinitialized = 1;

    return 1;
}

static gnutls_anon_server_credentials vnc_tls_initialize_anon_cred(void)
{
    gnutls_anon_server_credentials anon_cred;
    int ret;

    if ((ret = gnutls_anon_allocate_server_credentials(&anon_cred)) < 0) {
	VNC_DEBUG("Cannot allocate credentials %s\n", gnutls_strerror(ret));
	return NULL;
    }

    gnutls_anon_set_server_dh_params(anon_cred, dh_params);

    return anon_cred;
}


static gnutls_certificate_credentials_t vnc_tls_initialize_x509_cred(VncState *vs)
{
    gnutls_certificate_credentials_t x509_cred;
    int ret;

    if (!vs->x509cacert) {
	VNC_DEBUG("No CA x509 certificate specified\n");
	return NULL;
    }
    if (!vs->x509cert) {
	VNC_DEBUG("No server x509 certificate specified\n");
	return NULL;
    }
    if (!vs->x509key) {
	VNC_DEBUG("No server private key specified\n");
	return NULL;
    }

    if ((ret = gnutls_certificate_allocate_credentials(&x509_cred)) < 0) {
	VNC_DEBUG("Cannot allocate credentials %s\n", gnutls_strerror(ret));
	return NULL;
    }
    if ((ret = gnutls_certificate_set_x509_trust_file(x509_cred,
						      vs->x509cacert,
						      GNUTLS_X509_FMT_PEM)) < 0) {
	VNC_DEBUG("Cannot load CA certificate %s\n", gnutls_strerror(ret));
	gnutls_certificate_free_credentials(x509_cred);
	return NULL;
    }

    if ((ret = gnutls_certificate_set_x509_key_file (x509_cred,
						     vs->x509cert,
						     vs->x509key,
						     GNUTLS_X509_FMT_PEM)) < 0) {
	VNC_DEBUG("Cannot load certificate & key %s\n", gnutls_strerror(ret));
	gnutls_certificate_free_credentials(x509_cred);
	return NULL;
    }

    if (vs->x509cacrl) {
	if ((ret = gnutls_certificate_set_x509_crl_file(x509_cred,
							vs->x509cacrl,
							GNUTLS_X509_FMT_PEM)) < 0) {
	    VNC_DEBUG("Cannot load CRL %s\n", gnutls_strerror(ret));
	    gnutls_certificate_free_credentials(x509_cred);
	    return NULL;
	}
    }

    gnutls_certificate_set_dh_params (x509_cred, dh_params);

    return x509_cred;
}

static int vnc_validate_certificate(struct VncState *vs)
{
    int ret;
    unsigned int status;
    const gnutls_datum_t *certs;
    unsigned int nCerts, i;
    time_t now;

    VNC_DEBUG("Validating client certificate\n");
    if ((ret = gnutls_certificate_verify_peers2 (vs->tls_session, &status)) < 0) {
	VNC_DEBUG("Verify failed %s\n", gnutls_strerror(ret));
	return -1;
    }

    if ((now = time(NULL)) == ((time_t)-1)) {
	return -1;
    }

    if (status != 0) {
	if (status & GNUTLS_CERT_INVALID)
	    VNC_DEBUG("The certificate is not trusted.\n");

	if (status & GNUTLS_CERT_SIGNER_NOT_FOUND)
	    VNC_DEBUG("The certificate hasn't got a known issuer.\n");

	if (status & GNUTLS_CERT_REVOKED)
	    VNC_DEBUG("The certificate has been revoked.\n");

	if (status & GNUTLS_CERT_INSECURE_ALGORITHM)
	    VNC_DEBUG("The certificate uses an insecure algorithm\n");

	return -1;
    } else {
	VNC_DEBUG("Certificate is valid!\n");
    }

    /* Only support x509 for now */
    if (gnutls_certificate_type_get(vs->tls_session) != GNUTLS_CRT_X509)
	return -1;

    if (!(certs = gnutls_certificate_get_peers(vs->tls_session, &nCerts)))
	return -1;

    for (i = 0 ; i < nCerts ; i++) {
	gnutls_x509_crt_t cert;
	VNC_DEBUG ("Checking certificate chain %d\n", i);
	if (gnutls_x509_crt_init (&cert) < 0)
	    return -1;

	if (gnutls_x509_crt_import(cert, &certs[i], GNUTLS_X509_FMT_DER) < 0) {
	    gnutls_x509_crt_deinit (cert);
	    return -1;
	}

	if (gnutls_x509_crt_get_expiration_time (cert) < now) {
	    VNC_DEBUG("The certificate has expired\n");
	    gnutls_x509_crt_deinit (cert);
	    return -1;
	}

	if (gnutls_x509_crt_get_activation_time (cert) > now) {
	    VNC_DEBUG("The certificate is not yet activated\n");
	    gnutls_x509_crt_deinit (cert);
	    return -1;
	}

	if (gnutls_x509_crt_get_activation_time (cert) > now) {
	    VNC_DEBUG("The certificate is not yet activated\n");
	    gnutls_x509_crt_deinit (cert);
	    return -1;
	}

	gnutls_x509_crt_deinit (cert);
    }

    return 0;
}


static int start_auth_vencrypt_subauth(VncState *vs)
{
    switch (vs->subauth) {
    case VNC_AUTH_VENCRYPT_TLSNONE:
    case VNC_AUTH_VENCRYPT_X509NONE:
       VNC_DEBUG("Accept TLS auth none\n");
       vnc_write_u32(vs, 0); /* Accept auth completion */
       vnc_read_when(vs, protocol_client_init, 1);
       break;

    case VNC_AUTH_VENCRYPT_TLSVNC:
    case VNC_AUTH_VENCRYPT_X509VNC:
       VNC_DEBUG("Start TLS auth VNC\n");
       return start_auth_vnc(vs);

    default: /* Should not be possible, but just in case */
       VNC_DEBUG("Reject auth %d\n", vs->auth);
       vnc_write_u8(vs, 1);
       if (vs->minor >= 8) {
           static const char err[] = "Unsupported authentication type";
           vnc_write_u32(vs, sizeof(err));
           vnc_write(vs, err, sizeof(err));
       }
       vnc_client_error(vs);
    }

    return 0;
}

static void vnc_handshake_io(void *opaque);

static int vnc_continue_handshake(struct VncState *vs) {
    int ret;

    if ((ret = gnutls_handshake(vs->tls_session)) < 0) {
       if (!gnutls_error_is_fatal(ret)) {
           VNC_DEBUG("Handshake interrupted (blocking)\n");
           if (!gnutls_record_get_direction(vs->tls_session))
               qemu_set_fd_handler(vs->csock, vnc_handshake_io, NULL, vs);
           else
               qemu_set_fd_handler(vs->csock, NULL, vnc_handshake_io, vs);
           return 0;
       }
       VNC_DEBUG("Handshake failed %s\n", gnutls_strerror(ret));
       vnc_client_error(vs);
       return -1;
    }

    if (vs->x509verify) {
	if (vnc_validate_certificate(vs) < 0) {
	    VNC_DEBUG("Client verification failed\n");
	    vnc_client_error(vs);
	    return -1;
	} else {
	    VNC_DEBUG("Client verification passed\n");
	}
    }

    VNC_DEBUG("Handshake done, switching to TLS data mode\n");
    vs->wiremode = VNC_WIREMODE_TLS;
    qemu_set_fd_handler2(vs->csock, NULL, vnc_client_read, vnc_client_write, vs);

    return start_auth_vencrypt_subauth(vs);
}

static void vnc_handshake_io(void *opaque) {
    struct VncState *vs = (struct VncState *)opaque;

    VNC_DEBUG("Handshake IO continue\n");
    vnc_continue_handshake(vs);
}

#define NEED_X509_AUTH(vs)			      \
    ((vs)->subauth == VNC_AUTH_VENCRYPT_X509NONE ||   \
     (vs)->subauth == VNC_AUTH_VENCRYPT_X509VNC ||    \
     (vs)->subauth == VNC_AUTH_VENCRYPT_X509PLAIN)


static int vnc_start_tls(struct VncState *vs) {
    static const int cert_type_priority[] = { GNUTLS_CRT_X509, 0 };
    static const int protocol_priority[]= { GNUTLS_TLS1_1, GNUTLS_TLS1_0, GNUTLS_SSL3, 0 };
    static const int kx_anon[] = {GNUTLS_KX_ANON_DH, 0};
    static const int kx_x509[] = {GNUTLS_KX_DHE_DSS, GNUTLS_KX_RSA, GNUTLS_KX_DHE_RSA, GNUTLS_KX_SRP, 0};

    VNC_DEBUG("Do TLS setup\n");
    if (vnc_tls_initialize() < 0) {
	VNC_DEBUG("Failed to init TLS\n");
	vnc_client_error(vs);
	return -1;
    }
    if (vs->tls_session == NULL) {
	if (gnutls_init(&vs->tls_session, GNUTLS_SERVER) < 0) {
	    vnc_client_error(vs);
	    return -1;
	}

	if (gnutls_set_default_priority(vs->tls_session) < 0) {
	    gnutls_deinit(vs->tls_session);
	    vs->tls_session = NULL;
	    vnc_client_error(vs);
	    return -1;
	}

	if (gnutls_kx_set_priority(vs->tls_session, NEED_X509_AUTH(vs) ? kx_x509 : kx_anon) < 0) {
	    gnutls_deinit(vs->tls_session);
	    vs->tls_session = NULL;
	    vnc_client_error(vs);
	    return -1;
	}

	if (gnutls_certificate_type_set_priority(vs->tls_session, cert_type_priority) < 0) {
	    gnutls_deinit(vs->tls_session);
	    vs->tls_session = NULL;
	    vnc_client_error(vs);
	    return -1;
	}

	if (gnutls_protocol_set_priority(vs->tls_session, protocol_priority) < 0) {
	    gnutls_deinit(vs->tls_session);
	    vs->tls_session = NULL;
	    vnc_client_error(vs);
	    return -1;
	}

	if (NEED_X509_AUTH(vs)) {
	    gnutls_certificate_server_credentials x509_cred = vnc_tls_initialize_x509_cred(vs);
	    if (!x509_cred) {
		gnutls_deinit(vs->tls_session);
		vs->tls_session = NULL;
		vnc_client_error(vs);
		return -1;
	    }
	    if (gnutls_credentials_set(vs->tls_session, GNUTLS_CRD_CERTIFICATE, x509_cred) < 0) {
		gnutls_deinit(vs->tls_session);
		vs->tls_session = NULL;
		gnutls_certificate_free_credentials(x509_cred);
		vnc_client_error(vs);
		return -1;
	    }
	    if (vs->x509verify) {
		VNC_DEBUG("Requesting a client certificate\n");
		gnutls_certificate_server_set_request (vs->tls_session, GNUTLS_CERT_REQUEST);
	    }

	} else {
	    gnutls_anon_server_credentials anon_cred = vnc_tls_initialize_anon_cred();
	    if (!anon_cred) {
		gnutls_deinit(vs->tls_session);
		vs->tls_session = NULL;
		vnc_client_error(vs);
		return -1;
	    }
	    if (gnutls_credentials_set(vs->tls_session, GNUTLS_CRD_ANON, anon_cred) < 0) {
		gnutls_deinit(vs->tls_session);
		vs->tls_session = NULL;
		gnutls_anon_free_server_credentials(anon_cred);
		vnc_client_error(vs);
		return -1;
	    }
	}

	gnutls_transport_set_ptr(vs->tls_session, (gnutls_transport_ptr_t)vs);
	gnutls_transport_set_push_function(vs->tls_session, vnc_tls_push);
	gnutls_transport_set_pull_function(vs->tls_session, vnc_tls_pull);
    }

    VNC_DEBUG("Start TLS handshake process\n");
    return vnc_continue_handshake(vs);
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
	VNC_DEBUG("Accepting auth %d, starting handshake\n", auth);
	vnc_write_u8(vs, 1); /* Accept auth */
	vnc_flush(vs);

	if (vnc_start_tls(vs) < 0) {
	    VNC_DEBUG("Failed to complete TLS\n");
	    return 0;
	}

	if (vs->wiremode == VNC_WIREMODE_TLS) {
	    VNC_DEBUG("Starting VeNCrypt subauth\n");
	    return start_auth_vencrypt_subauth(vs);
	} else {
	    VNC_DEBUG("TLS handshake blocked\n");
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

static int start_auth_vencrypt(VncState *vs)
{
    /* Send VeNCrypt version 0.2 */
    vnc_write_u8(vs, 0);
    vnc_write_u8(vs, 2);

    vnc_read_when(vs, protocol_client_vencrypt_init, 2);
    return 0;
}
#endif /* CONFIG_VNC_TLS */

static int protocol_client_auth(VncState *vs, uint8_t *data, size_t len)
{
    /* We only advertise 1 auth scheme at a time, so client
     * must pick the one we sent. Verify this */
    if (data[0] != vs->auth) { /* Reject auth */
       VNC_DEBUG("Reject auth %d\n", (int)data[0]);
       vnc_write_u32(vs, 1);
       if (vs->minor >= 8) {
           static const char err[] = "Authentication failed";
           vnc_write_u32(vs, sizeof(err));
           vnc_write(vs, err, sizeof(err));
       }
       vnc_client_error(vs);
    } else { /* Accept requested auth */
       VNC_DEBUG("Client requested auth %d\n", (int)data[0]);
       switch (vs->auth) {
       case VNC_AUTH_NONE:
           VNC_DEBUG("Accept auth none\n");
           if (vs->minor >= 8) {
               vnc_write_u32(vs, 0); /* Accept auth completion */
               vnc_flush(vs);
           }
           vnc_read_when(vs, protocol_client_init, 1);
           break;

       case VNC_AUTH_VNC:
           VNC_DEBUG("Start VNC auth\n");
           return start_auth_vnc(vs);

#if CONFIG_VNC_TLS
       case VNC_AUTH_VENCRYPT:
           VNC_DEBUG("Accept VeNCrypt auth\n");;
           return start_auth_vencrypt(vs);
#endif /* CONFIG_VNC_TLS */

       default: /* Should not be possible, but just in case */
           VNC_DEBUG("Reject auth %d\n", vs->auth);
           vnc_write_u8(vs, 1);
           if (vs->minor >= 8) {
               static const char err[] = "Authentication failed";
               vnc_write_u32(vs, sizeof(err));
               vnc_write(vs, err, sizeof(err));
           }
           vnc_client_error(vs);
       }
    }
    return 0;
}

static int protocol_version(VncState *vs, uint8_t *version, size_t len)
{
    char local[13];

    memcpy(local, version, 12);
    local[12] = 0;

    if (sscanf(local, "RFB %03d.%03d\n", &vs->major, &vs->minor) != 2) {
	VNC_DEBUG("Malformed protocol version %s\n", local);
	vnc_client_error(vs);
	return 0;
    }
    VNC_DEBUG("Client request protocol version %d.%d\n", vs->major, vs->minor);
    if (vs->major != 3 ||
	(vs->minor != 3 &&
	 vs->minor != 4 &&
	 vs->minor != 5 &&
	 vs->minor != 7 &&
	 vs->minor != 8)) {
	VNC_DEBUG("Unsupported client version\n");
	vnc_write_u32(vs, VNC_AUTH_INVALID);
	vnc_flush(vs);
	vnc_client_error(vs);
	return 0;
    }
    /* Some broken clients report v3.4 or v3.5, which spec requires to be treated
     * as equivalent to v3.3 by servers
     */
    if (vs->minor == 4 || vs->minor == 5)
	vs->minor = 3;

    if (vs->minor == 3) {
	if (vs->auth == VNC_AUTH_NONE) {
            VNC_DEBUG("Tell client auth none\n");
            vnc_write_u32(vs, vs->auth);
            vnc_flush(vs);
            vnc_read_when(vs, protocol_client_init, 1);
       } else if (vs->auth == VNC_AUTH_VNC) {
            VNC_DEBUG("Tell client VNC auth\n");
            vnc_write_u32(vs, vs->auth);
            vnc_flush(vs);
            start_auth_vnc(vs);
       } else {
            VNC_DEBUG("Unsupported auth %d for protocol 3.3\n", vs->auth);
            vnc_write_u32(vs, VNC_AUTH_INVALID);
            vnc_flush(vs);
            vnc_client_error(vs);
       }
    } else {
	VNC_DEBUG("Telling client we support auth %d\n", vs->auth);
	vnc_write_u8(vs, 1); /* num auth */
	vnc_write_u8(vs, vs->auth);
	vnc_read_when(vs, protocol_client_auth, 1);
	vnc_flush(vs);
    }

    return 0;
}

static void vnc_connect(VncState *vs)
{
    VNC_DEBUG("New client on socket %d\n", vs->csock);
    vs->ds->idle = 0;
    socket_set_nonblock(vs->csock);
    qemu_set_fd_handler2(vs->csock, NULL, vnc_client_read, NULL, vs);
    vnc_write(vs, "RFB 003.008\n", 12);
    vnc_flush(vs);
    vnc_read_when(vs, protocol_version, 12);
    memset(vs->old_data, 0, vs->ds->linesize * vs->ds->height);
    memset(vs->dirty_row, 0xFF, sizeof(vs->dirty_row));
    vs->has_resize = 0;
    vs->has_hextile = 0;
    vs->ds->dpy_copy = NULL;
    vnc_update_client(vs);
}

static void vnc_listen_read(void *opaque)
{
    VncState *vs = opaque;
    struct sockaddr_in addr;
    socklen_t addrlen = sizeof(addr);

    /* Catch-up */
    vga_hw_update();

    vs->csock = accept(vs->lsock, (struct sockaddr *)&addr, &addrlen);
    if (vs->csock != -1) {
        vnc_connect(vs);
    }
}

extern int parse_host_port(struct sockaddr_in *saddr, const char *str);

void vnc_display_init(DisplayState *ds)
{
    VncState *vs;

    vs = qemu_mallocz(sizeof(VncState));
    if (!vs)
	exit(1);

    ds->opaque = vs;
    ds->idle = 1;
    vnc_state = vs;
    vs->display = NULL;
    vs->password = NULL;

    vs->lsock = -1;
    vs->csock = -1;
    vs->depth = 4;
    vs->last_x = -1;
    vs->last_y = -1;

    vs->ds = ds;

    if (!keyboard_layout)
	keyboard_layout = "en-us";

    vs->kbd_layout = init_keyboard_layout(keyboard_layout);
    if (!vs->kbd_layout)
	exit(1);

    vs->timer = qemu_new_timer(rt_clock, vnc_update_client, vs);

    vs->ds->data = NULL;
    vs->ds->dpy_update = vnc_dpy_update;
    vs->ds->dpy_resize = vnc_dpy_resize;
    vs->ds->dpy_refresh = NULL;

    vnc_dpy_resize(vs->ds, 640, 400);
}

#if CONFIG_VNC_TLS
static int vnc_set_x509_credential(VncState *vs,
				   const char *certdir,
				   const char *filename,
				   char **cred,
				   int ignoreMissing)
{
    struct stat sb;

    if (*cred) {
	qemu_free(*cred);
	*cred = NULL;
    }

    if (!(*cred = qemu_malloc(strlen(certdir) + strlen(filename) + 2)))
	return -1;

    strcpy(*cred, certdir);
    strcat(*cred, "/");
    strcat(*cred, filename);

    VNC_DEBUG("Check %s\n", *cred);
    if (stat(*cred, &sb) < 0) {
	qemu_free(*cred);
	*cred = NULL;
	if (ignoreMissing && errno == ENOENT)
	    return 0;
	return -1;
    }

    return 0;
}

static int vnc_set_x509_credential_dir(VncState *vs,
				       const char *certdir)
{
    if (vnc_set_x509_credential(vs, certdir, X509_CA_CERT_FILE, &vs->x509cacert, 0) < 0)
	goto cleanup;
    if (vnc_set_x509_credential(vs, certdir, X509_CA_CRL_FILE, &vs->x509cacrl, 1) < 0)
	goto cleanup;
    if (vnc_set_x509_credential(vs, certdir, X509_SERVER_CERT_FILE, &vs->x509cert, 0) < 0)
	goto cleanup;
    if (vnc_set_x509_credential(vs, certdir, X509_SERVER_KEY_FILE, &vs->x509key, 0) < 0)
	goto cleanup;

    return 0;

 cleanup:
    qemu_free(vs->x509cacert);
    qemu_free(vs->x509cacrl);
    qemu_free(vs->x509cert);
    qemu_free(vs->x509key);
    vs->x509cacert = vs->x509cacrl = vs->x509cert = vs->x509key = NULL;
    return -1;
}
#endif /* CONFIG_VNC_TLS */

void vnc_display_close(DisplayState *ds)
{
    VncState *vs = ds ? (VncState *)ds->opaque : vnc_state;

    if (vs->display) {
	qemu_free(vs->display);
	vs->display = NULL;
    }
    if (vs->lsock != -1) {
	qemu_set_fd_handler2(vs->lsock, NULL, NULL, NULL, NULL);
	close(vs->lsock);
	vs->lsock = -1;
    }
    if (vs->csock != -1) {
	qemu_set_fd_handler2(vs->csock, NULL, NULL, NULL, NULL);
	closesocket(vs->csock);
	vs->csock = -1;
	buffer_reset(&vs->input);
	buffer_reset(&vs->output);
	vs->need_update = 0;
#if CONFIG_VNC_TLS
	if (vs->tls_session) {
	    gnutls_deinit(vs->tls_session);
	    vs->tls_session = NULL;
	}
	vs->wiremode = VNC_WIREMODE_CLEAR;
#endif /* CONFIG_VNC_TLS */
    }
    vs->auth = VNC_AUTH_INVALID;
#if CONFIG_VNC_TLS
    vs->subauth = VNC_AUTH_INVALID;
    vs->x509verify = 0;
#endif
}

int vnc_display_password(DisplayState *ds, const char *password)
{
    VncState *vs = ds ? (VncState *)ds->opaque : vnc_state;

    if (vs->password) {
	qemu_free(vs->password);
	vs->password = NULL;
    }
    if (password && password[0]) {
	if (!(vs->password = qemu_strdup(password)))
	    return -1;
    }

    return 0;
}

int vnc_display_open(DisplayState *ds, const char *display)
{
    struct sockaddr *addr;
    struct sockaddr_in iaddr;
#ifndef _WIN32
    struct sockaddr_un uaddr;
    const char *p;
#endif
    int reuse_addr, ret;
    socklen_t addrlen;
    VncState *vs = ds ? (VncState *)ds->opaque : vnc_state;
    const char *options;
    int password = 0;
    int reverse = 0;
#if CONFIG_VNC_TLS
    int tls = 0, x509 = 0;
#endif

    vnc_display_close(ds);
    if (strcmp(display, "none") == 0)
	return 0;

    if (!(vs->display = strdup(display)))
	return -1;

    options = display;
    while ((options = strchr(options, ','))) {
	options++;
	if (strncmp(options, "password", 8) == 0) {
	    password = 1; /* Require password auth */
	} else if (strncmp(options, "reverse", 7) == 0) {
	    reverse = 1;
#if CONFIG_VNC_TLS
	} else if (strncmp(options, "tls", 3) == 0) {
	    tls = 1; /* Require TLS */
	} else if (strncmp(options, "x509", 4) == 0) {
	    char *start, *end;
	    x509 = 1; /* Require x509 certificates */
	    if (strncmp(options, "x509verify", 10) == 0)
	        vs->x509verify = 1; /* ...and verify client certs */

	    /* Now check for 'x509=/some/path' postfix
	     * and use that to setup x509 certificate/key paths */
	    start = strchr(options, '=');
	    end = strchr(options, ',');
	    if (start && (!end || (start < end))) {
		int len = end ? end-(start+1) : strlen(start+1);
		char *path = qemu_malloc(len+1);
		strncpy(path, start+1, len);
		path[len] = '\0';
		VNC_DEBUG("Trying certificate path '%s'\n", path);
		if (vnc_set_x509_credential_dir(vs, path) < 0) {
		    fprintf(stderr, "Failed to find x509 certificates/keys in %s\n", path);
		    qemu_free(path);
		    qemu_free(vs->display);
		    vs->display = NULL;
		    return -1;
		}
		qemu_free(path);
	    } else {
		fprintf(stderr, "No certificate path provided\n");
		qemu_free(vs->display);
		vs->display = NULL;
		return -1;
	    }
#endif
	}
    }

    if (password) {
#if CONFIG_VNC_TLS
	if (tls) {
	    vs->auth = VNC_AUTH_VENCRYPT;
	    if (x509) {
		VNC_DEBUG("Initializing VNC server with x509 password auth\n");
		vs->subauth = VNC_AUTH_VENCRYPT_X509VNC;
	    } else {
		VNC_DEBUG("Initializing VNC server with TLS password auth\n");
		vs->subauth = VNC_AUTH_VENCRYPT_TLSVNC;
	    }
	} else {
#endif
	    VNC_DEBUG("Initializing VNC server with password auth\n");
	    vs->auth = VNC_AUTH_VNC;
#if CONFIG_VNC_TLS
	    vs->subauth = VNC_AUTH_INVALID;
	}
#endif
    } else {
#if CONFIG_VNC_TLS
	if (tls) {
	    vs->auth = VNC_AUTH_VENCRYPT;
	    if (x509) {
		VNC_DEBUG("Initializing VNC server with x509 no auth\n");
		vs->subauth = VNC_AUTH_VENCRYPT_X509NONE;
	    } else {
		VNC_DEBUG("Initializing VNC server with TLS no auth\n");
		vs->subauth = VNC_AUTH_VENCRYPT_TLSNONE;
	    }
	} else {
#endif
	    VNC_DEBUG("Initializing VNC server with no auth\n");
	    vs->auth = VNC_AUTH_NONE;
#if CONFIG_VNC_TLS
	    vs->subauth = VNC_AUTH_INVALID;
	}
#endif
    }
#ifndef _WIN32
    if (strstart(display, "unix:", &p)) {
	addr = (struct sockaddr *)&uaddr;
	addrlen = sizeof(uaddr);

	vs->lsock = socket(PF_UNIX, SOCK_STREAM, 0);
	if (vs->lsock == -1) {
	    fprintf(stderr, "Could not create socket\n");
	    free(vs->display);
	    vs->display = NULL;
	    return -1;
	}

	uaddr.sun_family = AF_UNIX;
	memset(uaddr.sun_path, 0, 108);
	snprintf(uaddr.sun_path, 108, "%s", p);

	if (!reverse) {
	    unlink(uaddr.sun_path);
	}
    } else
#endif
    {
	addr = (struct sockaddr *)&iaddr;
	addrlen = sizeof(iaddr);

	if (parse_host_port(&iaddr, display) < 0) {
	    fprintf(stderr, "Could not parse VNC address\n");
	    free(vs->display);
	    vs->display = NULL;
	    return -1;
	}

	iaddr.sin_port = htons(ntohs(iaddr.sin_port) + (reverse ? 0 : 5900));

	vs->lsock = socket(PF_INET, SOCK_STREAM, 0);
	if (vs->lsock == -1) {
	    fprintf(stderr, "Could not create socket\n");
	    free(vs->display);
	    vs->display = NULL;
	    return -1;
	}

	reuse_addr = 1;
	ret = setsockopt(vs->lsock, SOL_SOCKET, SO_REUSEADDR,
			 (const char *)&reuse_addr, sizeof(reuse_addr));
	if (ret == -1) {
	    fprintf(stderr, "setsockopt() failed\n");
	    close(vs->lsock);
	    vs->lsock = -1;
	    free(vs->display);
	    vs->display = NULL;
	    return -1;
	}
    }

    if (reverse) {
        if (connect(vs->lsock, addr, addrlen) == -1) {
            fprintf(stderr, "Connection to VNC client failed\n");
            close(vs->lsock);
            vs->lsock = -1;
            free(vs->display);
            vs->display = NULL;
            return -1;
        } else {
            vs->csock = vs->lsock;
            vs->lsock = -1;
            vnc_connect(vs);
            return 0;
        }
    }

    if (bind(vs->lsock, addr, addrlen) == -1) {
	fprintf(stderr, "bind() failed\n");
	close(vs->lsock);
	vs->lsock = -1;
	free(vs->display);
	vs->display = NULL;
	return -1;
    }

    if (listen(vs->lsock, 1) == -1) {
	fprintf(stderr, "listen() failed\n");
	close(vs->lsock);
	vs->lsock = -1;
	free(vs->display);
	vs->display = NULL;
	return -1;
    }

    return qemu_set_fd_handler2(vs->lsock, vnc_listen_poll, vnc_listen_read, NULL, vs);
}
