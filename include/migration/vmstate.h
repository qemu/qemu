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
#define QEMU_VMSTATE_H 1

#ifndef CONFIG_USER_ONLY
#include <migration/qemu-file.h>
#endif

typedef void SaveStateHandler(QEMUFile *f, void *opaque);
typedef int LoadStateHandler(QEMUFile *f, void *opaque, int version_id);

typedef struct SaveVMHandlers {
    /* This runs inside the iothread lock.  */
    void (*set_params)(const MigrationParams *params, void * opaque);
    SaveStateHandler *save_state;

    void (*cancel)(void *opaque);
    int (*save_live_complete)(QEMUFile *f, void *opaque);

    /* This runs both outside and inside the iothread lock.  */
    bool (*is_active)(void *opaque);

    /* This runs outside the iothread lock in the migration case, and
     * within the lock in the savevm case.  The callback had better only
     * use data that is local to the migration thread or protected
     * by other locks.
     */
    int (*save_live_iterate)(QEMUFile *f, void *opaque);

    /* This runs outside the iothread lock!  */
    int (*save_live_setup)(QEMUFile *f, void *opaque);
    uint64_t (*save_live_pending)(QEMUFile *f, void *opaque, uint64_t max_size);

    LoadStateHandler *load_state;
} SaveVMHandlers;

int register_savevm(DeviceState *dev,
                    const char *idstr,
                    int instance_id,
                    int version_id,
                    SaveStateHandler *save_state,
                    LoadStateHandler *load_state,
                    void *opaque);

int register_savevm_live(DeviceState *dev,
                         const char *idstr,
                         int instance_id,
                         int version_id,
                         SaveVMHandlers *ops,
                         void *opaque);

void unregister_savevm(DeviceState *dev, const char *idstr, void *opaque);
void register_device_unmigratable(DeviceState *dev, const char *idstr,
                                                                void *opaque);


typedef struct VMStateInfo VMStateInfo;
typedef struct VMStateDescription VMStateDescription;

struct VMStateInfo {
    const char *name;
    int (*get)(QEMUFile *f, void *pv, size_t size);
    void (*put)(QEMUFile *f, void *pv, size_t size);
};

enum VMStateFlags {
    VMS_SINGLE           = 0x001,
    VMS_POINTER          = 0x002,
    VMS_ARRAY            = 0x004,
    VMS_STRUCT           = 0x008,
    VMS_VARRAY_INT32     = 0x010,  /* Array with size in int32_t field*/
    VMS_BUFFER           = 0x020,  /* static sized buffer */
    VMS_ARRAY_OF_POINTER = 0x040,
    VMS_VARRAY_UINT16    = 0x080,  /* Array with size in uint16_t field */
    VMS_VBUFFER          = 0x100,  /* Buffer with size in int32_t field */
    VMS_MULTIPLY         = 0x200,  /* multiply "size" field by field_size */
    VMS_VARRAY_UINT8     = 0x400,  /* Array with size in uint8_t field*/
    VMS_VARRAY_UINT32    = 0x800,  /* Array with size in uint32_t field*/
    VMS_MUST_EXIST       = 0x1000, /* Field must exist in input */
};

typedef struct {
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
    bool (*field_exists)(void *opaque, int version_id);
} VMStateField;

typedef struct VMStateSubsection {
    const VMStateDescription *vmsd;
    bool (*needed)(void *opaque);
} VMStateSubsection;

struct VMStateDescription {
    const char *name;
    int unmigratable;
    int version_id;
    int minimum_version_id;
    int minimum_version_id_old;
    LoadStateHandler *load_state_old;
    int (*pre_load)(void *opaque);
    int (*post_load)(void *opaque, int version_id);
    void (*pre_save)(void *opaque);
    VMStateField *fields;
    const VMStateSubsection *subsections;
};

#ifdef CONFIG_USER_ONLY
extern const VMStateDescription vmstate_dummy;
#endif

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

extern const VMStateInfo vmstate_info_float64;

extern const VMStateInfo vmstate_info_timer;
extern const VMStateInfo vmstate_info_buffer;
extern const VMStateInfo vmstate_info_unused_buffer;
extern const VMStateInfo vmstate_info_bitmap;

#define type_check_2darray(t1,t2,n,m) ((t1(*)[n][m])0 - (t2*)0)
#define type_check_array(t1,t2,n) ((t1(*)[n])0 - (t2*)0)
#define type_check_pointer(t1,t2) ((t1**)0 - (t2*)0)

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
    (offsetof(_state, _field[_start]))

#define vmstate_offset_buffer(_state, _field)                        \
    vmstate_offset_array(_state, _field, uint8_t,                    \
                         sizeof(typeof_field(_state, _field)))

#define VMSTATE_SINGLE_TEST(_field, _state, _test, _version, _info, _type) { \
    .name         = (stringify(_field)),                             \
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
    .offset     = offsetof(_state, _field),                          \
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

#define VMSTATE_VARRAY_UINT16_UNSAFE(_field, _state, _field_num, _version, _info, _type) {\
    .name       = (stringify(_field)),                               \
    .version_id = (_version),                                        \
    .num_offset = vmstate_offset_value(_state, _field_num, uint16_t),\
    .info       = &(_info),                                          \
    .size       = sizeof(_type),                                     \
    .flags      = VMS_VARRAY_UINT16,                                 \
    .offset     = offsetof(_state, _field),                          \
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

#define VMSTATE_STRUCT_VARRAY_UINT8(_field, _state, _field_num, _version, _vmsd, _type) { \
    .name       = (stringify(_field)),                               \
    .num_offset = vmstate_offset_value(_state, _field_num, uint8_t), \
    .version_id = (_version),                                        \
    .vmsd       = &(_vmsd),                                          \
    .size       = sizeof(_type),                                     \
    .flags      = VMS_STRUCT|VMS_VARRAY_UINT8,                       \
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
    .offset     = offsetof(_state, _field),                          \
}

#define VMSTATE_STRUCT_VARRAY_UINT32(_field, _state, _field_num, _version, _vmsd, _type) { \
    .name       = (stringify(_field)),                               \
    .num_offset = vmstate_offset_value(_state, _field_num, uint32_t), \
    .version_id = (_version),                                        \
    .vmsd       = &(_vmsd),                                          \
    .size       = sizeof(_type),                                     \
    .flags      = VMS_STRUCT|VMS_VARRAY_UINT32,                      \
    .offset     = offsetof(_state, _field),                          \
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

#define VMSTATE_VBUFFER_MULTIPLY(_field, _state, _version, _test, _start, _field_size, _multiply) { \
    .name         = (stringify(_field)),                             \
    .version_id   = (_version),                                      \
    .field_exists = (_test),                                         \
    .size_offset  = vmstate_offset_value(_state, _field_size, uint32_t),\
    .size         = (_multiply),                                      \
    .info         = &vmstate_info_buffer,                            \
    .flags        = VMS_VBUFFER|VMS_POINTER|VMS_MULTIPLY,            \
    .offset       = offsetof(_state, _field),                        \
    .start        = (_start),                                        \
}

#define VMSTATE_VBUFFER(_field, _state, _version, _test, _start, _field_size) { \
    .name         = (stringify(_field)),                             \
    .version_id   = (_version),                                      \
    .field_exists = (_test),                                         \
    .size_offset  = vmstate_offset_value(_state, _field_size, int32_t),\
    .info         = &vmstate_info_buffer,                            \
    .flags        = VMS_VBUFFER|VMS_POINTER,                         \
    .offset       = offsetof(_state, _field),                        \
    .start        = (_start),                                        \
}

#define VMSTATE_VBUFFER_UINT32(_field, _state, _version, _test, _start, _field_size) { \
    .name         = (stringify(_field)),                             \
    .version_id   = (_version),                                      \
    .field_exists = (_test),                                         \
    .size_offset  = vmstate_offset_value(_state, _field_size, uint32_t),\
    .info         = &vmstate_info_buffer,                            \
    .flags        = VMS_VBUFFER|VMS_POINTER,                         \
    .offset       = offsetof(_state, _field),                        \
    .start        = (_start),                                        \
}

#define VMSTATE_BUFFER_UNSAFE_INFO(_field, _state, _version, _info, _size) { \
    .name       = (stringify(_field)),                               \
    .version_id = (_version),                                        \
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

#define VMSTATE_UNUSED_BUFFER(_test, _version, _size) {              \
    .name         = "unused",                                        \
    .field_exists = (_test),                                         \
    .version_id   = (_version),                                      \
    .size         = (_size),                                         \
    .info         = &vmstate_info_unused_buffer,                     \
    .flags        = VMS_BUFFER,                                      \
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

/* _f : field name
   _f_n : num of elements field_name
   _n : num of elements
   _s : struct state name
   _v : version
*/

#define VMSTATE_SINGLE(_field, _state, _version, _info, _type)        \
    VMSTATE_SINGLE_TEST(_field, _state, NULL, _version, _info, _type)

#define VMSTATE_STRUCT(_field, _state, _version, _vmsd, _type)        \
    VMSTATE_STRUCT_TEST(_field, _state, NULL, _version, _vmsd, _type)

#define VMSTATE_STRUCT_POINTER(_field, _state, _vmsd, _type)          \
    VMSTATE_STRUCT_POINTER_V(_field, _state, 0, _vmsd, _type)

#define VMSTATE_STRUCT_POINTER_TEST(_field, _state, _test, _vmsd, _type)     \
    VMSTATE_STRUCT_POINTER_TEST_V(_field, _state, _test, 0, _vmsd, _type)

#define VMSTATE_STRUCT_ARRAY(_field, _state, _num, _version, _vmsd, _type) \
    VMSTATE_STRUCT_ARRAY_TEST(_field, _state, _num, NULL, _version,   \
            _vmsd, _type)

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

#define VMSTATE_UINT8_EQUAL(_f, _s)                                   \
    VMSTATE_SINGLE(_f, _s, 0, vmstate_info_uint8_equal, uint8_t)

#define VMSTATE_UINT16_EQUAL(_f, _s)                                  \
    VMSTATE_SINGLE(_f, _s, 0, vmstate_info_uint16_equal, uint16_t)

#define VMSTATE_UINT16_EQUAL_V(_f, _s, _v)                            \
    VMSTATE_SINGLE(_f, _s, _v, vmstate_info_uint16_equal, uint16_t)

#define VMSTATE_INT32_EQUAL(_f, _s)                                   \
    VMSTATE_SINGLE(_f, _s, 0, vmstate_info_int32_equal, int32_t)

#define VMSTATE_UINT32_EQUAL_V(_f, _s, _v)                            \
    VMSTATE_SINGLE(_f, _s, _v, vmstate_info_uint32_equal, uint32_t)

#define VMSTATE_UINT32_EQUAL(_f, _s)                                  \
    VMSTATE_UINT32_EQUAL_V(_f, _s, 0)

#define VMSTATE_UINT64_EQUAL_V(_f, _s, _v)                            \
    VMSTATE_SINGLE(_f, _s, _v, vmstate_info_uint64_equal, uint64_t)

#define VMSTATE_UINT64_EQUAL(_f, _s)                                  \
    VMSTATE_UINT64_EQUAL_V(_f, _s, 0)

#define VMSTATE_INT32_POSITIVE_LE(_f, _s)                             \
    VMSTATE_SINGLE(_f, _s, 0, vmstate_info_int32_le, int32_t)

#define VMSTATE_UINT8_TEST(_f, _s, _t)                               \
    VMSTATE_SINGLE_TEST(_f, _s, _t, 0, vmstate_info_uint8, uint8_t)

#define VMSTATE_UINT16_TEST(_f, _s, _t)                               \
    VMSTATE_SINGLE_TEST(_f, _s, _t, 0, vmstate_info_uint16, uint16_t)

#define VMSTATE_UINT32_TEST(_f, _s, _t)                                  \
    VMSTATE_SINGLE_TEST(_f, _s, _t, 0, vmstate_info_uint32, uint32_t)


#define VMSTATE_FLOAT64_V(_f, _s, _v)                                 \
    VMSTATE_SINGLE(_f, _s, _v, vmstate_info_float64, float64)

#define VMSTATE_FLOAT64(_f, _s)                                       \
    VMSTATE_FLOAT64_V(_f, _s, 0)

#define VMSTATE_TIMER_TEST(_f, _s, _test)                             \
    VMSTATE_POINTER_TEST(_f, _s, _test, vmstate_info_timer, QEMUTimer *)

#define VMSTATE_TIMER_V(_f, _s, _v)                                   \
    VMSTATE_POINTER(_f, _s, _v, vmstate_info_timer, QEMUTimer *)

#define VMSTATE_TIMER(_f, _s)                                         \
    VMSTATE_TIMER_V(_f, _s, 0)

#define VMSTATE_TIMER_ARRAY(_f, _s, _n)                              \
    VMSTATE_ARRAY_OF_POINTER(_f, _s, _n, 0, vmstate_info_timer, QEMUTimer *)

#define VMSTATE_BOOL_ARRAY_V(_f, _s, _n, _v)                         \
    VMSTATE_ARRAY(_f, _s, _n, _v, vmstate_info_bool, bool)

#define VMSTATE_BOOL_ARRAY(_f, _s, _n)                               \
    VMSTATE_BOOL_ARRAY_V(_f, _s, _n, 0)

#define VMSTATE_UINT16_ARRAY_V(_f, _s, _n, _v)                         \
    VMSTATE_ARRAY(_f, _s, _n, _v, vmstate_info_uint16, uint16_t)

#define VMSTATE_UINT16_2DARRAY_V(_f, _s, _n1, _n2, _v)                \
    VMSTATE_2DARRAY(_f, _s, _n1, _n2, _v, vmstate_info_uint16, uint16_t)

#define VMSTATE_UINT16_ARRAY(_f, _s, _n)                               \
    VMSTATE_UINT16_ARRAY_V(_f, _s, _n, 0)

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

#define VMSTATE_UINT32_2DARRAY(_f, _s, _n1, _n2)                      \
    VMSTATE_UINT32_2DARRAY_V(_f, _s, _n1, _n2, 0)

#define VMSTATE_UINT64_ARRAY_V(_f, _s, _n, _v)                        \
    VMSTATE_ARRAY(_f, _s, _n, _v, vmstate_info_uint64, uint64_t)

#define VMSTATE_UINT64_ARRAY(_f, _s, _n)                              \
    VMSTATE_UINT64_ARRAY_V(_f, _s, _n, 0)

#define VMSTATE_INT16_ARRAY_V(_f, _s, _n, _v)                         \
    VMSTATE_ARRAY(_f, _s, _n, _v, vmstate_info_int16, int16_t)

#define VMSTATE_INT16_ARRAY(_f, _s, _n)                               \
    VMSTATE_INT16_ARRAY_V(_f, _s, _n, 0)

#define VMSTATE_INT32_ARRAY_V(_f, _s, _n, _v)                         \
    VMSTATE_ARRAY(_f, _s, _n, _v, vmstate_info_int32, int32_t)

#define VMSTATE_INT32_ARRAY(_f, _s, _n)                               \
    VMSTATE_INT32_ARRAY_V(_f, _s, _n, 0)

#define VMSTATE_UINT32_SUB_ARRAY(_f, _s, _start, _num)                \
    VMSTATE_SUB_ARRAY(_f, _s, _start, _num, 0, vmstate_info_uint32, uint32_t)

#define VMSTATE_UINT32_ARRAY(_f, _s, _n)                              \
    VMSTATE_UINT32_ARRAY_V(_f, _s, _n, 0)

#define VMSTATE_INT64_ARRAY_V(_f, _s, _n, _v)                         \
    VMSTATE_ARRAY(_f, _s, _n, _v, vmstate_info_int64, int64_t)

#define VMSTATE_INT64_ARRAY(_f, _s, _n)                               \
    VMSTATE_INT64_ARRAY_V(_f, _s, _n, 0)

#define VMSTATE_FLOAT64_ARRAY_V(_f, _s, _n, _v)                       \
    VMSTATE_ARRAY(_f, _s, _n, _v, vmstate_info_float64, float64)

#define VMSTATE_FLOAT64_ARRAY(_f, _s, _n)                             \
    VMSTATE_FLOAT64_ARRAY_V(_f, _s, _n, 0)

#define VMSTATE_BUFFER_V(_f, _s, _v)                                  \
    VMSTATE_STATIC_BUFFER(_f, _s, _v, NULL, 0, sizeof(typeof_field(_s, _f)))

#define VMSTATE_BUFFER(_f, _s)                                        \
    VMSTATE_BUFFER_V(_f, _s, 0)

#define VMSTATE_PARTIAL_BUFFER(_f, _s, _size)                         \
    VMSTATE_STATIC_BUFFER(_f, _s, 0, NULL, 0, _size)

#define VMSTATE_BUFFER_START_MIDDLE(_f, _s, _start) \
    VMSTATE_STATIC_BUFFER(_f, _s, 0, NULL, _start, sizeof(typeof_field(_s, _f)))

#define VMSTATE_PARTIAL_VBUFFER(_f, _s, _size)                        \
    VMSTATE_VBUFFER(_f, _s, 0, NULL, 0, _size)

#define VMSTATE_PARTIAL_VBUFFER_UINT32(_f, _s, _size)                        \
    VMSTATE_VBUFFER_UINT32(_f, _s, 0, NULL, 0, _size)

#define VMSTATE_SUB_VBUFFER(_f, _s, _start, _size)                    \
    VMSTATE_VBUFFER(_f, _s, 0, NULL, _start, _size)

#define VMSTATE_BUFFER_TEST(_f, _s, _test)                            \
    VMSTATE_STATIC_BUFFER(_f, _s, 0, _test, 0, sizeof(typeof_field(_s, _f)))

#define VMSTATE_BUFFER_UNSAFE(_field, _state, _version, _size)        \
    VMSTATE_BUFFER_UNSAFE_INFO(_field, _state, _version, vmstate_info_buffer, _size)

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
void vmstate_save_state(QEMUFile *f, const VMStateDescription *vmsd,
                        void *opaque);

int vmstate_register_with_alias_id(DeviceState *dev, int instance_id,
                                   const VMStateDescription *vmsd,
                                   void *base, int alias_id,
                                   int required_for_version);

static inline int vmstate_register(DeviceState *dev, int instance_id,
                                   const VMStateDescription *vmsd,
                                   void *opaque)
{
    return vmstate_register_with_alias_id(dev, instance_id, vmsd,
                                          opaque, -1, 0);
}

void vmstate_unregister(DeviceState *dev, const VMStateDescription *vmsd,
                        void *opaque);

struct MemoryRegion;
void vmstate_register_ram(struct MemoryRegion *memory, DeviceState *dev);
void vmstate_unregister_ram(struct MemoryRegion *memory, DeviceState *dev);
void vmstate_register_ram_global(struct MemoryRegion *memory);

#endif
