/* Support for generating ACPI tables and passing them to Guests
 *
 * Copyright (C) 2015 Red Hat Inc
 *
 * Author: Michael S. Tsirkin <mst@redhat.com>
 * Author: Igor Mammedov <imammedo@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stdarg.h>
#include <assert.h>
#include <stdbool.h>
#include "hw/acpi/aml-build.h"

GArray *build_alloc_array(void)
{
    return g_array_new(false, true /* clear */, 1);
}

void build_free_array(GArray *array)
{
    g_array_free(array, true);
}

void build_prepend_byte(GArray *array, uint8_t val)
{
    g_array_prepend_val(array, val);
}

void build_append_byte(GArray *array, uint8_t val)
{
    g_array_append_val(array, val);
}

void build_append_array(GArray *array, GArray *val)
{
    g_array_append_vals(array, val->data, val->len);
}

#define ACPI_NAMESEG_LEN 4

void GCC_FMT_ATTR(2, 3)
build_append_nameseg(GArray *array, const char *format, ...)
{
    /* It would be nicer to use g_string_vprintf but it's only there in 2.22 */
    char s[] = "XXXX";
    int len;
    va_list args;

    va_start(args, format);
    len = vsnprintf(s, sizeof s, format, args);
    va_end(args);

    assert(len <= ACPI_NAMESEG_LEN);

    g_array_append_vals(array, s, len);
    /* Pad up to ACPI_NAMESEG_LEN characters if necessary. */
    g_array_append_vals(array, "____", ACPI_NAMESEG_LEN - len);
}

/* 5.4 Definition Block Encoding */
enum {
    PACKAGE_LENGTH_1BYTE_SHIFT = 6, /* Up to 63 - use extra 2 bits. */
    PACKAGE_LENGTH_2BYTE_SHIFT = 4,
    PACKAGE_LENGTH_3BYTE_SHIFT = 12,
    PACKAGE_LENGTH_4BYTE_SHIFT = 20,
};

void build_prepend_package_length(GArray *package, unsigned min_bytes)
{
    uint8_t byte;
    unsigned length = package->len;
    unsigned length_bytes;

    if (length + 1 < (1 << PACKAGE_LENGTH_1BYTE_SHIFT)) {
        length_bytes = 1;
    } else if (length + 2 < (1 << PACKAGE_LENGTH_3BYTE_SHIFT)) {
        length_bytes = 2;
    } else if (length + 3 < (1 << PACKAGE_LENGTH_4BYTE_SHIFT)) {
        length_bytes = 3;
    } else {
        length_bytes = 4;
    }

    /* Force length to at least min_bytes.
     * This wastes memory but that's how bios did it.
     */
    length_bytes = MAX(length_bytes, min_bytes);

    /* PkgLength is the length of the inclusive length of the data. */
    length += length_bytes;

    switch (length_bytes) {
    case 1:
        byte = length;
        build_prepend_byte(package, byte);
        return;
    case 4:
        byte = length >> PACKAGE_LENGTH_4BYTE_SHIFT;
        build_prepend_byte(package, byte);
        length &= (1 << PACKAGE_LENGTH_4BYTE_SHIFT) - 1;
        /* fall through */
    case 3:
        byte = length >> PACKAGE_LENGTH_3BYTE_SHIFT;
        build_prepend_byte(package, byte);
        length &= (1 << PACKAGE_LENGTH_3BYTE_SHIFT) - 1;
        /* fall through */
    case 2:
        byte = length >> PACKAGE_LENGTH_2BYTE_SHIFT;
        build_prepend_byte(package, byte);
        length &= (1 << PACKAGE_LENGTH_2BYTE_SHIFT) - 1;
        /* fall through */
    }
    /*
     * Most significant two bits of byte zero indicate how many following bytes
     * are in PkgLength encoding.
     */
    byte = ((length_bytes - 1) << PACKAGE_LENGTH_1BYTE_SHIFT) | length;
    build_prepend_byte(package, byte);
}

void build_package(GArray *package, uint8_t op, unsigned min_bytes)
{
    build_prepend_package_length(package, min_bytes);
    build_prepend_byte(package, op);
}

void build_extop_package(GArray *package, uint8_t op)
{
    build_package(package, op, 1);
    build_prepend_byte(package, 0x5B); /* ExtOpPrefix */
}

void build_append_value(GArray *table, uint32_t value, int size)
{
    uint8_t prefix;
    int i;

    switch (size) {
    case 1:
        prefix = 0x0A; /* BytePrefix */
        break;
    case 2:
        prefix = 0x0B; /* WordPrefix */
        break;
    case 4:
        prefix = 0x0C; /* DWordPrefix */
        break;
    default:
        assert(0);
        return;
    }
    build_append_byte(table, prefix);
    for (i = 0; i < size; ++i) {
        build_append_byte(table, value & 0xFF);
        value = value >> 8;
    }
}

void build_append_int(GArray *table, uint32_t value)
{
    if (value == 0x00) {
        build_append_byte(table, 0x00); /* ZeroOp */
    } else if (value == 0x01) {
        build_append_byte(table, 0x01); /* OneOp */
    } else if (value <= 0xFF) {
        build_append_value(table, value, 1);
    } else if (value <= 0xFFFF) {
        build_append_value(table, value, 2);
    } else {
        build_append_value(table, value, 4);
    }
}

