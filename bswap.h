#ifndef BSWAP_H
#define BSWAP_H

#include "config-host.h"

#include <inttypes.h>

#ifdef CONFIG_MACHINE_BSWAP_H
#include <sys/endian.h>
#include <sys/types.h>
#include <machine/bswap.h>
#else

#include "softfloat.h"

#ifdef CONFIG_BYTESWAP_H
#include <byteswap.h>
#else

#define bswap_16(x) \
({ \
	uint16_t __x = (x); \
	((uint16_t)( \
		(((uint16_t)(__x) & (uint16_t)0x00ffU) << 8) | \
		(((uint16_t)(__x) & (uint16_t)0xff00U) >> 8) )); \
})

#define bswap_32(x) \
({ \
	uint32_t __x = (x); \
	((uint32_t)( \
		(((uint32_t)(__x) & (uint32_t)0x000000ffUL) << 24) | \
		(((uint32_t)(__x) & (uint32_t)0x0000ff00UL) <<  8) | \
		(((uint32_t)(__x) & (uint32_t)0x00ff0000UL) >>  8) | \
		(((uint32_t)(__x) & (uint32_t)0xff000000UL) >> 24) )); \
})

#define bswap_64(x) \
({ \
	uint64_t __x = (x); \
	((uint64_t)( \
		(uint64_t)(((uint64_t)(__x) & (uint64_t)0x00000000000000ffULL) << 56) | \
		(uint64_t)(((uint64_t)(__x) & (uint64_t)0x000000000000ff00ULL) << 40) | \
		(uint64_t)(((uint64_t)(__x) & (uint64_t)0x0000000000ff0000ULL) << 24) | \
		(uint64_t)(((uint64_t)(__x) & (uint64_t)0x00000000ff000000ULL) <<  8) | \
	        (uint64_t)(((uint64_t)(__x) & (uint64_t)0x000000ff00000000ULL) >>  8) | \
		(uint64_t)(((uint64_t)(__x) & (uint64_t)0x0000ff0000000000ULL) >> 24) | \
		(uint64_t)(((uint64_t)(__x) & (uint64_t)0x00ff000000000000ULL) >> 40) | \
		(uint64_t)(((uint64_t)(__x) & (uint64_t)0xff00000000000000ULL) >> 56) )); \
})

#endif /* !CONFIG_BYTESWAP_H */

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
#define le_bswap(v, size) bswap ## size(v)
#define be_bswaps(v, size)
#define le_bswaps(p, size) *p = bswap ## size(*p);
#else
#define le_bswap(v, size) (v)
#define be_bswap(v, size) bswap ## size(v)
#define le_bswaps(v, size)
#define be_bswaps(p, size) *p = bswap ## size(*p);
#endif

#define CPU_CONVERT(endian, size, type)\
static inline type endian ## size ## _to_cpu(type v)\
{\
    return endian ## _bswap(v, size);\
}\
\
static inline type cpu_to_ ## endian ## size(type v)\
{\
    return endian ## _bswap(v, size);\
}\
\
static inline void endian ## size ## _to_cpus(type *p)\
{\
    endian ## _bswaps(p, size)\
}\
\
static inline void cpu_to_ ## endian ## size ## s(type *p)\
{\
    endian ## _bswaps(p, size)\
}\
\
static inline type endian ## size ## _to_cpup(const type *p)\
{\
    return endian ## size ## _to_cpu(*p);\
}\
\
static inline void cpu_to_ ## endian ## size ## w(type *p, type v)\
{\
     *p = cpu_to_ ## endian ## size(v);\
}

CPU_CONVERT(be, 16, uint16_t)
CPU_CONVERT(be, 32, uint32_t)
CPU_CONVERT(be, 64, uint64_t)

CPU_CONVERT(le, 16, uint16_t)
CPU_CONVERT(le, 32, uint32_t)
CPU_CONVERT(le, 64, uint64_t)

/* unaligned versions (optimized for frequent unaligned accesses)*/

#if defined(__i386__) || defined(_ARCH_PPC)

#define cpu_to_le16wu(p, v) cpu_to_le16w(p, v)
#define cpu_to_le32wu(p, v) cpu_to_le32w(p, v)
#define le16_to_cpupu(p) le16_to_cpup(p)
#define le32_to_cpupu(p) le32_to_cpup(p)
#define be32_to_cpupu(p) be32_to_cpup(p)

#define cpu_to_be16wu(p, v) cpu_to_be16w(p, v)
#define cpu_to_be32wu(p, v) cpu_to_be32w(p, v)
#define cpu_to_be64wu(p, v) cpu_to_be64w(p, v)

#else

static inline void cpu_to_le16wu(uint16_t *p, uint16_t v)
{
    uint8_t *p1 = (uint8_t *)p;

    p1[0] = v & 0xff;
    p1[1] = v >> 8;
}

static inline void cpu_to_le32wu(uint32_t *p, uint32_t v)
{
    uint8_t *p1 = (uint8_t *)p;

    p1[0] = v & 0xff;
    p1[1] = v >> 8;
    p1[2] = v >> 16;
    p1[3] = v >> 24;
}

static inline uint16_t le16_to_cpupu(const uint16_t *p)
{
    const uint8_t *p1 = (const uint8_t *)p;
    return p1[0] | (p1[1] << 8);
}

static inline uint32_t le32_to_cpupu(const uint32_t *p)
{
    const uint8_t *p1 = (const uint8_t *)p;
    return p1[0] | (p1[1] << 8) | (p1[2] << 16) | (p1[3] << 24);
}

static inline uint32_t be32_to_cpupu(const uint32_t *p)
{
    const uint8_t *p1 = (const uint8_t *)p;
    return p1[3] | (p1[2] << 8) | (p1[1] << 16) | (p1[0] << 24);
}

static inline void cpu_to_be16wu(uint16_t *p, uint16_t v)
{
    uint8_t *p1 = (uint8_t *)p;

    p1[0] = v >> 8;
    p1[1] = v & 0xff;
}

static inline void cpu_to_be32wu(uint32_t *p, uint32_t v)
{
    uint8_t *p1 = (uint8_t *)p;

    p1[0] = v >> 24;
    p1[1] = v >> 16;
    p1[2] = v >> 8;
    p1[3] = v & 0xff;
}

static inline void cpu_to_be64wu(uint64_t *p, uint64_t v)
{
    uint8_t *p1 = (uint8_t *)p;

    p1[0] = v >> 56;
    p1[1] = v >> 48;
    p1[2] = v >> 40;
    p1[3] = v >> 32;
    p1[4] = v >> 24;
    p1[5] = v >> 16;
    p1[6] = v >> 8;
    p1[7] = v & 0xff;
}

#endif

#ifdef HOST_WORDS_BIGENDIAN
#define cpu_to_32wu cpu_to_be32wu
#define leul_to_cpu(v) glue(glue(le,HOST_LONG_BITS),_to_cpu)(v)
#else
#define cpu_to_32wu cpu_to_le32wu
#define leul_to_cpu(v) (v)
#endif

#undef le_bswap
#undef be_bswap
#undef le_bswaps
#undef be_bswaps

/* len must be one of 1, 2, 4 */
static inline uint32_t qemu_bswap_len(uint32_t value, int len)
{
    return bswap32(value) >> (32 - 8 * len);
}

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
 * (empty): 8 bit access
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

/* NOTE: on arm, putting 2 in /proc/sys/debug/alignment so that the
   kernel handles unaligned load/stores may give better results, but
   it is a system wide setting : bad */
#if defined(HOST_WORDS_BIGENDIAN) || defined(WORDS_ALIGNED)

/* conservative code for little endian unaligned accesses */
static inline int lduw_le_p(const void *ptr)
{
#ifdef _ARCH_PPC
    int val;
    __asm__ __volatile__ ("lhbrx %0,0,%1" : "=r" (val) : "r" (ptr));
    return val;
#else
    const uint8_t *p = ptr;
    return p[0] | (p[1] << 8);
#endif
}

static inline int ldsw_le_p(const void *ptr)
{
#ifdef _ARCH_PPC
    int val;
    __asm__ __volatile__ ("lhbrx %0,0,%1" : "=r" (val) : "r" (ptr));
    return (int16_t)val;
#else
    const uint8_t *p = ptr;
    return (int16_t)(p[0] | (p[1] << 8));
#endif
}

static inline int ldl_le_p(const void *ptr)
{
#ifdef _ARCH_PPC
    int val;
    __asm__ __volatile__ ("lwbrx %0,0,%1" : "=r" (val) : "r" (ptr));
    return val;
#else
    const uint8_t *p = ptr;
    return p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24);
#endif
}

static inline uint64_t ldq_le_p(const void *ptr)
{
    const uint8_t *p = ptr;
    uint32_t v1, v2;
    v1 = ldl_le_p(p);
    v2 = ldl_le_p(p + 4);
    return v1 | ((uint64_t)v2 << 32);
}

static inline void stw_le_p(void *ptr, int v)
{
#ifdef _ARCH_PPC
    __asm__ __volatile__ ("sthbrx %1,0,%2" : "=m" (*(uint16_t *)ptr) : "r" (v), "r" (ptr));
#else
    uint8_t *p = ptr;
    p[0] = v;
    p[1] = v >> 8;
#endif
}

static inline void stl_le_p(void *ptr, int v)
{
#ifdef _ARCH_PPC
    __asm__ __volatile__ ("stwbrx %1,0,%2" : "=m" (*(uint32_t *)ptr) : "r" (v), "r" (ptr));
#else
    uint8_t *p = ptr;
    p[0] = v;
    p[1] = v >> 8;
    p[2] = v >> 16;
    p[3] = v >> 24;
#endif
}

static inline void stq_le_p(void *ptr, uint64_t v)
{
    uint8_t *p = ptr;
    stl_le_p(p, (uint32_t)v);
    stl_le_p(p + 4, v >> 32);
}

/* float access */

static inline float32 ldfl_le_p(const void *ptr)
{
    union {
        float32 f;
        uint32_t i;
    } u;
    u.i = ldl_le_p(ptr);
    return u.f;
}

static inline void stfl_le_p(void *ptr, float32 v)
{
    union {
        float32 f;
        uint32_t i;
    } u;
    u.f = v;
    stl_le_p(ptr, u.i);
}

static inline float64 ldfq_le_p(const void *ptr)
{
    CPU_DoubleU u;
    u.l.lower = ldl_le_p(ptr);
    u.l.upper = ldl_le_p(ptr + 4);
    return u.d;
}

static inline void stfq_le_p(void *ptr, float64 v)
{
    CPU_DoubleU u;
    u.d = v;
    stl_le_p(ptr, u.l.lower);
    stl_le_p(ptr + 4, u.l.upper);
}

#else

static inline int lduw_le_p(const void *ptr)
{
    return *(uint16_t *)ptr;
}

static inline int ldsw_le_p(const void *ptr)
{
    return *(int16_t *)ptr;
}

static inline int ldl_le_p(const void *ptr)
{
    return *(uint32_t *)ptr;
}

static inline uint64_t ldq_le_p(const void *ptr)
{
    return *(uint64_t *)ptr;
}

static inline void stw_le_p(void *ptr, int v)
{
    *(uint16_t *)ptr = v;
}

static inline void stl_le_p(void *ptr, int v)
{
    *(uint32_t *)ptr = v;
}

static inline void stq_le_p(void *ptr, uint64_t v)
{
    *(uint64_t *)ptr = v;
}

/* float access */

static inline float32 ldfl_le_p(const void *ptr)
{
    return *(float32 *)ptr;
}

static inline float64 ldfq_le_p(const void *ptr)
{
    return *(float64 *)ptr;
}

static inline void stfl_le_p(void *ptr, float32 v)
{
    *(float32 *)ptr = v;
}

static inline void stfq_le_p(void *ptr, float64 v)
{
    *(float64 *)ptr = v;
}
#endif

#if !defined(HOST_WORDS_BIGENDIAN) || defined(WORDS_ALIGNED)

static inline int lduw_be_p(const void *ptr)
{
#if defined(__i386__)
    int val;
    asm volatile ("movzwl %1, %0\n"
                  "xchgb %b0, %h0\n"
                  : "=q" (val)
                  : "m" (*(uint16_t *)ptr));
    return val;
#else
    const uint8_t *b = ptr;
    return ((b[0] << 8) | b[1]);
#endif
}

static inline int ldsw_be_p(const void *ptr)
{
#if defined(__i386__)
    int val;
    asm volatile ("movzwl %1, %0\n"
                  "xchgb %b0, %h0\n"
                  : "=q" (val)
                  : "m" (*(uint16_t *)ptr));
    return (int16_t)val;
#else
    const uint8_t *b = ptr;
    return (int16_t)((b[0] << 8) | b[1]);
#endif
}

static inline int ldl_be_p(const void *ptr)
{
#if defined(__i386__) || defined(__x86_64__)
    int val;
    asm volatile ("movl %1, %0\n"
                  "bswap %0\n"
                  : "=r" (val)
                  : "m" (*(uint32_t *)ptr));
    return val;
#else
    const uint8_t *b = ptr;
    return (b[0] << 24) | (b[1] << 16) | (b[2] << 8) | b[3];
#endif
}

static inline uint64_t ldq_be_p(const void *ptr)
{
    uint32_t a,b;
    a = ldl_be_p(ptr);
    b = ldl_be_p((uint8_t *)ptr + 4);
    return (((uint64_t)a<<32)|b);
}

static inline void stw_be_p(void *ptr, int v)
{
#if defined(__i386__)
    asm volatile ("xchgb %b0, %h0\n"
                  "movw %w0, %1\n"
                  : "=q" (v)
                  : "m" (*(uint16_t *)ptr), "0" (v));
#else
    uint8_t *d = (uint8_t *) ptr;
    d[0] = v >> 8;
    d[1] = v;
#endif
}

static inline void stl_be_p(void *ptr, int v)
{
#if defined(__i386__) || defined(__x86_64__)
    asm volatile ("bswap %0\n"
                  "movl %0, %1\n"
                  : "=r" (v)
                  : "m" (*(uint32_t *)ptr), "0" (v));
#else
    uint8_t *d = (uint8_t *) ptr;
    d[0] = v >> 24;
    d[1] = v >> 16;
    d[2] = v >> 8;
    d[3] = v;
#endif
}

static inline void stq_be_p(void *ptr, uint64_t v)
{
    stl_be_p(ptr, v >> 32);
    stl_be_p((uint8_t *)ptr + 4, v);
}

/* float access */

static inline float32 ldfl_be_p(const void *ptr)
{
    union {
        float32 f;
        uint32_t i;
    } u;
    u.i = ldl_be_p(ptr);
    return u.f;
}

static inline void stfl_be_p(void *ptr, float32 v)
{
    union {
        float32 f;
        uint32_t i;
    } u;
    u.f = v;
    stl_be_p(ptr, u.i);
}

static inline float64 ldfq_be_p(const void *ptr)
{
    CPU_DoubleU u;
    u.l.upper = ldl_be_p(ptr);
    u.l.lower = ldl_be_p((uint8_t *)ptr + 4);
    return u.d;
}

static inline void stfq_be_p(void *ptr, float64 v)
{
    CPU_DoubleU u;
    u.d = v;
    stl_be_p(ptr, u.l.upper);
    stl_be_p((uint8_t *)ptr + 4, u.l.lower);
}

#else

static inline int lduw_be_p(const void *ptr)
{
    return *(uint16_t *)ptr;
}

static inline int ldsw_be_p(const void *ptr)
{
    return *(int16_t *)ptr;
}

static inline int ldl_be_p(const void *ptr)
{
    return *(uint32_t *)ptr;
}

static inline uint64_t ldq_be_p(const void *ptr)
{
    return *(uint64_t *)ptr;
}

static inline void stw_be_p(void *ptr, int v)
{
    *(uint16_t *)ptr = v;
}

static inline void stl_be_p(void *ptr, int v)
{
    *(uint32_t *)ptr = v;
}

static inline void stq_be_p(void *ptr, uint64_t v)
{
    *(uint64_t *)ptr = v;
}

/* float access */

static inline float32 ldfl_be_p(const void *ptr)
{
    return *(float32 *)ptr;
}

static inline float64 ldfq_be_p(const void *ptr)
{
    return *(float64 *)ptr;
}

static inline void stfl_be_p(void *ptr, float32 v)
{
    *(float32 *)ptr = v;
}

static inline void stfq_be_p(void *ptr, float64 v)
{
    *(float64 *)ptr = v;
}

#endif

#endif /* BSWAP_H */
