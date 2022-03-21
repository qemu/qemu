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

#include "qemu/osdep.h"
#include "qemu/units.h"

#include "ui/input.h"
#include "ui/console.h"
#include "hw/xen/xen-legacy-backend.h"

#include "hw/xen/interface/io/fbif.h"
#include "hw/xen/interface/io/kbdif.h"
#include "hw/xen/interface/io/protocols.h"

#include "trace.h"

#ifndef BTN_LEFT
#define BTN_LEFT 0x110 /* from <linux/input.h> */
#endif

/* -------------------------------------------------------------------- */

struct common {
    struct XenLegacyDevice  xendev;  /* must be first */
    void              *page;
};

struct XenInput {
    struct common c;
    int abs_pointer_wanted; /* Whether guest supports absolute pointer */
    int raw_pointer_wanted; /* Whether guest supports raw (unscaled) pointer */
    QemuInputHandlerState *qkbd;
    QemuInputHandlerState *qmou;
    int axis[INPUT_AXIS__MAX];
    int wheel;
};

#define UP_QUEUE 8

struct XenFB {
    struct common     c;
    QemuConsole       *con;
    size_t            fb_len;
    int               row_stride;
    int               depth;
    int               width;
    int               height;
    int               offset;
    void              *pixels;
    int               fbpages;
    int               feature_update;
    int               bug_trigger;
    int               do_resize;

    struct {
	int x,y,w,h;
    } up_rects[UP_QUEUE];
    int               up_count;
    int               up_fullscreen;
};
static const GraphicHwOps xenfb_ops;

/* -------------------------------------------------------------------- */

static int common_bind(struct common *c)
{
    uint64_t val;
    xen_pfn_t mfn;

    if (xenstore_read_fe_uint64(&c->xendev, "page-ref", &val) == -1)
        return -1;
    mfn = (xen_pfn_t)val;
    assert(val == mfn);

    if (xenstore_read_fe_int(&c->xendev, "event-channel", &c->xendev.remote_port) == -1)
        return -1;

    c->page = xenforeignmemory_map(xen_fmem, c->xendev.dom,
                                   PROT_READ | PROT_WRITE, 1, &mfn, NULL);
    if (c->page == NULL)
        return -1;

    xen_be_bind_evtchn(&c->xendev);
    xen_pv_printf(&c->xendev, 1,
                  "ring mfn %"PRI_xen_pfn", remote-port %d, local-port %d\n",
                  mfn, c->xendev.remote_port, c->xendev.local_port);

    return 0;
}

static void common_unbind(struct common *c)
{
    xen_pv_unbind_evtchn(&c->xendev);
    if (c->page) {
        xenforeignmemory_unmap(xen_fmem, c->page, 1);
	c->page = NULL;
    }
}

/* -------------------------------------------------------------------- */
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
    return xen_pv_send_notify(&xenfb->c.xendev);
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
    event.motion.rel_z = rel_z;

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
    event.pos.rel_z = z;

    return xenfb_kbd_event(xenfb, &event);
}

/*
 * Send a key event from the client to the guest OS
 * QEMU gives us a QCode.
 * We have to turn this into a Linux Input layer keycode.
 *
 * Wish we could just send scancodes straight to the guest which
 * already has code for dealing with this...
 */
static void xenfb_key_event(DeviceState *dev, QemuConsole *src,
                            InputEvent *evt)
{
    struct XenInput *xenfb = (struct XenInput *)dev;
    InputKeyEvent *key = evt->u.key.data;
    int qcode = qemu_input_key_value_to_qcode(key->key);
    int lnx;

    if (qcode < qemu_input_map_qcode_to_linux_len) {
        lnx = qemu_input_map_qcode_to_linux[qcode];

        if (lnx) {
            trace_xenfb_key_event(xenfb, lnx, key->down);
            xenfb_send_key(xenfb, key->down, lnx);
        }
    }
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
static void xenfb_mouse_event(DeviceState *dev, QemuConsole *src,
                              InputEvent *evt)
{
    struct XenInput *xenfb = (struct XenInput *)dev;
    InputBtnEvent *btn;
    InputMoveEvent *move;
    QemuConsole *con;
    DisplaySurface *surface;
    int scale;

    switch (evt->type) {
    case INPUT_EVENT_KIND_BTN:
        btn = evt->u.btn.data;
        switch (btn->button) {
        case INPUT_BUTTON_LEFT:
            xenfb_send_key(xenfb, btn->down, BTN_LEFT);
            break;
        case INPUT_BUTTON_RIGHT:
            xenfb_send_key(xenfb, btn->down, BTN_LEFT + 1);
            break;
        case INPUT_BUTTON_MIDDLE:
            xenfb_send_key(xenfb, btn->down, BTN_LEFT + 2);
            break;
        case INPUT_BUTTON_WHEEL_UP:
            if (btn->down) {
                xenfb->wheel--;
            }
            break;
        case INPUT_BUTTON_WHEEL_DOWN:
            if (btn->down) {
                xenfb->wheel++;
            }
            break;
        default:
            break;
        }
        break;

    case INPUT_EVENT_KIND_ABS:
        move = evt->u.abs.data;
        if (xenfb->raw_pointer_wanted) {
            xenfb->axis[move->axis] = move->value;
        } else {
            con = qemu_console_lookup_by_index(0);
            if (!con) {
                xen_pv_printf(&xenfb->c.xendev, 0, "No QEMU console available");
                return;
            }
            surface = qemu_console_surface(con);
            switch (move->axis) {
            case INPUT_AXIS_X:
                scale = surface_width(surface) - 1;
                break;
            case INPUT_AXIS_Y:
                scale = surface_height(surface) - 1;
                break;
            default:
                scale = 0x8000;
                break;
            }
            xenfb->axis[move->axis] = move->value * scale / 0x7fff;
        }
        break;

    case INPUT_EVENT_KIND_REL:
        move = evt->u.rel.data;
        xenfb->axis[move->axis] += move->value;
        break;

    default:
        break;
    }
}

static void xenfb_mouse_sync(DeviceState *dev)
{
    struct XenInput *xenfb = (struct XenInput *)dev;

    trace_xenfb_mouse_event(xenfb, xenfb->axis[INPUT_AXIS_X],
                            xenfb->axis[INPUT_AXIS_Y],
                            xenfb->wheel, 0,
                            xenfb->abs_pointer_wanted);
    if (xenfb->abs_pointer_wanted) {
        xenfb_send_position(xenfb, xenfb->axis[INPUT_AXIS_X],
                            xenfb->axis[INPUT_AXIS_Y],
                            xenfb->wheel);
    } else {
        xenfb_send_motion(xenfb, xenfb->axis[INPUT_AXIS_X],
                          xenfb->axis[INPUT_AXIS_Y],
                          xenfb->wheel);
        xenfb->axis[INPUT_AXIS_X] = 0;
        xenfb->axis[INPUT_AXIS_Y] = 0;
    }
    xenfb->wheel = 0;
}

static QemuInputHandler xenfb_keyboard = {
    .name  = "Xen PV Keyboard",
    .mask  = INPUT_EVENT_MASK_KEY,
    .event = xenfb_key_event,
};

static QemuInputHandler xenfb_abs_mouse = {
    .name  = "Xen PV Mouse",
    .mask  = INPUT_EVENT_MASK_BTN | INPUT_EVENT_MASK_ABS,
    .event = xenfb_mouse_event,
    .sync  = xenfb_mouse_sync,
};

static QemuInputHandler xenfb_rel_mouse = {
    .name  = "Xen PV Mouse",
    .mask  = INPUT_EVENT_MASK_BTN | INPUT_EVENT_MASK_REL,
    .event = xenfb_mouse_event,
    .sync  = xenfb_mouse_sync,
};

static int input_init(struct XenLegacyDevice *xendev)
{
    xenstore_write_be_int(xendev, "feature-abs-pointer", 1);
    xenstore_write_be_int(xendev, "feature-raw-pointer", 1);
    return 0;
}

static int input_initialise(struct XenLegacyDevice *xendev)
{
    struct XenInput *in = container_of(xendev, struct XenInput, c.xendev);
    int rc;

    rc = common_bind(&in->c);
    if (rc != 0)
	return rc;

    return 0;
}

static void input_connected(struct XenLegacyDevice *xendev)
{
    struct XenInput *in = container_of(xendev, struct XenInput, c.xendev);

    if (xenstore_read_fe_int(xendev, "request-abs-pointer",
                             &in->abs_pointer_wanted) == -1) {
        in->abs_pointer_wanted = 0;
    }
    if (xenstore_read_fe_int(xendev, "request-raw-pointer",
                             &in->raw_pointer_wanted) == -1) {
        in->raw_pointer_wanted = 0;
    }
    if (in->raw_pointer_wanted && in->abs_pointer_wanted == 0) {
        xen_pv_printf(xendev, 0, "raw pointer set without abs pointer");
    }

    if (in->qkbd) {
        qemu_input_handler_unregister(in->qkbd);
    }
    if (in->qmou) {
        qemu_input_handler_unregister(in->qmou);
    }
    trace_xenfb_input_connected(xendev, in->abs_pointer_wanted);

    in->qkbd = qemu_input_handler_register((DeviceState *)in, &xenfb_keyboard);
    in->qmou = qemu_input_handler_register((DeviceState *)in,
               in->abs_pointer_wanted ? &xenfb_abs_mouse : &xenfb_rel_mouse);

    if (in->raw_pointer_wanted) {
        qemu_input_handler_activate(in->qkbd);
        qemu_input_handler_activate(in->qmou);
    }
}

static void input_disconnect(struct XenLegacyDevice *xendev)
{
    struct XenInput *in = container_of(xendev, struct XenInput, c.xendev);

    if (in->qkbd) {
        qemu_input_handler_unregister(in->qkbd);
        in->qkbd = NULL;
    }
    if (in->qmou) {
        qemu_input_handler_unregister(in->qmou);
        in->qmou = NULL;
    }
    common_unbind(&in->c);
}

static void input_event(struct XenLegacyDevice *xendev)
{
    struct XenInput *xenfb = container_of(xendev, struct XenInput, c.xendev);
    struct xenkbd_page *page = xenfb->c.page;

    /* We don't understand any keyboard events, so just ignore them. */
    if (page->out_prod == page->out_cons)
	return;
    page->out_cons = page->out_prod;
    xen_pv_send_notify(&xenfb->c.xendev);
}

/* -------------------------------------------------------------------- */

static void xenfb_copy_mfns(int mode, int count, xen_pfn_t *dst, void *src)
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
    xen_pfn_t *pgmfns = NULL;
    xen_pfn_t *fbmfns = NULL;
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

    xenfb->fbpages = DIV_ROUND_UP(xenfb->fb_len, XC_PAGE_SIZE);
    n_fbdirs = xenfb->fbpages * mode / 8;
    n_fbdirs = DIV_ROUND_UP(n_fbdirs, XC_PAGE_SIZE);

    pgmfns = g_new0(xen_pfn_t, n_fbdirs);
    fbmfns = g_new0(xen_pfn_t, xenfb->fbpages);

    xenfb_copy_mfns(mode, n_fbdirs, pgmfns, pd);
    map = xenforeignmemory_map(xen_fmem, xenfb->c.xendev.dom,
                               PROT_READ, n_fbdirs, pgmfns, NULL);
    if (map == NULL)
	goto out;
    xenfb_copy_mfns(mode, xenfb->fbpages, fbmfns, map);
    xenforeignmemory_unmap(xen_fmem, map, n_fbdirs);

    xenfb->pixels = xenforeignmemory_map(xen_fmem, xenfb->c.xendev.dom,
            PROT_READ, xenfb->fbpages, fbmfns, NULL);
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
    size_t mfn_sz = sizeof_field(struct xenfb_page, pd[0]);
    size_t pd_len = sizeof_field(struct xenfb_page, pd) / mfn_sz;
    size_t fb_pages = pd_len * XC_PAGE_SIZE / mfn_sz;
    size_t fb_len_max = fb_pages * XC_PAGE_SIZE;
    int max_width, max_height;

    if (fb_len_lim > fb_len_max) {
        xen_pv_printf(&xenfb->c.xendev, 0,
                      "fb size limit %zu exceeds %zu, corrected\n",
                      fb_len_lim, fb_len_max);
        fb_len_lim = fb_len_max;
    }
    if (fb_len_lim && fb_len > fb_len_lim) {
        xen_pv_printf(&xenfb->c.xendev, 0,
                      "frontend fb size %zu limited to %zu\n",
                      fb_len, fb_len_lim);
        fb_len = fb_len_lim;
    }
    if (depth != 8 && depth != 16 && depth != 24 && depth != 32) {
        xen_pv_printf(&xenfb->c.xendev, 0,
                      "can't handle frontend fb depth %d\n",
                      depth);
        return -1;
    }
    if (row_stride <= 0 || row_stride > fb_len) {
        xen_pv_printf(&xenfb->c.xendev, 0, "invalid frontend stride %d\n",
                      row_stride);
        return -1;
    }
    max_width = row_stride / (depth / 8);
    if (width < 0 || width > max_width) {
        xen_pv_printf(&xenfb->c.xendev, 0,
                      "invalid frontend width %d limited to %d\n",
                      width, max_width);
        width = max_width;
    }
    if (offset < 0 || offset >= fb_len) {
        xen_pv_printf(&xenfb->c.xendev, 0,
                      "invalid frontend offset %d (max %zu)\n",
                      offset, fb_len - 1);
        return -1;
    }
    max_height = (fb_len - offset) / row_stride;
    if (height < 0 || height > max_height) {
        xen_pv_printf(&xenfb->c.xendev, 0,
                      "invalid frontend height %d limited to %d\n",
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
    xen_pv_printf(&xenfb->c.xendev, 1,
                  "framebuffer %dx%dx%d offset %d stride %d\n",
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
    DisplaySurface *surface = qemu_console_surface(xenfb->con);
    int line, oops = 0;
    int bpp = surface_bits_per_pixel(surface);
    int linesize = surface_stride(surface);
    uint8_t *data = surface_data(surface);

    if (!is_buffer_shared(surface)) {
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
        xen_pv_printf(&xenfb->c.xendev, 0, "%s: oops: convert %d -> %d bpp?\n",
                      __func__, xenfb->depth, bpp);

    dpy_gfx_update(xenfb->con, x, y, w, h);
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

    xen_pv_send_notify(&xenfb->c.xendev);
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
    DisplaySurface *surface;
    int i;

    if (xenfb->c.xendev.be_state != XenbusStateConnected)
        return;

    if (!xenfb->feature_update) {
        /* we don't get update notifications, thus use the
         * sledge hammer approach ... */
        xenfb->up_fullscreen = 1;
    }

    /* resize if needed */
    if (xenfb->do_resize) {
        pixman_format_code_t format;

        xenfb->do_resize = 0;
        switch (xenfb->depth) {
        case 16:
        case 32:
            /* console.c supported depth -> buffer can be used directly */
            format = qemu_default_pixman_format(xenfb->depth, true);
            surface = qemu_create_displaysurface_from
                (xenfb->width, xenfb->height, format,
                 xenfb->row_stride, xenfb->pixels + xenfb->offset);
            break;
        default:
            /* we must convert stuff */
            surface = qemu_create_displaysurface(xenfb->width, xenfb->height);
            break;
        }
        dpy_gfx_replace_surface(xenfb->con, surface);
        xen_pv_printf(&xenfb->c.xendev, 1,
                      "update: resizing: %dx%d @ %d bpp%s\n",
                      xenfb->width, xenfb->height, xenfb->depth,
                      is_buffer_shared(surface) ? " (shared)" : "");
        xenfb->up_fullscreen = 1;
    }

    /* run queued updates */
    if (xenfb->up_fullscreen) {
        xen_pv_printf(&xenfb->c.xendev, 3, "update: fullscreen\n");
        xenfb_guest_copy(xenfb, 0, 0, xenfb->width, xenfb->height);
    } else if (xenfb->up_count) {
        xen_pv_printf(&xenfb->c.xendev, 3, "update: %d rects\n",
                      xenfb->up_count);
        for (i = 0; i < xenfb->up_count; i++)
            xenfb_guest_copy(xenfb,
                             xenfb->up_rects[i].x,
                             xenfb->up_rects[i].y,
                             xenfb->up_rects[i].w,
                             xenfb->up_rects[i].h);
    } else {
        xen_pv_printf(&xenfb->c.xendev, 3, "update: nothing\n");
    }
    xenfb->up_count = 0;
    xenfb->up_fullscreen = 0;
}

static void xenfb_update_interval(void *opaque, uint64_t interval)
{
    struct XenFB *xenfb = opaque;

    if (xenfb->feature_update) {
#ifdef XENFB_TYPE_REFRESH_PERIOD
        if (xenfb_queue_full(xenfb)) {
            return;
        }
        xenfb_send_refresh_period(xenfb, interval);
#endif
    }
}

/* QEMU display state changed, so refresh the framebuffer copy */
static void xenfb_invalidate(void *opaque)
{
    struct XenFB *xenfb = opaque;
    xenfb->up_fullscreen = 1;
}

static void xenfb_handle_events(struct XenFB *xenfb)
{
    uint32_t prod, cons, out_cons;
    struct xenfb_page *page = xenfb->c.page;

    prod = page->out_prod;
    out_cons = page->out_cons;
    if (prod - out_cons > XENFB_OUT_RING_LEN) {
        return;
    }
    xen_rmb();		/* ensure we see ring contents up to prod */
    for (cons = out_cons; cons != prod; cons++) {
	union xenfb_out_event *event = &XENFB_OUT_RING_REF(page, cons);
        uint8_t type = event->type;
	int x, y, w, h;

	switch (type) {
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
                xen_pv_printf(&xenfb->c.xendev, 1, "bogus update ignored\n");
		break;
	    }
	    if (x != event->update.x ||
                y != event->update.y ||
		w != event->update.width ||
		h != event->update.height) {
                xen_pv_printf(&xenfb->c.xendev, 1, "bogus update clipped\n");
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

static int fb_init(struct XenLegacyDevice *xendev)
{
#ifdef XENFB_TYPE_RESIZE
    xenstore_write_be_int(xendev, "feature-resize", 1);
#endif
    return 0;
}

static int fb_initialise(struct XenLegacyDevice *xendev)
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
    rc = xenfb_configure_fb(fb, videoram * MiB,
			    fb_page->width, fb_page->height, fb_page->depth,
			    fb_page->mem_length, 0, fb_page->line_length);
    if (rc != 0)
	return rc;

    rc = xenfb_map_fb(fb);
    if (rc != 0)
	return rc;

    fb->con = graphic_console_init(NULL, 0, &xenfb_ops, fb);

    if (xenstore_read_fe_int(xendev, "feature-update", &fb->feature_update) == -1)
	fb->feature_update = 0;
    if (fb->feature_update)
	xenstore_write_be_int(xendev, "request-update", 1);

    xen_pv_printf(xendev, 1, "feature-update=%d, videoram=%d\n",
		  fb->feature_update, videoram);
    return 0;
}

static void fb_disconnect(struct XenLegacyDevice *xendev)
{
    struct XenFB *fb = container_of(xendev, struct XenFB, c.xendev);

    /*
     * FIXME: qemu can't un-init gfx display (yet?).
     *   Replacing the framebuffer with anonymous shared memory
     *   instead.  This releases the guest pages and keeps qemu happy.
     */
    xenforeignmemory_unmap(xen_fmem, fb->pixels, fb->fbpages);
    fb->pixels = mmap(fb->pixels, fb->fbpages * XC_PAGE_SIZE,
                      PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANON,
                      -1, 0);
    if (fb->pixels == MAP_FAILED) {
        xen_pv_printf(xendev, 0,
                "Couldn't replace the framebuffer with anonymous memory errno=%d\n",
                errno);
    }
    common_unbind(&fb->c);
    fb->feature_update = 0;
    fb->bug_trigger    = 0;
}

static void fb_frontend_changed(struct XenLegacyDevice *xendev,
                                const char *node)
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
        xen_pv_printf(xendev, 2, "re-trigger connected (frontend bug)\n");
        xen_be_set_state(xendev, XenbusStateConnected);
        fb->bug_trigger = 1; /* only once */
    }
}

static void fb_event(struct XenLegacyDevice *xendev)
{
    struct XenFB *xenfb = container_of(xendev, struct XenFB, c.xendev);

    xenfb_handle_events(xenfb);
    xen_pv_send_notify(&xenfb->c.xendev);
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

static const GraphicHwOps xenfb_ops = {
    .invalidate  = xenfb_invalidate,
    .gfx_update  = xenfb_update,
    .update_interval = xenfb_update_interval,
};
