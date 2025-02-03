/*
 * QEMU Character Hub Device
 *
 * Author: Roman Penyaev <r.peniaev@gmail.com>
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
#include "qemu/option.h"
#include "chardev/char.h"
#include "chardev-internal.h"

/*
 * Character hub device aggregates input from multiple backend devices
 * and forwards it to a single frontend device. Additionally, hub
 * device takes the output from the frontend device and sends it back
 * to all the connected backend devices.
 */

/*
 * Write to all backends. Different backend devices accept data with
 * various rate, so it is quite possible that one device returns less,
 * then others. In this case we return minimum to the caller,
 * expecting caller will repeat operation soon. When repeat happens
 * send to the devices which consume data faster must be avoided
 * for obvious reasons not to send data, which was already sent.
 * Called with chr_write_lock held.
 */
static int hub_chr_write(Chardev *chr, const uint8_t *buf, int len)
{
    HubChardev *d = HUB_CHARDEV(chr);
    int r, i, ret = len;
    unsigned int written;

    /* Invalidate index on every write */
    d->be_eagain_ind = -1;

    for (i = 0; i < d->be_cnt; i++) {
        if (!d->backends[i].be.chr->be_open) {
            /* Skip closed backend */
            continue;
        }
        written = d->be_written[i] - d->be_min_written;
        if (written) {
            /* Written in the previous call so take into account */
            ret = MIN(written, ret);
            continue;
        }
        r = qemu_chr_fe_write(&d->backends[i].be, buf, len);
        if (r < 0) {
            if (errno == EAGAIN) {
                /* Set index and expect to be called soon on watch wake up */
                d->be_eagain_ind = i;
            }
            return r;
        }
        d->be_written[i] += r;
        ret = MIN(r, ret);
    }
    d->be_min_written += ret;


    return ret;
}

static int hub_chr_can_read(void *opaque)
{
    HubCharBackend *backend = opaque;
    CharBackend *fe = backend->hub->parent.be;

    if (fe && fe->chr_can_read) {
        return fe->chr_can_read(fe->opaque);
    }

    return 0;
}

static void hub_chr_read(void *opaque, const uint8_t *buf, int size)
{
    HubCharBackend *backend = opaque;
    CharBackend *fe = backend->hub->parent.be;

    if (fe && fe->chr_read) {
        fe->chr_read(fe->opaque, buf, size);
    }
}

static void hub_chr_event(void *opaque, QEMUChrEvent event)
{
    HubCharBackend *backend = opaque;
    HubChardev *d = backend->hub;
    CharBackend *fe = d->parent.be;

    if (event == CHR_EVENT_OPENED) {
        /*
         * Catch up with what was already written while this backend
         * was closed
         */
        d->be_written[backend->be_ind] = d->be_min_written;

        if (d->be_event_opened_cnt++) {
            /* Ignore subsequent open events from other backends */
            return;
        }
    } else if (event == CHR_EVENT_CLOSED) {
        if (!d->be_event_opened_cnt) {
            /* Don't go below zero. Probably assert is better */
            return;
        }
        if (--d->be_event_opened_cnt) {
            /* Serve only the last one close event */
            return;
        }
    }

    if (fe && fe->chr_event) {
        fe->chr_event(fe->opaque, event);
    }
}

static GSource *hub_chr_add_watch(Chardev *s, GIOCondition cond)
{
    HubChardev *d = HUB_CHARDEV(s);
    Chardev *chr;
    ChardevClass *cc;

    if (d->be_eagain_ind == -1) {
        return NULL;
    }

    assert(d->be_eagain_ind < d->be_cnt);
    chr = qemu_chr_fe_get_driver(&d->backends[d->be_eagain_ind].be);
    cc = CHARDEV_GET_CLASS(chr);
    if (!cc->chr_add_watch) {
        return NULL;
    }

    return cc->chr_add_watch(chr, cond);
}

static bool hub_chr_attach_chardev(HubChardev *d, Chardev *chr,
                                   Error **errp)
{
    bool ret;

    if (d->be_cnt >= MAX_HUB) {
        error_setg(errp, "hub: too many uses of chardevs '%s'"
                   " (maximum is " stringify(MAX_HUB) ")",
                   d->parent.label);
        return false;
    }
    ret = qemu_chr_fe_init(&d->backends[d->be_cnt].be, chr, errp);
    if (ret) {
        d->backends[d->be_cnt].hub = d;
        d->backends[d->be_cnt].be_ind = d->be_cnt;
        d->be_cnt += 1;
    }

    return ret;
}

static void char_hub_finalize(Object *obj)
{
    HubChardev *d = HUB_CHARDEV(obj);
    int i;

    for (i = 0; i < d->be_cnt; i++) {
        qemu_chr_fe_deinit(&d->backends[i].be, false);
    }
}

static void hub_chr_update_read_handlers(Chardev *chr)
{
    HubChardev *d = HUB_CHARDEV(chr);
    int i;

    for (i = 0; i < d->be_cnt; i++) {
        qemu_chr_fe_set_handlers_full(&d->backends[i].be,
                                      hub_chr_can_read,
                                      hub_chr_read,
                                      hub_chr_event,
                                      NULL,
                                      &d->backends[i],
                                      chr->gcontext, true, false);
    }
}

static void qemu_chr_open_hub(Chardev *chr,
                                 ChardevBackend *backend,
                                 bool *be_opened,
                                 Error **errp)
{
    ChardevHub *hub = backend->u.hub.data;
    HubChardev *d = HUB_CHARDEV(chr);
    strList *list = hub->chardevs;

    d->be_eagain_ind = -1;

    if (list == NULL) {
        error_setg(errp, "hub: 'chardevs' list is not defined");
        return;
    }

    while (list) {
        Chardev *s;

        s = qemu_chr_find(list->value);
        if (s == NULL) {
            error_setg(errp, "hub: chardev can't be found by id '%s'",
                       list->value);
            return;
        }
        if (CHARDEV_IS_HUB(s) || CHARDEV_IS_MUX(s)) {
            error_setg(errp, "hub: multiplexers and hub devices can't be "
                       "stacked, check chardev '%s', chardev should not "
                       "be a hub device or have 'mux=on' enabled",
                       list->value);
            return;
        }
        if (!hub_chr_attach_chardev(d, s, errp)) {
            return;
        }
        list = list->next;
    }

    /* Closed until an explicit event from backend */
    *be_opened = false;
}

static void qemu_chr_parse_hub(QemuOpts *opts, ChardevBackend *backend,
                                  Error **errp)
{
    ChardevHub *hub;
    strList **tail;
    int i;

    backend->type = CHARDEV_BACKEND_KIND_HUB;
    hub = backend->u.hub.data = g_new0(ChardevHub, 1);
    qemu_chr_parse_common(opts, qapi_ChardevHub_base(hub));

    tail = &hub->chardevs;

    for (i = 0; i < MAX_HUB; i++) {
        char optbuf[16];
        const char *dev;

        snprintf(optbuf, sizeof(optbuf), "chardevs.%u", i);
        dev = qemu_opt_get(opts, optbuf);
        if (!dev) {
            break;
        }

        QAPI_LIST_APPEND(tail, g_strdup(dev));
    }
}

static void char_hub_class_init(ObjectClass *oc, void *data)
{
    ChardevClass *cc = CHARDEV_CLASS(oc);

    cc->parse = qemu_chr_parse_hub;
    cc->open = qemu_chr_open_hub;
    cc->chr_write = hub_chr_write;
    cc->chr_add_watch = hub_chr_add_watch;
    /* We handle events from backends only */
    cc->chr_be_event = NULL;
    cc->chr_update_read_handler = hub_chr_update_read_handlers;
}

static const TypeInfo char_hub_type_info = {
    .name = TYPE_CHARDEV_HUB,
    .parent = TYPE_CHARDEV,
    .class_init = char_hub_class_init,
    .instance_size = sizeof(HubChardev),
    .instance_finalize = char_hub_finalize,
};

static void register_types(void)
{
    type_register_static(&char_hub_type_info);
}

type_init(register_types);
