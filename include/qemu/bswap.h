#ifndef BSWAP_H
#define BSWAP_H

#include "config-host.h"
#include <inttypes.h>
#include <limits.h>
#include <string.h>
#include "fpu/softfloat.h"

#ifdef CONFIG_MACHINE_BSWAP_H
# include <sys/endian.h>
# include <sys/types.h>
# include <machine/bswap.h>
#elif defined(CONFIG_BYTESWAP_H)
# include <byteswap.h>

static inline uint16_t bswap16(uint16_t x)
{
    return bswap_16(x);
}

static inline uint32_t bswap32(uint32_t x)
{
    return bswap_32(x);
}

static inline uint64_t bswap64(uint64_t x)
{
    return bswap_64(x);
}
# else
static inline uint16_t bswap16(uint16_t x)
{
    return (((x & 0x00ff) << 8) |
            ((x & 0xff00) >> 8));
}

static inline uint32_t bswap32(uint32_t x)
{
    return (((x & 0x000000ffU) << 24) |
            ((x & 0x0000ff00U) <<  8) |
            ((x & 0x00ff0000U) >>  8) |
            ((x & 0xff000000U) >> 24));
}

static inline uint64_t bswap64(uint64_t x)
{
    return (((x & 0x00000000000000ffULL) << 56) |
            ((x & 0x000000000000ff00ULL) << 40) |
            ((x & 0x0000000000ff0000ULL) << 24) |
            ((x & 0x00000000ff000000ULL) <<  8) |
            ((x & 0x000000ff00000000ULL) >>  8) |
            ((x & 0x0000ff0000000000ULL) >> 24) |
            ((x & 0x00ff000000000000ULL) >> 40) |
            ((x & 0xff00000000000000ULL) >> 56));
}
#endif /* ! CONFIG_MACHINE_BSWAP_H */

static inline void bswap16s(uint16_t *s)
{
    *s = bswap16(*s);
}

static inline void bswap32s(uint32_t *s)
{
    *s = bswap32(*s);
}

static inline void bswap64s(uint64_t *s)
{
    *s = bswap64(*s);
}

#if defined(HOST_WORDS_BIGENDIAN)
#define be_bswap(v, size) (v)
#define le_bswap(v, size) glue(bswap, size)(v)
#define be_bswaps(v, size)
#define le_bswaps(p, size) do { *p = glue(bswap, size)(*p); } while(0)
#else
#define le_bswap(v, size) (v)
#define be_bswap(v, size) glue(bswap, size)(v)
#define le_bswaps(v, size)
#define be_bswaps(p, size) do { *p = glue(bswap, size)(*p); } while(0)
#endif

#define CPU_CONVERT(endian, size, type)\
static inline type endian ## size ## _to_cpu(type v)\
{\
    return glue(endian, _bswap)(v, size);\
}\
\
static inline type cpu_to_ ## endian ## size(type v)\
{\
    return glue(endian, _bswap)(v, size);\
}\
\
static inline void endian ## size ## _to_cpus(type *p)\
{\
    glue(endian, _bswaps)(p, size);\
}\
\
static inline void cpu_to_ ## endian ## size ## s(type *p)\
{\
    glue(endian, _bswaps)(p, size);\
}\
\
static inline type endian ## size ## _to_cpup(const type *p)\
{\
    return glue(glue(endian, size), _to_cpu)(*p);\
}\
\
static inline void cpu_to_ ## endian ## size ## w(type *p, type v)\
{\
    *p = glue(glue(cpu_to_, endian), size)(v);\
}

CPU_CONVERT(be, 16, uint16_t)
CPU_CONVERT(be, 32, uint32_t)
CPU_CONVERT(be, 64, uint64_t)

CPU_CONVERT(le, 16, uint16_t)
CPU_CONVERT(le, 32, uint32_t)
CPU_CONVERT(le, 64, uint64_t)

/* len must be one of 1, 2, 4 */
static inline uint32_t qemu_bswap_len(uint32_t value, int len)
{
    return bswap32(value) >> (32 - 8 * len);
}

/* Unions for reinterpreting between floats and integers.  */

typedef union {
    float32 f;
    uint32_t l;
} CPU_FloatU;

typedef union {
    float64 d;
#if defined(HOST_WORDS_BIGENDIAN)
    struct {
        uint32_t upper;
        uint32_t lower;
    } l;
#else
    struct {
        uint32_t lower;
        uint32_t upper;
    } l;
#endif
    uint64_t ll;
} CPU_DoubleU;

typedef union {
     floatx80 d;
     struct {
         uint64_t lower;
         uint16_t upper;
     } l;
} CPU_LDoubleU;

typedef union {
    float128 q;
#if defined(HOST_WORDS_BIGENDIAN)
    struct {
        uint32_t upmost;
        uint32_t upper;
        uint32_t lower;
        uint32_t lowest;
    } l;
    struct {
        uint64_t upper;
        uint64_t lower;
    } ll;
#else
    struct {
        uint32_t lowest;
        uint32_t lower;
        uint32_t upper;
        uint32_t upmost;
    } l;
    struct {
        uint64_t lower;
        uint64_t upper;
    } ll;
#endif
} CPU_QuadU;

/* unaligned/endian-independent pointer access */

/*
 * the generic syntax is:
 *
 * load: ld{type}{sign}{size}{endian}_p(ptr)
 *
 * store: st{type}{size}{endian}_p(ptr, val)
 *
 * Note there are small differences with the softmmu access API!
 *
 * type is:
 * (empty): integer access
 *   f    : float access
 *
 * sign is:
 * (empty): for floats or 32 bit size
 *   u    : unsigned
 *   s    : signed
 *
 * size is:
 *   b: 8 bits
 *   w: 16 bits
 *   l: 32 bits
 *   q: 64 bits
 *
 * endian is:
 * (empty): host endian
 *   be   : big endian
 *   le   : little endian
 */

static inline int ldub_p(const void *ptr)
{
    return *(uint8_t *)ptr;
}

static inline int ldsb_p(const void *ptr)
{
    return *(int8_t *)ptr;
}

static inline void stb_p(void *ptr, int v)
{
    *(uint8_t *)ptr = v;
}

/* Any compiler worth its salt will turn these memcpy into native unaligned
   operations.  Thus we don't need to play games with packed attributes, or
   inline byte-by-byte stores.  */

static inline int lduw_p(const void *ptr)
{
    uint16_t r;
    memcpy(&r, ptr, sizeof(r));
    return r;
}

static inline int ldsw_p(const void *ptr)
{
    int16_t r;
    memcpy(&r, ptr, sizeof(r));
    return r;
}

static inline void stw_p(void *ptr, uint16_t v)
{
    memcpy(ptr, &v, sizeof(v));
}

static inline int ldl_p(const void *ptr)
{
    int32_t r;
    memcpy(&r, ptr, sizeof(r));
    return r;
}

static inline void stl_p(void *ptr, uint32_t v)
{
    memcpy(ptr, &v, sizeof(v));
}

static inline uint64_t ldq_p(const void *ptr)
{
    uint64_t r;
    memcpy(&r, ptr, sizeof(r));
    return r;
}

static inline void stq_p(void *ptr, uint64_t v)
{
    memcpy(ptr, &v, sizeof(v));
}

static inline int lduw_le_p(const void *ptr)
{
    return (uint16_t)le_bswap(lduw_p(ptr), 16);
}

static inline int ldsw_le_p(const void *ptr)
{
    return (int16_t)le_bswap(lduw_p(ptr), 16);
}

static inline int ldl_le_p(const void *ptr)
{
    return le_bswap(ldl_p(ptr), 32);
}

static inline uint64_t ldq_le_p(const void *ptr)
{
    return le_bswap(ldq_p(ptr), 64);
}

static inline void stw_le_p(void *ptr, int v)
{
    stw_p(ptr, le_bswap(v, 16));
}

static inline void stl_le_p(void *ptr, int v)
{
    stl_p(ptr, le_bswap(v, 32));
}

static inline void stq_le_p(void *ptr, uint64_t v)
{
    stq_p(ptr, le_bswap(v, 64));
}

/* float access */

static inline float32 ldfl_le_p(const void *ptr)
{
    CPU_FloatU u;
    u.l = ldl_le_p(ptr);
    return u.f;
}

static inline void stfl_le_p(void *ptr, float32 v)
{
    CPU_FloatU u;
    u.f = v;
    stl_le_p(ptr, u.l);
}

static inline float64 ldfq_le_p(const void *ptr)
{
    CPU_DoubleU u;
    u.ll = ldq_le_p(ptr);
    return u.d;
}

static inline void stfq_le_p(void *ptr, float64 v)
{
    CPU_DoubleU u;
    u.d = v;
    stq_le_p(ptr, u.ll);
}

static inline int lduw_be_p(const void *ptr)
{
    return (uint16_t)be_bswap(lduw_p(ptr), 16);
}

static inline int ldsw_be_p(const void *ptr)
{
    return (int16_t)be_bswap(lduw_p(ptr), 16);
}

static inline int ldl_be_p(const void *ptr)
{
    return be_bswap(ldl_p(ptr), 32);
}

static inline uint64_t ldq_be_p(const void *ptr)
{
    return be_bswap(ldq_p(ptr), 64);
}

static inline void stw_be_p(void *ptr, int v)
{
    stw_p(ptr, be_bswap(v, 16));
}

static inline void stl_be_p(void *ptr, int v)
{
    stl_p(ptr, be_bswap(v, 32));
}

static inline void stq_be_p(void *ptr, uint64_t v)
{
    stq_p(ptr, be_bswap(v, 64));
}

/* float access */

static inline float32 ldfl_be_p(const void *ptr)
{
    CPU_FloatU u;
    u.l = ldl_be_p(ptr);
    return u.f;
}

static inline void stfl_be_p(void *ptr, float32 v)
{
    CPU_FloatU u;
    u.f = v;
    stl_be_p(ptr, u.l);
}

static inline float64 ldfq_be_p(const void *ptr)
{
    CPU_DoubleU u;
    u.ll = ldq_be_p(ptr);
    return u.d;
}

static inline void stfq_be_p(void *ptr, float64 v)
{
    CPU_DoubleU u;
    u.d = v;
    stq_be_p(ptr, u.ll);
}

/* Legacy unaligned versions.  Note that we never had a complete set.  */

static inline void cpu_to_le16wu(uint16_t *p, uint16_t v)
{
    stw_le_p(p, v);
}

static inline void cpu_to_le32wu(uint32_t *p, uint32_t v)
{
    stl_le_p(p, v);
}

static inline uint16_t le16_to_cpupu(const uint16_t *p)
{
    return lduw_le_p(p);
}

static inline uint32_t le32_to_cpupu(const uint32_t *p)
{
    return ldl_le_p(p);
}

static inline uint32_t be32_to_cpupu(const uint32_t *p)
{
    return ldl_be_p(p);
}

static inline void cpu_to_be16wu(uint16_t *p, uint16_t v)
{
    stw_be_p(p, v);
}

static inline void cpu_to_be32wu(uint32_t *p, uint32_t v)
{
    stl_be_p(p, v);
}

static inline void cpu_to_be64wu(uint64_t *p, uint64_t v)
{
    stq_be_p(p, v);
}

static inline void cpu_to_32wu(uint32_t *p, uint32_t v)
{
    stl_p(p, v);
}

static inline unsigned long leul_to_cpu(unsigned long v)
{
    /* In order to break an include loop between here and
       qemu-common.h, don't rely on HOST_LONG_BITS.  */
#if ULONG_MAX == UINT32_MAX
    return le_bswap(v, 32);
#elif ULONG_MAX == UINT64_MAX
    return le_bswap(v, 64);
#else
# error Unknown sizeof long
#endif
}

#undef le_bswap
#undef be_bswap
#undef le_bswaps
#undef be_bswaps

#endif /* BSWAP_H */
