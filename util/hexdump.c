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

void qemu_hexdump_line(char *line, unsigned int b, const void *bufptr,
                       unsigned int len, bool ascii)
{
    const char *buf = bufptr;
    int i, c;

    if (len > QEMU_HEXDUMP_LINE_BYTES) {
        len = QEMU_HEXDUMP_LINE_BYTES;
    }

    line += snprintf(line, 6, "%04x:", b);
    for (i = 0; i < QEMU_HEXDUMP_LINE_BYTES; i++) {
        if ((i % 4) == 0) {
            *line++ = ' ';
        }
        if (i < len) {
            line += sprintf(line, " %02x", (unsigned char)buf[b + i]);
        } else {
            line += sprintf(line, "   ");
        }
    }
    if (ascii) {
        *line++ = ' ';
        for (i = 0; i < len; i++) {
            c = buf[b + i];
            if (c < ' ' || c > '~') {
                c = '.';
            }
            *line++ = c;
        }
    }
    *line = '\0';
}

void qemu_hexdump(FILE *fp, const char *prefix,
                  const void *bufptr, size_t size)
{
    unsigned int b, len;
    char line[QEMU_HEXDUMP_LINE_LEN];

    for (b = 0; b < size; b += QEMU_HEXDUMP_LINE_BYTES) {
        len = size - b;
        qemu_hexdump_line(line, b, bufptr, len, true);
        fprintf(fp, "%s: %s\n", prefix, line);
    }

}
