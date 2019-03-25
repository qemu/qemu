/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * QEMU migration/snapshot declarations
 *
 * Copyright (c) 2009-2011 Red Hat, Inc.
 *
 * Original author: Juan Quintela <quintela@redhat.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above
 * copyright notice, this list of conditions and the following
 * disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following
 * disclaimer in the documentation and/or other materials provided
 * with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 * contributors may be used to endorse or promote products derived
 * from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#ifndef VMSTATE_H_
#define VMSTATE_H_

#include <unistd.h>
#include <stdint.h>
#include <stdbool.h>
#include "slirp.h"
#include "stream.h"

#define stringify(s) tostring(s)
#define tostring(s) #s

typedef struct VMStateInfo VMStateInfo;
typedef struct VMStateDescription VMStateDescription;
typedef struct VMStateField VMStateField;

int slirp_vmstate_save_state(SlirpOStream *f, const VMStateDescription *vmsd,
                             void *opaque);
int slirp_vmstate_load_state(SlirpIStream *f, const VMStateDescription *vmsd,
                             void *opaque, int version_id);

/* VMStateInfo allows customized migration of objects that don't fit in
 * any category in VMStateFlags. Additional information is always passed
 * into get and put in terms of field and vmdesc parameters. However
 * these two parameters should only be used in cases when customized
 * handling is needed, such as QTAILQ. For primitive data types such as
 * integer, field and vmdesc parameters should be ignored inside get/put.
 */
struct VMStateInfo {
    const char *name;
    int (*get)(SlirpIStream *f, void *pv, size_t size, const VMStateField *field);
    int (*put)(SlirpOStream *f, void *pv, size_t size, const VMStateField *field);
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

struct VMStateField {
    const char *name;
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
    int version_id;
    int (*pre_load)(void *opaque);
    int (*post_load)(void *opaque, int version_id);
    int (*pre_save)(void *opaque);
    VMStateField *fields;
};


extern const VMStateInfo slirp_vmstate_info_int16;
extern const VMStateInfo slirp_vmstate_info_int32;
extern const VMStateInfo slirp_vmstate_info_uint8;
extern const VMStateInfo slirp_vmstate_info_uint16;
extern const VMStateInfo slirp_vmstate_info_uint32;

/** Put this in the stream when migrating a null pointer.*/
#define VMS_NULLPTR_MARKER (0x30U) /* '0' */
extern const VMStateInfo slirp_vmstate_info_nullptr;

extern const VMStateInfo slirp_vmstate_info_buffer;
extern const VMStateInfo slirp_vmstate_info_tmp;

#define type_check_array(t1,t2,n) ((t1(*)[n])0 - (t2*)0)
#define type_check_pointer(t1,t2) ((t1**)0 - (t2*)0)
#define typeof_field(type, field) typeof(((type *)0)->field)
#define type_check(t1,t2) ((t1*)0 - (t2*)0)

#define vmstate_offset_value(_state, _field, _type)                  \
    (offsetof(_state, _field) +                                      \
     type_check(_type, typeof_field(_state, _field)))

#define vmstate_offset_pointer(_state, _field, _type)                \
    (offsetof(_state, _field) +                                      \
     type_check_pointer(_type, typeof_field(_state, _field)))

#define vmstate_offset_array(_state, _field, _type, _num)            \
    (offsetof(_state, _field) +                                      \
     type_check_array(_type, typeof_field(_state, _field), _num))

#define vmstate_offset_buffer(_state, _field)                        \
    vmstate_offset_array(_state, _field, uint8_t,                    \
                         sizeof(typeof_field(_state, _field)))

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

#define VMSTATE_ARRAY(_field, _state, _num, _version, _info, _type) {\
    .name       = (stringify(_field)),                               \
    .version_id = (_version),                                        \
    .num        = (_num),                                            \
    .info       = &(_info),                                          \
    .size       = sizeof(_type),                                     \
    .flags      = VMS_ARRAY,                                         \
    .offset     = vmstate_offset_array(_state, _field, _type, _num), \
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

#define VMSTATE_STATIC_BUFFER(_field, _state, _version, _test, _start, _size) { \
    .name         = (stringify(_field)),                             \
    .version_id   = (_version),                                      \
    .field_exists = (_test),                                         \
    .size         = (_size - _start),                                \
    .info         = &slirp_vmstate_info_buffer,                      \
    .flags        = VMS_BUFFER,                                      \
    .offset       = vmstate_offset_buffer(_state, _field) + _start,  \
}

#define VMSTATE_VBUFFER_UINT32(_field, _state, _version, _test, _field_size) { \
    .name         = (stringify(_field)),                             \
    .version_id   = (_version),                                      \
    .field_exists = (_test),                                         \
    .size_offset  = vmstate_offset_value(_state, _field_size, uint32_t),\
    .info         = &slirp_vmstate_info_buffer,                      \
    .flags        = VMS_VBUFFER|VMS_POINTER,                         \
    .offset       = offsetof(_state, _field),                        \
}

#define QEMU_BUILD_BUG_ON_STRUCT(x)             \
    struct {                                    \
        int:(x) ? -1 : 1;                       \
    }

#define QEMU_BUILD_BUG_ON_ZERO(x) (sizeof(QEMU_BUILD_BUG_ON_STRUCT(x)) - \
                                   sizeof(QEMU_BUILD_BUG_ON_STRUCT(x)))

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
    .info         = &slirp_vmstate_info_tmp,                         \
}

#define VMSTATE_SINGLE(_field, _state, _version, _info, _type)          \
    VMSTATE_SINGLE_TEST(_field, _state, NULL, _version, _info, _type)

#define VMSTATE_STRUCT(_field, _state, _version, _vmsd, _type)        \
    VMSTATE_STRUCT_TEST(_field, _state, NULL, _version, _vmsd, _type)

#define VMSTATE_STRUCT_POINTER(_field, _state, _vmsd, _type)          \
    VMSTATE_STRUCT_POINTER_V(_field, _state, 0, _vmsd, _type)

#define VMSTATE_STRUCT_ARRAY(_field, _state, _num, _version, _vmsd, _type) \
    VMSTATE_STRUCT_ARRAY_TEST(_field, _state, _num, NULL, _version,   \
            _vmsd, _type)

#define VMSTATE_INT16_V(_f, _s, _v)                                   \
    VMSTATE_SINGLE(_f, _s, _v, slirp_vmstate_info_int16, int16_t)
#define VMSTATE_INT32_V(_f, _s, _v)                                   \
    VMSTATE_SINGLE(_f, _s, _v, slirp_vmstate_info_int32, int32_t)

#define VMSTATE_UINT8_V(_f, _s, _v)                                   \
    VMSTATE_SINGLE(_f, _s, _v, slirp_vmstate_info_uint8, uint8_t)
#define VMSTATE_UINT16_V(_f, _s, _v)                                  \
    VMSTATE_SINGLE(_f, _s, _v, slirp_vmstate_info_uint16, uint16_t)
#define VMSTATE_UINT32_V(_f, _s, _v)                                  \
    VMSTATE_SINGLE(_f, _s, _v, slirp_vmstate_info_uint32, uint32_t)

#define VMSTATE_INT16(_f, _s)                                         \
    VMSTATE_INT16_V(_f, _s, 0)
#define VMSTATE_INT32(_f, _s)                                         \
    VMSTATE_INT32_V(_f, _s, 0)

#define VMSTATE_UINT8(_f, _s)                                         \
    VMSTATE_UINT8_V(_f, _s, 0)
#define VMSTATE_UINT16(_f, _s)                                        \
    VMSTATE_UINT16_V(_f, _s, 0)
#define VMSTATE_UINT32(_f, _s)                                        \
    VMSTATE_UINT32_V(_f, _s, 0)

#define VMSTATE_UINT16_TEST(_f, _s, _t)                               \
    VMSTATE_SINGLE_TEST(_f, _s, _t, 0, slirp_vmstate_info_uint16, uint16_t)

#define VMSTATE_UINT32_TEST(_f, _s, _t)                                  \
    VMSTATE_SINGLE_TEST(_f, _s, _t, 0, slirp_vmstate_info_uint32, uint32_t)

#define VMSTATE_INT16_ARRAY_V(_f, _s, _n, _v)                         \
    VMSTATE_ARRAY(_f, _s, _n, _v, slirp_vmstate_info_int16, int16_t)

#define VMSTATE_INT16_ARRAY(_f, _s, _n)                               \
    VMSTATE_INT16_ARRAY_V(_f, _s, _n, 0)

#define VMSTATE_BUFFER_V(_f, _s, _v)                                    \
    VMSTATE_STATIC_BUFFER(_f, _s, _v, NULL, 0, sizeof(typeof_field(_s, _f)))

#define VMSTATE_BUFFER(_f, _s)                                        \
    VMSTATE_BUFFER_V(_f, _s, 0)

#define VMSTATE_END_OF_LIST()                                         \
    {}

#endif
