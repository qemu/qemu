/*
* Helper to hexdump a buffer
 *
 * Copyright (c) 2013 Red Hat, Inc.
 * Copyright (c) 2013 Gerd Hoffmann <kraxel@redhat.com>
 * Copyright (c) 2013 Peter Crosthwaite <peter.crosthwaite@xilinx.com>
 * Copyright (c) 2013 Xilinx, Inc
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qemu/cutils.h"

static inline char hexdump_nibble(unsigned x)
{
    return (x < 10 ? '0' : 'a' - 10) + x;
}

GString *qemu_hexdump_line(GString *str, const void *vbuf, size_t len,
                           size_t unit_len, size_t block_len)
{
    const uint8_t *buf = vbuf;
    size_t u, b;

    if (str == NULL) {
        /* Estimate the length of the output to avoid reallocs. */
        size_t est = len * 2;
        if (unit_len) {
            est += len / unit_len;
        }
        if (block_len) {
            est += len / block_len;
        }
        str = g_string_sized_new(est + 1);
    }

    for (u = 0, b = 0; len; u++, b++, len--, buf++) {
        uint8_t c;

        if (unit_len && u == unit_len) {
            g_string_append_c(str, ' ');
            u = 0;
        }
        if (block_len && b == block_len) {
            g_string_append_c(str, ' ');
            b = 0;
        }

        c = *buf;
        g_string_append_c(str, hexdump_nibble(c / 16));
        g_string_append_c(str, hexdump_nibble(c % 16));
    }

    return str;
}

static void asciidump_line(char *line, const void *bufptr, size_t len)
{
    const char *buf = bufptr;

    for (size_t i = 0; i < len; i++) {
        char c = buf[i];

        if (c < ' ' || c > '~') {
            c = '.';
        }
        *line++ = c;
    }
    *line = '\0';
}

#define QEMU_HEXDUMP_LINE_BYTES 16
#define QEMU_HEXDUMP_LINE_WIDTH \
    (QEMU_HEXDUMP_LINE_BYTES * 2 + QEMU_HEXDUMP_LINE_BYTES / 4)

void qemu_hexdump(FILE *fp, const char *prefix,
                  const void *bufptr, size_t size)
{
    g_autoptr(GString) str = g_string_sized_new(QEMU_HEXDUMP_LINE_WIDTH + 1);
    char ascii[QEMU_HEXDUMP_LINE_BYTES + 1];
    size_t b, len;

    for (b = 0; b < size; b += len) {
        len = MIN(size - b, QEMU_HEXDUMP_LINE_BYTES);

        g_string_truncate(str, 0);
        qemu_hexdump_line(str, bufptr + b, len, 1, 4);
        asciidump_line(ascii, bufptr + b, len);

        fprintf(fp, "%s: %04zx: %-*s %s\n",
                prefix, b, QEMU_HEXDUMP_LINE_WIDTH, str->str, ascii);
    }

}
