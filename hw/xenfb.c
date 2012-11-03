/*
 *  xen paravirt framebuffer backend
 *
 *  Copyright IBM, Corp. 2005-2006
 *  Copyright Red Hat, Inc. 2006-2008
 *
 *  Authors:
 *       Anthony Liguori <aliguori@us.ibm.com>,
 *       Markus Armbruster <armbru@redhat.com>,
 *       Daniel P. Berrange <berrange@redhat.com>,
 *       Pat Campbell <plc@novell.com>,
 *       Gerd Hoffmann <kraxel@redhat.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; under version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include <stdarg.h>
#include <stdlib.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "hw.h"
#include "console.h"
#include "qemu-char.h"
#include "xen_backend.h"

#include <xen/event_channel.h>
#include <xen/io/fbif.h>
#include <xen/io/kbdif.h>
#include <xen/io/protocols.h>

#ifndef BTN_LEFT
#define BTN_LEFT 0x110 /* from <linux/input.h> */
#endif

/* -------------------------------------------------------------------- */

struct common {
    struct XenDevice  xendev;  /* must be first */
    void              *page;
    DisplayState      *ds;
};

struct XenInput {
    struct common c;
    int abs_pointer_wanted; /* Whether guest supports absolute pointer */
    int button_state;       /* Last seen pointer button state */
    int extended;
    QEMUPutMouseEntry *qmouse;
};

#define UP_QUEUE 8

struct XenFB {
    struct common     c;
    size_t            fb_len;
    int               row_stride;
    int               depth;
    int               width;
    int               height;
    int               offset;
    void              *pixels;
    int               fbpages;
    int               feature_update;
    int               refresh_period;
    int               bug_trigger;
    int               have_console;
    int               do_resize;

    struct {
	int x,y,w,h;
    } up_rects[UP_QUEUE];
    int               up_count;
    int               up_fullscreen;
};

/* -------------------------------------------------------------------- */

static int common_bind(struct common *c)
{
    int mfn;

    if (xenstore_read_fe_int(&c->xendev, "page-ref", &mfn) == -1)
	return -1;
    if (xenstore_read_fe_int(&c->xendev, "event-channel", &c->xendev.remote_port) == -1)
	return -1;

    c->page = xc_map_foreign_range(xen_xc, c->xendev.dom,
				   XC_PAGE_SIZE,
				   PROT_READ | PROT_WRITE, mfn);
    if (c->page == NULL)
	return -1;

    xen_be_bind_evtchn(&c->xendev);
    xen_be_printf(&c->xendev, 1, "ring mfn %d, remote-port %d, local-port %d\n",
		  mfn, c->xendev.remote_port, c->xendev.local_port);

    return 0;
}

static void common_unbind(struct common *c)
{
    xen_be_unbind_evtchn(&c->xendev);
    if (c->page) {
	munmap(c->page, XC_PAGE_SIZE);
	c->page = NULL;
    }
}

/* -------------------------------------------------------------------- */

#if 0
/*
 * These two tables are not needed any more, but left in here
 * intentionally as documentation, to show how scancode2linux[]
 * was generated.
 *
 * Tables to map from scancode to Linux input layer keycode.
 * Scancodes are hardware-specific.  These maps assumes a
 * standard AT or PS/2 keyboard which is what QEMU feeds us.
 */
const unsigned char atkbd_set2_keycode[512] = {

     0, 67, 65, 63, 61, 59, 60, 88,  0, 68, 66, 64, 62, 15, 41,117,
     0, 56, 42, 93, 29, 16,  2,  0,  0,  0, 44, 31, 30, 17,  3,  0,
     0, 46, 45, 32, 18,  5,  4, 95,  0, 57, 47, 33, 20, 19,  6,183,
     0, 49, 48, 35, 34, 21,  7,184,  0,  0, 50, 36, 22,  8,  9,185,
     0, 51, 37, 23, 24, 11, 10,  0,  0, 52, 53, 38, 39, 25, 12,  0,
     0, 89, 40,  0, 26, 13,  0,  0, 58, 54, 28, 27,  0, 43,  0, 85,
     0, 86, 91, 90, 92,  0, 14, 94,  0, 79,124, 75, 71,121,  0,  0,
    82, 83, 80, 76, 77, 72,  1, 69, 87, 78, 81, 74, 55, 73, 70, 99,

      0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    217,100,255,  0, 97,165,  0,  0,156,  0,  0,  0,  0,  0,  0,125,
    173,114,  0,113,  0,  0,  0,126,128,  0,  0,140,  0,  0,  0,127,
    159,  0,115,  0,164,  0,  0,116,158,  0,150,166,  0,  0,  0,142,
    157,  0,  0,  0,  0,  0,  0,  0,155,  0, 98,  0,  0,163,  0,  0,
    226,  0,  0,  0,  0,  0,  0,  0,  0,255, 96,  0,  0,  0,143,  0,
      0,  0,  0,  0,  0,  0,  0,  0,  0,107,  0,105,102,  0,  0,112,
    110,111,108,112,106,103,  0,119,  0,118,109,  0, 99,104,119,  0,

};

const unsigned char atkbd_unxlate_table[128] = {

      0,118, 22, 30, 38, 37, 46, 54, 61, 62, 70, 69, 78, 85,102, 13,
     21, 29, 36, 45, 44, 53, 60, 67, 68, 77, 84, 91, 90, 20, 28, 27,
     35, 43, 52, 51, 59, 66, 75, 76, 82, 14, 18, 93, 26, 34, 33, 42,
     50, 49, 58, 65, 73, 74, 89,124, 17, 41, 88,  5,  6,  4, 12,  3,
     11,  2, 10,  1,  9,119,126,108,117,125,123,107,115,116,121,105,
    114,122,112,113,127, 96, 97,120,  7, 15, 23, 31, 39, 47, 55, 63,
     71, 79, 86, 94,  8, 16, 24, 32, 40, 48, 56, 64, 72, 80, 87,111,
     19, 25, 57, 81, 83, 92, 95, 98, 99,100,101,103,104,106,109,110

};
#endif

/*
 * for (i = 0; i < 128; i++) {
 *     scancode2linux[i] = atkbd_set2_keycode[atkbd_unxlate_table[i]];
 *     scancode2linux[i | 0x80] = atkbd_set2_keycode[atkbd_unxlate_table[i] | 0x80];
 * }
 */
static const unsigned char scancode2linux[512] = {
      0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15,
     16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
     32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47,
     48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63,
     64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79,
     80, 81, 82, 83, 99,  0, 86, 87, 88,117,  0,  0, 95,183,184,185,
      0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
     93,  0,  0, 89,  0,  0, 85, 91, 90, 92,  0, 94,  0,124,121,  0,

      0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    165,  0,  0,  0,  0,  0,  0,  0,  0,163,  0,  0, 96, 97,  0,  0,
    113,140,164,  0,166,  0,  0,  0,  0,  0,255,  0,  0,  0,114,  0,
    115,  0,150,  0,  0, 98,255, 99,100,  0,  0,  0,  0,  0,  0,  0,
      0,  0,  0,  0,  0,119,119,102,103,104,  0,105,112,106,118,107,
    108,109,110,111,  0,  0,  0,  0,  0,  0,  0,125,126,127,116,142,
      0,  0,  0,143,  0,217,156,173,128,159,158,157,155,226,  0,112,
      0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
};

/* Send an event to the keyboard frontend driver */
static int xenfb_kbd_event(struct XenInput *xenfb,
			   union xenkbd_in_event *event)
{
    struct xenkbd_page *page = xenfb->c.page;
    uint32_t prod;

    if (xenfb->c.xendev.be_state != XenbusStateConnected)
	return 0;
    if (!page)
        return 0;

    prod = page->in_prod;
    if (prod - page->in_cons == XENKBD_IN_RING_LEN) {
	errno = EAGAIN;
	return -1;
    }

    xen_mb();		/* ensure ring space available */
    XENKBD_IN_RING_REF(page, prod) = *event;
    xen_wmb();		/* ensure ring contents visible */
    page->in_prod = prod + 1;
    return xen_be_send_notify(&xenfb->c.xendev);
}

/* Send a keyboard (or mouse button) event */
static int xenfb_send_key(struct XenInput *xenfb, bool down, int keycode)
{
    union xenkbd_in_event event;

    memset(&event, 0, XENKBD_IN_EVENT_SIZE);
    event.type = XENKBD_TYPE_KEY;
    event.key.pressed = down ? 1 : 0;
    event.key.keycode = keycode;

    return xenfb_kbd_event(xenfb, &event);
}

/* Send a relative mouse movement event */
static int xenfb_send_motion(struct XenInput *xenfb,
			     int rel_x, int rel_y, int rel_z)
{
    union xenkbd_in_event event;

    memset(&event, 0, XENKBD_IN_EVENT_SIZE);
    event.type = XENKBD_TYPE_MOTION;
    event.motion.rel_x = rel_x;
    event.motion.rel_y = rel_y;
#if __XEN_LATEST_INTERFACE_VERSION__ >= 0x00030207
    event.motion.rel_z = rel_z;
#endif

    return xenfb_kbd_event(xenfb, &event);
}

/* Send an absolute mouse movement event */
static int xenfb_send_position(struct XenInput *xenfb,
			       int abs_x, int abs_y, int z)
{
    union xenkbd_in_event event;

    memset(&event, 0, XENKBD_IN_EVENT_SIZE);
    event.type = XENKBD_TYPE_POS;
    event.pos.abs_x = abs_x;
    event.pos.abs_y = abs_y;
#if __XEN_LATEST_INTERFACE_VERSION__ == 0x00030207
    event.pos.abs_z = z;
#endif
#if __XEN_LATEST_INTERFACE_VERSION__ >= 0x00030208
    event.pos.rel_z = z;
#endif

    return xenfb_kbd_event(xenfb, &event);
}

/*
 * Send a key event from the client to the guest OS
 * QEMU gives us a raw scancode from an AT / PS/2 style keyboard.
 * We have to turn this into a Linux Input layer keycode.
 *
 * Extra complexity from the fact that with extended scancodes
 * (like those produced by arrow keys) this method gets called
 * twice, but we only want to send a single event. So we have to
 * track the '0xe0' scancode state & collapse the extended keys
 * as needed.
 *
 * Wish we could just send scancodes straight to the guest which
 * already has code for dealing with this...
 */
static void xenfb_key_event(void *opaque, int scancode)
{
    struct XenInput *xenfb = opaque;
    int down = 1;

    if (scancode == 0xe0) {
	xenfb->extended = 1;
	return;
    } else if (scancode & 0x80) {
	scancode &= 0x7f;
	down = 0;
    }
    if (xenfb->extended) {
	scancode |= 0x80;
	xenfb->extended = 0;
    }
    xenfb_send_key(xenfb, down, scancode2linux[scancode]);
}

/*
 * Send a mouse event from the client to the guest OS
 *
 * The QEMU mouse can be in either relative, or absolute mode.
 * Movement is sent separately from button state, which has to
 * be encoded as virtual key events. We also don't actually get
 * given any button up/down events, so have to track changes in
 * the button state.
 */
static void xenfb_mouse_event(void *opaque,
			      int dx, int dy, int dz, int button_state)
{
    struct XenInput *xenfb = opaque;
    int dw = ds_get_width(xenfb->c.ds);
    int dh = ds_get_height(xenfb->c.ds);
    int i;

    if (xenfb->abs_pointer_wanted)
	xenfb_send_position(xenfb,
			    dx * (dw - 1) / 0x7fff,
			    dy * (dh - 1) / 0x7fff,
			    dz);
    else
	xenfb_send_motion(xenfb, dx, dy, dz);

    for (i = 0 ; i < 8 ; i++) {
	int lastDown = xenfb->button_state & (1 << i);
	int down = button_state & (1 << i);
	if (down == lastDown)
	    continue;

	if (xenfb_send_key(xenfb, down, BTN_LEFT+i) < 0)
	    return;
    }
    xenfb->button_state = button_state;
}

static int input_init(struct XenDevice *xendev)
{
    xenstore_write_be_int(xendev, "feature-abs-pointer", 1);
    return 0;
}

static int input_initialise(struct XenDevice *xendev)
{
    struct XenInput *in = container_of(xendev, struct XenInput, c.xendev);
    int rc;

    if (!in->c.ds) {
        char *vfb = xenstore_read_str(NULL, "device/vfb");
        if (vfb == NULL) {
            /* there is no vfb, run vkbd on its own */
            in->c.ds = get_displaystate();
        } else {
            g_free(vfb);
            xen_be_printf(xendev, 1, "ds not set (yet)\n");
            return -1;
        }
    }

    rc = common_bind(&in->c);
    if (rc != 0)
	return rc;

    qemu_add_kbd_event_handler(xenfb_key_event, in);
    return 0;
}

static void input_connected(struct XenDevice *xendev)
{
    struct XenInput *in = container_of(xendev, struct XenInput, c.xendev);

    if (xenstore_read_fe_int(xendev, "request-abs-pointer",
                             &in->abs_pointer_wanted) == -1) {
        in->abs_pointer_wanted = 0;
    }

    if (in->qmouse) {
        qemu_remove_mouse_event_handler(in->qmouse);
    }
    in->qmouse = qemu_add_mouse_event_handler(xenfb_mouse_event, in,
					      in->abs_pointer_wanted,
					      "Xen PVFB Mouse");
}

static void input_disconnect(struct XenDevice *xendev)
{
    struct XenInput *in = container_of(xendev, struct XenInput, c.xendev);

    if (in->qmouse) {
	qemu_remove_mouse_event_handler(in->qmouse);
	in->qmouse = NULL;
    }
    qemu_add_kbd_event_handler(NULL, NULL);
    common_unbind(&in->c);
}

static void input_event(struct XenDevice *xendev)
{
    struct XenInput *xenfb = container_of(xendev, struct XenInput, c.xendev);
    struct xenkbd_page *page = xenfb->c.page;

    /* We don't understand any keyboard events, so just ignore them. */
    if (page->out_prod == page->out_cons)
	return;
    page->out_cons = page->out_prod;
    xen_be_send_notify(&xenfb->c.xendev);
}

/* -------------------------------------------------------------------- */

static void xenfb_copy_mfns(int mode, int count, unsigned long *dst, void *src)
{
    uint32_t *src32 = src;
    uint64_t *src64 = src;
    int i;

    for (i = 0; i < count; i++)
	dst[i] = (mode == 32) ? src32[i] : src64[i];
}

static int xenfb_map_fb(struct XenFB *xenfb)
{
    struct xenfb_page *page = xenfb->c.page;
    char *protocol = xenfb->c.xendev.protocol;
    int n_fbdirs;
    unsigned long *pgmfns = NULL;
    unsigned long *fbmfns = NULL;
    void *map, *pd;
    int mode, ret = -1;

    /* default to native */
    pd = page->pd;
    mode = sizeof(unsigned long) * 8;

    if (!protocol) {
	/*
	 * Undefined protocol, some guesswork needed.
	 *
	 * Old frontends which don't set the protocol use
	 * one page directory only, thus pd[1] must be zero.
	 * pd[1] of the 32bit struct layout and the lower
	 * 32 bits of pd[0] of the 64bit struct layout have
	 * the same location, so we can check that ...
	 */
	uint32_t *ptr32 = NULL;
	uint32_t *ptr64 = NULL;
#if defined(__i386__)
	ptr32 = (void*)page->pd;
	ptr64 = ((void*)page->pd) + 4;
#elif defined(__x86_64__)
	ptr32 = ((void*)page->pd) - 4;
	ptr64 = (void*)page->pd;
#endif
	if (ptr32) {
	    if (ptr32[1] == 0) {
		mode = 32;
		pd   = ptr32;
	    } else {
		mode = 64;
		pd   = ptr64;
	    }
	}
#if defined(__x86_64__)
    } else if (strcmp(protocol, XEN_IO_PROTO_ABI_X86_32) == 0) {
	/* 64bit dom0, 32bit domU */
	mode = 32;
	pd   = ((void*)page->pd) - 4;
#elif defined(__i386__)
    } else if (strcmp(protocol, XEN_IO_PROTO_ABI_X86_64) == 0) {
	/* 32bit dom0, 64bit domU */
	mode = 64;
	pd   = ((void*)page->pd) + 4;
#endif
    }

    if (xenfb->pixels) {
        munmap(xenfb->pixels, xenfb->fbpages * XC_PAGE_SIZE);
        xenfb->pixels = NULL;
    }

    xenfb->fbpages = (xenfb->fb_len + (XC_PAGE_SIZE - 1)) / XC_PAGE_SIZE;
    n_fbdirs = xenfb->fbpages * mode / 8;
    n_fbdirs = (n_fbdirs + (XC_PAGE_SIZE - 1)) / XC_PAGE_SIZE;

    pgmfns = g_malloc0(sizeof(unsigned long) * n_fbdirs);
    fbmfns = g_malloc0(sizeof(unsigned long) * xenfb->fbpages);

    xenfb_copy_mfns(mode, n_fbdirs, pgmfns, pd);
    map = xc_map_foreign_pages(xen_xc, xenfb->c.xendev.dom,
			       PROT_READ, pgmfns, n_fbdirs);
    if (map == NULL)
	goto out;
    xenfb_copy_mfns(mode, xenfb->fbpages, fbmfns, map);
    munmap(map, n_fbdirs * XC_PAGE_SIZE);

    xenfb->pixels = xc_map_foreign_pages(xen_xc, xenfb->c.xendev.dom,
					 PROT_READ | PROT_WRITE, fbmfns, xenfb->fbpages);
    if (xenfb->pixels == NULL)
	goto out;

    ret = 0; /* all is fine */

out:
    g_free(pgmfns);
    g_free(fbmfns);
    return ret;
}

static int xenfb_configure_fb(struct XenFB *xenfb, size_t fb_len_lim,
			      int width, int height, int depth,
			      size_t fb_len, int offset, int row_stride)
{
    size_t mfn_sz = sizeof(*((struct xenfb_page *)0)->pd);
    size_t pd_len = sizeof(((struct xenfb_page *)0)->pd) / mfn_sz;
    size_t fb_pages = pd_len * XC_PAGE_SIZE / mfn_sz;
    size_t fb_len_max = fb_pages * XC_PAGE_SIZE;
    int max_width, max_height;

    if (fb_len_lim > fb_len_max) {
	xen_be_printf(&xenfb->c.xendev, 0, "fb size limit %zu exceeds %zu, corrected\n",
		      fb_len_lim, fb_len_max);
	fb_len_lim = fb_len_max;
    }
    if (fb_len_lim && fb_len > fb_len_lim) {
	xen_be_printf(&xenfb->c.xendev, 0, "frontend fb size %zu limited to %zu\n",
		      fb_len, fb_len_lim);
	fb_len = fb_len_lim;
    }
    if (depth != 8 && depth != 16 && depth != 24 && depth != 32) {
	xen_be_printf(&xenfb->c.xendev, 0, "can't handle frontend fb depth %d\n",
		      depth);
	return -1;
    }
    if (row_stride <= 0 || row_stride > fb_len) {
	xen_be_printf(&xenfb->c.xendev, 0, "invalid frontend stride %d\n", row_stride);
	return -1;
    }
    max_width = row_stride / (depth / 8);
    if (width < 0 || width > max_width) {
	xen_be_printf(&xenfb->c.xendev, 0, "invalid frontend width %d limited to %d\n",
		      width, max_width);
	width = max_width;
    }
    if (offset < 0 || offset >= fb_len) {
	xen_be_printf(&xenfb->c.xendev, 0, "invalid frontend offset %d (max %zu)\n",
		      offset, fb_len - 1);
	return -1;
    }
    max_height = (fb_len - offset) / row_stride;
    if (height < 0 || height > max_height) {
	xen_be_printf(&xenfb->c.xendev, 0, "invalid frontend height %d limited to %d\n",
		      height, max_height);
	height = max_height;
    }
    xenfb->fb_len = fb_len;
    xenfb->row_stride = row_stride;
    xenfb->depth = depth;
    xenfb->width = width;
    xenfb->height = height;
    xenfb->offset = offset;
    xenfb->up_fullscreen = 1;
    xenfb->do_resize = 1;
    xen_be_printf(&xenfb->c.xendev, 1, "framebuffer %dx%dx%d offset %d stride %d\n",
		  width, height, depth, offset, row_stride);
    return 0;
}

/* A convenient function for munging pixels between different depths */
#define BLT(SRC_T,DST_T,RSB,GSB,BSB,RDB,GDB,BDB)                        \
    for (line = y ; line < (y+h) ; line++) {				\
	SRC_T *src = (SRC_T *)(xenfb->pixels				\
			       + xenfb->offset				\
			       + (line * xenfb->row_stride)		\
			       + (x * xenfb->depth / 8));		\
	DST_T *dst = (DST_T *)(data					\
			       + (line * linesize)			\
			       + (x * bpp / 8));			\
	int col;							\
	const int RSS = 32 - (RSB + GSB + BSB);				\
	const int GSS = 32 - (GSB + BSB);				\
	const int BSS = 32 - (BSB);					\
	const uint32_t RSM = (~0U) << (32 - RSB);			\
	const uint32_t GSM = (~0U) << (32 - GSB);			\
	const uint32_t BSM = (~0U) << (32 - BSB);			\
	const int RDS = 32 - (RDB + GDB + BDB);				\
	const int GDS = 32 - (GDB + BDB);				\
	const int BDS = 32 - (BDB);					\
	const uint32_t RDM = (~0U) << (32 - RDB);			\
	const uint32_t GDM = (~0U) << (32 - GDB);			\
	const uint32_t BDM = (~0U) << (32 - BDB);			\
	for (col = x ; col < (x+w) ; col++) {				\
	    uint32_t spix = *src;					\
	    *dst = (((spix << RSS) & RSM & RDM) >> RDS) |		\
		(((spix << GSS) & GSM & GDM) >> GDS) |			\
		(((spix << BSS) & BSM & BDM) >> BDS);			\
	    src = (SRC_T *) ((unsigned long) src + xenfb->depth / 8);	\
	    dst = (DST_T *) ((unsigned long) dst + bpp / 8);		\
	}								\
    }


/*
 * This copies data from the guest framebuffer region, into QEMU's
 * displaysurface. qemu uses 16 or 32 bpp.  In case the pv framebuffer
 * uses something else we must convert and copy, otherwise we can
 * supply the buffer directly and no thing here.
 */
static void xenfb_guest_copy(struct XenFB *xenfb, int x, int y, int w, int h)
{
    int line, oops = 0;
    int bpp = ds_get_bits_per_pixel(xenfb->c.ds);
    int linesize = ds_get_linesize(xenfb->c.ds);
    uint8_t *data = ds_get_data(xenfb->c.ds);

    if (!is_buffer_shared(xenfb->c.ds->surface)) {
        switch (xenfb->depth) {
        case 8:
            if (bpp == 16) {
                BLT(uint8_t, uint16_t,   3, 3, 2,   5, 6, 5);
            } else if (bpp == 32) {
                BLT(uint8_t, uint32_t,   3, 3, 2,   8, 8, 8);
            } else {
                oops = 1;
            }
            break;
        case 24:
            if (bpp == 16) {
                BLT(uint32_t, uint16_t,  8, 8, 8,   5, 6, 5);
            } else if (bpp == 32) {
                BLT(uint32_t, uint32_t,  8, 8, 8,   8, 8, 8);
            } else {
                oops = 1;
            }
            break;
        default:
            oops = 1;
	}
    }
    if (oops) /* should not happen */
        xen_be_printf(&xenfb->c.xendev, 0, "%s: oops: convert %d -> %d bpp?\n",
                      __FUNCTION__, xenfb->depth, bpp);

    dpy_gfx_update(xenfb->c.ds, x, y, w, h);
}

#ifdef XENFB_TYPE_REFRESH_PERIOD
static int xenfb_queue_full(struct XenFB *xenfb)
{
    struct xenfb_page *page = xenfb->c.page;
    uint32_t cons, prod;

    if (!page)
        return 1;

    prod = page->in_prod;
    cons = page->in_cons;
    return prod - cons == XENFB_IN_RING_LEN;
}

static void xenfb_send_event(struct XenFB *xenfb, union xenfb_in_event *event)
{
    uint32_t prod;
    struct xenfb_page *page = xenfb->c.page;

    prod = page->in_prod;
    /* caller ensures !xenfb_queue_full() */
    xen_mb();                   /* ensure ring space available */
    XENFB_IN_RING_REF(page, prod) = *event;
    xen_wmb();                  /* ensure ring contents visible */
    page->in_prod = prod + 1;

    xen_be_send_notify(&xenfb->c.xendev);
}

static void xenfb_send_refresh_period(struct XenFB *xenfb, int period)
{
    union xenfb_in_event event;

    memset(&event, 0, sizeof(event));
    event.type = XENFB_TYPE_REFRESH_PERIOD;
    event.refresh_period.period = period;
    xenfb_send_event(xenfb, &event);
}
#endif

/*
 * Periodic update of display.
 * Also transmit the refresh interval to the frontend.
 *
 * Never ever do any qemu display operations
 * (resize, screen update) outside this function.
 * Our screen might be inactive.  When asked for
 * an update we know it is active.
 */
static void xenfb_update(void *opaque)
{
    struct XenFB *xenfb = opaque;
    int i;

    if (xenfb->c.xendev.be_state != XenbusStateConnected)
        return;

    if (xenfb->feature_update) {
#ifdef XENFB_TYPE_REFRESH_PERIOD
        struct DisplayChangeListener *l;
        int period = 99999999;
        int idle = 1;

	if (xenfb_queue_full(xenfb))
	    return;

        QLIST_FOREACH(l, &xenfb->c.ds->listeners, next) {
            if (l->idle)
                continue;
            idle = 0;
            if (!l->gui_timer_interval) {
                if (period > GUI_REFRESH_INTERVAL)
                    period = GUI_REFRESH_INTERVAL;
            } else {
                if (period > l->gui_timer_interval)
                    period = l->gui_timer_interval;
            }
        }
        if (idle)
	    period = XENFB_NO_REFRESH;

	if (xenfb->refresh_period != period) {
	    xenfb_send_refresh_period(xenfb, period);
	    xenfb->refresh_period = period;
            xen_be_printf(&xenfb->c.xendev, 1, "refresh period: %d\n", period);
	}
#else
	; /* nothing */
#endif
    } else {
	/* we don't get update notifications, thus use the
	 * sledge hammer approach ... */
	xenfb->up_fullscreen = 1;
    }

    /* resize if needed */
    if (xenfb->do_resize) {
        xenfb->do_resize = 0;
        switch (xenfb->depth) {
        case 16:
        case 32:
            /* console.c supported depth -> buffer can be used directly */
            qemu_free_displaysurface(xenfb->c.ds);
            xenfb->c.ds->surface = qemu_create_displaysurface_from
                (xenfb->width, xenfb->height, xenfb->depth,
                 xenfb->row_stride, xenfb->pixels + xenfb->offset);
            break;
        default:
            /* we must convert stuff */
            qemu_resize_displaysurface(xenfb->c.ds, xenfb->width, xenfb->height);
            break;
        }
        xen_be_printf(&xenfb->c.xendev, 1, "update: resizing: %dx%d @ %d bpp%s\n",
                      xenfb->width, xenfb->height, xenfb->depth,
                      is_buffer_shared(xenfb->c.ds->surface) ? " (shared)" : "");
        dpy_gfx_resize(xenfb->c.ds);
        xenfb->up_fullscreen = 1;
    }

    /* run queued updates */
    if (xenfb->up_fullscreen) {
	xen_be_printf(&xenfb->c.xendev, 3, "update: fullscreen\n");
	xenfb_guest_copy(xenfb, 0, 0, xenfb->width, xenfb->height);
    } else if (xenfb->up_count) {
	xen_be_printf(&xenfb->c.xendev, 3, "update: %d rects\n", xenfb->up_count);
	for (i = 0; i < xenfb->up_count; i++)
	    xenfb_guest_copy(xenfb,
			     xenfb->up_rects[i].x,
			     xenfb->up_rects[i].y,
			     xenfb->up_rects[i].w,
			     xenfb->up_rects[i].h);
    } else {
	xen_be_printf(&xenfb->c.xendev, 3, "update: nothing\n");
    }
    xenfb->up_count = 0;
    xenfb->up_fullscreen = 0;
}

/* QEMU display state changed, so refresh the framebuffer copy */
static void xenfb_invalidate(void *opaque)
{
    struct XenFB *xenfb = opaque;
    xenfb->up_fullscreen = 1;
}

static void xenfb_handle_events(struct XenFB *xenfb)
{
    uint32_t prod, cons;
    struct xenfb_page *page = xenfb->c.page;

    prod = page->out_prod;
    if (prod == page->out_cons)
	return;
    xen_rmb();		/* ensure we see ring contents up to prod */
    for (cons = page->out_cons; cons != prod; cons++) {
	union xenfb_out_event *event = &XENFB_OUT_RING_REF(page, cons);
	int x, y, w, h;

	switch (event->type) {
	case XENFB_TYPE_UPDATE:
	    if (xenfb->up_count == UP_QUEUE)
		xenfb->up_fullscreen = 1;
	    if (xenfb->up_fullscreen)
		break;
	    x = MAX(event->update.x, 0);
	    y = MAX(event->update.y, 0);
	    w = MIN(event->update.width, xenfb->width - x);
	    h = MIN(event->update.height, xenfb->height - y);
	    if (w < 0 || h < 0) {
                xen_be_printf(&xenfb->c.xendev, 1, "bogus update ignored\n");
		break;
	    }
	    if (x != event->update.x ||
                y != event->update.y ||
		w != event->update.width ||
		h != event->update.height) {
                xen_be_printf(&xenfb->c.xendev, 1, "bogus update clipped\n");
	    }
	    if (w == xenfb->width && h > xenfb->height / 2) {
		/* scroll detector: updated more than 50% of the lines,
		 * don't bother keeping track of the rectangles then */
		xenfb->up_fullscreen = 1;
	    } else {
		xenfb->up_rects[xenfb->up_count].x = x;
		xenfb->up_rects[xenfb->up_count].y = y;
		xenfb->up_rects[xenfb->up_count].w = w;
		xenfb->up_rects[xenfb->up_count].h = h;
		xenfb->up_count++;
	    }
	    break;
#ifdef XENFB_TYPE_RESIZE
	case XENFB_TYPE_RESIZE:
	    if (xenfb_configure_fb(xenfb, xenfb->fb_len,
				   event->resize.width,
				   event->resize.height,
				   event->resize.depth,
				   xenfb->fb_len,
				   event->resize.offset,
				   event->resize.stride) < 0)
		break;
	    xenfb_invalidate(xenfb);
	    break;
#endif
	}
    }
    xen_mb();		/* ensure we're done with ring contents */
    page->out_cons = cons;
}

static int fb_init(struct XenDevice *xendev)
{
    struct XenFB *fb = container_of(xendev, struct XenFB, c.xendev);

    fb->refresh_period = -1;

#ifdef XENFB_TYPE_RESIZE
    xenstore_write_be_int(xendev, "feature-resize", 1);
#endif
    return 0;
}

static int fb_initialise(struct XenDevice *xendev)
{
    struct XenFB *fb = container_of(xendev, struct XenFB, c.xendev);
    struct xenfb_page *fb_page;
    int videoram;
    int rc;

    if (xenstore_read_fe_int(xendev, "videoram", &videoram) == -1)
	videoram = 0;

    rc = common_bind(&fb->c);
    if (rc != 0)
	return rc;

    fb_page = fb->c.page;
    rc = xenfb_configure_fb(fb, videoram * 1024 * 1024U,
			    fb_page->width, fb_page->height, fb_page->depth,
			    fb_page->mem_length, 0, fb_page->line_length);
    if (rc != 0)
	return rc;

    rc = xenfb_map_fb(fb);
    if (rc != 0)
	return rc;

#if 0  /* handled in xen_init_display() for now */
    if (!fb->have_console) {
        fb->c.ds = graphic_console_init(xenfb_update,
                                        xenfb_invalidate,
                                        NULL,
                                        NULL,
                                        fb);
        fb->have_console = 1;
    }
#endif

    if (xenstore_read_fe_int(xendev, "feature-update", &fb->feature_update) == -1)
	fb->feature_update = 0;
    if (fb->feature_update)
	xenstore_write_be_int(xendev, "request-update", 1);

    xen_be_printf(xendev, 1, "feature-update=%d, videoram=%d\n",
		  fb->feature_update, videoram);
    return 0;
}

static void fb_disconnect(struct XenDevice *xendev)
{
    struct XenFB *fb = container_of(xendev, struct XenFB, c.xendev);

    /*
     * FIXME: qemu can't un-init gfx display (yet?).
     *   Replacing the framebuffer with anonymous shared memory
     *   instead.  This releases the guest pages and keeps qemu happy.
     */
    fb->pixels = mmap(fb->pixels, fb->fbpages * XC_PAGE_SIZE,
                      PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANON,
                      -1, 0);
    common_unbind(&fb->c);
    fb->feature_update = 0;
    fb->bug_trigger    = 0;
}

static void fb_frontend_changed(struct XenDevice *xendev, const char *node)
{
    struct XenFB *fb = container_of(xendev, struct XenFB, c.xendev);

    /*
     * Set state to Connected *again* once the frontend switched
     * to connected.  We must trigger the watch a second time to
     * workaround a frontend bug.
     */
    if (fb->bug_trigger == 0 && strcmp(node, "state") == 0 &&
        xendev->fe_state == XenbusStateConnected &&
        xendev->be_state == XenbusStateConnected) {
        xen_be_printf(xendev, 2, "re-trigger connected (frontend bug)\n");
        xen_be_set_state(xendev, XenbusStateConnected);
        fb->bug_trigger = 1; /* only once */
    }
}

static void fb_event(struct XenDevice *xendev)
{
    struct XenFB *xenfb = container_of(xendev, struct XenFB, c.xendev);

    xenfb_handle_events(xenfb);
    xen_be_send_notify(&xenfb->c.xendev);
}

/* -------------------------------------------------------------------- */

struct XenDevOps xen_kbdmouse_ops = {
    .size       = sizeof(struct XenInput),
    .init       = input_init,
    .initialise = input_initialise,
    .connected  = input_connected,
    .disconnect = input_disconnect,
    .event      = input_event,
};

struct XenDevOps xen_framebuffer_ops = {
    .size       = sizeof(struct XenFB),
    .init       = fb_init,
    .initialise = fb_initialise,
    .disconnect = fb_disconnect,
    .event      = fb_event,
    .frontend_changed = fb_frontend_changed,
};

/*
 * FIXME/TODO: Kill this.
 * Temporary needed while DisplayState reorganization is in flight.
 */
void xen_init_display(int domid)
{
    struct XenDevice *xfb, *xin;
    struct XenFB *fb;
    struct XenInput *in;
    int i = 0;

wait_more:
    i++;
    main_loop_wait(true);
    xfb = xen_be_find_xendev("vfb", domid, 0);
    xin = xen_be_find_xendev("vkbd", domid, 0);
    if (!xfb || !xin) {
        if (i < 256) {
            usleep(10000);
            goto wait_more;
        }
        xen_be_printf(NULL, 1, "displaystate setup failed\n");
        return;
    }

    /* vfb */
    fb = container_of(xfb, struct XenFB, c.xendev);
    fb->c.ds = graphic_console_init(xenfb_update,
                                    xenfb_invalidate,
                                    NULL,
                                    NULL,
                                    fb);
    fb->have_console = 1;

    /* vkbd */
    in = container_of(xin, struct XenInput, c.xendev);
    in->c.ds = fb->c.ds;

    /* retry ->init() */
    xen_be_check_state(xin);
    xen_be_check_state(xfb);
}
