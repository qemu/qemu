/*
 * QEMU migration/snapshot declarations
 *
 * Copyright (c) 2009-2011 Red Hat, Inc.
 *
 * Original author: Juan Quintela <quintela@redhat.com>
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

#ifndef QEMU_VMSTATE_H
#define QEMU_VMSTATE_H

#include "hw/vmstate-if.h"

typedef struct VMStateInfo VMStateInfo;
typedef struct VMStateField VMStateField;

/* VMStateInfo allows customized migration of objects that don't fit in
 * any category in VMStateFlags. Additional information is always passed
 * into get and put in terms of field and vmdesc parameters. However
 * these two parameters should only be used in cases when customized
 * handling is needed, such as QTAILQ. For primitive data types such as
 * integer, field and vmdesc parameters should be ignored inside get/put.
 */
struct VMStateInfo {
    const char *name;
    int (*get)(QEMUFile *f, void *pv, size_t size, const VMStateField *field);
    int (*put)(QEMUFile *f, void *pv, size_t size, const VMStateField *field,
               QJSON *vmdesc);
};

enum VMStateFlags {
    /* Ignored */
    VMS_SINGLE           = 0x001,

    /* The struct member at opaque + VMStateField.offset is a pointer
     * to the actual field (e.g. struct a { uint8_t *b;
     * }). Dereference the pointer before using it as basis for
     * further pointer arithmetic (see e.g. VMS_ARRAY). Does not
     * affect the meaning of VMStateField.num_offset or
     * VMStateField.size_offset; see VMS_VARRAY* and VMS_VBUFFER for
     * those. */
    VMS_POINTER          = 0x002,

    /* The field is an array of fixed size. VMStateField.num contains
     * the number of entries in the array. The size of each entry is
     * given by VMStateField.size and / or opaque +
     * VMStateField.size_offset; see VMS_VBUFFER and
     * VMS_MULTIPLY. Each array entry will be processed individually
     * (VMStateField.info.get()/put() if VMS_STRUCT is not set,
     * recursion into VMStateField.vmsd if VMS_STRUCT is set). May not
     * be combined with VMS_VARRAY*. */
    VMS_ARRAY            = 0x004,

    /* The field is itself a struct, containing one or more
     * fields. Recurse into VMStateField.vmsd. Most useful in
     * combination with VMS_ARRAY / VMS_VARRAY*, recursing into each
     * array entry. */
    VMS_STRUCT           = 0x008,

    /* The field is an array of variable size. The int32_t at opaque +
     * VMStateField.num_offset contains the number of entries in the
     * array. See the VMS_ARRAY description regarding array handling
     * in general. May not be combined with VMS_ARRAY or any other
     * VMS_VARRAY*. */
    VMS_VARRAY_INT32     = 0x010,

    /* Ignored */
    VMS_BUFFER           = 0x020,

    /* The field is a (fixed-size or variable-size) array of pointers
     * (e.g. struct a { uint8_t *b[]; }). Dereference each array entry
     * before using it. Note: Does not imply any one of VMS_ARRAY /
     * VMS_VARRAY*; these need to be set explicitly. */
    VMS_ARRAY_OF_POINTER = 0x040,

    /* The field is an array of variable size. The uint16_t at opaque
     * + VMStateField.num_offset (subject to VMS_MULTIPLY_ELEMENTS)
     * contains the number of entries in the array. See the VMS_ARRAY
     * description regarding array handling in general. May not be
     * combined with VMS_ARRAY or any other VMS_VARRAY*. */
    VMS_VARRAY_UINT16    = 0x080,

    /* The size of the individual entries (a single array entry if
     * VMS_ARRAY or any of VMS_VARRAY* are set, or the field itself if
     * neither is set) is variable (i.e. not known at compile-time),
     * but the same for all entries. Use the int32_t at opaque +
     * VMStateField.size_offset (subject to VMS_MULTIPLY) to determine
     * the size of each (and every) entry. */
    VMS_VBUFFER          = 0x100,

    /* Multiply the entry size given by the int32_t at opaque +
     * VMStateField.size_offset (see VMS_VBUFFER description) with
     * VMStateField.size to determine the number of bytes to be
     * allocated. Only valid in combination with VMS_VBUFFER. */
    VMS_MULTIPLY         = 0x200,

    /* The field is an array of variable size. The uint8_t at opaque +
     * VMStateField.num_offset (subject to VMS_MULTIPLY_ELEMENTS)
     * contains the number of entries in the array. See the VMS_ARRAY
     * description regarding array handling in general. May not be
     * combined with VMS_ARRAY or any other VMS_VARRAY*. */
    VMS_VARRAY_UINT8     = 0x400,

    /* The field is an array of variable size. The uint32_t at opaque
     * + VMStateField.num_offset (subject to VMS_MULTIPLY_ELEMENTS)
     * contains the number of entries in the array. See the VMS_ARRAY
     * description regarding array handling in general. May not be
     * combined with VMS_ARRAY or any other VMS_VARRAY*. */
    VMS_VARRAY_UINT32    = 0x800,

    /* Fail loading the serialised VM state if this field is missing
     * from the input. */
    VMS_MUST_EXIST       = 0x1000,

    /* When loading serialised VM state, allocate memory for the
     * (entire) field. Only valid in combination with
     * VMS_POINTER. Note: Not all combinations with other flags are
     * currently supported, e.g. VMS_ALLOC|VMS_ARRAY_OF_POINTER won't
     * cause the individual entries to be allocated. */
    VMS_ALLOC            = 0x2000,

    /* Multiply the number of entries given by the integer at opaque +
     * VMStateField.num_offset (see VMS_VARRAY*) with VMStateField.num
     * to determine the number of entries in the array. Only valid in
     * combination with one of VMS_VARRAY*. */
    VMS_MULTIPLY_ELEMENTS = 0x4000,

    /* A structure field that is like VMS_STRUCT, but uses
     * VMStateField.struct_version_id to tell which version of the
     * structure we are referencing to use. */
    VMS_VSTRUCT           = 0x8000,
};

typedef enum {
    MIG_PRI_DEFAULT = 0,
    MIG_PRI_IOMMU,              /* Must happen before PCI devices */
    MIG_PRI_PCI_BUS,            /* Must happen before IOMMU */
    MIG_PRI_GICV3_ITS,          /* Must happen before PCI devices */
    MIG_PRI_GICV3,              /* Must happen before the ITS */
    MIG_PRI_MAX,
} MigrationPriority;

struct VMStateField {
    const char *name;
    const char *err_hint;
    size_t offset;
    size_t size;
    size_t start;
    int num;
    size_t num_offset;
    size_t size_offset;
    const VMStateInfo *info;
    enum VMStateFlags flags;
    const VMStateDescription *vmsd;
    int version_id;
    int struct_version_id;
    bool (*field_exists)(void *opaque, int version_id);
};

struct VMStateDescription {
    const char *name;
    int unmigratable;
    int version_id;
    int minimum_version_id;
    int minimum_version_id_old;
    MigrationPriority priority;
    LoadStateHandler *load_state_old;
    int (*pre_load)(void *opaque);
    int (*post_load)(void *opaque, int version_id);
    int (*pre_save)(void *opaque);
    int (*post_save)(void *opaque);
    bool (*needed)(void *opaque);
    bool (*dev_unplug_pending)(void *opaque);

    const VMStateField *fields;
    const VMStateDescription **subsections;
};

extern const VMStateDescription vmstate_dummy;

extern const VMStateInfo vmstate_info_bool;

extern const VMStateInfo vmstate_info_int8;
extern const VMStateInfo vmstate_info_int16;
extern const VMStateInfo vmstate_info_int32;
extern const VMStateInfo vmstate_info_int64;

extern const VMStateInfo vmstate_info_uint8_equal;
extern const VMStateInfo vmstate_info_uint16_equal;
extern const VMStateInfo vmstate_info_int32_equal;
extern const VMStateInfo vmstate_info_uint32_equal;
extern const VMStateInfo vmstate_info_uint64_equal;
extern const VMStateInfo vmstate_info_int32_le;

extern const VMStateInfo vmstate_info_uint8;
extern const VMStateInfo vmstate_info_uint16;
extern const VMStateInfo vmstate_info_uint32;
extern const VMStateInfo vmstate_info_uint64;

/** Put this in the stream when migrating a null pointer.*/
#define VMS_NULLPTR_MARKER (0x30U) /* '0' */
extern const VMStateInfo vmstate_info_nullptr;

extern const VMStateInfo vmstate_info_float64;
extern const VMStateInfo vmstate_info_cpudouble;

extern const VMStateInfo vmstate_info_timer;
extern const VMStateInfo vmstate_info_buffer;
extern const VMStateInfo vmstate_info_unused_buffer;
extern const VMStateInfo vmstate_info_tmp;
extern const VMStateInfo vmstate_info_bitmap;
extern const VMStateInfo vmstate_info_qtailq;
extern const VMStateInfo vmstate_info_gtree;
extern const VMStateInfo vmstate_info_qlist;

#define type_check_2darray(t1,t2,n,m) ((t1(*)[n][m])0 - (t2*)0)
/*
 * Check that type t2 is an array of type t1 of size n,
 * e.g. if t1 is 'foo' and n is 32 then t2 must be 'foo[32]'
 */
#define type_check_array(t1,t2,n) ((t1(*)[n])0 - (t2*)0)
#define type_check_pointer(t1,t2) ((t1**)0 - (t2*)0)
/*
 * type of element 0 of the specified (array) field of the type.
 * Note that if the field is a pointer then this will return the
 * pointed-to type rather than complaining.
 */
#define typeof_elt_of_field(type, field) typeof(((type *)0)->field[0])
/* Check that field f in struct type t2 is an array of t1, of any size */
#define type_check_varray(t1, t2, f)                                 \
    (type_check(t1, typeof_elt_of_field(t2, f))                      \
     + QEMU_BUILD_BUG_ON_ZERO(!QEMU_IS_ARRAY(((t2 *)0)->f)))

#define vmstate_offset_value(_state, _field, _type)                  \
    (offsetof(_state, _field) +                                      \
     type_check(_type, typeof_field(_state, _field)))

#define vmstate_offset_pointer(_state, _field, _type)                \
    (offsetof(_state, _field) +                                      \
     type_check_pointer(_type, typeof_field(_state, _field)))

#define vmstate_offset_array(_state, _field, _type, _num)            \
    (offsetof(_state, _field) +                                      \
     type_check_array(_type, typeof_field(_state, _field), _num))

#define vmstate_offset_2darray(_state, _field, _type, _n1, _n2)      \
    (offsetof(_state, _field) +                                      \
     type_check_2darray(_type, typeof_field(_state, _field), _n1, _n2))

#define vmstate_offset_sub_array(_state, _field, _type, _start)      \
    vmstate_offset_value(_state, _field[_start], _type)

#define vmstate_offset_buffer(_state, _field)                        \
    vmstate_offset_array(_state, _field, uint8_t,                    \
                         sizeof(typeof_field(_state, _field)))

#define vmstate_offset_varray(_state, _field, _type)                 \
    (offsetof(_state, _field) +                                      \
     type_check_varray(_type, _state, _field))

/* In the macros below, if there is a _version, that means the macro's
 * field will be processed only if the version being received is >=
 * the _version specified.  In general, if you add a new field, you
 * would increment the structure's version and put that version
 * number into the new field so it would only be processed with the
 * new version.
 *
 * In particular, for VMSTATE_STRUCT() and friends the _version does
 * *NOT* pick the version of the sub-structure.  It works just as
 * specified above.  The version of the top-level structure received
 * is passed down to all sub-structures.  This means that the
 * sub-structures must have version that are compatible with all the
 * structures that use them.
 *
 * If you want to specify the version of the sub-structure, use
 * VMSTATE_VSTRUCT(), which allows the specific sub-structure version
 * to be directly specified.
 */

#define VMSTATE_SINGLE_TEST(_field, _state, _test, _version, _info, _type) { \
    .name         = (stringify(_field)),                             \
    .version_id   = (_version),                                      \
    .field_exists = (_test),                                         \
    .size         = sizeof(_type),                                   \
    .info         = &(_info),                                        \
    .flags        = VMS_SINGLE,                                      \
    .offset       = vmstate_offset_value(_state, _field, _type),     \
}

#define VMSTATE_SINGLE_FULL(_field, _state, _test, _version, _info,  \
                            _type, _err_hint) {                      \
    .name         = (stringify(_field)),                             \
    .err_hint     = (_err_hint),                                     \
    .version_id   = (_version),                                      \
    .field_exists = (_test),                                         \
    .size         = sizeof(_type),                                   \
    .info         = &(_info),                                        \
    .flags        = VMS_SINGLE,                                      \
    .offset       = vmstate_offset_value(_state, _field, _type),     \
}

/* Validate state using a boolean predicate. */
#define VMSTATE_VALIDATE(_name, _test) { \
    .name         = (_name),                                         \
    .field_exists = (_test),                                         \
    .flags        = VMS_ARRAY | VMS_MUST_EXIST,                      \
    .num          = 0, /* 0 elements: no data, only run _test */     \
}

#define VMSTATE_POINTER(_field, _state, _version, _info, _type) {    \
    .name       = (stringify(_field)),                               \
    .version_id = (_version),                                        \
    .info       = &(_info),                                          \
    .size       = sizeof(_type),                                     \
    .flags      = VMS_SINGLE|VMS_POINTER,                            \
    .offset     = vmstate_offset_value(_state, _field, _type),       \
}

#define VMSTATE_POINTER_TEST(_field, _state, _test, _info, _type) {  \
    .name       = (stringify(_field)),                               \
    .info       = &(_info),                                          \
    .field_exists = (_test),                                         \
    .size       = sizeof(_type),                                     \
    .flags      = VMS_SINGLE|VMS_POINTER,                            \
    .offset     = vmstate_offset_value(_state, _field, _type),       \
}

#define VMSTATE_ARRAY(_field, _state, _num, _version, _info, _type) {\
    .name       = (stringify(_field)),                               \
    .version_id = (_version),                                        \
    .num        = (_num),                                            \
    .info       = &(_info),                                          \
    .size       = sizeof(_type),                                     \
    .flags      = VMS_ARRAY,                                         \
    .offset     = vmstate_offset_array(_state, _field, _type, _num), \
}

#define VMSTATE_2DARRAY(_field, _state, _n1, _n2, _version, _info, _type) { \
    .name       = (stringify(_field)),                                      \
    .version_id = (_version),                                               \
    .num        = (_n1) * (_n2),                                            \
    .info       = &(_info),                                                 \
    .size       = sizeof(_type),                                            \
    .flags      = VMS_ARRAY,                                                \
    .offset     = vmstate_offset_2darray(_state, _field, _type, _n1, _n2),  \
}

#define VMSTATE_VARRAY_MULTIPLY(_field, _state, _field_num, _multiply, _info, _type) { \
    .name       = (stringify(_field)),                               \
    .num_offset = vmstate_offset_value(_state, _field_num, uint32_t),\
    .num        = (_multiply),                                       \
    .info       = &(_info),                                          \
    .size       = sizeof(_type),                                     \
    .flags      = VMS_VARRAY_UINT32|VMS_MULTIPLY_ELEMENTS,           \
    .offset     = vmstate_offset_varray(_state, _field, _type),      \
}

#define VMSTATE_ARRAY_TEST(_field, _state, _num, _test, _info, _type) {\
    .name         = (stringify(_field)),                              \
    .field_exists = (_test),                                          \
    .num          = (_num),                                           \
    .info         = &(_info),                                         \
    .size         = sizeof(_type),                                    \
    .flags        = VMS_ARRAY,                                        \
    .offset       = vmstate_offset_array(_state, _field, _type, _num),\
}

#define VMSTATE_SUB_ARRAY(_field, _state, _start, _num, _version, _info, _type) { \
    .name       = (stringify(_field)),                               \
    .version_id = (_version),                                        \
    .num        = (_num),                                            \
    .info       = &(_info),                                          \
    .size       = sizeof(_type),                                     \
    .flags      = VMS_ARRAY,                                         \
    .offset     = vmstate_offset_sub_array(_state, _field, _type, _start), \
}

#define VMSTATE_ARRAY_INT32_UNSAFE(_field, _state, _field_num, _info, _type) {\
    .name       = (stringify(_field)),                               \
    .num_offset = vmstate_offset_value(_state, _field_num, int32_t), \
    .info       = &(_info),                                          \
    .size       = sizeof(_type),                                     \
    .flags      = VMS_VARRAY_INT32,                                  \
    .offset     = vmstate_offset_varray(_state, _field, _type),      \
}

#define VMSTATE_VARRAY_INT32(_field, _state, _field_num, _version, _info, _type) {\
    .name       = (stringify(_field)),                               \
    .version_id = (_version),                                        \
    .num_offset = vmstate_offset_value(_state, _field_num, int32_t), \
    .info       = &(_info),                                          \
    .size       = sizeof(_type),                                     \
    .flags      = VMS_VARRAY_INT32|VMS_POINTER,                      \
    .offset     = vmstate_offset_pointer(_state, _field, _type),     \
}

#define VMSTATE_VARRAY_UINT32(_field, _state, _field_num, _version, _info, _type) {\
    .name       = (stringify(_field)),                               \
    .version_id = (_version),                                        \
    .num_offset = vmstate_offset_value(_state, _field_num, uint32_t),\
    .info       = &(_info),                                          \
    .size       = sizeof(_type),                                     \
    .flags      = VMS_VARRAY_UINT32|VMS_POINTER,                     \
    .offset     = vmstate_offset_pointer(_state, _field, _type),     \
}

#define VMSTATE_VARRAY_UINT32_ALLOC(_field, _state, _field_num, _version, _info, _type) {\
    .name       = (stringify(_field)),                               \
    .version_id = (_version),                                        \
    .num_offset = vmstate_offset_value(_state, _field_num, uint32_t),\
    .info       = &(_info),                                          \
    .size       = sizeof(_type),                                     \
    .flags      = VMS_VARRAY_UINT32|VMS_POINTER|VMS_ALLOC,           \
    .offset     = vmstate_offset_pointer(_state, _field, _type),     \
}

#define VMSTATE_VARRAY_UINT16_ALLOC(_field, _state, _field_num, _version, _info, _type) {\
    .name       = (stringify(_field)),                               \
    .version_id = (_version),                                        \
    .num_offset = vmstate_offset_value(_state, _field_num, uint16_t),\
    .info       = &(_info),                                          \
    .size       = sizeof(_type),                                     \
    .flags      = VMS_VARRAY_UINT16 | VMS_POINTER | VMS_ALLOC,       \
    .offset     = vmstate_offset_pointer(_state, _field, _type),     \
}

#define VMSTATE_VARRAY_UINT16_UNSAFE(_field, _state, _field_num, _version, _info, _type) {\
    .name       = (stringify(_field)),                               \
    .version_id = (_version),                                        \
    .num_offset = vmstate_offset_value(_state, _field_num, uint16_t),\
    .info       = &(_info),                                          \
    .size       = sizeof(_type),                                     \
    .flags      = VMS_VARRAY_UINT16,                                 \
    .offset     = vmstate_offset_varray(_state, _field, _type),      \
}

#define VMSTATE_VSTRUCT_TEST(_field, _state, _test, _version, _vmsd, _type, _struct_version) { \
    .name         = (stringify(_field)),                             \
    .version_id   = (_version),                                      \
    .struct_version_id = (_struct_version),                          \
    .field_exists = (_test),                                         \
    .vmsd         = &(_vmsd),                                        \
    .size         = sizeof(_type),                                   \
    .flags        = VMS_VSTRUCT,                                     \
    .offset       = vmstate_offset_value(_state, _field, _type),     \
}

#define VMSTATE_STRUCT_TEST(_field, _state, _test, _version, _vmsd, _type) { \
    .name         = (stringify(_field)),                             \
    .version_id   = (_version),                                      \
    .field_exists = (_test),                                         \
    .vmsd         = &(_vmsd),                                        \
    .size         = sizeof(_type),                                   \
    .flags        = VMS_STRUCT,                                      \
    .offset       = vmstate_offset_value(_state, _field, _type),     \
}

#define VMSTATE_STRUCT_POINTER_V(_field, _state, _version, _vmsd, _type) { \
    .name         = (stringify(_field)),                             \
    .version_id   = (_version),                                        \
    .vmsd         = &(_vmsd),                                        \
    .size         = sizeof(_type *),                                 \
    .flags        = VMS_STRUCT|VMS_POINTER,                          \
    .offset       = vmstate_offset_pointer(_state, _field, _type),   \
}

#define VMSTATE_STRUCT_POINTER_TEST_V(_field, _state, _test, _version, _vmsd, _type) { \
    .name         = (stringify(_field)),                             \
    .version_id   = (_version),                                        \
    .field_exists = (_test),                                         \
    .vmsd         = &(_vmsd),                                        \
    .size         = sizeof(_type *),                                 \
    .flags        = VMS_STRUCT|VMS_POINTER,                          \
    .offset       = vmstate_offset_pointer(_state, _field, _type),   \
}

#define VMSTATE_ARRAY_OF_POINTER(_field, _state, _num, _version, _info, _type) {\
    .name       = (stringify(_field)),                               \
    .version_id = (_version),                                        \
    .num        = (_num),                                            \
    .info       = &(_info),                                          \
    .size       = sizeof(_type),                                     \
    .flags      = VMS_ARRAY|VMS_ARRAY_OF_POINTER,                    \
    .offset     = vmstate_offset_array(_state, _field, _type, _num), \
}

#define VMSTATE_ARRAY_OF_POINTER_TO_STRUCT(_f, _s, _n, _v, _vmsd, _type) { \
    .name       = (stringify(_f)),                                   \
    .version_id = (_v),                                              \
    .num        = (_n),                                              \
    .vmsd       = &(_vmsd),                                          \
    .size       = sizeof(_type *),                                    \
    .flags      = VMS_ARRAY|VMS_STRUCT|VMS_ARRAY_OF_POINTER,         \
    .offset     = vmstate_offset_array(_s, _f, _type*, _n),          \
}

#define VMSTATE_STRUCT_SUB_ARRAY(_field, _state, _start, _num, _version, _vmsd, _type) { \
    .name       = (stringify(_field)),                                     \
    .version_id = (_version),                                              \
    .num        = (_num),                                                  \
    .vmsd       = &(_vmsd),                                                \
    .size       = sizeof(_type),                                           \
    .flags      = VMS_STRUCT|VMS_ARRAY,                                    \
    .offset     = vmstate_offset_sub_array(_state, _field, _type, _start), \
}

#define VMSTATE_STRUCT_ARRAY_TEST(_field, _state, _num, _test, _version, _vmsd, _type) { \
    .name         = (stringify(_field)),                             \
    .num          = (_num),                                          \
    .field_exists = (_test),                                         \
    .version_id   = (_version),                                      \
    .vmsd         = &(_vmsd),                                        \
    .size         = sizeof(_type),                                   \
    .flags        = VMS_STRUCT|VMS_ARRAY,                            \
    .offset       = vmstate_offset_array(_state, _field, _type, _num),\
}

#define VMSTATE_STRUCT_2DARRAY_TEST(_field, _state, _n1, _n2, _test, \
                                    _version, _vmsd, _type) {        \
    .name         = (stringify(_field)),                             \
    .num          = (_n1) * (_n2),                                   \
    .field_exists = (_test),                                         \
    .version_id   = (_version),                                      \
    .vmsd         = &(_vmsd),                                        \
    .size         = sizeof(_type),                                   \
    .flags        = VMS_STRUCT | VMS_ARRAY,                          \
    .offset       = vmstate_offset_2darray(_state, _field, _type,    \
                                           _n1, _n2),                \
}

#define VMSTATE_STRUCT_VARRAY_UINT8(_field, _state, _field_num, _version, _vmsd, _type) { \
    .name       = (stringify(_field)),                               \
    .num_offset = vmstate_offset_value(_state, _field_num, uint8_t), \
    .version_id = (_version),                                        \
    .vmsd       = &(_vmsd),                                          \
    .size       = sizeof(_type),                                     \
    .flags      = VMS_STRUCT|VMS_VARRAY_UINT8,                       \
    .offset     = vmstate_offset_varray(_state, _field, _type),      \
}

/* a variable length array (i.e. _type *_field) but we know the
 * length
 */
#define VMSTATE_STRUCT_VARRAY_POINTER_KNOWN(_field, _state, _num, _version, _vmsd, _type) { \
    .name       = (stringify(_field)),                               \
    .num          = (_num),                                          \
    .version_id = (_version),                                        \
    .vmsd       = &(_vmsd),                                          \
    .size       = sizeof(_type),                                     \
    .flags      = VMS_STRUCT|VMS_ARRAY|VMS_POINTER,                  \
    .offset     = offsetof(_state, _field),                          \
}

#define VMSTATE_STRUCT_VARRAY_POINTER_INT32(_field, _state, _field_num, _vmsd, _type) { \
    .name       = (stringify(_field)),                               \
    .version_id = 0,                                                 \
    .num_offset = vmstate_offset_value(_state, _field_num, int32_t), \
    .size       = sizeof(_type),                                     \
    .vmsd       = &(_vmsd),                                          \
    .flags      = VMS_POINTER | VMS_VARRAY_INT32 | VMS_STRUCT,       \
    .offset     = vmstate_offset_pointer(_state, _field, _type),     \
}

#define VMSTATE_STRUCT_VARRAY_POINTER_UINT32(_field, _state, _field_num, _vmsd, _type) { \
    .name       = (stringify(_field)),                               \
    .version_id = 0,                                                 \
    .num_offset = vmstate_offset_value(_state, _field_num, uint32_t),\
    .size       = sizeof(_type),                                     \
    .vmsd       = &(_vmsd),                                          \
    .flags      = VMS_POINTER | VMS_VARRAY_INT32 | VMS_STRUCT,       \
    .offset     = vmstate_offset_pointer(_state, _field, _type),     \
}

#define VMSTATE_STRUCT_VARRAY_POINTER_UINT16(_field, _state, _field_num, _vmsd, _type) { \
    .name       = (stringify(_field)),                               \
    .version_id = 0,                                                 \
    .num_offset = vmstate_offset_value(_state, _field_num, uint16_t),\
    .size       = sizeof(_type),                                     \
    .vmsd       = &(_vmsd),                                          \
    .flags      = VMS_POINTER | VMS_VARRAY_UINT16 | VMS_STRUCT,      \
    .offset     = vmstate_offset_pointer(_state, _field, _type),     \
}

#define VMSTATE_STRUCT_VARRAY_INT32(_field, _state, _field_num, _version, _vmsd, _type) { \
    .name       = (stringify(_field)),                               \
    .num_offset = vmstate_offset_value(_state, _field_num, int32_t), \
    .version_id = (_version),                                        \
    .vmsd       = &(_vmsd),                                          \
    .size       = sizeof(_type),                                     \
    .flags      = VMS_STRUCT|VMS_VARRAY_INT32,                       \
    .offset     = vmstate_offset_varray(_state, _field, _type),      \
}

#define VMSTATE_STRUCT_VARRAY_UINT32(_field, _state, _field_num, _version, _vmsd, _type) { \
    .name       = (stringify(_field)),                               \
    .num_offset = vmstate_offset_value(_state, _field_num, uint32_t), \
    .version_id = (_version),                                        \
    .vmsd       = &(_vmsd),                                          \
    .size       = sizeof(_type),                                     \
    .flags      = VMS_STRUCT|VMS_VARRAY_UINT32,                      \
    .offset     = vmstate_offset_varray(_state, _field, _type),      \
}

#define VMSTATE_STRUCT_VARRAY_ALLOC(_field, _state, _field_num, _version, _vmsd, _type) {\
    .name       = (stringify(_field)),                               \
    .version_id = (_version),                                        \
    .vmsd       = &(_vmsd),                                          \
    .num_offset = vmstate_offset_value(_state, _field_num, int32_t), \
    .size       = sizeof(_type),                                     \
    .flags      = VMS_STRUCT|VMS_VARRAY_INT32|VMS_ALLOC|VMS_POINTER, \
    .offset     = vmstate_offset_pointer(_state, _field, _type),     \
}

#define VMSTATE_STATIC_BUFFER(_field, _state, _version, _test, _start, _size) { \
    .name         = (stringify(_field)),                             \
    .version_id   = (_version),                                      \
    .field_exists = (_test),                                         \
    .size         = (_size - _start),                                \
    .info         = &vmstate_info_buffer,                            \
    .flags        = VMS_BUFFER,                                      \
    .offset       = vmstate_offset_buffer(_state, _field) + _start,  \
}

#define VMSTATE_VBUFFER_MULTIPLY(_field, _state, _version, _test,    \
                                 _field_size, _multiply) {           \
    .name         = (stringify(_field)),                             \
    .version_id   = (_version),                                      \
    .field_exists = (_test),                                         \
    .size_offset  = vmstate_offset_value(_state, _field_size, uint32_t),\
    .size         = (_multiply),                                      \
    .info         = &vmstate_info_buffer,                            \
    .flags        = VMS_VBUFFER|VMS_POINTER|VMS_MULTIPLY,            \
    .offset       = offsetof(_state, _field),                        \
}

#define VMSTATE_VBUFFER(_field, _state, _version, _test, _field_size) { \
    .name         = (stringify(_field)),                             \
    .version_id   = (_version),                                      \
    .field_exists = (_test),                                         \
    .size_offset  = vmstate_offset_value(_state, _field_size, int32_t),\
    .info         = &vmstate_info_buffer,                            \
    .flags        = VMS_VBUFFER|VMS_POINTER,                         \
    .offset       = offsetof(_state, _field),                        \
}

#define VMSTATE_VBUFFER_UINT32(_field, _state, _version, _test, _field_size) { \
    .name         = (stringify(_field)),                             \
    .version_id   = (_version),                                      \
    .field_exists = (_test),                                         \
    .size_offset  = vmstate_offset_value(_state, _field_size, uint32_t),\
    .info         = &vmstate_info_buffer,                            \
    .flags        = VMS_VBUFFER|VMS_POINTER,                         \
    .offset       = offsetof(_state, _field),                        \
}

#define VMSTATE_VBUFFER_ALLOC_UINT32(_field, _state, _version,       \
                                     _test, _field_size) {           \
    .name         = (stringify(_field)),                             \
    .version_id   = (_version),                                      \
    .field_exists = (_test),                                         \
    .size_offset  = vmstate_offset_value(_state, _field_size, uint32_t),\
    .info         = &vmstate_info_buffer,                            \
    .flags        = VMS_VBUFFER|VMS_POINTER|VMS_ALLOC,               \
    .offset       = offsetof(_state, _field),                        \
}

#define VMSTATE_BUFFER_UNSAFE_INFO_TEST(_field, _state, _test, _version, _info, _size) { \
    .name       = (stringify(_field)),                               \
    .version_id = (_version),                                        \
    .field_exists = (_test),                                         \
    .size       = (_size),                                           \
    .info       = &(_info),                                          \
    .flags      = VMS_BUFFER,                                        \
    .offset     = offsetof(_state, _field),                          \
}

#define VMSTATE_BUFFER_POINTER_UNSAFE(_field, _state, _version, _size) { \
    .name       = (stringify(_field)),                               \
    .version_id = (_version),                                        \
    .size       = (_size),                                           \
    .info       = &vmstate_info_buffer,                              \
    .flags      = VMS_BUFFER|VMS_POINTER,                            \
    .offset     = offsetof(_state, _field),                          \
}

/* Allocate a temporary of type 'tmp_type', set tmp->parent to _state
 * and execute the vmsd on the temporary.  Note that we're working with
 * the whole of _state here, not a field within it.
 * We compile time check that:
 *    That _tmp_type contains a 'parent' member that's a pointer to the
 *        '_state' type
 *    That the pointer is right at the start of _tmp_type.
 */
#define VMSTATE_WITH_TMP(_state, _tmp_type, _vmsd) {                 \
    .name         = "tmp",                                           \
    .size         = sizeof(_tmp_type) +                              \
                    QEMU_BUILD_BUG_ON_ZERO(offsetof(_tmp_type, parent) != 0) + \
                    type_check_pointer(_state,                       \
                        typeof_field(_tmp_type, parent)),            \
    .vmsd         = &(_vmsd),                                        \
    .info         = &vmstate_info_tmp,                               \
}

#define VMSTATE_UNUSED_BUFFER(_test, _version, _size) {              \
    .name         = "unused",                                        \
    .field_exists = (_test),                                         \
    .version_id   = (_version),                                      \
    .size         = (_size),                                         \
    .info         = &vmstate_info_unused_buffer,                     \
    .flags        = VMS_BUFFER,                                      \
}

/* Discard size * field_num bytes, where field_num is a uint32 member */
#define VMSTATE_UNUSED_VARRAY_UINT32(_state, _test, _version, _field_num, _size) {\
    .name         = "unused",                                        \
    .field_exists = (_test),                                         \
    .num_offset   = vmstate_offset_value(_state, _field_num, uint32_t),\
    .version_id   = (_version),                                      \
    .size         = (_size),                                         \
    .info         = &vmstate_info_unused_buffer,                     \
    .flags        = VMS_VARRAY_UINT32 | VMS_BUFFER,                  \
}

/* _field_size should be a int32_t field in the _state struct giving the
 * size of the bitmap _field in bits.
 */
#define VMSTATE_BITMAP(_field, _state, _version, _field_size) {      \
    .name         = (stringify(_field)),                             \
    .version_id   = (_version),                                      \
    .size_offset  = vmstate_offset_value(_state, _field_size, int32_t),\
    .info         = &vmstate_info_bitmap,                            \
    .flags        = VMS_VBUFFER|VMS_POINTER,                         \
    .offset       = offsetof(_state, _field),                        \
}

/* For migrating a QTAILQ.
 * Target QTAILQ needs be properly initialized.
 * _type: type of QTAILQ element
 * _next: name of QTAILQ entry field in QTAILQ element
 * _vmsd: VMSD for QTAILQ element
 * size: size of QTAILQ element
 * start: offset of QTAILQ entry in QTAILQ element
 */
#define VMSTATE_QTAILQ_V(_field, _state, _version, _vmsd, _type, _next)  \
{                                                                        \
    .name         = (stringify(_field)),                                 \
    .version_id   = (_version),                                          \
    .vmsd         = &(_vmsd),                                            \
    .size         = sizeof(_type),                                       \
    .info         = &vmstate_info_qtailq,                                \
    .offset       = offsetof(_state, _field),                            \
    .start        = offsetof(_type, _next),                              \
}

/*
 * For migrating a GTree whose key is a pointer to _key_type and the
 * value, a pointer to _val_type
 * The target tree must have been properly initialized
 * _vmsd: Start address of the 2 element array containing the data vmsd
 *        and the key vmsd, in that order
 * _key_type: type of the key
 * _val_type: type of the value
 */
#define VMSTATE_GTREE_V(_field, _state, _version, _vmsd,                       \
                        _key_type, _val_type)                                  \
{                                                                              \
    .name         = (stringify(_field)),                                       \
    .version_id   = (_version),                                                \
    .vmsd         = (_vmsd),                                                   \
    .info         = &vmstate_info_gtree,                                       \
    .start        = sizeof(_key_type),                                         \
    .size         = sizeof(_val_type),                                         \
    .offset       = offsetof(_state, _field),                                  \
}

/*
 * For migrating a GTree with direct key and the value a pointer
 * to _val_type
 * The target tree must have been properly initialized
 * _vmsd: data vmsd
 * _val_type: type of the value
 */
#define VMSTATE_GTREE_DIRECT_KEY_V(_field, _state, _version, _vmsd, _val_type) \
{                                                                              \
    .name         = (stringify(_field)),                                       \
    .version_id   = (_version),                                                \
    .vmsd         = (_vmsd),                                                   \
    .info         = &vmstate_info_gtree,                                       \
    .start        = 0,                                                         \
    .size         = sizeof(_val_type),                                         \
    .offset       = offsetof(_state, _field),                                  \
}

/*
 * For migrating a QLIST
 * Target QLIST needs be properly initialized.
 * _type: type of QLIST element
 * _next: name of QLIST_ENTRY entry field in QLIST element
 * _vmsd: VMSD for QLIST element
 * size: size of QLIST element
 * start: offset of QLIST_ENTRY in QTAILQ element
 */
#define VMSTATE_QLIST_V(_field, _state, _version, _vmsd, _type, _next)  \
{                                                                        \
    .name         = (stringify(_field)),                                 \
    .version_id   = (_version),                                          \
    .vmsd         = &(_vmsd),                                            \
    .size         = sizeof(_type),                                       \
    .info         = &vmstate_info_qlist,                                 \
    .offset       = offsetof(_state, _field),                            \
    .start        = offsetof(_type, _next),                              \
}

/* _f : field name
   _f_n : num of elements field_name
   _n : num of elements
   _s : struct state name
   _v : version
*/

#define VMSTATE_SINGLE(_field, _state, _version, _info, _type)        \
    VMSTATE_SINGLE_TEST(_field, _state, NULL, _version, _info, _type)

#define VMSTATE_VSTRUCT(_field, _state, _vmsd, _type, _struct_version)\
    VMSTATE_VSTRUCT_TEST(_field, _state, NULL, 0, _vmsd, _type, _struct_version)

#define VMSTATE_VSTRUCT_V(_field, _state, _version, _vmsd, _type, _struct_version) \
    VMSTATE_VSTRUCT_TEST(_field, _state, NULL, _version, _vmsd, _type, \
                         _struct_version)

#define VMSTATE_STRUCT(_field, _state, _version, _vmsd, _type)        \
    VMSTATE_STRUCT_TEST(_field, _state, NULL, _version, _vmsd, _type)

#define VMSTATE_STRUCT_POINTER(_field, _state, _vmsd, _type)          \
    VMSTATE_STRUCT_POINTER_V(_field, _state, 0, _vmsd, _type)

#define VMSTATE_STRUCT_POINTER_TEST(_field, _state, _test, _vmsd, _type)     \
    VMSTATE_STRUCT_POINTER_TEST_V(_field, _state, _test, 0, _vmsd, _type)

#define VMSTATE_STRUCT_ARRAY(_field, _state, _num, _version, _vmsd, _type) \
    VMSTATE_STRUCT_ARRAY_TEST(_field, _state, _num, NULL, _version,   \
            _vmsd, _type)

#define VMSTATE_STRUCT_2DARRAY(_field, _state, _n1, _n2, _version,    \
            _vmsd, _type)                                             \
    VMSTATE_STRUCT_2DARRAY_TEST(_field, _state, _n1, _n2, NULL,       \
            _version, _vmsd, _type)

#define VMSTATE_BUFFER_UNSAFE_INFO(_field, _state, _version, _info, _size) \
    VMSTATE_BUFFER_UNSAFE_INFO_TEST(_field, _state, NULL, _version, _info, \
            _size)

#define VMSTATE_BOOL_V(_f, _s, _v)                                    \
    VMSTATE_SINGLE(_f, _s, _v, vmstate_info_bool, bool)

#define VMSTATE_INT8_V(_f, _s, _v)                                    \
    VMSTATE_SINGLE(_f, _s, _v, vmstate_info_int8, int8_t)
#define VMSTATE_INT16_V(_f, _s, _v)                                   \
    VMSTATE_SINGLE(_f, _s, _v, vmstate_info_int16, int16_t)
#define VMSTATE_INT32_V(_f, _s, _v)                                   \
    VMSTATE_SINGLE(_f, _s, _v, vmstate_info_int32, int32_t)
#define VMSTATE_INT64_V(_f, _s, _v)                                   \
    VMSTATE_SINGLE(_f, _s, _v, vmstate_info_int64, int64_t)

#define VMSTATE_UINT8_V(_f, _s, _v)                                   \
    VMSTATE_SINGLE(_f, _s, _v, vmstate_info_uint8, uint8_t)
#define VMSTATE_UINT16_V(_f, _s, _v)                                  \
    VMSTATE_SINGLE(_f, _s, _v, vmstate_info_uint16, uint16_t)
#define VMSTATE_UINT32_V(_f, _s, _v)                                  \
    VMSTATE_SINGLE(_f, _s, _v, vmstate_info_uint32, uint32_t)
#define VMSTATE_UINT64_V(_f, _s, _v)                                  \
    VMSTATE_SINGLE(_f, _s, _v, vmstate_info_uint64, uint64_t)

#ifdef CONFIG_LINUX

#define VMSTATE_U8_V(_f, _s, _v)                                   \
    VMSTATE_SINGLE(_f, _s, _v, vmstate_info_uint8, __u8)
#define VMSTATE_U16_V(_f, _s, _v)                                  \
    VMSTATE_SINGLE(_f, _s, _v, vmstate_info_uint16, __u16)
#define VMSTATE_U32_V(_f, _s, _v)                                  \
    VMSTATE_SINGLE(_f, _s, _v, vmstate_info_uint32, __u32)
#define VMSTATE_U64_V(_f, _s, _v)                                  \
    VMSTATE_SINGLE(_f, _s, _v, vmstate_info_uint64, __u64)

#endif

#define VMSTATE_BOOL(_f, _s)                                          \
    VMSTATE_BOOL_V(_f, _s, 0)

#define VMSTATE_INT8(_f, _s)                                          \
    VMSTATE_INT8_V(_f, _s, 0)
#define VMSTATE_INT16(_f, _s)                                         \
    VMSTATE_INT16_V(_f, _s, 0)
#define VMSTATE_INT32(_f, _s)                                         \
    VMSTATE_INT32_V(_f, _s, 0)
#define VMSTATE_INT64(_f, _s)                                         \
    VMSTATE_INT64_V(_f, _s, 0)

#define VMSTATE_UINT8(_f, _s)                                         \
    VMSTATE_UINT8_V(_f, _s, 0)
#define VMSTATE_UINT16(_f, _s)                                        \
    VMSTATE_UINT16_V(_f, _s, 0)
#define VMSTATE_UINT32(_f, _s)                                        \
    VMSTATE_UINT32_V(_f, _s, 0)
#define VMSTATE_UINT64(_f, _s)                                        \
    VMSTATE_UINT64_V(_f, _s, 0)

#ifdef CONFIG_LINUX

#define VMSTATE_U8(_f, _s)                                         \
    VMSTATE_U8_V(_f, _s, 0)
#define VMSTATE_U16(_f, _s)                                        \
    VMSTATE_U16_V(_f, _s, 0)
#define VMSTATE_U32(_f, _s)                                        \
    VMSTATE_U32_V(_f, _s, 0)
#define VMSTATE_U64(_f, _s)                                        \
    VMSTATE_U64_V(_f, _s, 0)

#endif

#define VMSTATE_UINT8_EQUAL(_f, _s, _err_hint)                        \
    VMSTATE_SINGLE_FULL(_f, _s, 0, 0,                                 \
                        vmstate_info_uint8_equal, uint8_t, _err_hint)

#define VMSTATE_UINT16_EQUAL(_f, _s, _err_hint)                       \
    VMSTATE_SINGLE_FULL(_f, _s, 0, 0,                                 \
                        vmstate_info_uint16_equal, uint16_t, _err_hint)

#define VMSTATE_UINT16_EQUAL_V(_f, _s, _v, _err_hint)                 \
    VMSTATE_SINGLE_FULL(_f, _s, 0,  _v,                               \
                        vmstate_info_uint16_equal, uint16_t, _err_hint)

#define VMSTATE_INT32_EQUAL(_f, _s, _err_hint)                        \
    VMSTATE_SINGLE_FULL(_f, _s, 0, 0,                                 \
                        vmstate_info_int32_equal, int32_t, _err_hint)

#define VMSTATE_UINT32_EQUAL_V(_f, _s, _v, _err_hint)                 \
    VMSTATE_SINGLE_FULL(_f, _s, 0,  _v,                               \
                        vmstate_info_uint32_equal, uint32_t, _err_hint)

#define VMSTATE_UINT32_EQUAL(_f, _s, _err_hint)                       \
    VMSTATE_UINT32_EQUAL_V(_f, _s, 0, _err_hint)

#define VMSTATE_UINT64_EQUAL_V(_f, _s, _v, _err_hint)                 \
    VMSTATE_SINGLE_FULL(_f, _s, 0,  _v,                               \
                        vmstate_info_uint64_equal, uint64_t, _err_hint)

#define VMSTATE_UINT64_EQUAL(_f, _s, _err_hint)                       \
    VMSTATE_UINT64_EQUAL_V(_f, _s, 0, _err_hint)

#define VMSTATE_INT32_POSITIVE_LE(_f, _s)                             \
    VMSTATE_SINGLE(_f, _s, 0, vmstate_info_int32_le, int32_t)

#define VMSTATE_BOOL_TEST(_f, _s, _t)                               \
    VMSTATE_SINGLE_TEST(_f, _s, _t, 0, vmstate_info_bool, bool)

#define VMSTATE_INT8_TEST(_f, _s, _t)                               \
    VMSTATE_SINGLE_TEST(_f, _s, _t, 0, vmstate_info_int8, int8_t)

#define VMSTATE_INT16_TEST(_f, _s, _t)                               \
    VMSTATE_SINGLE_TEST(_f, _s, _t, 0, vmstate_info_int16, int16_t)

#define VMSTATE_INT32_TEST(_f, _s, _t)                                  \
    VMSTATE_SINGLE_TEST(_f, _s, _t, 0, vmstate_info_int32, int32_t)

#define VMSTATE_INT64_TEST(_f, _s, _t)                                  \
    VMSTATE_SINGLE_TEST(_f, _s, _t, 0, vmstate_info_int64, int64_t)

#define VMSTATE_UINT8_TEST(_f, _s, _t)                               \
    VMSTATE_SINGLE_TEST(_f, _s, _t, 0, vmstate_info_uint8, uint8_t)

#define VMSTATE_UINT16_TEST(_f, _s, _t)                               \
    VMSTATE_SINGLE_TEST(_f, _s, _t, 0, vmstate_info_uint16, uint16_t)

#define VMSTATE_UINT32_TEST(_f, _s, _t)                                  \
    VMSTATE_SINGLE_TEST(_f, _s, _t, 0, vmstate_info_uint32, uint32_t)

#define VMSTATE_UINT64_TEST(_f, _s, _t)                                  \
    VMSTATE_SINGLE_TEST(_f, _s, _t, 0, vmstate_info_uint64, uint64_t)


#define VMSTATE_FLOAT64_V(_f, _s, _v)                                 \
    VMSTATE_SINGLE(_f, _s, _v, vmstate_info_float64, float64)

#define VMSTATE_FLOAT64(_f, _s)                                       \
    VMSTATE_FLOAT64_V(_f, _s, 0)

#define VMSTATE_TIMER_PTR_TEST(_f, _s, _test)                             \
    VMSTATE_POINTER_TEST(_f, _s, _test, vmstate_info_timer, QEMUTimer *)

#define VMSTATE_TIMER_PTR_V(_f, _s, _v)                                   \
    VMSTATE_POINTER(_f, _s, _v, vmstate_info_timer, QEMUTimer *)

#define VMSTATE_TIMER_PTR(_f, _s)                                         \
    VMSTATE_TIMER_PTR_V(_f, _s, 0)

#define VMSTATE_TIMER_PTR_ARRAY(_f, _s, _n)                              \
    VMSTATE_ARRAY_OF_POINTER(_f, _s, _n, 0, vmstate_info_timer, QEMUTimer *)

#define VMSTATE_TIMER_TEST(_f, _s, _test)                             \
    VMSTATE_SINGLE_TEST(_f, _s, _test, 0, vmstate_info_timer, QEMUTimer)

#define VMSTATE_TIMER_V(_f, _s, _v)                                   \
    VMSTATE_SINGLE(_f, _s, _v, vmstate_info_timer, QEMUTimer)

#define VMSTATE_TIMER(_f, _s)                                         \
    VMSTATE_TIMER_V(_f, _s, 0)

#define VMSTATE_TIMER_ARRAY(_f, _s, _n)                              \
    VMSTATE_ARRAY(_f, _s, _n, 0, vmstate_info_timer, QEMUTimer)

#define VMSTATE_BOOL_ARRAY_V(_f, _s, _n, _v)                         \
    VMSTATE_ARRAY(_f, _s, _n, _v, vmstate_info_bool, bool)

#define VMSTATE_BOOL_ARRAY(_f, _s, _n)                               \
    VMSTATE_BOOL_ARRAY_V(_f, _s, _n, 0)

#define VMSTATE_BOOL_SUB_ARRAY(_f, _s, _start, _num)                \
    VMSTATE_SUB_ARRAY(_f, _s, _start, _num, 0, vmstate_info_bool, bool)

#define VMSTATE_UINT16_ARRAY_V(_f, _s, _n, _v)                         \
    VMSTATE_ARRAY(_f, _s, _n, _v, vmstate_info_uint16, uint16_t)

#define VMSTATE_UINT16_2DARRAY_V(_f, _s, _n1, _n2, _v)                \
    VMSTATE_2DARRAY(_f, _s, _n1, _n2, _v, vmstate_info_uint16, uint16_t)

#define VMSTATE_UINT16_ARRAY(_f, _s, _n)                               \
    VMSTATE_UINT16_ARRAY_V(_f, _s, _n, 0)

#define VMSTATE_UINT16_SUB_ARRAY(_f, _s, _start, _num)                \
    VMSTATE_SUB_ARRAY(_f, _s, _start, _num, 0, vmstate_info_uint16, uint16_t)

#define VMSTATE_UINT16_2DARRAY(_f, _s, _n1, _n2)                      \
    VMSTATE_UINT16_2DARRAY_V(_f, _s, _n1, _n2, 0)

#define VMSTATE_UINT8_2DARRAY_V(_f, _s, _n1, _n2, _v)                 \
    VMSTATE_2DARRAY(_f, _s, _n1, _n2, _v, vmstate_info_uint8, uint8_t)

#define VMSTATE_UINT8_ARRAY_V(_f, _s, _n, _v)                         \
    VMSTATE_ARRAY(_f, _s, _n, _v, vmstate_info_uint8, uint8_t)

#define VMSTATE_UINT8_ARRAY(_f, _s, _n)                               \
    VMSTATE_UINT8_ARRAY_V(_f, _s, _n, 0)

#define VMSTATE_UINT8_SUB_ARRAY(_f, _s, _start, _num)                \
    VMSTATE_SUB_ARRAY(_f, _s, _start, _num, 0, vmstate_info_uint8, uint8_t)

#define VMSTATE_UINT8_2DARRAY(_f, _s, _n1, _n2)                       \
    VMSTATE_UINT8_2DARRAY_V(_f, _s, _n1, _n2, 0)

#define VMSTATE_UINT32_ARRAY_V(_f, _s, _n, _v)                        \
    VMSTATE_ARRAY(_f, _s, _n, _v, vmstate_info_uint32, uint32_t)

#define VMSTATE_UINT32_2DARRAY_V(_f, _s, _n1, _n2, _v)                \
    VMSTATE_2DARRAY(_f, _s, _n1, _n2, _v, vmstate_info_uint32, uint32_t)

#define VMSTATE_UINT32_ARRAY(_f, _s, _n)                              \
    VMSTATE_UINT32_ARRAY_V(_f, _s, _n, 0)

#define VMSTATE_UINT32_SUB_ARRAY(_f, _s, _start, _num)                \
    VMSTATE_SUB_ARRAY(_f, _s, _start, _num, 0, vmstate_info_uint32, uint32_t)

#define VMSTATE_UINT32_2DARRAY(_f, _s, _n1, _n2)                      \
    VMSTATE_UINT32_2DARRAY_V(_f, _s, _n1, _n2, 0)

#define VMSTATE_UINT64_ARRAY_V(_f, _s, _n, _v)                        \
    VMSTATE_ARRAY(_f, _s, _n, _v, vmstate_info_uint64, uint64_t)

#define VMSTATE_UINT64_ARRAY(_f, _s, _n)                              \
    VMSTATE_UINT64_ARRAY_V(_f, _s, _n, 0)

#define VMSTATE_UINT64_SUB_ARRAY(_f, _s, _start, _num)                \
    VMSTATE_SUB_ARRAY(_f, _s, _start, _num, 0, vmstate_info_uint64, uint64_t)

#define VMSTATE_UINT64_2DARRAY(_f, _s, _n1, _n2)                      \
    VMSTATE_UINT64_2DARRAY_V(_f, _s, _n1, _n2, 0)

#define VMSTATE_UINT64_2DARRAY_V(_f, _s, _n1, _n2, _v)                 \
    VMSTATE_2DARRAY(_f, _s, _n1, _n2, _v, vmstate_info_uint64, uint64_t)

#define VMSTATE_INT16_ARRAY_V(_f, _s, _n, _v)                         \
    VMSTATE_ARRAY(_f, _s, _n, _v, vmstate_info_int16, int16_t)

#define VMSTATE_INT16_ARRAY(_f, _s, _n)                               \
    VMSTATE_INT16_ARRAY_V(_f, _s, _n, 0)

#define VMSTATE_INT32_ARRAY_V(_f, _s, _n, _v)                         \
    VMSTATE_ARRAY(_f, _s, _n, _v, vmstate_info_int32, int32_t)

#define VMSTATE_INT32_ARRAY(_f, _s, _n)                               \
    VMSTATE_INT32_ARRAY_V(_f, _s, _n, 0)

#define VMSTATE_INT64_ARRAY_V(_f, _s, _n, _v)                         \
    VMSTATE_ARRAY(_f, _s, _n, _v, vmstate_info_int64, int64_t)

#define VMSTATE_INT64_ARRAY(_f, _s, _n)                               \
    VMSTATE_INT64_ARRAY_V(_f, _s, _n, 0)

#define VMSTATE_FLOAT64_ARRAY_V(_f, _s, _n, _v)                       \
    VMSTATE_ARRAY(_f, _s, _n, _v, vmstate_info_float64, float64)

#define VMSTATE_FLOAT64_ARRAY(_f, _s, _n)                             \
    VMSTATE_FLOAT64_ARRAY_V(_f, _s, _n, 0)

#define VMSTATE_CPUDOUBLE_ARRAY_V(_f, _s, _n, _v)                     \
    VMSTATE_ARRAY(_f, _s, _n, _v, vmstate_info_cpudouble, CPU_DoubleU)

#define VMSTATE_CPUDOUBLE_ARRAY(_f, _s, _n)                           \
    VMSTATE_CPUDOUBLE_ARRAY_V(_f, _s, _n, 0)

#define VMSTATE_BUFFER_V(_f, _s, _v)                                  \
    VMSTATE_STATIC_BUFFER(_f, _s, _v, NULL, 0, sizeof(typeof_field(_s, _f)))

#define VMSTATE_BUFFER(_f, _s)                                        \
    VMSTATE_BUFFER_V(_f, _s, 0)

#define VMSTATE_PARTIAL_BUFFER(_f, _s, _size)                         \
    VMSTATE_STATIC_BUFFER(_f, _s, 0, NULL, 0, _size)

#define VMSTATE_BUFFER_START_MIDDLE_V(_f, _s, _start, _v) \
    VMSTATE_STATIC_BUFFER(_f, _s, _v, NULL, _start, sizeof(typeof_field(_s, _f)))

#define VMSTATE_BUFFER_START_MIDDLE(_f, _s, _start) \
    VMSTATE_BUFFER_START_MIDDLE_V(_f, _s, _start, 0)

#define VMSTATE_PARTIAL_VBUFFER(_f, _s, _size)                        \
    VMSTATE_VBUFFER(_f, _s, 0, NULL, _size)

#define VMSTATE_PARTIAL_VBUFFER_UINT32(_f, _s, _size)                        \
    VMSTATE_VBUFFER_UINT32(_f, _s, 0, NULL, _size)

#define VMSTATE_BUFFER_TEST(_f, _s, _test)                            \
    VMSTATE_STATIC_BUFFER(_f, _s, 0, _test, 0, sizeof(typeof_field(_s, _f)))

#define VMSTATE_BUFFER_UNSAFE(_field, _state, _version, _size)        \
    VMSTATE_BUFFER_UNSAFE_INFO(_field, _state, _version, vmstate_info_buffer, _size)

/*
 * These VMSTATE_UNUSED*() macros can be used to fill in the holes
 * when some of the vmstate fields are obsolete to be compatible with
 * migrations between new/old binaries.
 *
 * CAUTION: when using any of the VMSTATE_UNUSED*() macros please be
 * sure that the size passed in is the size that was actually *sent*
 * rather than the size of the *structure*.  One example is the
 * boolean type - the size of the structure can vary depending on the
 * definition of boolean, however the size we actually sent is always
 * 1 byte (please refer to implementation of VMSTATE_BOOL_V and
 * vmstate_info_bool).  So here we should always pass in size==1
 * rather than size==sizeof(bool).
 */
#define VMSTATE_UNUSED_V(_v, _size)                                   \
    VMSTATE_UNUSED_BUFFER(NULL, _v, _size)

#define VMSTATE_UNUSED(_size)                                         \
    VMSTATE_UNUSED_V(0, _size)

#define VMSTATE_UNUSED_TEST(_test, _size)                             \
    VMSTATE_UNUSED_BUFFER(_test, 0, _size)

#define VMSTATE_END_OF_LIST()                                         \
    {}

int vmstate_load_state(QEMUFile *f, const VMStateDescription *vmsd,
                       void *opaque, int version_id);
int vmstate_save_state(QEMUFile *f, const VMStateDescription *vmsd,
                       void *opaque, QJSON *vmdesc);
int vmstate_save_state_v(QEMUFile *f, const VMStateDescription *vmsd,
                         void *opaque, QJSON *vmdesc, int version_id);

bool vmstate_save_needed(const VMStateDescription *vmsd, void *opaque);

#define  VMSTATE_INSTANCE_ID_ANY  -1

/* Returns: 0 on success, -1 on failure */
int vmstate_register_with_alias_id(VMStateIf *obj, uint32_t instance_id,
                                   const VMStateDescription *vmsd,
                                   void *base, int alias_id,
                                   int required_for_version,
                                   Error **errp);

/* Returns: 0 on success, -1 on failure */
static inline int vmstate_register(VMStateIf *obj, int instance_id,
                                   const VMStateDescription *vmsd,
                                   void *opaque)
{
    return vmstate_register_with_alias_id(obj, instance_id, vmsd,
                                          opaque, -1, 0, NULL);
}

void vmstate_unregister(VMStateIf *obj, const VMStateDescription *vmsd,
                        void *opaque);

void vmstate_register_ram(struct MemoryRegion *memory, DeviceState *dev);
void vmstate_unregister_ram(struct MemoryRegion *memory, DeviceState *dev);
void vmstate_register_ram_global(struct MemoryRegion *memory);

bool vmstate_check_only_migratable(const VMStateDescription *vmsd);

#endif
