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
    /* It would be nicer to use g_string_vprintf but it's only there in 2.22 */
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

/* ACPI 1.0b: 16.2.5.4 Type 2 Opcodes Encoding: DefAnd */
Aml *aml_and(Aml *arg1, Aml *arg2)
{
    Aml *var = aml_opcode(0x7B /* AndOp */);
    aml_append(var, arg1);
    aml_append(var, arg2);
    build_append_byte(var->buf, 0x00 /* NullNameOp */);
    return var;
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

/* ACPI 1.0b: 16.2.5.4 Type 2 Opcodes Encoding: DefLEqual */
Aml *aml_equal(Aml *arg1, Aml *arg2)
{
    Aml *var = aml_opcode(0x93 /* LequalOp */);
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

/* ACPI 1.0b: 6.4.1 ASL Macros for Resource Descriptors */
Aml *aml_resource_template(void)
{
    /* ResourceTemplate is a buffer of Resources with EndTag at the end */
    Aml *var = aml_bundle(0x11 /* BufferOp */, AML_RES_TEMPLATE);
    return var;
}

/* ACPI 1.0b: 16.2.5.4 Type 2 Opcodes Encoding: DefBuffer */
Aml *aml_buffer(void)
{
    Aml *var = aml_bundle(0x11 /* BufferOp */, AML_BUFFER);
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
                          uint32_t offset, uint32_t len)
{
    Aml *var = aml_alloc();
    build_append_byte(var->buf, 0x5B); /* ExtOpPrefix */
    build_append_byte(var->buf, 0x80); /* OpRegionOp */
    build_append_namestring(var->buf, "%s", name);
    build_append_byte(var->buf, rs);
    build_append_int(var->buf, offset);
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
Aml *aml_field(const char *name, AmlFieldFlags flags)
{
    Aml *var = aml_bundle(0x81 /* FieldOp */, AML_EXT_PACKAGE);
    build_append_namestring(var->buf, "%s", name);
    build_append_byte(var->buf, flags);
    return var;
}

/* ACPI 1.0b: 16.2.3 Data Objects Encoding: String */
Aml *aml_string(const char *name_format, ...)
{
    Aml *var = aml_opcode(0x0D /* StringPrefix */);
    va_list ap, va_len;
    char *s;
    int len;

    va_start(ap, name_format);
    va_copy(va_len, ap);
    len = vsnprintf(NULL, 0, name_format, va_len);
    va_end(va_len);
    len += 1;
    s = g_new0(typeof(*s), len);

    len = vsnprintf(s, len, name_format, ap);
    va_end(ap);

    g_array_append_vals(var->buf, s, len);
    build_append_byte(var->buf, 0x0); /* NullChar */
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
    return aml_word_as_desc(aml_bus_number_range, min_fixed, max_fixed, dec,
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
    return aml_word_as_desc(aml_io_range, min_fixed, max_fixed, dec,
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
                      AmlMaxFixed max_fixed, AmlCacheble cacheable,
                      AmlReadAndWrite read_and_write,
                      uint32_t addr_gran, uint32_t addr_min,
                      uint32_t addr_max, uint32_t addr_trans,
                      uint32_t len)
{
    uint8_t flags = read_and_write | (cacheable << 1);

    return aml_dword_as_desc(aml_memory_range, min_fixed, max_fixed,
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
                      AmlMaxFixed max_fixed, AmlCacheble cacheable,
                      AmlReadAndWrite read_and_write,
                      uint64_t addr_gran, uint64_t addr_min,
                      uint64_t addr_max, uint64_t addr_trans,
                      uint64_t len)
{
    uint8_t flags = read_and_write | (cacheable << 1);

    return aml_qword_as_desc(aml_memory_range, min_fixed, max_fixed,
                             dec, addr_gran, addr_min, addr_max,
                             addr_trans, len, flags);
}
