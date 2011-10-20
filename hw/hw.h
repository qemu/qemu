/* Declarations for use by hardware emulation.  */
#ifndef QEMU_HW_H
#define QEMU_HW_H

#include "qemu-common.h"

#if defined(TARGET_PHYS_ADDR_BITS) && !defined(NEED_CPU_H)
#include "cpu-common.h"
#endif

#include "ioport.h"
#include "irq.h"

/* VM Load/Save */

/* This function writes a chunk of data to a file at the given position.
 * The pos argument can be ignored if the file is only being used for
 * streaming.  The handler should try to write all of the data it can.
 */
typedef int (QEMUFilePutBufferFunc)(void *opaque, const uint8_t *buf,
                                    int64_t pos, int size);

/* Read a chunk of data from a file at the given position.  The pos argument
 * can be ignored if the file is only be used for streaming.  The number of
 * bytes actually read should be returned.
 */
typedef int (QEMUFileGetBufferFunc)(void *opaque, uint8_t *buf,
                                    int64_t pos, int size);

/* Close a file and return an error code */
typedef int (QEMUFileCloseFunc)(void *opaque);

/* Called to determine if the file has exceeded it's bandwidth allocation.  The
 * bandwidth capping is a soft limit, not a hard limit.
 */
typedef int (QEMUFileRateLimit)(void *opaque);

/* Called to change the current bandwidth allocation. This function must return
 * the new actual bandwidth. It should be new_rate if everything goes ok, and
 * the old rate otherwise
 */
typedef int64_t (QEMUFileSetRateLimit)(void *opaque, int64_t new_rate);
typedef int64_t (QEMUFileGetRateLimit)(void *opaque);

QEMUFile *qemu_fopen_ops(void *opaque, QEMUFilePutBufferFunc *put_buffer,
                         QEMUFileGetBufferFunc *get_buffer,
                         QEMUFileCloseFunc *close,
                         QEMUFileRateLimit *rate_limit,
                         QEMUFileSetRateLimit *set_rate_limit,
			 QEMUFileGetRateLimit *get_rate_limit);
QEMUFile *qemu_fopen(const char *filename, const char *mode);
QEMUFile *qemu_fdopen(int fd, const char *mode);
QEMUFile *qemu_fopen_socket(int fd);
QEMUFile *qemu_popen(FILE *popen_file, const char *mode);
QEMUFile *qemu_popen_cmd(const char *command, const char *mode);
int qemu_stdio_fd(QEMUFile *f);
void qemu_fflush(QEMUFile *f);
int qemu_fclose(QEMUFile *f);
void qemu_put_buffer(QEMUFile *f, const uint8_t *buf, int size);
void qemu_put_byte(QEMUFile *f, int v);

static inline void qemu_put_ubyte(QEMUFile *f, unsigned int v)
{
    qemu_put_byte(f, (int)v);
}

#define qemu_put_sbyte qemu_put_byte

void qemu_put_be16(QEMUFile *f, unsigned int v);
void qemu_put_be32(QEMUFile *f, unsigned int v);
void qemu_put_be64(QEMUFile *f, uint64_t v);
int qemu_get_buffer(QEMUFile *f, uint8_t *buf, int size);
int qemu_get_byte(QEMUFile *f);

static inline unsigned int qemu_get_ubyte(QEMUFile *f)
{
    return (unsigned int)qemu_get_byte(f);
}

#define qemu_get_sbyte qemu_get_byte

unsigned int qemu_get_be16(QEMUFile *f);
unsigned int qemu_get_be32(QEMUFile *f);
uint64_t qemu_get_be64(QEMUFile *f);
int qemu_file_rate_limit(QEMUFile *f);
int64_t qemu_file_set_rate_limit(QEMUFile *f, int64_t new_rate);
int64_t qemu_file_get_rate_limit(QEMUFile *f);
int qemu_file_get_error(QEMUFile *f);
void qemu_file_set_error(QEMUFile *f, int error);

/* Try to send any outstanding data.  This function is useful when output is
 * halted due to rate limiting or EAGAIN errors occur as it can be used to
 * resume output. */
void qemu_file_put_notify(QEMUFile *f);

static inline void qemu_put_be64s(QEMUFile *f, const uint64_t *pv)
{
    qemu_put_be64(f, *pv);
}

static inline void qemu_put_be32s(QEMUFile *f, const uint32_t *pv)
{
    qemu_put_be32(f, *pv);
}

static inline void qemu_put_be16s(QEMUFile *f, const uint16_t *pv)
{
    qemu_put_be16(f, *pv);
}

static inline void qemu_put_8s(QEMUFile *f, const uint8_t *pv)
{
    qemu_put_byte(f, *pv);
}

static inline void qemu_get_be64s(QEMUFile *f, uint64_t *pv)
{
    *pv = qemu_get_be64(f);
}

static inline void qemu_get_be32s(QEMUFile *f, uint32_t *pv)
{
    *pv = qemu_get_be32(f);
}

static inline void qemu_get_be16s(QEMUFile *f, uint16_t *pv)
{
    *pv = qemu_get_be16(f);
}

static inline void qemu_get_8s(QEMUFile *f, uint8_t *pv)
{
    *pv = qemu_get_byte(f);
}

// Signed versions for type safety
static inline void qemu_put_sbuffer(QEMUFile *f, const int8_t *buf, int size)
{
    qemu_put_buffer(f, (const uint8_t *)buf, size);
}

static inline void qemu_put_sbe16(QEMUFile *f, int v)
{
    qemu_put_be16(f, (unsigned int)v);
}

static inline void qemu_put_sbe32(QEMUFile *f, int v)
{
    qemu_put_be32(f, (unsigned int)v);
}

static inline void qemu_put_sbe64(QEMUFile *f, int64_t v)
{
    qemu_put_be64(f, (uint64_t)v);
}

static inline size_t qemu_get_sbuffer(QEMUFile *f, int8_t *buf, int size)
{
    return qemu_get_buffer(f, (uint8_t *)buf, size);
}

static inline int qemu_get_sbe16(QEMUFile *f)
{
    return (int)qemu_get_be16(f);
}

static inline int qemu_get_sbe32(QEMUFile *f)
{
    return (int)qemu_get_be32(f);
}

static inline int64_t qemu_get_sbe64(QEMUFile *f)
{
    return (int64_t)qemu_get_be64(f);
}

static inline void qemu_put_s8s(QEMUFile *f, const int8_t *pv)
{
    qemu_put_8s(f, (const uint8_t *)pv);
}

static inline void qemu_put_sbe16s(QEMUFile *f, const int16_t *pv)
{
    qemu_put_be16s(f, (const uint16_t *)pv);
}

static inline void qemu_put_sbe32s(QEMUFile *f, const int32_t *pv)
{
    qemu_put_be32s(f, (const uint32_t *)pv);
}

static inline void qemu_put_sbe64s(QEMUFile *f, const int64_t *pv)
{
    qemu_put_be64s(f, (const uint64_t *)pv);
}

static inline void qemu_get_s8s(QEMUFile *f, int8_t *pv)
{
    qemu_get_8s(f, (uint8_t *)pv);
}

static inline void qemu_get_sbe16s(QEMUFile *f, int16_t *pv)
{
    qemu_get_be16s(f, (uint16_t *)pv);
}

static inline void qemu_get_sbe32s(QEMUFile *f, int32_t *pv)
{
    qemu_get_be32s(f, (uint32_t *)pv);
}

static inline void qemu_get_sbe64s(QEMUFile *f, int64_t *pv)
{
    qemu_get_be64s(f, (uint64_t *)pv);
}

#ifdef NEED_CPU_H
#if TARGET_LONG_BITS == 64
#define qemu_put_betl qemu_put_be64
#define qemu_get_betl qemu_get_be64
#define qemu_put_betls qemu_put_be64s
#define qemu_get_betls qemu_get_be64s
#define qemu_put_sbetl qemu_put_sbe64
#define qemu_get_sbetl qemu_get_sbe64
#define qemu_put_sbetls qemu_put_sbe64s
#define qemu_get_sbetls qemu_get_sbe64s
#else
#define qemu_put_betl qemu_put_be32
#define qemu_get_betl qemu_get_be32
#define qemu_put_betls qemu_put_be32s
#define qemu_get_betls qemu_get_be32s
#define qemu_put_sbetl qemu_put_sbe32
#define qemu_get_sbetl qemu_get_sbe32
#define qemu_put_sbetls qemu_put_sbe32s
#define qemu_get_sbetls qemu_get_sbe32s
#endif
#endif

int64_t qemu_ftell(QEMUFile *f);
int64_t qemu_fseek(QEMUFile *f, int64_t pos, int whence);

typedef void SaveSetParamsHandler(int blk_enable, int shared, void * opaque);
typedef void SaveStateHandler(QEMUFile *f, void *opaque);
typedef int SaveLiveStateHandler(Monitor *mon, QEMUFile *f, int stage,
                                 void *opaque);
typedef int LoadStateHandler(QEMUFile *f, void *opaque, int version_id);

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
                         SaveSetParamsHandler *set_params,
			 SaveLiveStateHandler *save_live_state,
                         SaveStateHandler *save_state,
                         LoadStateHandler *load_state,
                         void *opaque);

void unregister_savevm(DeviceState *dev, const char *idstr, void *opaque);
void register_device_unmigratable(DeviceState *dev, const char *idstr,
                                                                void *opaque);

typedef void QEMUResetHandler(void *opaque);

void qemu_register_reset(QEMUResetHandler *func, void *opaque);
void qemu_unregister_reset(QEMUResetHandler *func, void *opaque);

/* handler to set the boot_device order for a specific type of QEMUMachine */
/* return 0 if success */
typedef int QEMUBootSetHandler(void *opaque, const char *boot_devices);
void qemu_register_boot_set(QEMUBootSetHandler *func, void *opaque);
int qemu_boot_set(const char *boot_devices);

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

extern const VMStateInfo vmstate_info_bool;

extern const VMStateInfo vmstate_info_int8;
extern const VMStateInfo vmstate_info_int16;
extern const VMStateInfo vmstate_info_int32;
extern const VMStateInfo vmstate_info_int64;

extern const VMStateInfo vmstate_info_uint8_equal;
extern const VMStateInfo vmstate_info_uint16_equal;
extern const VMStateInfo vmstate_info_int32_equal;
extern const VMStateInfo vmstate_info_uint32_equal;
extern const VMStateInfo vmstate_info_int32_le;

extern const VMStateInfo vmstate_info_uint8;
extern const VMStateInfo vmstate_info_uint16;
extern const VMStateInfo vmstate_info_uint32;
extern const VMStateInfo vmstate_info_uint64;

extern const VMStateInfo vmstate_info_timer;
extern const VMStateInfo vmstate_info_ptimer;
extern const VMStateInfo vmstate_info_buffer;
extern const VMStateInfo vmstate_info_unused_buffer;

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

#define VMSTATE_STRUCT_POINTER_TEST(_field, _state, _test, _vmsd, _type) { \
    .name         = (stringify(_field)),                             \
    .field_exists = (_test),                                         \
    .vmsd         = &(_vmsd),                                        \
    .size         = sizeof(_type),                                   \
    .flags        = VMS_STRUCT|VMS_POINTER,                          \
    .offset       = vmstate_offset_value(_state, _field, _type),     \
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

#define VMSTATE_BUFFER_MULTIPLY(_field, _state, _version, _test, _start, _field_size, _multiply) { \
    .name         = (stringify(_field)),                             \
    .version_id   = (_version),                                      \
    .field_exists = (_test),                                         \
    .size_offset  = vmstate_offset_value(_state, _field_size, uint32_t),\
    .size         = (_multiply),                                      \
    .info         = &vmstate_info_buffer,                            \
    .flags        = VMS_VBUFFER|VMS_MULTIPLY,                        \
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

#define VMSTATE_UNUSED_BUFFER(_test, _version, _size) {              \
    .name         = "unused",                                        \
    .field_exists = (_test),                                         \
    .version_id   = (_version),                                      \
    .size         = (_size),                                         \
    .info         = &vmstate_info_unused_buffer,                     \
    .flags        = VMS_BUFFER,                                      \
}
extern const VMStateDescription vmstate_pci_device;

#define VMSTATE_PCI_DEVICE(_field, _state) {                         \
    .name       = (stringify(_field)),                               \
    .size       = sizeof(PCIDevice),                                 \
    .vmsd       = &vmstate_pci_device,                               \
    .flags      = VMS_STRUCT,                                        \
    .offset     = vmstate_offset_value(_state, _field, PCIDevice),   \
}

#define VMSTATE_PCI_DEVICE_POINTER(_field, _state) {                 \
    .name       = (stringify(_field)),                               \
    .size       = sizeof(PCIDevice),                                 \
    .vmsd       = &vmstate_pci_device,                               \
    .flags      = VMS_STRUCT|VMS_POINTER,                            \
    .offset     = vmstate_offset_pointer(_state, _field, PCIDevice), \
}

extern const VMStateDescription vmstate_pcie_device;

#define VMSTATE_PCIE_DEVICE(_field, _state) {                        \
    .name       = (stringify(_field)),                               \
    .version_id = 2,                                                 \
    .size       = sizeof(PCIDevice),                                 \
    .vmsd       = &vmstate_pcie_device,                              \
    .flags      = VMS_STRUCT,                                        \
    .offset     = vmstate_offset_value(_state, _field, PCIDevice),   \
}

extern const VMStateDescription vmstate_i2c_slave;

#define VMSTATE_I2C_SLAVE(_field, _state) {                          \
    .name       = (stringify(_field)),                               \
    .size       = sizeof(i2c_slave),                                 \
    .vmsd       = &vmstate_i2c_slave,                                \
    .flags      = VMS_STRUCT,                                        \
    .offset     = vmstate_offset_value(_state, _field, i2c_slave),   \
}

extern const VMStateDescription vmstate_usb_device;

#define VMSTATE_USB_DEVICE(_field, _state) {                         \
    .name       = (stringify(_field)),                               \
    .size       = sizeof(USBDevice),                                 \
    .vmsd       = &vmstate_usb_device,                               \
    .flags      = VMS_STRUCT,                                        \
    .offset     = vmstate_offset_value(_state, _field, USBDevice),   \
}

#define vmstate_offset_macaddr(_state, _field)                       \
    vmstate_offset_array(_state, _field.a, uint8_t,                \
                         sizeof(typeof_field(_state, _field)))

#define VMSTATE_MACADDR(_field, _state) {                            \
    .name       = (stringify(_field)),                               \
    .size       = sizeof(MACAddr),                                   \
    .info       = &vmstate_info_buffer,                              \
    .flags      = VMS_BUFFER,                                        \
    .offset     = vmstate_offset_macaddr(_state, _field),            \
}

extern const VMStateDescription vmstate_ptimer;

#define VMSTATE_PTIMER(_field, _state) {                             \
    .name       = (stringify(_field)),                               \
    .version_id = (1),                                               \
    .vmsd       = &vmstate_ptimer,                                   \
    .size       = sizeof(ptimer_state *),                            \
    .flags      = VMS_STRUCT|VMS_POINTER,                            \
    .offset     = vmstate_offset_pointer(_state, _field, ptimer_state), \
}

extern const VMStateDescription vmstate_hid_keyboard_device;

#define VMSTATE_HID_KEYBOARD_DEVICE(_field, _state) {                \
    .name       = (stringify(_field)),                               \
    .size       = sizeof(HIDState),                                  \
    .vmsd       = &vmstate_hid_keyboard_device,                      \
    .flags      = VMS_STRUCT,                                        \
    .offset     = vmstate_offset_value(_state, _field, HIDState),    \
}

extern const VMStateDescription vmstate_hid_ptr_device;

#define VMSTATE_HID_POINTER_DEVICE(_field, _state) {                 \
    .name       = (stringify(_field)),                               \
    .size       = sizeof(HIDState),                                  \
    .vmsd       = &vmstate_hid_ptr_device,                           \
    .flags      = VMS_STRUCT,                                        \
    .offset     = vmstate_offset_value(_state, _field, HIDState),    \
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
    VMSTATE_STRUCT_POINTER_TEST(_field, _state, NULL, _vmsd, _type)

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

#define VMSTATE_UINT32_EQUAL(_f, _s)                                   \
    VMSTATE_SINGLE(_f, _s, 0, vmstate_info_uint32_equal, uint32_t)

#define VMSTATE_INT32_LE(_f, _s)                                   \
    VMSTATE_SINGLE(_f, _s, 0, vmstate_info_int32_le, int32_t)

#define VMSTATE_UINT8_TEST(_f, _s, _t)                               \
    VMSTATE_SINGLE_TEST(_f, _s, _t, 0, vmstate_info_uint8, uint8_t)

#define VMSTATE_UINT16_TEST(_f, _s, _t)                               \
    VMSTATE_SINGLE_TEST(_f, _s, _t, 0, vmstate_info_uint16, uint16_t)

#define VMSTATE_UINT32_TEST(_f, _s, _t)                                  \
    VMSTATE_SINGLE_TEST(_f, _s, _t, 0, vmstate_info_uint32, uint32_t)

#define VMSTATE_TIMER_TEST(_f, _s, _test)                             \
    VMSTATE_POINTER_TEST(_f, _s, _test, vmstate_info_timer, QEMUTimer *)

#define VMSTATE_TIMER(_f, _s)                                         \
    VMSTATE_TIMER_TEST(_f, _s, NULL)

#define VMSTATE_TIMER_ARRAY(_f, _s, _n)                              \
    VMSTATE_ARRAY_OF_POINTER(_f, _s, _n, 0, vmstate_info_timer, QEMUTimer *)

#define VMSTATE_BOOL_ARRAY_V(_f, _s, _n, _v)                         \
    VMSTATE_ARRAY(_f, _s, _n, _v, vmstate_info_bool, bool)

#define VMSTATE_BOOL_ARRAY(_f, _s, _n)                               \
    VMSTATE_BOOL_ARRAY_V(_f, _s, _n, 0)

#define VMSTATE_UINT16_ARRAY_V(_f, _s, _n, _v)                         \
    VMSTATE_ARRAY(_f, _s, _n, _v, vmstate_info_uint16, uint16_t)

#define VMSTATE_UINT16_ARRAY(_f, _s, _n)                               \
    VMSTATE_UINT16_ARRAY_V(_f, _s, _n, 0)

#define VMSTATE_UINT8_ARRAY_V(_f, _s, _n, _v)                         \
    VMSTATE_ARRAY(_f, _s, _n, _v, vmstate_info_uint8, uint8_t)

#define VMSTATE_UINT8_ARRAY(_f, _s, _n)                               \
    VMSTATE_UINT8_ARRAY_V(_f, _s, _n, 0)

#define VMSTATE_UINT32_ARRAY_V(_f, _s, _n, _v)                        \
    VMSTATE_ARRAY(_f, _s, _n, _v, vmstate_info_uint32, uint32_t)

#define VMSTATE_UINT32_ARRAY(_f, _s, _n)                              \
    VMSTATE_UINT32_ARRAY_V(_f, _s, _n, 0)

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

#ifdef NEED_CPU_H
#if TARGET_LONG_BITS == 64
#define VMSTATE_UINTTL_V(_f, _s, _v)                                  \
    VMSTATE_UINT64_V(_f, _s, _v)
#define VMSTATE_UINTTL_ARRAY_V(_f, _s, _n, _v)                        \
    VMSTATE_UINT64_ARRAY_V(_f, _s, _n, _v)
#else
#define VMSTATE_UINTTL_V(_f, _s, _v)                                  \
    VMSTATE_UINT32_V(_f, _s, _v)
#define VMSTATE_UINTTL_ARRAY_V(_f, _s, _n, _v)                        \
    VMSTATE_UINT32_ARRAY_V(_f, _s, _n, _v)
#endif
#define VMSTATE_UINTTL(_f, _s)                                        \
    VMSTATE_UINTTL_V(_f, _s, 0)
#define VMSTATE_UINTTL_ARRAY(_f, _s, _n)                              \
    VMSTATE_UINTTL_ARRAY_V(_f, _s, _n, 0)

#endif

#define VMSTATE_END_OF_LIST()                                         \
    {}

int vmstate_load_state(QEMUFile *f, const VMStateDescription *vmsd,
                       void *opaque, int version_id);
void vmstate_save_state(QEMUFile *f, const VMStateDescription *vmsd,
                        void *opaque);
int vmstate_register(DeviceState *dev, int instance_id,
                     const VMStateDescription *vmsd, void *base);
int vmstate_register_with_alias_id(DeviceState *dev, int instance_id,
                                   const VMStateDescription *vmsd,
                                   void *base, int alias_id,
                                   int required_for_version);
void vmstate_unregister(DeviceState *dev, const VMStateDescription *vmsd,
                        void *opaque);
#endif
