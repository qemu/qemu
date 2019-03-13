/* SPDX-License-Identifier: MIT */
/*
 * libslirp io streams
 *
 * Copyright (c) 2018 Red Hat, Inc.
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
#include "stream.h"
#include <glib.h>

bool slirp_istream_read(SlirpIStream *f, void *buf, size_t size)
{
    return f->read_cb(buf, size, f->opaque) == size;
}

bool slirp_ostream_write(SlirpOStream *f, const void *buf, size_t size)
{
    return f->write_cb(buf, size, f->opaque) == size;
}

uint8_t slirp_istream_read_u8(SlirpIStream *f)
{
    uint8_t b;

    if (slirp_istream_read(f, &b, sizeof(b))) {
        return b;
    }

    return 0;
}

bool slirp_ostream_write_u8(SlirpOStream *f, uint8_t b)
{
    return slirp_ostream_write(f, &b, sizeof(b));
}

uint16_t slirp_istream_read_u16(SlirpIStream *f)
{
    uint16_t b;

    if (slirp_istream_read(f, &b, sizeof(b))) {
        return GUINT16_FROM_BE(b);
    }

    return 0;
}

bool slirp_ostream_write_u16(SlirpOStream *f, uint16_t b)
{
    b =  GUINT16_TO_BE(b);
    return slirp_ostream_write(f, &b, sizeof(b));
}

uint32_t slirp_istream_read_u32(SlirpIStream *f)
{
    uint32_t b;

    if (slirp_istream_read(f, &b, sizeof(b))) {
        return GUINT32_FROM_BE(b);
    }

    return 0;
}

bool slirp_ostream_write_u32(SlirpOStream *f, uint32_t b)
{
    b = GUINT32_TO_BE(b);
    return slirp_ostream_write(f, &b, sizeof(b));
}

int16_t slirp_istream_read_i16(SlirpIStream *f)
{
    int16_t b;

    if (slirp_istream_read(f, &b, sizeof(b))) {
        return GINT16_FROM_BE(b);
    }

    return 0;
}

bool slirp_ostream_write_i16(SlirpOStream *f, int16_t b)
{
    b = GINT16_TO_BE(b);
    return slirp_ostream_write(f, &b, sizeof(b));
}

int32_t slirp_istream_read_i32(SlirpIStream *f)
{
    int32_t b;

    if (slirp_istream_read(f, &b, sizeof(b))) {
        return GINT32_FROM_BE(b);
    }

    return 0;
}

bool slirp_ostream_write_i32(SlirpOStream *f, int32_t b)
{
    b = GINT32_TO_BE(b);
    return slirp_ostream_write(f, &b, sizeof(b));
}
