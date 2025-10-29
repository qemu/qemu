/*
 * QEMU System Emulator
 *
 * Copyright (c) 2003-2008 Fabrice Bellard
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
#include "qemu/module.h"
#include "qemu/option.h"
#include "qemu/bitops.h"
#include "chardev/char.h"
#include "system/block-backend.h"
#include "qapi/qapi-commands-control.h"
#include "chardev-internal.h"

/* MUX driver for serial I/O splitting */

/*
 * Set to false by suspend_mux_open.  Open events are delayed until
 * resume_mux_open.  Usually suspend_mux_open is called before
 * command line processing and resume_mux_open afterwards.
 */
static bool muxes_opened = true;

/* Called with chr_write_lock held.  */
static int mux_chr_write(Chardev *chr, const uint8_t *buf, int len)
{
    MuxChardev *d = MUX_CHARDEV(chr);
    int ret;
    if (!d->timestamps) {
        ret = qemu_chr_fe_write(&d->chr, buf, len);
    } else {
        int i;

        ret = 0;
        for (i = 0; i < len; i++) {
            if (d->linestart) {
                char buf1[64];
                int64_t ti;
                int secs;

                ti = qemu_clock_get_ms(QEMU_CLOCK_REALTIME);
                if (d->timestamps_start == -1) {
                    d->timestamps_start = ti;
                }
                ti -= d->timestamps_start;
                secs = ti / 1000;
                snprintf(buf1, sizeof(buf1),
                         "[%02d:%02d:%02d.%03d] ",
                         secs / 3600,
                         (secs / 60) % 60,
                         secs % 60,
                         (int)(ti % 1000));
                /* XXX this blocks entire thread. Rewrite to use
                 * qemu_chr_fe_write and background I/O callbacks */
                qemu_chr_fe_write_all(&d->chr,
                                      (uint8_t *)buf1, strlen(buf1));
                d->linestart = false;
            }
            ret += qemu_chr_fe_write(&d->chr, buf + i, 1);
            if (buf[i] == '\n') {
                d->linestart = true;
            }
        }
    }
    return ret;
}

static const char * const mux_help[] = {
    "% h    print this help\n\r",
    "% x    exit emulator\n\r",
    "% s    save disk data back to file (if -snapshot)\n\r",
    "% t    toggle console timestamps\n\r",
    "% b    send break (magic sysrq)\n\r",
    "% c    switch between console and monitor\n\r",
    "% %  sends %\n\r",
    NULL
};

int term_escape_char = 0x01; /* ctrl-a is used for escape */
static void mux_print_help(Chardev *chr)
{
    int i, j;
    char ebuf[15] = "Escape-Char";
    char cbuf[50] = "\n\r";

    if (term_escape_char > 0 && term_escape_char < 26) {
        snprintf(cbuf, sizeof(cbuf), "\n\r");
        snprintf(ebuf, sizeof(ebuf), "C-%c", term_escape_char - 1 + 'a');
    } else {
        snprintf(cbuf, sizeof(cbuf),
                 "\n\rEscape-Char set to Ascii: 0x%02x\n\r\n\r",
                 term_escape_char);
    }
    /* XXX this blocks entire thread. Rewrite to use
     * qemu_chr_fe_write and background I/O callbacks */
    qemu_chr_write_all(chr, (uint8_t *)cbuf, strlen(cbuf));
    for (i = 0; mux_help[i] != NULL; i++) {
        for (j = 0; mux_help[i][j] != '\0'; j++) {
            if (mux_help[i][j] == '%') {
                qemu_chr_write_all(chr, (uint8_t *)ebuf, strlen(ebuf));
            } else {
                qemu_chr_write_all(chr, (uint8_t *)&mux_help[i][j], 1);
            }
        }
    }
}

static void mux_chr_send_event(MuxChardev *d, unsigned int mux_nr,
                               QEMUChrEvent event)
{
    CharFrontend *fe = d->frontends[mux_nr];

    if (fe && fe->chr_event) {
        fe->chr_event(fe->opaque, event);
    }
}

static void mux_chr_be_event(Chardev *chr, QEMUChrEvent event)
{
    MuxChardev *d = MUX_CHARDEV(chr);

    if (d->focus != -1) {
        mux_chr_send_event(d, d->focus, event);
    }
}

static int mux_proc_byte(Chardev *chr, MuxChardev *d, int ch)
{
    if (d->term_got_escape) {
        d->term_got_escape = false;
        if (ch == term_escape_char) {
            goto send_char;
        }
        switch (ch) {
        case '?':
        case 'h':
            mux_print_help(chr);
            break;
        case 'x':
            {
                 const char *term =  "QEMU: Terminated\n\r";
                 qemu_chr_write_all(chr, (uint8_t *)term, strlen(term));
                 qmp_quit(NULL);
                 break;
            }
        case 's':
            blk_commit_all();
            break;
        case 'b':
            qemu_chr_be_event(chr, CHR_EVENT_BREAK);
            break;
        case 'c': {
            unsigned int bit;

            /* Handler registered with first fe */
            assert(d->mux_bitset != 0);
            /* Switch to the next registered device */
            bit = find_next_bit(&d->mux_bitset, MAX_MUX, d->focus + 1);
            if (bit >= MAX_MUX) {
                bit = find_next_bit(&d->mux_bitset, MAX_MUX, 0);
            }
            mux_set_focus(chr, bit);
            break;
        } case 't':
            d->timestamps = !d->timestamps;
            d->timestamps_start = -1;
            d->linestart = false;
            break;
        }
    } else if (ch == term_escape_char) {
        d->term_got_escape = true;
    } else {
    send_char:
        return 1;
    }
    return 0;
}

static void mux_chr_accept_input(Chardev *chr)
{
    MuxChardev *d = MUX_CHARDEV(chr);
    int m = d->focus;
    CharFrontend *fe = d->frontends[m];

    while (fe && d->prod[m] != d->cons[m] &&
           fe->chr_can_read && fe->chr_can_read(fe->opaque)) {
        fe->chr_read(fe->opaque,
                     &d->buffer[m][d->cons[m]++ & MUX_BUFFER_MASK], 1);
    }
}

static int mux_chr_can_read(void *opaque)
{
    MuxChardev *d = MUX_CHARDEV(opaque);
    int m = d->focus;
    CharFrontend *fe = d->frontends[m];

    if ((d->prod[m] - d->cons[m]) < MUX_BUFFER_SIZE) {
        return 1;
    }

    if (fe && fe->chr_can_read) {
        return fe->chr_can_read(fe->opaque);
    }

    return 0;
}

static void mux_chr_read(void *opaque, const uint8_t *buf, int size)
{
    Chardev *chr = CHARDEV(opaque);
    MuxChardev *d = MUX_CHARDEV(opaque);
    int m = d->focus;
    CharFrontend *fe = d->frontends[m];
    int i;

    mux_chr_accept_input(opaque);

    for (i = 0; i < size; i++)
        if (mux_proc_byte(chr, d, buf[i])) {
            if (d->prod[m] == d->cons[m] &&
                fe && fe->chr_can_read &&
                fe->chr_can_read(fe->opaque)) {
                fe->chr_read(fe->opaque, &buf[i], 1);
            } else {
                d->buffer[m][d->prod[m]++ & MUX_BUFFER_MASK] = buf[i];
            }
        }
}

void mux_chr_send_all_event(Chardev *chr, QEMUChrEvent event)
{
    MuxChardev *d = MUX_CHARDEV(chr);
    int bit;

    if (!muxes_opened) {
        return;
    }

    /* Send the event to all registered listeners */
    bit = -1;
    while ((bit = find_next_bit(&d->mux_bitset, MAX_MUX, bit + 1)) < MAX_MUX) {
        mux_chr_send_event(d, bit, event);
    }
}

static void mux_chr_event(void *opaque, QEMUChrEvent event)
{
    mux_chr_send_all_event(CHARDEV(opaque), event);
}

static GSource *mux_chr_add_watch(Chardev *s, GIOCondition cond)
{
    MuxChardev *d = MUX_CHARDEV(s);
    Chardev *chr = qemu_chr_fe_get_driver(&d->chr);
    ChardevClass *cc = CHARDEV_GET_CLASS(chr);

    if (!cc->chr_add_watch) {
        return NULL;
    }

    return cc->chr_add_watch(chr, cond);
}

static void char_mux_finalize(Object *obj)
{
    MuxChardev *d = MUX_CHARDEV(obj);
    int bit;

    bit = -1;
    while ((bit = find_next_bit(&d->mux_bitset, MAX_MUX, bit + 1)) < MAX_MUX) {
        CharFrontend *be = d->frontends[bit];
        be->chr = NULL;
        d->frontends[bit] = NULL;
    }
    d->mux_bitset = 0;
    qemu_chr_fe_deinit(&d->chr, false);
}

static void mux_chr_update_read_handlers(Chardev *chr)
{
    MuxChardev *d = MUX_CHARDEV(chr);

    /* Fix up the real driver with mux routines */
    qemu_chr_fe_set_handlers_full(&d->chr,
                                  mux_chr_can_read,
                                  mux_chr_read,
                                  mux_chr_event,
                                  NULL,
                                  chr,
                                  chr->gcontext, true, false);
}

bool mux_chr_attach_frontend(MuxChardev *d, CharFrontend *c,
                             unsigned int *tag, Error **errp)
{
    unsigned int bit;

    QEMU_BUILD_BUG_ON(MAX_MUX > (sizeof(d->mux_bitset) * BITS_PER_BYTE));

    bit = find_next_zero_bit(&d->mux_bitset, MAX_MUX, 0);
    if (bit >= MAX_MUX) {
        error_setg(errp,
                   "too many uses of multiplexed chardev '%s'"
                   " (maximum is " stringify(MAX_MUX) ")",
                   d->parent.label);
        return false;
    }

    d->mux_bitset |= (1ul << bit);
    d->frontends[bit] = c;
    *tag = bit;

    return true;
}

bool mux_chr_detach_frontend(MuxChardev *d, unsigned int tag)
{
    if (!(d->mux_bitset & (1ul << tag))) {
        return false;
    }

    d->mux_bitset &= ~(1ul << tag);
    d->frontends[tag] = NULL;

    return true;
}

void mux_set_focus(Chardev *chr, unsigned int focus)
{
    MuxChardev *d = MUX_CHARDEV(chr);

    assert(d->mux_bitset & (1ul << focus));

    if (d->focus != -1) {
        mux_chr_send_event(d, d->focus, CHR_EVENT_MUX_OUT);
    }

    d->focus = focus;
    chr->fe = d->frontends[focus];
    mux_chr_send_event(d, d->focus, CHR_EVENT_MUX_IN);
}

static void qemu_chr_open_mux(Chardev *chr,
                              ChardevBackend *backend,
                              bool *be_opened,
                              Error **errp)
{
    ChardevMux *mux = backend->u.mux.data;
    Chardev *drv;
    MuxChardev *d = MUX_CHARDEV(chr);

    drv = qemu_chr_find(mux->chardev);
    if (drv == NULL) {
        error_setg(errp, "mux: base chardev %s not found", mux->chardev);
        return;
    }

    d->focus = -1;
    /* only default to opened state if we've realized the initial
     * set of muxes
     */
    *be_opened = muxes_opened;
    qemu_chr_fe_init(&d->chr, drv, errp);
}

static void qemu_chr_parse_mux(QemuOpts *opts, ChardevBackend *backend,
                               Error **errp)
{
    const char *chardev = qemu_opt_get(opts, "chardev");
    ChardevMux *mux;

    if (chardev == NULL) {
        error_setg(errp, "chardev: mux: no chardev given");
        return;
    }
    backend->type = CHARDEV_BACKEND_KIND_MUX;
    mux = backend->u.mux.data = g_new0(ChardevMux, 1);
    qemu_chr_parse_common(opts, qapi_ChardevMux_base(mux));
    mux->chardev = g_strdup(chardev);
}

/**
 * Called after processing of default and command-line-specified
 * chardevs to deliver CHR_EVENT_OPENED events to any FEs attached
 * to a mux chardev. This is done here to ensure that
 * output/prompts/banners are only displayed for the FE that has
 * focus when initial command-line processing/machine init is
 * completed.
 *
 * After this point, any new FE attached to any new or existing
 * mux will receive CHR_EVENT_OPENED notifications for the BE
 * immediately.
 */
static void open_muxes(Chardev *chr)
{
    /* send OPENED to all already-attached FEs */
    mux_chr_send_all_event(chr, CHR_EVENT_OPENED);

    /*
     * mark mux as OPENED so any new FEs will immediately receive
     * OPENED event
     */
    chr->be_open = 1;
}

void suspend_mux_open(void)
{
    muxes_opened = false;
}

static int chardev_options_parsed_cb(Object *child, void *opaque)
{
    Chardev *chr = (Chardev *)child;

    if (!chr->be_open && CHARDEV_IS_MUX(chr)) {
        open_muxes(chr);
    }

    return 0;
}

void resume_mux_open(void)
{
    muxes_opened = true;
    object_child_foreach(get_chardevs_root(),
                         chardev_options_parsed_cb, NULL);
}

static void char_mux_class_init(ObjectClass *oc, const void *data)
{
    ChardevClass *cc = CHARDEV_CLASS(oc);

    cc->parse = qemu_chr_parse_mux;
    cc->open = qemu_chr_open_mux;
    cc->chr_write = mux_chr_write;
    cc->chr_accept_input = mux_chr_accept_input;
    cc->chr_add_watch = mux_chr_add_watch;
    cc->chr_be_event = mux_chr_be_event;
    cc->chr_update_read_handler = mux_chr_update_read_handlers;
}

static const TypeInfo char_mux_type_info = {
    .name = TYPE_CHARDEV_MUX,
    .parent = TYPE_CHARDEV,
    .class_init = char_mux_class_init,
    .instance_size = sizeof(MuxChardev),
    .instance_finalize = char_mux_finalize,
};

static void register_types(void)
{
    type_register_static(&char_mux_type_info);
}

type_init(register_types);
