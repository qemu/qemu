/* Declarations for use by hardware emulation.  */
#ifndef QEMU_HW_H
#define QEMU_HW_H

#include "qemu-common.h"

#if defined(TARGET_PHYS_ADDR_BITS) && !defined(NEED_CPU_H)
#include "targphys.h"
#include "poison.h"
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
typedef size_t (QEMUFileSetRateLimit)(void *opaque, size_t new_rate);

QEMUFile *qemu_fopen_ops(void *opaque, QEMUFilePutBufferFunc *put_buffer,
                         QEMUFileGetBufferFunc *get_buffer,
                         QEMUFileCloseFunc *close,
                         QEMUFileRateLimit *rate_limit,
                         QEMUFileSetRateLimit *set_rate_limit);
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
size_t qemu_file_set_rate_limit(QEMUFile *f, size_t new_rate);
int qemu_file_has_error(QEMUFile *f);
void qemu_file_set_error(QEMUFile *f);

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

typedef void SaveStateHandler(QEMUFile *f, void *opaque);
typedef int SaveLiveStateHandler(QEMUFile *f, int stage, void *opaque);
typedef int LoadStateHandler(QEMUFile *f, void *opaque, int version_id);

int register_savevm(const char *idstr,
                    int instance_id,
                    int version_id,
                    SaveStateHandler *save_state,
                    LoadStateHandler *load_state,
                    void *opaque);

int register_savevm_live(const char *idstr,
                         int instance_id,
                         int version_id,
                         SaveLiveStateHandler *save_live_state,
                         SaveStateHandler *save_state,
                         LoadStateHandler *load_state,
                         void *opaque);

void unregister_savevm(const char *idstr, void *opaque);

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
    void (*put)(QEMUFile *f, const void *pv, size_t size);
};

enum VMStateFlags {
    VMS_SINGLE  = 0x001,
    VMS_POINTER = 0x002,
    VMS_ARRAY   = 0x004,
    VMS_STRUCT  = 0x008,
    VMS_VARRAY  = 0x010,  /* Array with size in another field */
    VMS_BUFFER  = 0x020,  /* static sized buffer */
};

typedef struct {
    const char *name;
    size_t offset;
    size_t size;
    int num;
    size_t num_offset;
    const VMStateInfo *info;
    enum VMStateFlags flags;
    const VMStateDescription *vmsd;
    int version_id;
} VMStateField;

struct VMStateDescription {
    const char *name;
    int version_id;
    int minimum_version_id;
    int minimum_version_id_old;
    LoadStateHandler *load_state_old;
    VMStateField *fields;
};

extern const VMStateInfo vmstate_info_int8;
extern const VMStateInfo vmstate_info_int16;
extern const VMStateInfo vmstate_info_int32;
extern const VMStateInfo vmstate_info_int64;

extern const VMStateInfo vmstate_info_int32_equal;

extern const VMStateInfo vmstate_info_uint8;
extern const VMStateInfo vmstate_info_uint16;
extern const VMStateInfo vmstate_info_uint32;
extern const VMStateInfo vmstate_info_uint64;

extern const VMStateInfo vmstate_info_timer;
extern const VMStateInfo vmstate_info_buffer;

#define type_check_array(t1,t2,n) ((t1(*)[n])0 - (t2*)0)
#define type_check_pointer(t1,t2) ((t1**)0 - (t2*)0)

#define VMSTATE_SINGLE(_field, _state, _version, _info, _type) {     \
    .name       = (stringify(_field)),                               \
    .version_id = (_version),                                        \
    .size       = sizeof(_type),                                     \
    .info       = &(_info),                                          \
    .flags      = VMS_SINGLE,                                        \
    .offset     = offsetof(_state, _field)                           \
            + type_check(_type,typeof_field(_state, _field))         \
}

#define VMSTATE_POINTER(_field, _state, _version, _info, _type) {    \
    .name       = (stringify(_field)),                               \
    .version_id = (_version),                                        \
    .info       = &(_info),                                          \
    .size       = sizeof(_type),                                     \
    .flags      = VMS_SINGLE|VMS_POINTER,                            \
    .offset     = offsetof(_state, _field)                           \
            + type_check(_type,typeof_field(_state, _field))         \
}

#define VMSTATE_ARRAY(_field, _state, _num, _version, _info, _type) {\
    .name       = (stringify(_field)),                               \
    .version_id = (_version),                                        \
    .num        = (_num),                                            \
    .info       = &(_info),                                          \
    .size       = sizeof(_type),                                     \
    .flags      = VMS_ARRAY,                                         \
    .offset     = offsetof(_state, _field)                           \
        + type_check_array(_type,typeof_field(_state, _field),_num)  \
}

#define VMSTATE_VARRAY(_field, _state, _field_num, _version, _info, _type) {\
    .name       = (stringify(_field)),                               \
    .version_id = (_version),                                        \
    .num_offset = offsetof(_state, _field_num)                       \
        + type_check(int32_t,typeof_field(_state, _field_num)),      \
    .info       = &(_info),                                          \
    .size       = sizeof(_type),                                     \
    .flags      = VMS_VARRAY|VMS_POINTER,                            \
    .offset     = offsetof(_state, _field)                           \
        + type_check_pointer(_type,typeof_field(_state, _field))     \
}

#define VMSTATE_STRUCT(_field, _state, _version, _vmsd, _type) {     \
    .name       = (stringify(_field)),                               \
    .version_id = (_version),                                        \
    .vmsd       = &(_vmsd),                                          \
    .size       = sizeof(_type),                                     \
    .flags      = VMS_STRUCT,                                        \
    .offset     = offsetof(_state, _field)                           \
            + type_check(_type,typeof_field(_state, _field))         \
}

#define VMSTATE_STRUCT_ARRAY(_field, _state, _num, _version, _vmsd, _type) { \
    .name       = (stringify(_field)),                               \
    .num        = (_num),                                            \
    .version_id = (_version),                                        \
    .vmsd       = &(_vmsd),                                          \
    .size       = sizeof(_type),                                     \
    .flags      = VMS_STRUCT|VMS_ARRAY,                              \
    .offset     = offsetof(_state, _field)                           \
        + type_check_array(_type,typeof_field(_state, _field),_num)  \
}

#define VMSTATE_STATIC_BUFFER(_field, _state, _version) {            \
    .name       = (stringify(_field)),                               \
    .version_id = (_version),                                        \
    .size       = sizeof(typeof_field(_state,_field)),               \
    .info       = &vmstate_info_buffer,                              \
    .flags      = VMS_BUFFER,                                        \
    .offset     = offsetof(_state, _field)                           \
        + type_check_array(uint8_t,typeof_field(_state, _field),sizeof(typeof_field(_state,_field))) \
}

/* _f : field name
   _f_n : num of elements field_name
   _n : num of elements
   _s : struct state name
   _v : version
*/

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

#define VMSTATE_INT32_EQUAL(_f, _s)                                   \
    VMSTATE_SINGLE(_f, _s, 0, vmstate_info_int32_equal, int32_t)

#define VMSTATE_TIMER_V(_f, _s, _v)                                   \
    VMSTATE_POINTER(_f, _s, _v, vmstate_info_timer, QEMUTimer *)

#define VMSTATE_TIMER(_f, _s)                                         \
    VMSTATE_TIMER_V(_f, _s, 0)

#define VMSTATE_UINT32_ARRAY_V(_f, _s, _n, _v)                        \
    VMSTATE_ARRAY(_f, _s, _n, _v, vmstate_info_uint32, uint32_t)

#define VMSTATE_UINT32_ARRAY(_f, _s, _n)                              \
    VMSTATE_UINT32_ARRAY_V(_f, _s, _n, 0)

#define VMSTATE_INT32_ARRAY_V(_f, _s, _n, _v)                         \
    VMSTATE_ARRAY(_f, _s, _n, _v, vmstate_info_int32, int32_t)

#define VMSTATE_INT32_ARRAY(_f, _s, _n)                               \
    VMSTATE_INT32_ARRAY_V(_f, _s, _n, 0)

#define VMSTATE_INT32_VARRAY_V(_f, _s, _f_n, _v)                      \
    VMSTATE_VARRAY(_f, _s, _f_n, _v, vmstate_info_int32, int32_t)

#define VMSTATE_INT32_VARRAY(_f, _s, _f_n)                            \
    VMSTATE_INT32_VARRAY_V(_f, _s, _f_n, 0)

#define VMSTATE_BUFFER_V(_f, _s, _v)                                  \
    VMSTATE_STATIC_BUFFER(_f, _s, _v)

#define VMSTATE_BUFFER(_f, _s)                                        \
    VMSTATE_STATIC_BUFFER(_f, _s, 0)

#define VMSTATE_END_OF_LIST()                                         \
    {}

extern int vmstate_load_state(QEMUFile *f, const VMStateDescription *vmsd,
                              void *opaque, int version_id);
extern void vmstate_save_state(QEMUFile *f, const VMStateDescription *vmsd,
                               const void *opaque);
extern int vmstate_register(int instance_id, const VMStateDescription *vmsd,
                            void *base);
extern void vmstate_unregister(const char *idstr, void *opaque);
#endif
