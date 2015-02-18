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
#include <string.h>
#include "hw/acpi/aml-build.h"
#include "qemu/bswap.h"

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

static void
build_append_nameseg(GArray *array, const char *seg)
{
    /* It would be nicer to use g_string_vprintf but it's only there in 2.22 */
    int len;

    len = strlen(seg);
    assert(len <= ACPI_NAMESEG_LEN);

    g_array_append_vals(array, seg, len);
    /* Pad up to ACPI_NAMESEG_LEN characters if necessary. */
    g_array_append_vals(array, "____", ACPI_NAMESEG_LEN - len);
}

static void
build_append_namestringv(GArray *array, const char *format, va_list ap)
{
    /* It would be nicer to use g_string_vprintf but it's only there in 2.22 */
    char *s;
    int len;
    va_list va_len;
    char **segs;
    char **segs_iter;
    int seg_count = 0;

    va_copy(va_len, ap);
    len = vsnprintf(NULL, 0, format, va_len);
    va_end(va_len);
    len += 1;
    s = g_new(typeof(*s), len);

    len = vsnprintf(s, len, format, ap);

    segs = g_strsplit(s, ".", 0);
    g_free(s);

    /* count segments */
    segs_iter = segs;
    while (*segs_iter) {
        ++segs_iter;
        ++seg_count;
    }
    /*
     * ACPI 5.0 spec: 20.2.2 Name Objects Encoding:
     * "SegCount can be from 1 to 255"
     */
    assert(seg_count > 0 && seg_count <= 255);

    /* handle RootPath || PrefixPath */
    s = *segs;
    while (*s == '\\' || *s == '^') {
        build_append_byte(array, *s);
        ++s;
    }

    switch (seg_count) {
    case 1:
        if (!*s) {
            build_append_byte(array, 0x0); /* NullName */
        } else {
            build_append_nameseg(array, s);
        }
        break;

    case 2:
        build_append_byte(array, 0x2E); /* DualNamePrefix */
        build_append_nameseg(array, s);
        build_append_nameseg(array, segs[1]);
        break;
    default:
        build_append_byte(array, 0x2F); /* MultiNamePrefix */
        build_append_byte(array, seg_count);

        /* handle the 1st segment manually due to prefix/root path */
        build_append_nameseg(array, s);

        /* add the rest of segments */
        segs_iter = segs + 1;
        while (*segs_iter) {
            build_append_nameseg(array, *segs_iter);
            ++segs_iter;
        }
        break;
    }
    g_strfreev(segs);
}

void build_append_namestring(GArray *array, const char *format, ...)
{
    va_list ap;

    va_start(ap, format);
    build_append_namestringv(array, format, ap);
    va_end(ap);
}

/* 5.4 Definition Block Encoding */
enum {
    PACKAGE_LENGTH_1BYTE_SHIFT = 6, /* Up to 63 - use extra 2 bits. */
    PACKAGE_LENGTH_2BYTE_SHIFT = 4,
    PACKAGE_LENGTH_3BYTE_SHIFT = 12,
    PACKAGE_LENGTH_4BYTE_SHIFT = 20,
};

void build_prepend_package_length(GArray *package)
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

void build_package(GArray *package, uint8_t op)
{
    build_prepend_package_length(package);
    build_prepend_byte(package, op);
}

void build_extop_package(GArray *package, uint8_t op)
{
    build_package(package, op);
    build_prepend_byte(package, 0x5B); /* ExtOpPrefix */
}

static void build_append_int_noprefix(GArray *table, uint64_t value, int size)
{
    int i;

    for (i = 0; i < size; ++i) {
        build_append_byte(table, value & 0xFF);
        value = value >> 8;
    }
}

void build_append_int(GArray *table, uint64_t value)
{
    if (value == 0x00) {
        build_append_byte(table, 0x00); /* ZeroOp */
    } else if (value == 0x01) {
        build_append_byte(table, 0x01); /* OneOp */
    } else if (value <= 0xFF) {
        build_append_byte(table, 0x0A); /* BytePrefix */
        build_append_int_noprefix(table, value, 1);
    } else if (value <= 0xFFFF) {
        build_append_byte(table, 0x0B); /* WordPrefix */
        build_append_int_noprefix(table, value, 2);
    } else if (value <= 0xFFFFFFFF) {
        build_append_byte(table, 0x0C); /* DWordPrefix */
        build_append_int_noprefix(table, value, 4);
    } else {
        build_append_byte(table, 0x0E); /* QWordPrefix */
        build_append_int_noprefix(table, value, 8);
    }
}

static GPtrArray *alloc_list;

static Aml *aml_alloc(void)
{
    Aml *var = g_new0(typeof(*var), 1);

    g_ptr_array_add(alloc_list, var);
    var->block_flags = AML_NO_OPCODE;
    var->buf = build_alloc_array();
    return var;
}

static Aml *aml_opcode(uint8_t op)
{
    Aml *var = aml_alloc();

    var->op  = op;
    var->block_flags = AML_OPCODE;
    return var;
}

static Aml *aml_bundle(uint8_t op, AmlBlockFlags flags)
{
    Aml *var = aml_alloc();

    var->op  = op;
    var->block_flags = flags;
    return var;
}

static void aml_free(gpointer data)
{
    Aml *var = data;
    build_free_array(var->buf);
}

Aml *init_aml_allocator(void)
{
    Aml *var;

    assert(!alloc_list);
    alloc_list = g_ptr_array_new_with_free_func(aml_free);
    var = aml_alloc();
    return var;
}

void free_aml_allocator(void)
{
    g_ptr_array_free(alloc_list, true);
    alloc_list = 0;
}

/* pack data with DefBuffer encoding */
static void build_buffer(GArray *array, uint8_t op)
{
    GArray *data = build_alloc_array();

    build_append_int(data, array->len);
    g_array_prepend_vals(array, data->data, data->len);
    build_free_array(data);
    build_package(array, op);
}

void aml_append(Aml *parent_ctx, Aml *child)
{
    switch (child->block_flags) {
    case AML_OPCODE:
        build_append_byte(parent_ctx->buf, child->op);
        break;
    case AML_EXT_PACKAGE:
        build_extop_package(child->buf, child->op);
        break;
    case AML_PACKAGE:
        build_package(child->buf, child->op);
        break;
    case AML_RES_TEMPLATE:
        build_append_byte(child->buf, 0x79); /* EndTag */
        /*
         * checksum operations are treated as succeeded if checksum
         * field is zero. [ACPI Spec 1.0b, 6.4.2.8 End Tag]
         */
        build_append_byte(child->buf, 0);
        /* fall through, to pack resources in buffer */
    case AML_BUFFER:
        build_buffer(child->buf, child->op);
        break;
    case AML_NO_OPCODE:
        break;
    default:
        assert(0);
        break;
    }
    build_append_array(parent_ctx->buf, child->buf);
}

/* ACPI 1.0b: 16.2.5.1 Namespace Modifier Objects Encoding: DefScope */
Aml *aml_scope(const char *name_format, ...)
{
    va_list ap;
    Aml *var = aml_bundle(0x10 /* ScopeOp */, AML_PACKAGE);
    va_start(ap, name_format);
    build_append_namestringv(var->buf, name_format, ap);
    va_end(ap);
    return var;
}

/* ACPI 1.0b: 16.2.5.3 Type 1 Opcodes Encoding: DefReturn */
Aml *aml_return(Aml *val)
{
    Aml *var = aml_opcode(0xA4 /* ReturnOp */);
    aml_append(var, val);
    return var;
}

/*
 * ACPI 1.0b: 16.2.3 Data Objects Encoding:
 * encodes: ByteConst, WordConst, DWordConst, QWordConst, ZeroOp, OneOp
 */
Aml *aml_int(const uint64_t val)
{
    Aml *var = aml_alloc();
    build_append_int(var->buf, val);
    return var;
}

/*
 * helper to construct NameString, which returns Aml object
 * for using with aml_append or other aml_* terms
 */
Aml *aml_name(const char *name_format, ...)
{
    va_list ap;
    Aml *var = aml_alloc();
    va_start(ap, name_format);
    build_append_namestringv(var->buf, name_format, ap);
    va_end(ap);
    return var;
}

/* ACPI 1.0b: 16.2.5.1 Namespace Modifier Objects Encoding: DefName */
Aml *aml_name_decl(const char *name, Aml *val)
{
    Aml *var = aml_opcode(0x08 /* NameOp */);
    build_append_namestring(var->buf, "%s", name);
    aml_append(var, val);
    return var;
}

/* ACPI 1.0b: 16.2.6.1 Arg Objects Encoding */
Aml *aml_arg(int pos)
{
    Aml *var;
    uint8_t op = 0x68 /* ARG0 op */ + pos;

    assert(pos <= 6);
    var = aml_opcode(op);
    return var;
}

/* ACPI 1.0b: 16.2.5.4 Type 2 Opcodes Encoding: DefStore */
Aml *aml_store(Aml *val, Aml *target)
{
    Aml *var = aml_opcode(0x70 /* StoreOp */);
    aml_append(var, val);
    aml_append(var, target);
    return var;
}

/* ACPI 1.0b: 16.2.5.3 Type 1 Opcodes Encoding: DefIfElse */
Aml *aml_if(Aml *predicate)
{
    Aml *var = aml_bundle(0xA0 /* IfOp */, AML_PACKAGE);
    aml_append(var, predicate);
    return var;
}

/* ACPI 1.0b: 16.2.5.2 Named Objects Encoding: DefMethod */
Aml *aml_method(const char *name, int arg_count)
{
    Aml *var = aml_bundle(0x14 /* MethodOp */, AML_PACKAGE);
    build_append_namestring(var->buf, "%s", name);
    build_append_byte(var->buf, arg_count); /* MethodFlags: ArgCount */
    return var;
}

/* ACPI 1.0b: 16.2.5.2 Named Objects Encoding: DefDevice */
Aml *aml_device(const char *name_format, ...)
{
    va_list ap;
    Aml *var = aml_bundle(0x82 /* DeviceOp */, AML_EXT_PACKAGE);
    va_start(ap, name_format);
    build_append_namestringv(var->buf, name_format, ap);
    va_end(ap);
    return var;
}
