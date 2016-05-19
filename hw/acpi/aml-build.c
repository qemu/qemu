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

#include "qemu/osdep.h"
#include <glib/gprintf.h>
#include "hw/acpi/aml-build.h"
#include "qemu/bswap.h"
#include "qemu/bitops.h"

static GArray *build_alloc_array(void)
{
    return g_array_new(false, true /* clear */, 1);
}

static void build_free_array(GArray *array)
{
    g_array_free(array, true);
}

static void build_prepend_byte(GArray *array, uint8_t val)
{
    g_array_prepend_val(array, val);
}

static void build_append_byte(GArray *array, uint8_t val)
{
    g_array_append_val(array, val);
}

static void build_append_array(GArray *array, GArray *val)
{
    g_array_append_vals(array, val->data, val->len);
}

#define ACPI_NAMESEG_LEN 4

static void
build_append_nameseg(GArray *array, const char *seg)
{
    int len;

    len = strlen(seg);
    assert(len <= ACPI_NAMESEG_LEN);

    g_array_append_vals(array, seg, len);
    /* Pad up to ACPI_NAMESEG_LEN characters if necessary. */
    g_array_append_vals(array, "____", ACPI_NAMESEG_LEN - len);
}

static void GCC_FMT_ATTR(2, 0)
build_append_namestringv(GArray *array, const char *format, va_list ap)
{
    char *s;
    char **segs;
    char **segs_iter;
    int seg_count = 0;

    s = g_strdup_vprintf(format, ap);
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
            build_append_byte(array, 0x00); /* NullName */
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

GCC_FMT_ATTR(2, 3)
static void build_append_namestring(GArray *array, const char *format, ...)
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

static void
build_prepend_package_length(GArray *package, unsigned length, bool incl_self)
{
    uint8_t byte;
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

    /*
     * NamedField uses PkgLength encoding but it doesn't include length
     * of PkgLength itself.
     */
    if (incl_self) {
        /*
         * PkgLength is the length of the inclusive length of the data
         * and PkgLength's length itself when used for terms with
         * explitit length.
         */
        length += length_bytes;
    }

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

static void
build_append_pkg_length(GArray *array, unsigned length, bool incl_self)
{
    GArray *tmp = build_alloc_array();

    build_prepend_package_length(tmp, length, incl_self);
    build_append_array(array, tmp);
    build_free_array(tmp);
}

static void build_package(GArray *package, uint8_t op)
{
    build_prepend_package_length(package, package->len, true);
    build_prepend_byte(package, op);
}

static void build_extop_package(GArray *package, uint8_t op)
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

static void build_append_int(GArray *table, uint64_t value)
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

/*
 * Build NAME(XXXX, 0x00000000) where 0x00000000 is encoded as a dword,
 * and return the offset to 0x00000000 for runtime patching.
 *
 * Warning: runtime patching is best avoided. Only use this as
 * a replacement for DataTableRegion (for guests that don't
 * support it).
 */
int
build_append_named_dword(GArray *array, const char *name_format, ...)
{
    int offset;
    va_list ap;

    build_append_byte(array, 0x08); /* NameOp */
    va_start(ap, name_format);
    build_append_namestringv(array, name_format, ap);
    va_end(ap);

    build_append_byte(array, 0x0C); /* DWordPrefix */

    offset = array->len;
    build_append_int_noprefix(array, 0x00000000, 4);
    assert(array->len == offset + 4);

    return offset;
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

static void aml_free(gpointer data, gpointer user_data)
{
    Aml *var = data;
    build_free_array(var->buf);
    g_free(var);
}

Aml *init_aml_allocator(void)
{
    Aml *var;

    assert(!alloc_list);
    alloc_list = g_ptr_array_new();
    var = aml_alloc();
    return var;
}

void free_aml_allocator(void)
{
    g_ptr_array_foreach(alloc_list, aml_free, NULL);
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
    GArray *buf = build_alloc_array();
    build_append_array(buf, child->buf);

    switch (child->block_flags) {
    case AML_OPCODE:
        build_append_byte(parent_ctx->buf, child->op);
        break;
    case AML_EXT_PACKAGE:
        build_extop_package(buf, child->op);
        break;
    case AML_PACKAGE:
        build_package(buf, child->op);
        break;
    case AML_RES_TEMPLATE:
        build_append_byte(buf, 0x79); /* EndTag */
        /*
         * checksum operations are treated as succeeded if checksum
         * field is zero. [ACPI Spec 1.0b, 6.4.2.8 End Tag]
         */
        build_append_byte(buf, 0);
        /* fall through, to pack resources in buffer */
    case AML_BUFFER:
        build_buffer(buf, child->op);
        break;
    case AML_NO_OPCODE:
        break;
    default:
        assert(0);
        break;
    }
    build_append_array(parent_ctx->buf, buf);
    build_free_array(buf);
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

/* ACPI 1.0b: 16.2.6.3 Debug Objects Encoding: DebugObj */
Aml *aml_debug(void)
{
    Aml *var = aml_alloc();
    build_append_byte(var->buf, 0x5B); /* ExtOpPrefix */
    build_append_byte(var->buf, 0x31); /* DebugOp */
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

/* ACPI 2.0a: 17.2.4.4 Type 2 Opcodes Encoding: DefToInteger */
Aml *aml_to_integer(Aml *arg)
{
    Aml *var = aml_opcode(0x99 /* ToIntegerOp */);
    aml_append(var, arg);
    build_append_byte(var->buf, 0x00 /* NullNameOp */);
    return var;
}

/* ACPI 2.0a: 17.2.4.4 Type 2 Opcodes Encoding: DefToHexString */
Aml *aml_to_hexstring(Aml *src, Aml *dst)
{
    Aml *var = aml_opcode(0x98 /* ToHexStringOp */);
    aml_append(var, src);
    if (dst) {
        aml_append(var, dst);
    } else {
        build_append_byte(var->buf, 0x00 /* NullNameOp */);
    }
    return var;
}

/* ACPI 2.0a: 17.2.4.4 Type 2 Opcodes Encoding: DefToBuffer */
Aml *aml_to_buffer(Aml *src, Aml *dst)
{
    Aml *var = aml_opcode(0x96 /* ToBufferOp */);
    aml_append(var, src);
    if (dst) {
        aml_append(var, dst);
    } else {
        build_append_byte(var->buf, 0x00 /* NullNameOp */);
    }
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

/**
 * build_opcode_2arg_dst:
 * @op: 1-byte opcode
 * @arg1: 1st operand
 * @arg2: 2nd operand
 * @dst: optional target to store to, set to NULL if it's not required
 *
 * An internal helper to compose AML terms that have
 *   "Op Operand Operand Target"
 * pattern.
 *
 * Returns: The newly allocated and composed according to patter Aml object.
 */
static Aml *
build_opcode_2arg_dst(uint8_t op, Aml *arg1, Aml *arg2, Aml *dst)
{
    Aml *var = aml_opcode(op);
    aml_append(var, arg1);
    aml_append(var, arg2);
    if (dst) {
        aml_append(var, dst);
    } else {
        build_append_byte(var->buf, 0x00 /* NullNameOp */);
    }
    return var;
}

/* ACPI 1.0b: 16.2.5.4 Type 2 Opcodes Encoding: DefAnd */
Aml *aml_and(Aml *arg1, Aml *arg2, Aml *dst)
{
    return build_opcode_2arg_dst(0x7B /* AndOp */, arg1, arg2, dst);
}

/* ACPI 1.0b: 16.2.5.4 Type 2 Opcodes Encoding: DefOr */
Aml *aml_or(Aml *arg1, Aml *arg2, Aml *dst)
{
    return build_opcode_2arg_dst(0x7D /* OrOp */, arg1, arg2, dst);
}

/* ACPI 1.0b: 16.2.5.4 Type 2 Opcodes Encoding: DefLOr */
Aml *aml_lor(Aml *arg1, Aml *arg2)
{
    Aml *var = aml_opcode(0x91 /* LOrOp */);
    aml_append(var, arg1);
    aml_append(var, arg2);
    return var;
}

/* ACPI 1.0b: 16.2.5.4 Type 2 Opcodes Encoding: DefShiftLeft */
Aml *aml_shiftleft(Aml *arg1, Aml *count)
{
    return build_opcode_2arg_dst(0x79 /* ShiftLeftOp */, arg1, count, NULL);
}

/* ACPI 1.0b: 16.2.5.4 Type 2 Opcodes Encoding: DefShiftRight */
Aml *aml_shiftright(Aml *arg1, Aml *count, Aml *dst)
{
    return build_opcode_2arg_dst(0x7A /* ShiftRightOp */, arg1, count, dst);
}

/* ACPI 1.0b: 16.2.5.4 Type 2 Opcodes Encoding: DefLLess */
Aml *aml_lless(Aml *arg1, Aml *arg2)
{
    Aml *var = aml_opcode(0x95 /* LLessOp */);
    aml_append(var, arg1);
    aml_append(var, arg2);
    return var;
}

/* ACPI 1.0b: 16.2.5.4 Type 2 Opcodes Encoding: DefAdd */
Aml *aml_add(Aml *arg1, Aml *arg2, Aml *dst)
{
    return build_opcode_2arg_dst(0x72 /* AddOp */, arg1, arg2, dst);
}

/* ACPI 1.0b: 16.2.5.4 Type 2 Opcodes Encoding: DefSubtract */
Aml *aml_subtract(Aml *arg1, Aml *arg2, Aml *dst)
{
    return build_opcode_2arg_dst(0x74 /* SubtractOp */, arg1, arg2, dst);
}

/* ACPI 1.0b: 16.2.5.4 Type 2 Opcodes Encoding: DefIncrement */
Aml *aml_increment(Aml *arg)
{
    Aml *var = aml_opcode(0x75 /* IncrementOp */);
    aml_append(var, arg);
    return var;
}

/* ACPI 1.0b: 16.2.5.4 Type 2 Opcodes Encoding: DefDecrement */
Aml *aml_decrement(Aml *arg)
{
    Aml *var = aml_opcode(0x76 /* DecrementOp */);
    aml_append(var, arg);
    return var;
}

/* ACPI 1.0b: 16.2.5.4 Type 2 Opcodes Encoding: DefIndex */
Aml *aml_index(Aml *arg1, Aml *idx)
{
    return build_opcode_2arg_dst(0x88 /* IndexOp */, arg1, idx, NULL);
}

/* ACPI 1.0b: 16.2.5.3 Type 1 Opcodes Encoding: DefNotify */
Aml *aml_notify(Aml *arg1, Aml *arg2)
{
    Aml *var = aml_opcode(0x86 /* NotifyOp */);
    aml_append(var, arg1);
    aml_append(var, arg2);
    return var;
}

/* helper to call method with 1 argument */
Aml *aml_call0(const char *method)
{
    Aml *var = aml_alloc();
    build_append_namestring(var->buf, "%s", method);
    return var;
}

/* helper to call method with 1 argument */
Aml *aml_call1(const char *method, Aml *arg1)
{
    Aml *var = aml_alloc();
    build_append_namestring(var->buf, "%s", method);
    aml_append(var, arg1);
    return var;
}

/* helper to call method with 2 arguments */
Aml *aml_call2(const char *method, Aml *arg1, Aml *arg2)
{
    Aml *var = aml_alloc();
    build_append_namestring(var->buf, "%s", method);
    aml_append(var, arg1);
    aml_append(var, arg2);
    return var;
}

/* helper to call method with 3 arguments */
Aml *aml_call3(const char *method, Aml *arg1, Aml *arg2, Aml *arg3)
{
    Aml *var = aml_alloc();
    build_append_namestring(var->buf, "%s", method);
    aml_append(var, arg1);
    aml_append(var, arg2);
    aml_append(var, arg3);
    return var;
}

/* helper to call method with 4 arguments */
Aml *aml_call4(const char *method, Aml *arg1, Aml *arg2, Aml *arg3, Aml *arg4)
{
    Aml *var = aml_alloc();
    build_append_namestring(var->buf, "%s", method);
    aml_append(var, arg1);
    aml_append(var, arg2);
    aml_append(var, arg3);
    aml_append(var, arg4);
    return var;
}

/*
 * ACPI 5.0: 6.4.3.8.1 GPIO Connection Descriptor
 * Type 1, Large Item Name 0xC
 */

static Aml *aml_gpio_connection(AmlGpioConnectionType type,
                                AmlConsumerAndProducer con_and_pro,
                                uint8_t flags, AmlPinConfig pin_config,
                                uint16_t output_drive,
                                uint16_t debounce_timeout,
                                const uint32_t pin_list[], uint32_t pin_count,
                                const char *resource_source_name,
                                const uint8_t *vendor_data,
                                uint16_t vendor_data_len)
{
    Aml *var = aml_alloc();
    const uint16_t min_desc_len = 0x16;
    uint16_t resource_source_name_len, length;
    uint16_t pin_table_offset, resource_source_name_offset, vendor_data_offset;
    uint32_t i;

    assert(resource_source_name);
    resource_source_name_len = strlen(resource_source_name) + 1;
    length = min_desc_len + resource_source_name_len + vendor_data_len;
    pin_table_offset = min_desc_len + 1;
    resource_source_name_offset = pin_table_offset + pin_count * 2;
    vendor_data_offset = resource_source_name_offset + resource_source_name_len;

    build_append_byte(var->buf, 0x8C);  /* GPIO Connection Descriptor */
    build_append_int_noprefix(var->buf, length, 2); /* Length */
    build_append_byte(var->buf, 1);     /* Revision ID */
    build_append_byte(var->buf, type);  /* GPIO Connection Type */
    /* General Flags (2 bytes) */
    build_append_int_noprefix(var->buf, con_and_pro, 2);
    /* Interrupt and IO Flags (2 bytes) */
    build_append_int_noprefix(var->buf, flags, 2);
    /* Pin Configuration 0 = Default 1 = Pull-up 2 = Pull-down 3 = No Pull */
    build_append_byte(var->buf, pin_config);
    /* Output Drive Strength (2 bytes) */
    build_append_int_noprefix(var->buf, output_drive, 2);
    /* Debounce Timeout (2 bytes) */
    build_append_int_noprefix(var->buf, debounce_timeout, 2);
    /* Pin Table Offset (2 bytes) */
    build_append_int_noprefix(var->buf, pin_table_offset, 2);
    build_append_byte(var->buf, 0);     /* Resource Source Index */
    /* Resource Source Name Offset (2 bytes) */
    build_append_int_noprefix(var->buf, resource_source_name_offset, 2);
    /* Vendor Data Offset (2 bytes) */
    build_append_int_noprefix(var->buf, vendor_data_offset, 2);
    /* Vendor Data Length (2 bytes) */
    build_append_int_noprefix(var->buf, vendor_data_len, 2);
    /* Pin Number (2n bytes)*/
    for (i = 0; i < pin_count; i++) {
        build_append_int_noprefix(var->buf, pin_list[i], 2);
    }

    /* Resource Source Name */
    build_append_namestring(var->buf, "%s", resource_source_name);
    build_append_byte(var->buf, '\0');

    /* Vendor-defined Data */
    if (vendor_data != NULL) {
        g_array_append_vals(var->buf, vendor_data, vendor_data_len);
    }

    return var;
}

/*
 * ACPI 5.0: 19.5.53
 * GpioInt(GPIO Interrupt Connection Resource Descriptor Macro)
 */
Aml *aml_gpio_int(AmlConsumerAndProducer con_and_pro,
                  AmlLevelAndEdge edge_level,
                  AmlActiveHighAndLow active_level, AmlShared shared,
                  AmlPinConfig pin_config, uint16_t debounce_timeout,
                  const uint32_t pin_list[], uint32_t pin_count,
                  const char *resource_source_name,
                  const uint8_t *vendor_data, uint16_t vendor_data_len)
{
    uint8_t flags = edge_level | (active_level << 1) | (shared << 3);

    return aml_gpio_connection(AML_INTERRUPT_CONNECTION, con_and_pro, flags,
                               pin_config, 0, debounce_timeout, pin_list,
                               pin_count, resource_source_name, vendor_data,
                               vendor_data_len);
}

/*
 * ACPI 1.0b: 6.4.3.4 32-Bit Fixed Location Memory Range Descriptor
 * (Type 1, Large Item Name 0x6)
 */
Aml *aml_memory32_fixed(uint32_t addr, uint32_t size,
                        AmlReadAndWrite read_and_write)
{
    Aml *var = aml_alloc();
    build_append_byte(var->buf, 0x86); /* Memory32Fixed Resource Descriptor */
    build_append_byte(var->buf, 9);    /* Length, bits[7:0] value = 9 */
    build_append_byte(var->buf, 0);    /* Length, bits[15:8] value = 0 */
    build_append_byte(var->buf, read_and_write); /* Write status, 1 rw 0 ro */

    /* Range base address */
    build_append_byte(var->buf, extract32(addr, 0, 8));  /* bits[7:0] */
    build_append_byte(var->buf, extract32(addr, 8, 8));  /* bits[15:8] */
    build_append_byte(var->buf, extract32(addr, 16, 8)); /* bits[23:16] */
    build_append_byte(var->buf, extract32(addr, 24, 8)); /* bits[31:24] */

    /* Range length */
    build_append_byte(var->buf, extract32(size, 0, 8));  /* bits[7:0] */
    build_append_byte(var->buf, extract32(size, 8, 8));  /* bits[15:8] */
    build_append_byte(var->buf, extract32(size, 16, 8)); /* bits[23:16] */
    build_append_byte(var->buf, extract32(size, 24, 8)); /* bits[31:24] */
    return var;
}

/*
 * ACPI 5.0: 6.4.3.6 Extended Interrupt Descriptor
 * Type 1, Large Item Name 0x9
 */
Aml *aml_interrupt(AmlConsumerAndProducer con_and_pro,
                   AmlLevelAndEdge level_and_edge,
                   AmlActiveHighAndLow high_and_low, AmlShared shared,
                   uint32_t *irq_list, uint8_t irq_count)
{
    int i;
    Aml *var = aml_alloc();
    uint8_t irq_flags = con_and_pro | (level_and_edge << 1)
                        | (high_and_low << 2) | (shared << 3);
    const int header_bytes_in_len = 2;
    uint16_t len = header_bytes_in_len + irq_count * sizeof(uint32_t);

    assert(irq_count > 0);

    build_append_byte(var->buf, 0x89); /* Extended irq descriptor */
    build_append_byte(var->buf, len & 0xFF); /* Length, bits[7:0] */
    build_append_byte(var->buf, len >> 8); /* Length, bits[15:8] */
    build_append_byte(var->buf, irq_flags); /* Interrupt Vector Information. */
    build_append_byte(var->buf, irq_count);   /* Interrupt table length */

    /* Interrupt Number List */
    for (i = 0; i < irq_count; i++) {
        build_append_int_noprefix(var->buf, irq_list[i], 4);
    }
    return var;
}

/* ACPI 1.0b: 6.4.2.5 I/O Port Descriptor */
Aml *aml_io(AmlIODecode dec, uint16_t min_base, uint16_t max_base,
            uint8_t aln, uint8_t len)
{
    Aml *var = aml_alloc();
    build_append_byte(var->buf, 0x47); /* IO port descriptor */
    build_append_byte(var->buf, dec);
    build_append_byte(var->buf, min_base & 0xff);
    build_append_byte(var->buf, (min_base >> 8) & 0xff);
    build_append_byte(var->buf, max_base & 0xff);
    build_append_byte(var->buf, (max_base >> 8) & 0xff);
    build_append_byte(var->buf, aln);
    build_append_byte(var->buf, len);
    return var;
}

/*
 * ACPI 1.0b: 6.4.2.1.1 ASL Macro for IRQ Descriptor
 *
 * More verbose description at:
 * ACPI 5.0: 19.5.64 IRQNoFlags (Interrupt Resource Descriptor Macro)
 *           6.4.2.1 IRQ Descriptor
 */
Aml *aml_irq_no_flags(uint8_t irq)
{
    uint16_t irq_mask;
    Aml *var = aml_alloc();

    assert(irq < 16);
    build_append_byte(var->buf, 0x22); /* IRQ descriptor 2 byte form */

    irq_mask = 1U << irq;
    build_append_byte(var->buf, irq_mask & 0xFF); /* IRQ mask bits[7:0] */
    build_append_byte(var->buf, irq_mask >> 8); /* IRQ mask bits[15:8] */
    return var;
}

/* ACPI 1.0b: 16.2.5.4 Type 2 Opcodes Encoding: DefLNot */
Aml *aml_lnot(Aml *arg)
{
    Aml *var = aml_opcode(0x92 /* LNotOp */);
    aml_append(var, arg);
    return var;
}

/* ACPI 1.0b: 16.2.5.4 Type 2 Opcodes Encoding: DefLEqual */
Aml *aml_equal(Aml *arg1, Aml *arg2)
{
    Aml *var = aml_opcode(0x93 /* LequalOp */);
    aml_append(var, arg1);
    aml_append(var, arg2);
    return var;
}

/* ACPI 1.0b: 16.2.5.4 Type 2 Opcodes Encoding: DefLGreater */
Aml *aml_lgreater(Aml *arg1, Aml *arg2)
{
    Aml *var = aml_opcode(0x94 /* LGreaterOp */);
    aml_append(var, arg1);
    aml_append(var, arg2);
    return var;
}

/* ACPI 1.0b: 16.2.5.4 Type 2 Opcodes Encoding: DefLGreaterEqual */
Aml *aml_lgreater_equal(Aml *arg1, Aml *arg2)
{
    /* LGreaterEqualOp := LNotOp LLessOp */
    Aml *var = aml_opcode(0x92 /* LNotOp */);
    build_append_byte(var->buf, 0x95 /* LLessOp */);
    aml_append(var, arg1);
    aml_append(var, arg2);
    return var;
}

/* ACPI 1.0b: 16.2.5.3 Type 1 Opcodes Encoding: DefIfElse */
Aml *aml_if(Aml *predicate)
{
    Aml *var = aml_bundle(0xA0 /* IfOp */, AML_PACKAGE);
    aml_append(var, predicate);
    return var;
}

/* ACPI 1.0b: 16.2.5.3 Type 1 Opcodes Encoding: DefElse */
Aml *aml_else(void)
{
    Aml *var = aml_bundle(0xA1 /* ElseOp */, AML_PACKAGE);
    return var;
}

/* ACPI 1.0b: 16.2.5.3 Type 1 Opcodes Encoding: DefWhile */
Aml *aml_while(Aml *predicate)
{
    Aml *var = aml_bundle(0xA2 /* WhileOp */, AML_PACKAGE);
    aml_append(var, predicate);
    return var;
}

/* ACPI 1.0b: 16.2.5.2 Named Objects Encoding: DefMethod */
Aml *aml_method(const char *name, int arg_count, AmlSerializeFlag sflag)
{
    Aml *var = aml_bundle(0x14 /* MethodOp */, AML_PACKAGE);
    int methodflags;

    /*
     * MethodFlags:
     *   bit 0-2: ArgCount (0-7)
     *   bit 3: SerializeFlag
     *     0: NotSerialized
     *     1: Serialized
     *   bit 4-7: reserved (must be 0)
     */
    assert(arg_count < 8);
    methodflags = arg_count | (sflag << 3);

    build_append_namestring(var->buf, "%s", name);
    build_append_byte(var->buf, methodflags); /* MethodFlags: ArgCount */
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

/* ACPI 1.0b: 6.4.1 ASL Macros for Resource Descriptors */
Aml *aml_resource_template(void)
{
    /* ResourceTemplate is a buffer of Resources with EndTag at the end */
    Aml *var = aml_bundle(0x11 /* BufferOp */, AML_RES_TEMPLATE);
    return var;
}

/* ACPI 1.0b: 16.2.5.4 Type 2 Opcodes Encoding: DefBuffer
 * Pass byte_list as NULL to request uninitialized buffer to reserve space.
 */
Aml *aml_buffer(int buffer_size, uint8_t *byte_list)
{
    int i;
    Aml *var = aml_bundle(0x11 /* BufferOp */, AML_BUFFER);

    for (i = 0; i < buffer_size; i++) {
        if (byte_list == NULL) {
            build_append_byte(var->buf, 0x0);
        } else {
            build_append_byte(var->buf, byte_list[i]);
        }
    }

    return var;
}

/* ACPI 1.0b: 16.2.5.4 Type 2 Opcodes Encoding: DefPackage */
Aml *aml_package(uint8_t num_elements)
{
    Aml *var = aml_bundle(0x12 /* PackageOp */, AML_PACKAGE);
    build_append_byte(var->buf, num_elements);
    return var;
}

/* ACPI 1.0b: 16.2.5.2 Named Objects Encoding: DefOpRegion */
Aml *aml_operation_region(const char *name, AmlRegionSpace rs,
                          Aml *offset, uint32_t len)
{
    Aml *var = aml_alloc();
    build_append_byte(var->buf, 0x5B); /* ExtOpPrefix */
    build_append_byte(var->buf, 0x80); /* OpRegionOp */
    build_append_namestring(var->buf, "%s", name);
    build_append_byte(var->buf, rs);
    aml_append(var, offset);
    build_append_int(var->buf, len);
    return var;
}

/* ACPI 1.0b: 16.2.5.2 Named Objects Encoding: NamedField */
Aml *aml_named_field(const char *name, unsigned length)
{
    Aml *var = aml_alloc();
    build_append_nameseg(var->buf, name);
    build_append_pkg_length(var->buf, length, false);
    return var;
}

/* ACPI 1.0b: 16.2.5.2 Named Objects Encoding: ReservedField */
Aml *aml_reserved_field(unsigned length)
{
    Aml *var = aml_alloc();
    /* ReservedField  := 0x00 PkgLength */
    build_append_byte(var->buf, 0x00);
    build_append_pkg_length(var->buf, length, false);
    return var;
}

/* ACPI 1.0b: 16.2.5.2 Named Objects Encoding: DefField */
Aml *aml_field(const char *name, AmlAccessType type, AmlLockRule lock,
               AmlUpdateRule rule)
{
    Aml *var = aml_bundle(0x81 /* FieldOp */, AML_EXT_PACKAGE);
    uint8_t flags = rule << 5 | type;

    flags |= lock << 4; /* LockRule at 4 bit offset */

    build_append_namestring(var->buf, "%s", name);
    build_append_byte(var->buf, flags);
    return var;
}

static
Aml *create_field_common(int opcode, Aml *srcbuf, Aml *index, const char *name)
{
    Aml *var = aml_opcode(opcode);
    aml_append(var, srcbuf);
    aml_append(var, index);
    build_append_namestring(var->buf, "%s", name);
    return var;
}

/* ACPI 1.0b: 16.2.5.2 Named Objects Encoding: DefCreateField */
Aml *aml_create_field(Aml *srcbuf, Aml *bit_index, Aml *num_bits,
                      const char *name)
{
    Aml *var = aml_alloc();
    build_append_byte(var->buf, 0x5B); /* ExtOpPrefix */
    build_append_byte(var->buf, 0x13); /* CreateFieldOp */
    aml_append(var, srcbuf);
    aml_append(var, bit_index);
    aml_append(var, num_bits);
    build_append_namestring(var->buf, "%s", name);
    return var;
}

/* ACPI 1.0b: 16.2.5.2 Named Objects Encoding: DefCreateDWordField */
Aml *aml_create_dword_field(Aml *srcbuf, Aml *index, const char *name)
{
    return create_field_common(0x8A /* CreateDWordFieldOp */,
                               srcbuf, index, name);
}

/* ACPI 2.0a: 17.2.4.2 Named Objects Encoding: DefCreateQWordField */
Aml *aml_create_qword_field(Aml *srcbuf, Aml *index, const char *name)
{
    return create_field_common(0x8F /* CreateQWordFieldOp */,
                               srcbuf, index, name);
}

/* ACPI 1.0b: 16.2.3 Data Objects Encoding: String */
Aml *aml_string(const char *name_format, ...)
{
    Aml *var = aml_opcode(0x0D /* StringPrefix */);
    va_list ap;
    char *s;
    int len;

    va_start(ap, name_format);
    len = g_vasprintf(&s, name_format, ap);
    va_end(ap);

    g_array_append_vals(var->buf, s, len + 1);
    g_free(s);

    return var;
}

/* ACPI 1.0b: 16.2.6.2 Local Objects Encoding */
Aml *aml_local(int num)
{
    Aml *var;
    uint8_t op = 0x60 /* Local0Op */ + num;

    assert(num <= 7);
    var = aml_opcode(op);
    return var;
}

/* ACPI 2.0a: 17.2.2 Data Objects Encoding: DefVarPackage */
Aml *aml_varpackage(uint32_t num_elements)
{
    Aml *var = aml_bundle(0x13 /* VarPackageOp */, AML_PACKAGE);
    build_append_int(var->buf, num_elements);
    return var;
}

/* ACPI 1.0b: 16.2.5.2 Named Objects Encoding: DefProcessor */
Aml *aml_processor(uint8_t proc_id, uint32_t pblk_addr, uint8_t pblk_len,
                   const char *name_format, ...)
{
    va_list ap;
    Aml *var = aml_bundle(0x83 /* ProcessorOp */, AML_EXT_PACKAGE);
    va_start(ap, name_format);
    build_append_namestringv(var->buf, name_format, ap);
    va_end(ap);
    build_append_byte(var->buf, proc_id); /* ProcID */
    build_append_int_noprefix(var->buf, pblk_addr, sizeof(pblk_addr));
    build_append_byte(var->buf, pblk_len); /* PblkLen */
    return var;
}

static uint8_t Hex2Digit(char c)
{
    if (c >= 'A') {
        return c - 'A' + 10;
    }

    return c - '0';
}

/* ACPI 1.0b: 15.2.3.6.4.1 EISAID Macro - Convert EISA ID String To Integer */
Aml *aml_eisaid(const char *str)
{
    Aml *var = aml_alloc();
    uint32_t id;

    g_assert(strlen(str) == 7);
    id = (str[0] - 0x40) << 26 |
    (str[1] - 0x40) << 21 |
    (str[2] - 0x40) << 16 |
    Hex2Digit(str[3]) << 12 |
    Hex2Digit(str[4]) << 8 |
    Hex2Digit(str[5]) << 4 |
    Hex2Digit(str[6]);

    build_append_byte(var->buf, 0x0C); /* DWordPrefix */
    build_append_int_noprefix(var->buf, bswap32(id), sizeof(id));
    return var;
}

/* ACPI 1.0b: 6.4.3.5.5 Word Address Space Descriptor: bytes 3-5 */
static Aml *aml_as_desc_header(AmlResourceType type, AmlMinFixed min_fixed,
                               AmlMaxFixed max_fixed, AmlDecode dec,
                               uint8_t type_flags)
{
    uint8_t flags = max_fixed | min_fixed | dec;
    Aml *var = aml_alloc();

    build_append_byte(var->buf, type);
    build_append_byte(var->buf, flags);
    build_append_byte(var->buf, type_flags); /* Type Specific Flags */
    return var;
}

/* ACPI 1.0b: 6.4.3.5.5 Word Address Space Descriptor */
static Aml *aml_word_as_desc(AmlResourceType type, AmlMinFixed min_fixed,
                             AmlMaxFixed max_fixed, AmlDecode dec,
                             uint16_t addr_gran, uint16_t addr_min,
                             uint16_t addr_max, uint16_t addr_trans,
                             uint16_t len, uint8_t type_flags)
{
    Aml *var = aml_alloc();

    build_append_byte(var->buf, 0x88); /* Word Address Space Descriptor */
    /* minimum length since we do not encode optional fields */
    build_append_byte(var->buf, 0x0D);
    build_append_byte(var->buf, 0x0);

    aml_append(var,
        aml_as_desc_header(type, min_fixed, max_fixed, dec, type_flags));
    build_append_int_noprefix(var->buf, addr_gran, sizeof(addr_gran));
    build_append_int_noprefix(var->buf, addr_min, sizeof(addr_min));
    build_append_int_noprefix(var->buf, addr_max, sizeof(addr_max));
    build_append_int_noprefix(var->buf, addr_trans, sizeof(addr_trans));
    build_append_int_noprefix(var->buf, len, sizeof(len));
    return var;
}

/* ACPI 1.0b: 6.4.3.5.3 DWord Address Space Descriptor */
static Aml *aml_dword_as_desc(AmlResourceType type, AmlMinFixed min_fixed,
                              AmlMaxFixed max_fixed, AmlDecode dec,
                              uint32_t addr_gran, uint32_t addr_min,
                              uint32_t addr_max, uint32_t addr_trans,
                              uint32_t len, uint8_t type_flags)
{
    Aml *var = aml_alloc();

    build_append_byte(var->buf, 0x87); /* DWord Address Space Descriptor */
    /* minimum length since we do not encode optional fields */
    build_append_byte(var->buf, 23);
    build_append_byte(var->buf, 0x0);


    aml_append(var,
        aml_as_desc_header(type, min_fixed, max_fixed, dec, type_flags));
    build_append_int_noprefix(var->buf, addr_gran, sizeof(addr_gran));
    build_append_int_noprefix(var->buf, addr_min, sizeof(addr_min));
    build_append_int_noprefix(var->buf, addr_max, sizeof(addr_max));
    build_append_int_noprefix(var->buf, addr_trans, sizeof(addr_trans));
    build_append_int_noprefix(var->buf, len, sizeof(len));
    return var;
}

/* ACPI 1.0b: 6.4.3.5.1 QWord Address Space Descriptor */
static Aml *aml_qword_as_desc(AmlResourceType type, AmlMinFixed min_fixed,
                              AmlMaxFixed max_fixed, AmlDecode dec,
                              uint64_t addr_gran, uint64_t addr_min,
                              uint64_t addr_max, uint64_t addr_trans,
                              uint64_t len, uint8_t type_flags)
{
    Aml *var = aml_alloc();

    build_append_byte(var->buf, 0x8A); /* QWord Address Space Descriptor */
    /* minimum length since we do not encode optional fields */
    build_append_byte(var->buf, 0x2B);
    build_append_byte(var->buf, 0x0);

    aml_append(var,
        aml_as_desc_header(type, min_fixed, max_fixed, dec, type_flags));
    build_append_int_noprefix(var->buf, addr_gran, sizeof(addr_gran));
    build_append_int_noprefix(var->buf, addr_min, sizeof(addr_min));
    build_append_int_noprefix(var->buf, addr_max, sizeof(addr_max));
    build_append_int_noprefix(var->buf, addr_trans, sizeof(addr_trans));
    build_append_int_noprefix(var->buf, len, sizeof(len));
    return var;
}

/*
 * ACPI 1.0b: 6.4.3.5.6 ASL Macros for WORD Address Descriptor
 *
 * More verbose description at:
 * ACPI 5.0: 19.5.141 WordBusNumber (Word Bus Number Resource Descriptor Macro)
 */
Aml *aml_word_bus_number(AmlMinFixed min_fixed, AmlMaxFixed max_fixed,
                         AmlDecode dec, uint16_t addr_gran,
                         uint16_t addr_min, uint16_t addr_max,
                         uint16_t addr_trans, uint16_t len)

{
    return aml_word_as_desc(AML_BUS_NUMBER_RANGE, min_fixed, max_fixed, dec,
                            addr_gran, addr_min, addr_max, addr_trans, len, 0);
}

/*
 * ACPI 1.0b: 6.4.3.5.6 ASL Macros for WORD Address Descriptor
 *
 * More verbose description at:
 * ACPI 5.0: 19.5.142 WordIO (Word IO Resource Descriptor Macro)
 */
Aml *aml_word_io(AmlMinFixed min_fixed, AmlMaxFixed max_fixed,
                 AmlDecode dec, AmlISARanges isa_ranges,
                 uint16_t addr_gran, uint16_t addr_min,
                 uint16_t addr_max, uint16_t addr_trans,
                 uint16_t len)

{
    return aml_word_as_desc(AML_IO_RANGE, min_fixed, max_fixed, dec,
                            addr_gran, addr_min, addr_max, addr_trans, len,
                            isa_ranges);
}

/*
 * ACPI 1.0b: 6.4.3.5.4 ASL Macros for DWORD Address Descriptor
 *
 * More verbose description at:
 * ACPI 5.0: 19.5.33 DWordIO (DWord IO Resource Descriptor Macro)
 */
Aml *aml_dword_io(AmlMinFixed min_fixed, AmlMaxFixed max_fixed,
                 AmlDecode dec, AmlISARanges isa_ranges,
                 uint32_t addr_gran, uint32_t addr_min,
                 uint32_t addr_max, uint32_t addr_trans,
                 uint32_t len)

{
    return aml_dword_as_desc(AML_IO_RANGE, min_fixed, max_fixed, dec,
                            addr_gran, addr_min, addr_max, addr_trans, len,
                            isa_ranges);
}

/*
 * ACPI 1.0b: 6.4.3.5.4 ASL Macros for DWORD Address Space Descriptor
 *
 * More verbose description at:
 * ACPI 5.0: 19.5.34 DWordMemory (DWord Memory Resource Descriptor Macro)
 */
Aml *aml_dword_memory(AmlDecode dec, AmlMinFixed min_fixed,
                      AmlMaxFixed max_fixed, AmlCacheable cacheable,
                      AmlReadAndWrite read_and_write,
                      uint32_t addr_gran, uint32_t addr_min,
                      uint32_t addr_max, uint32_t addr_trans,
                      uint32_t len)
{
    uint8_t flags = read_and_write | (cacheable << 1);

    return aml_dword_as_desc(AML_MEMORY_RANGE, min_fixed, max_fixed,
                             dec, addr_gran, addr_min, addr_max,
                             addr_trans, len, flags);
}

/*
 * ACPI 1.0b: 6.4.3.5.2 ASL Macros for QWORD Address Space Descriptor
 *
 * More verbose description at:
 * ACPI 5.0: 19.5.102 QWordMemory (QWord Memory Resource Descriptor Macro)
 */
Aml *aml_qword_memory(AmlDecode dec, AmlMinFixed min_fixed,
                      AmlMaxFixed max_fixed, AmlCacheable cacheable,
                      AmlReadAndWrite read_and_write,
                      uint64_t addr_gran, uint64_t addr_min,
                      uint64_t addr_max, uint64_t addr_trans,
                      uint64_t len)
{
    uint8_t flags = read_and_write | (cacheable << 1);

    return aml_qword_as_desc(AML_MEMORY_RANGE, min_fixed, max_fixed,
                             dec, addr_gran, addr_min, addr_max,
                             addr_trans, len, flags);
}

/* ACPI 1.0b: 6.4.2.2 DMA Format/6.4.2.2.1 ASL Macro for DMA Descriptor */
Aml *aml_dma(AmlDmaType typ, AmlDmaBusMaster bm, AmlTransferSize sz,
             uint8_t channel)
{
    Aml *var = aml_alloc();
    uint8_t flags = sz | bm << 2 | typ << 5;

    assert(channel < 8);
    build_append_byte(var->buf, 0x2A);    /* Byte 0: DMA Descriptor */
    build_append_byte(var->buf, 1U << channel); /* Byte 1: _DMA - DmaChannel */
    build_append_byte(var->buf, flags);   /* Byte 2 */
    return var;
}

/* ACPI 1.0b: 16.2.5.3 Type 1 Opcodes Encoding: DefSleep */
Aml *aml_sleep(uint64_t msec)
{
    Aml *var = aml_alloc();
    build_append_byte(var->buf, 0x5B); /* ExtOpPrefix */
    build_append_byte(var->buf, 0x22); /* SleepOp */
    aml_append(var, aml_int(msec));
    return var;
}

static uint8_t Hex2Byte(const char *src)
{
    int hi, lo;

    hi = Hex2Digit(src[0]);
    assert(hi >= 0);
    assert(hi <= 15);

    lo = Hex2Digit(src[1]);
    assert(lo >= 0);
    assert(lo <= 15);
    return (hi << 4) | lo;
}

/*
 * ACPI 3.0: 17.5.124 ToUUID (Convert String to UUID Macro)
 * e.g. UUID: aabbccdd-eeff-gghh-iijj-kkllmmnnoopp
 * call aml_touuid("aabbccdd-eeff-gghh-iijj-kkllmmnnoopp");
 */
Aml *aml_touuid(const char *uuid)
{
    Aml *var = aml_bundle(0x11 /* BufferOp */, AML_BUFFER);

    assert(strlen(uuid) == 36);
    assert(uuid[8] == '-');
    assert(uuid[13] == '-');
    assert(uuid[18] == '-');
    assert(uuid[23] == '-');

    build_append_byte(var->buf, Hex2Byte(uuid + 6));  /* dd - at offset 00 */
    build_append_byte(var->buf, Hex2Byte(uuid + 4));  /* cc - at offset 01 */
    build_append_byte(var->buf, Hex2Byte(uuid + 2));  /* bb - at offset 02 */
    build_append_byte(var->buf, Hex2Byte(uuid + 0));  /* aa - at offset 03 */

    build_append_byte(var->buf, Hex2Byte(uuid + 11)); /* ff - at offset 04 */
    build_append_byte(var->buf, Hex2Byte(uuid + 9));  /* ee - at offset 05 */

    build_append_byte(var->buf, Hex2Byte(uuid + 16)); /* hh - at offset 06 */
    build_append_byte(var->buf, Hex2Byte(uuid + 14)); /* gg - at offset 07 */

    build_append_byte(var->buf, Hex2Byte(uuid + 19)); /* ii - at offset 08 */
    build_append_byte(var->buf, Hex2Byte(uuid + 21)); /* jj - at offset 09 */

    build_append_byte(var->buf, Hex2Byte(uuid + 24)); /* kk - at offset 10 */
    build_append_byte(var->buf, Hex2Byte(uuid + 26)); /* ll - at offset 11 */
    build_append_byte(var->buf, Hex2Byte(uuid + 28)); /* mm - at offset 12 */
    build_append_byte(var->buf, Hex2Byte(uuid + 30)); /* nn - at offset 13 */
    build_append_byte(var->buf, Hex2Byte(uuid + 32)); /* oo - at offset 14 */
    build_append_byte(var->buf, Hex2Byte(uuid + 34)); /* pp - at offset 15 */

    return var;
}

/*
 * ACPI 2.0b: 16.2.3.6.4.3  Unicode Macro (Convert Ascii String To Unicode)
 */
Aml *aml_unicode(const char *str)
{
    int i = 0;
    Aml *var = aml_bundle(0x11 /* BufferOp */, AML_BUFFER);

    do {
        build_append_byte(var->buf, str[i]);
        build_append_byte(var->buf, 0);
        i++;
    } while (i <= strlen(str));

    return var;
}

/* ACPI 1.0b: 16.2.5.4 Type 2 Opcodes Encoding: DefRefOf */
Aml *aml_refof(Aml *arg)
{
    Aml *var = aml_opcode(0x71 /* RefOfOp */);
    aml_append(var, arg);
    return var;
}

/* ACPI 1.0b: 16.2.5.4 Type 2 Opcodes Encoding: DefDerefOf */
Aml *aml_derefof(Aml *arg)
{
    Aml *var = aml_opcode(0x83 /* DerefOfOp */);
    aml_append(var, arg);
    return var;
}

/* ACPI 1.0b: 16.2.5.4 Type 2 Opcodes Encoding: DefSizeOf */
Aml *aml_sizeof(Aml *arg)
{
    Aml *var = aml_opcode(0x87 /* SizeOfOp */);
    aml_append(var, arg);
    return var;
}

/* ACPI 1.0b: 16.2.5.2 Named Objects Encoding: DefMutex */
Aml *aml_mutex(const char *name, uint8_t sync_level)
{
    Aml *var = aml_alloc();
    build_append_byte(var->buf, 0x5B); /* ExtOpPrefix */
    build_append_byte(var->buf, 0x01); /* MutexOp */
    build_append_namestring(var->buf, "%s", name);
    assert(!(sync_level & 0xF0));
    build_append_byte(var->buf, sync_level);
    return var;
}

/* ACPI 1.0b: 16.2.5.4 Type 2 Opcodes Encoding: DefAcquire */
Aml *aml_acquire(Aml *mutex, uint16_t timeout)
{
    Aml *var = aml_alloc();
    build_append_byte(var->buf, 0x5B); /* ExtOpPrefix */
    build_append_byte(var->buf, 0x23); /* AcquireOp */
    aml_append(var, mutex);
    build_append_int_noprefix(var->buf, timeout, sizeof(timeout));
    return var;
}

/* ACPI 1.0b: 16.2.5.3 Type 1 Opcodes Encoding: DefRelease */
Aml *aml_release(Aml *mutex)
{
    Aml *var = aml_alloc();
    build_append_byte(var->buf, 0x5B); /* ExtOpPrefix */
    build_append_byte(var->buf, 0x27); /* ReleaseOp */
    aml_append(var, mutex);
    return var;
}

/* ACPI 1.0b: 16.2.5.1 Name Space Modifier Objects Encoding: DefAlias */
Aml *aml_alias(const char *source_object, const char *alias_object)
{
    Aml *var = aml_opcode(0x06 /* AliasOp */);
    aml_append(var, aml_name("%s", source_object));
    aml_append(var, aml_name("%s", alias_object));
    return var;
}

/* ACPI 1.0b: 16.2.5.4 Type 2 Opcodes Encoding: DefConcat */
Aml *aml_concatenate(Aml *source1, Aml *source2, Aml *target)
{
    return build_opcode_2arg_dst(0x73 /* ConcatOp */, source1, source2,
                                 target);
}

void
build_header(BIOSLinker *linker, GArray *table_data,
             AcpiTableHeader *h, const char *sig, int len, uint8_t rev,
             const char *oem_id, const char *oem_table_id)
{
    memcpy(&h->signature, sig, 4);
    h->length = cpu_to_le32(len);
    h->revision = rev;

    if (oem_id) {
        strncpy((char *)h->oem_id, oem_id, sizeof h->oem_id);
    } else {
        memcpy(h->oem_id, ACPI_BUILD_APPNAME6, 6);
    }

    if (oem_table_id) {
        strncpy((char *)h->oem_table_id, oem_table_id, sizeof(h->oem_table_id));
    } else {
        memcpy(h->oem_table_id, ACPI_BUILD_APPNAME4, 4);
        memcpy(h->oem_table_id + 4, sig, 4);
    }

    h->oem_revision = cpu_to_le32(1);
    memcpy(h->asl_compiler_id, ACPI_BUILD_APPNAME4, 4);
    h->asl_compiler_revision = cpu_to_le32(1);
    h->checksum = 0;
    /* Checksum to be filled in by Guest linker */
    bios_linker_loader_add_checksum(linker, ACPI_BUILD_TABLE_FILE,
                                    h, len, &h->checksum);
}

void *acpi_data_push(GArray *table_data, unsigned size)
{
    unsigned off = table_data->len;
    g_array_set_size(table_data, off + size);
    return table_data->data + off;
}

unsigned acpi_data_len(GArray *table)
{
    assert(g_array_get_element_size(table) == 1);
    return table->len;
}

void acpi_add_table(GArray *table_offsets, GArray *table_data)
{
    uint32_t offset = cpu_to_le32(table_data->len);
    g_array_append_val(table_offsets, offset);
}

void acpi_build_tables_init(AcpiBuildTables *tables)
{
    tables->rsdp = g_array_new(false, true /* clear */, 1);
    tables->table_data = g_array_new(false, true /* clear */, 1);
    tables->tcpalog = g_array_new(false, true /* clear */, 1);
    tables->linker = bios_linker_loader_init();
}

void acpi_build_tables_cleanup(AcpiBuildTables *tables, bool mfre)
{
    void *linker_data = bios_linker_loader_cleanup(tables->linker);
    g_free(linker_data);
    g_array_free(tables->rsdp, true);
    g_array_free(tables->table_data, true);
    g_array_free(tables->tcpalog, mfre);
}

/* Build rsdt table */
void
build_rsdt(GArray *table_data, BIOSLinker *linker, GArray *table_offsets,
           const char *oem_id, const char *oem_table_id)
{
    AcpiRsdtDescriptorRev1 *rsdt;
    size_t rsdt_len;
    int i;
    const int table_data_len = (sizeof(uint32_t) * table_offsets->len);

    rsdt_len = sizeof(*rsdt) + table_data_len;
    rsdt = acpi_data_push(table_data, rsdt_len);
    memcpy(rsdt->table_offset_entry, table_offsets->data, table_data_len);
    for (i = 0; i < table_offsets->len; ++i) {
        /* rsdt->table_offset_entry to be filled by Guest linker */
        bios_linker_loader_add_pointer(linker,
                                       ACPI_BUILD_TABLE_FILE,
                                       ACPI_BUILD_TABLE_FILE,
                                       &rsdt->table_offset_entry[i],
                                       sizeof(uint32_t));
    }
    build_header(linker, table_data,
                 (void *)rsdt, "RSDT", rsdt_len, 1, oem_id, oem_table_id);
}

void build_srat_memory(AcpiSratMemoryAffinity *numamem, uint64_t base,
                       uint64_t len, int node, MemoryAffinityFlags flags)
{
    numamem->type = ACPI_SRAT_MEMORY;
    numamem->length = sizeof(*numamem);
    numamem->proximity = cpu_to_le32(node);
    numamem->flags = cpu_to_le32(flags);
    numamem->base_addr = cpu_to_le64(base);
    numamem->range_length = cpu_to_le64(len);
}
