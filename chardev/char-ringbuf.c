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
#include "chardev/char.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-char.h"
#include "qemu/base64.h"
#include "qemu/module.h"
#include "qemu/option.h"

/* Ring buffer chardev */

typedef struct {
    Chardev parent;
    size_t size;
    size_t prod;
    size_t cons;
    uint8_t *cbuf;
} RingBufChardev;

#define RINGBUF_CHARDEV(obj)                                    \
    OBJECT_CHECK(RingBufChardev, (obj), TYPE_CHARDEV_RINGBUF)

static size_t ringbuf_count(const Chardev *chr)
{
    const RingBufChardev *d = RINGBUF_CHARDEV(chr);

    return d->prod - d->cons;
}

static int ringbuf_chr_write(Chardev *chr, const uint8_t *buf, int len)
{
    RingBufChardev *d = RINGBUF_CHARDEV(chr);
    int i;

    if (!buf || (len < 0)) {
        return -1;
    }

    for (i = 0; i < len; i++) {
        d->cbuf[d->prod++ & (d->size - 1)] = buf[i];
        if (d->prod - d->cons > d->size) {
            d->cons = d->prod - d->size;
        }
    }

    return len;
}

static int ringbuf_chr_read(Chardev *chr, uint8_t *buf, int len)
{
    RingBufChardev *d = RINGBUF_CHARDEV(chr);
    int i;

    qemu_mutex_lock(&chr->chr_write_lock);
    for (i = 0; i < len && d->cons != d->prod; i++) {
        buf[i] = d->cbuf[d->cons++ & (d->size - 1)];
    }
    qemu_mutex_unlock(&chr->chr_write_lock);

    return i;
}

static void char_ringbuf_finalize(Object *obj)
{
    RingBufChardev *d = RINGBUF_CHARDEV(obj);

    g_free(d->cbuf);
}

static void qemu_chr_open_ringbuf(Chardev *chr,
                                  ChardevBackend *backend,
                                  bool *be_opened,
                                  Error **errp)
{
    ChardevRingbuf *opts = backend->u.ringbuf.data;
    RingBufChardev *d = RINGBUF_CHARDEV(chr);

    d->size = opts->has_size ? opts->size : 65536;

    /* The size must be power of 2 */
    if (d->size & (d->size - 1)) {
        error_setg(errp, "size of ringbuf chardev must be power of two");
        return;
    }

    d->prod = 0;
    d->cons = 0;
    d->cbuf = g_malloc0(d->size);
}

void qmp_ringbuf_write(const char *device, const char *data,
                       bool has_format, enum DataFormat format,
                       Error **errp)
{
    Chardev *chr;
    const uint8_t *write_data;
    int ret;
    gsize write_count;

    chr = qemu_chr_find(device);
    if (!chr) {
        error_setg(errp, "Device '%s' not found", device);
        return;
    }

    if (!CHARDEV_IS_RINGBUF(chr)) {
        error_setg(errp, "%s is not a ringbuf device", device);
        return;
    }

    if (has_format && (format == DATA_FORMAT_BASE64)) {
        write_data = qbase64_decode(data, -1,
                                    &write_count,
                                    errp);
        if (!write_data) {
            return;
        }
    } else {
        write_data = (uint8_t *)data;
        write_count = strlen(data);
    }

    ret = ringbuf_chr_write(chr, write_data, write_count);

    if (write_data != (uint8_t *)data) {
        g_free((void *)write_data);
    }

    if (ret < 0) {
        error_setg(errp, "Failed to write to device %s", device);
        return;
    }
}

char *qmp_ringbuf_read(const char *device, int64_t size,
                       bool has_format, enum DataFormat format,
                       Error **errp)
{
    Chardev *chr;
    uint8_t *read_data;
    size_t count;
    char *data;

    chr = qemu_chr_find(device);
    if (!chr) {
        error_setg(errp, "Device '%s' not found", device);
        return NULL;
    }

    if (!CHARDEV_IS_RINGBUF(chr)) {
        error_setg(errp, "%s is not a ringbuf device", device);
        return NULL;
    }

    if (size <= 0) {
        error_setg(errp, "size must be greater than zero");
        return NULL;
    }

    count = ringbuf_count(chr);
    size = size > count ? count : size;
    read_data = g_malloc(size + 1);

    ringbuf_chr_read(chr, read_data, size);

    if (has_format && (format == DATA_FORMAT_BASE64)) {
        data = g_base64_encode(read_data, size);
        g_free(read_data);
    } else {
        /*
         * FIXME should read only complete, valid UTF-8 characters up
         * to @size bytes.  Invalid sequences should be replaced by a
         * suitable replacement character.  Except when (and only
         * when) ring buffer lost characters since last read, initial
         * continuation characters should be dropped.
         */
        read_data[size] = 0;
        data = (char *)read_data;
    }

    return data;
}

static void qemu_chr_parse_ringbuf(QemuOpts *opts, ChardevBackend *backend,
                                   Error **errp)
{
    int val;
    ChardevRingbuf *ringbuf;

    backend->type = CHARDEV_BACKEND_KIND_RINGBUF;
    ringbuf = backend->u.ringbuf.data = g_new0(ChardevRingbuf, 1);
    qemu_chr_parse_common(opts, qapi_ChardevRingbuf_base(ringbuf));

    val = qemu_opt_get_size(opts, "size", 0);
    if (val != 0) {
        ringbuf->has_size = true;
        ringbuf->size = val;
    }
}

static void char_ringbuf_class_init(ObjectClass *oc, void *data)
{
    ChardevClass *cc = CHARDEV_CLASS(oc);

    cc->parse = qemu_chr_parse_ringbuf;
    cc->open = qemu_chr_open_ringbuf;
    cc->chr_write = ringbuf_chr_write;
}

static const TypeInfo char_ringbuf_type_info = {
    .name = TYPE_CHARDEV_RINGBUF,
    .parent = TYPE_CHARDEV,
    .class_init = char_ringbuf_class_init,
    .instance_size = sizeof(RingBufChardev),
    .instance_finalize = char_ringbuf_finalize,
};

/* Bug-compatibility: */
static const TypeInfo char_memory_type_info = {
    .name = TYPE_CHARDEV_MEMORY,
    .parent = TYPE_CHARDEV_RINGBUF,
};

static void register_types(void)
{
    type_register_static(&char_ringbuf_type_info);
    type_register_static(&char_memory_type_info);
}

type_init(register_types);
