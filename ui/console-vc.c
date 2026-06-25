/*
 * SPDX-License-Identifier: MIT
 * QEMU VC
 */
#include "qemu/osdep.h"

#include "chardev/char.h"
#include "qapi/error.h"
#include "qemu/option.h"
#include "qemu/queue.h"
#include "qom/compat-properties.h"
#include "ui/console.h"
#include "ui/vgafont.h"
#include "ui/vt100.h"

#include "pixman.h"
#include "trace.h"
#include "console-priv.h"

typedef struct QemuTextConsole {
    QemuConsole parent;

    QemuVT100 vt;
    Chardev *chr;
} QemuTextConsole;

typedef QemuConsoleClass QemuTextConsoleClass;

OBJECT_DEFINE_TYPE(QemuTextConsole, qemu_text_console, QEMU_TEXT_CONSOLE, QEMU_CONSOLE)

typedef struct QemuFixedTextConsole {
    QemuTextConsole parent;
} QemuFixedTextConsole;

typedef QemuTextConsoleClass QemuFixedTextConsoleClass;

OBJECT_DEFINE_TYPE(QemuFixedTextConsole, qemu_fixed_text_console, QEMU_FIXED_TEXT_CONSOLE, QEMU_TEXT_CONSOLE)

struct VCChardev {
    Chardev parent;

    ChardevVCEncoding encoding;
    QemuTextConsole *console;
};
typedef struct VCChardev VCChardev;

static char *
qemu_text_console_get_label(const QemuConsole *c)
{
    QemuTextConsole *tc = QEMU_TEXT_CONSOLE(c);

    return tc->chr ? g_strdup(tc->chr->label) : NULL;
}

static void qemu_text_console_out_flush(QemuTextConsole *s)
{
    uint32_t len, avail;

    len = qemu_chr_be_can_write(s->chr);
    avail = fifo8_num_used(&s->vt.out_fifo);
    while (len > 0 && avail > 0) {
        const uint8_t *buf;
        uint32_t size;

        buf = fifo8_pop_bufptr(&s->vt.out_fifo, MIN(len, avail), &size);
        qemu_chr_be_write(s->chr, buf, size);
        len = qemu_chr_be_can_write(s->chr);
        avail -= size;
    }
}

/* called when an ascii key is pressed */
void qemu_text_console_handle_keysym(QemuTextConsole *s, int keysym)
{
    vt100_keysym(&s->vt, keysym);
}

static void text_console_update(void *opaque, uint32_t *chardata)
{
    QemuTextConsole *s = QEMU_TEXT_CONSOLE(opaque);
    int i, j, src;

    if (s->vt.text_x[0] <= s->vt.text_x[1]) {
        src = (s->vt.y_base + s->vt.text_y[0]) * s->vt.width;
        chardata += s->vt.text_y[0] * s->vt.width;
        for (i = s->vt.text_y[0]; i <= s->vt.text_y[1]; i ++)
            for (j = 0; j < s->vt.width; j++, src++) {
                *chardata++ = ATTR2CHTYPE(s->vt.cells[src].ch,
                                          s->vt.cells[src].t_attrib.fgcol,
                                          s->vt.cells[src].t_attrib.bgcol,
                                          s->vt.cells[src].t_attrib.bold);
            }
        qemu_console_text_update(QEMU_CONSOLE(s), s->vt.text_x[0], s->vt.text_y[0],
                                 s->vt.text_x[1] - s->vt.text_x[0], i - s->vt.text_y[0]);
        s->vt.text_x[0] = s->vt.width;
        s->vt.text_y[0] = s->vt.height;
        s->vt.text_x[1] = 0;
        s->vt.text_y[1] = 0;
    }
    if (s->vt.cursor_invalidate) {
        qemu_console_text_set_cursor(QEMU_CONSOLE(s), s->vt.x, s->vt.y);
        s->vt.cursor_invalidate = 0;
    }
}

#define TYPE_CHARDEV_VC "chardev-vc"
DECLARE_INSTANCE_CHECKER(VCChardev, VC_CHARDEV,
                         TYPE_CHARDEV_VC)

static int vc_chr_write(Chardev *chr, const uint8_t *buf, int len)
{
    VCChardev *drv = VC_CHARDEV(chr);
    QemuTextConsole *s = drv->console;

    return vt100_input(&s->vt, buf, len);
}

static void text_console_invalidate(void *opaque)
{
    QemuTextConsole *s = QEMU_TEXT_CONSOLE(opaque);

    if (!QEMU_IS_FIXED_TEXT_CONSOLE(s)) {
        vt100_set_image(&s->vt, QEMU_CONSOLE(s)->surface->image);
    }
    vt100_refresh(&s->vt);
}

static void
qemu_text_console_finalize(Object *obj)
{
    QemuTextConsole *s = QEMU_TEXT_CONSOLE(obj);

    vt100_fini(&s->vt);
}

static void
qemu_text_console_class_init(ObjectClass *oc, const void *data)
{
    QemuConsoleClass *cc = QEMU_CONSOLE_CLASS(oc);

    cc->get_label = qemu_text_console_get_label;
}

static const GraphicHwOps text_console_ops = {
    .invalidate  = text_console_invalidate,
    .text_update = text_console_update,
};

static void
qemu_text_console_init(Object *obj)
{
    QemuTextConsole *c = QEMU_TEXT_CONSOLE(obj);

    QEMU_CONSOLE(c)->hw_ops = &text_console_ops;
    QEMU_CONSOLE(c)->hw = c;
}

static void
qemu_fixed_text_console_finalize(Object *obj)
{
}

static void
qemu_fixed_text_console_class_init(ObjectClass *oc, const void *data)
{
}

static void
qemu_fixed_text_console_init(Object *obj)
{
}

static void vc_chr_accept_input(Chardev *chr)
{
    VCChardev *drv = VC_CHARDEV(chr);

    qemu_text_console_out_flush(drv->console);
}

static void vc_chr_set_echo(Chardev *chr, bool echo)
{
    VCChardev *drv = VC_CHARDEV(chr);

    drv->console->vt.echo = echo;
}

void qemu_text_console_update_size(QemuTextConsole *c)
{
    qemu_console_text_resize(QEMU_CONSOLE(c), c->vt.width, c->vt.height);
}

static void text_console_image_update(QemuVT100 *vt, int x, int y, int width, int height)
{
    QemuTextConsole *console = container_of(vt, QemuTextConsole, vt);

    qemu_console_update(QEMU_CONSOLE(console), x, y, width, height);
}

static void text_console_out_flush(QemuVT100 *vt)
{
    QemuTextConsole *console = container_of(vt, QemuTextConsole, vt);

    qemu_text_console_out_flush(console);
}

static bool vc_chr_open(Chardev *chr, ChardevBackend *backend, Error **errp)
{
    ChardevVC *vc = backend->u.vc.data;
    VCChardev *drv = VC_CHARDEV(chr);
    QemuTextConsole *s;
    unsigned width = 0;
    unsigned height = 0;

    if (vc->has_width) {
        width = vc->width;
    } else if (vc->has_cols) {
        width = vc->cols * FONT_WIDTH;
    }

    if (vc->has_height) {
        height = vc->height;
    } else if (vc->has_rows) {
        height = vc->rows * FONT_HEIGHT;
    }

    trace_console_txt_new(width, height);
    if (width == 0 || height == 0) {
        s = QEMU_TEXT_CONSOLE(object_new(TYPE_QEMU_TEXT_CONSOLE));
        width = 80 * FONT_WIDTH;
        height = 24 * FONT_HEIGHT;
    } else {
        s = QEMU_TEXT_CONSOLE(object_new(TYPE_QEMU_FIXED_TEXT_CONSOLE));
    }

    qemu_console_set_surface(QEMU_CONSOLE(s), qemu_create_displaysurface(width, height));
    if (vc->has_encoding) {
        drv->encoding = vc->encoding;
    }
    vt100_init(&s->vt, QEMU_CONSOLE(s)->surface->image,
               drv->encoding,
               text_console_image_update,
               text_console_out_flush);

    s->chr = chr;
    drv->console = s;

    if (chr->label) {
        char *msg;

        s->vt.t_attrib.bgcol = QEMU_COLOR_BLUE;
        msg = g_strdup_printf("%s console\r\n", chr->label);
        qemu_chr_write(chr, (uint8_t *)msg, strlen(msg), true);
        g_free(msg);
        s->vt.t_attrib = TEXT_ATTRIBUTES_DEFAULT;
    }

    qemu_chr_be_event(chr, CHR_EVENT_OPENED);
    qemu_console_notify(QEMU_CONSOLE_ADDED, QEMU_CONSOLE(s));
    return true;
}

static void vc_chr_parse(QemuOpts *opts, ChardevBackend *backend, Error **errp)
{
    int val;
    const char *str;
    ChardevVC *vc;

    backend->type = CHARDEV_BACKEND_KIND_VC;
    vc = backend->u.vc.data = g_new0(ChardevVC, 1);
    qemu_chr_parse_common(opts, qapi_ChardevVC_base(vc));

    val = qemu_opt_get_number(opts, "width", 0);
    if (val != 0) {
        vc->has_width = true;
        vc->width = val;
    }

    val = qemu_opt_get_number(opts, "height", 0);
    if (val != 0) {
        vc->has_height = true;
        vc->height = val;
    }

    val = qemu_opt_get_number(opts, "cols", 0);
    if (val != 0) {
        vc->has_cols = true;
        vc->cols = val;
    }

    val = qemu_opt_get_number(opts, "rows", 0);
    if (val != 0) {
        vc->has_rows = true;
        vc->rows = val;
    }

    str = qemu_opt_get(opts, "encoding");
    if (str) {
        int cs = qapi_enum_parse(&ChardevVCEncoding_lookup, str, -1, errp);
        if (cs < 0) {
            return;
        }
        vc->has_encoding = true;
        vc->encoding = cs;
    }
}

CHARDEV_VC_ENCODING_PROPERTY_DEFINE(VC_CHARDEV)

static void char_vc_class_init(ObjectClass *oc, const void *data)
{
    ChardevClass *cc = CHARDEV_CLASS(oc);

    cc->chr_parse = vc_chr_parse;
    cc->chr_open = vc_chr_open;
    cc->chr_write = vc_chr_write;
    cc->chr_accept_input = vc_chr_accept_input;
    cc->chr_set_echo = vc_chr_set_echo;
    cc->supports_size_opts = true;
    cc->supports_encoding_opts = true;

    chardev_vc_add_encoding_prop(oc, get_encoding, set_encoding);
}

static void char_vc_init(Object *obj)
{
    VCChardev *vc = VC_CHARDEV(obj);

    vc->encoding = CHARDEV_VC_ENCODING_UTF8;
}

static void char_vc_finalize(Object *obj)
{
    VCChardev *vc = VC_CHARDEV(obj);
    QemuConsole *con = QEMU_CONSOLE(vc->console);

    if (con) {
        qemu_console_notify(QEMU_CONSOLE_REMOVED, con);
        object_unref(con);
    }
}

static const TypeInfo char_vc_type_info = {
    .name = TYPE_CHARDEV_VC,
    .parent = TYPE_CHARDEV,
    .instance_size = sizeof(VCChardev),
    .instance_init = char_vc_init,
    .instance_post_init = object_apply_compat_props,
    .instance_finalize = char_vc_finalize,
    .class_init = char_vc_class_init,
};

void qemu_console_early_init(void)
{
    /* set the default vc driver */
    if (!object_class_by_name(TYPE_CHARDEV_VC)) {
        type_register_static(&char_vc_type_info);
    }
}
