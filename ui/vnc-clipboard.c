/*
 * QEMU VNC display driver -- clipboard support
 *
 * Copyright (C) 2021 Gerd Hoffmann <kraxel@redhat.com>
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
#include "vnc.h"
#include "vnc-jobs.h"

static uint8_t *inflate_buffer(uint8_t *in, uint32_t in_len, uint32_t *size)
{
    z_stream stream = {
        .next_in  = in,
        .avail_in = in_len,
        .zalloc   = Z_NULL,
        .zfree    = Z_NULL,
    };
    uint32_t out_len = 8;
    uint8_t *out = g_malloc(out_len);
    int ret;

    stream.next_out = out + stream.total_out;
    stream.avail_out = out_len - stream.total_out;

    ret = inflateInit(&stream);
    if (ret != Z_OK) {
        goto err;
    }

    while (stream.avail_in) {
        ret = inflate(&stream, Z_FINISH);
        switch (ret) {
        case Z_OK:
            break;
        case Z_STREAM_END:
            *size = stream.total_out;
            inflateEnd(&stream);
            return out;
        case Z_BUF_ERROR:
            out_len <<= 1;
            if (out_len > (1 << 20)) {
                goto err_end;
            }
            out = g_realloc(out, out_len);
            stream.next_out = out + stream.total_out;
            stream.avail_out = out_len - stream.total_out;
            break;
        default:
            goto err_end;
        }
    }

    *size = stream.total_out;
    inflateEnd(&stream);

    return out;

err_end:
    inflateEnd(&stream);
err:
    g_free(out);
    return NULL;
}

static uint8_t *deflate_buffer(uint8_t *in, uint32_t in_len, uint32_t *size)
{
    z_stream stream = {
        .next_in  = in,
        .avail_in = in_len,
        .zalloc   = Z_NULL,
        .zfree    = Z_NULL,
    };
    uint32_t out_len = 8;
    uint8_t *out = g_malloc(out_len);
    int ret;

    stream.next_out = out + stream.total_out;
    stream.avail_out = out_len - stream.total_out;

    ret = deflateInit(&stream, Z_DEFAULT_COMPRESSION);
    if (ret != Z_OK) {
        goto err;
    }

    while (ret != Z_STREAM_END) {
        ret = deflate(&stream, Z_FINISH);
        switch (ret) {
        case Z_OK:
        case Z_STREAM_END:
            break;
        case Z_BUF_ERROR:
            out_len <<= 1;
            if (out_len > (1 << 20)) {
                goto err_end;
            }
            out = g_realloc(out, out_len);
            stream.next_out = out + stream.total_out;
            stream.avail_out = out_len - stream.total_out;
            break;
        default:
            goto err_end;
        }
    }

    *size = stream.total_out;
    deflateEnd(&stream);

    return out;

err_end:
    deflateEnd(&stream);
err:
    g_free(out);
    return NULL;
}

static void vnc_clipboard_send(VncState *vs, uint32_t count, uint32_t *dwords)
{
    int i;

    vnc_lock_output(vs);
    vnc_write_u8(vs, VNC_MSG_SERVER_CUT_TEXT);
    vnc_write_u8(vs, 0);
    vnc_write_u8(vs, 0);
    vnc_write_u8(vs, 0);
    vnc_write_s32(vs, -(count * sizeof(uint32_t)));  /* -(message length) */
    for (i = 0; i < count; i++) {
        vnc_write_u32(vs, dwords[i]);
    }
    vnc_unlock_output(vs);
    vnc_flush(vs);
}

static void vnc_clipboard_provide(VncState *vs,
                                  QemuClipboardInfo *info,
                                  QemuClipboardType type)
{
    uint32_t flags = 0;
    g_autofree uint8_t *buf = NULL;
    g_autofree void *zbuf = NULL;
    uint32_t zsize;

    switch (type) {
    case QEMU_CLIPBOARD_TYPE_TEXT:
        flags |= VNC_CLIPBOARD_TEXT;
        break;
    default:
        return;
    }
    flags |= VNC_CLIPBOARD_PROVIDE;

    buf = g_malloc(info->types[type].size + 4);
    buf[0] = (info->types[type].size >> 24) & 0xff;
    buf[1] = (info->types[type].size >> 16) & 0xff;
    buf[2] = (info->types[type].size >>  8) & 0xff;
    buf[3] = (info->types[type].size >>  0) & 0xff;
    memcpy(buf + 4, info->types[type].data, info->types[type].size);
    zbuf = deflate_buffer(buf, info->types[type].size + 4, &zsize);
    if (!zbuf) {
        return;
    }

    vnc_lock_output(vs);
    vnc_write_u8(vs, VNC_MSG_SERVER_CUT_TEXT);
    vnc_write_u8(vs, 0);
    vnc_write_u8(vs, 0);
    vnc_write_u8(vs, 0);
    vnc_write_s32(vs, -(sizeof(uint32_t) + zsize));  /* -(message length) */
    vnc_write_u32(vs, flags);
    vnc_write(vs, zbuf, zsize);
    vnc_unlock_output(vs);
    vnc_flush(vs);
}

static void vnc_clipboard_update_info(VncState *vs, QemuClipboardInfo *info)
{
    QemuClipboardType type;
    bool self_update = info->owner == &vs->cbpeer;
    uint32_t flags = 0;

    if (info != vs->cbinfo) {
        qemu_clipboard_info_unref(vs->cbinfo);
        vs->cbinfo = qemu_clipboard_info_ref(info);
        vs->cbpending = 0;
        if (!self_update) {
            if (info->types[QEMU_CLIPBOARD_TYPE_TEXT].available) {
                flags |= VNC_CLIPBOARD_TEXT;
            }
            flags |= VNC_CLIPBOARD_NOTIFY;
            vnc_clipboard_send(vs, 1, &flags);
        }
        return;
    }

    if (self_update) {
        return;
    }

    for (type = 0; type < QEMU_CLIPBOARD_TYPE__COUNT; type++) {
        if (vs->cbpending & (1 << type)) {
            vs->cbpending &= ~(1 << type);
            vnc_clipboard_provide(vs, info, type);
        }
    }
}

static void vnc_clipboard_notify(Notifier *notifier, void *data)
{
    VncState *vs = container_of(notifier, VncState, cbpeer.notifier);
    QemuClipboardNotify *notify = data;

    switch (notify->type) {
    case QEMU_CLIPBOARD_UPDATE_INFO:
        vnc_clipboard_update_info(vs, notify->info);
        return;
    case QEMU_CLIPBOARD_RESET_SERIAL:
        /* ignore */
        return;
    }
}

static void vnc_clipboard_request(QemuClipboardInfo *info,
                                  QemuClipboardType type)
{
    VncState *vs = container_of(info->owner, VncState, cbpeer);
    uint32_t flags = 0;

    if (type == QEMU_CLIPBOARD_TYPE_TEXT) {
        flags |= VNC_CLIPBOARD_TEXT;
    }
    if (!flags) {
        return;
    }
    flags |= VNC_CLIPBOARD_REQUEST;

    vnc_clipboard_send(vs, 1, &flags);
}

void vnc_client_cut_text_ext(VncState *vs, int32_t len, uint32_t flags, uint8_t *data)
{
    if (flags & VNC_CLIPBOARD_CAPS) {
        /* need store caps somewhere ? */
        return;
    }

    if (flags & VNC_CLIPBOARD_NOTIFY) {
        QemuClipboardInfo *info =
            qemu_clipboard_info_new(&vs->cbpeer, QEMU_CLIPBOARD_SELECTION_CLIPBOARD);
        if (flags & VNC_CLIPBOARD_TEXT) {
            info->types[QEMU_CLIPBOARD_TYPE_TEXT].available = true;
        }
        qemu_clipboard_update(info);
        qemu_clipboard_info_unref(info);
        return;
    }

    if (flags & VNC_CLIPBOARD_PROVIDE &&
        vs->cbinfo &&
        vs->cbinfo->owner == &vs->cbpeer) {
        uint32_t size = 0;
        g_autofree uint8_t *buf = inflate_buffer(data, len - 4, &size);
        if ((flags & VNC_CLIPBOARD_TEXT) &&
            buf && size >= 4) {
            uint32_t tsize = read_u32(buf, 0);
            uint8_t *tbuf = buf + 4;
            if (tsize < size) {
                qemu_clipboard_set_data(&vs->cbpeer, vs->cbinfo,
                                        QEMU_CLIPBOARD_TYPE_TEXT,
                                        tsize, tbuf, true);
            }
        }
    }

    if (flags & VNC_CLIPBOARD_REQUEST &&
        vs->cbinfo &&
        vs->cbinfo->owner != &vs->cbpeer) {
        if ((flags & VNC_CLIPBOARD_TEXT) &&
            vs->cbinfo->types[QEMU_CLIPBOARD_TYPE_TEXT].available) {
            if (vs->cbinfo->types[QEMU_CLIPBOARD_TYPE_TEXT].data) {
                vnc_clipboard_provide(vs, vs->cbinfo, QEMU_CLIPBOARD_TYPE_TEXT);
            } else {
                vs->cbpending |= (1 << QEMU_CLIPBOARD_TYPE_TEXT);
                qemu_clipboard_request(vs->cbinfo, QEMU_CLIPBOARD_TYPE_TEXT);
            }
        }
    }
}

void vnc_client_cut_text(VncState *vs, size_t len, uint8_t *text)
{
    QemuClipboardInfo *info =
        qemu_clipboard_info_new(&vs->cbpeer, QEMU_CLIPBOARD_SELECTION_CLIPBOARD);

    qemu_clipboard_set_data(&vs->cbpeer, info, QEMU_CLIPBOARD_TYPE_TEXT,
                            len, text, true);
    qemu_clipboard_info_unref(info);
}

void vnc_server_cut_text_caps(VncState *vs)
{
    uint32_t caps[2];

    if (!vnc_has_feature(vs, VNC_FEATURE_CLIPBOARD_EXT)) {
        return;
    }

    caps[0] = (VNC_CLIPBOARD_PROVIDE |
               VNC_CLIPBOARD_NOTIFY  |
               VNC_CLIPBOARD_REQUEST |
               VNC_CLIPBOARD_CAPS    |
               VNC_CLIPBOARD_TEXT);
    caps[1] = 0;
    vnc_clipboard_send(vs, 2, caps);

    if (!vs->cbpeer.notifier.notify) {
        vs->cbpeer.name = "vnc";
        vs->cbpeer.notifier.notify = vnc_clipboard_notify;
        vs->cbpeer.request = vnc_clipboard_request;
        qemu_clipboard_peer_register(&vs->cbpeer);
    }
}
