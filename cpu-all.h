/*
 * defines common to all virtual CPUs
 *
 *  Copyright (c) 2003 Fabrice Bellard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */
#ifndef CPU_ALL_H
#define CPU_ALL_H

#include "qemu-common.h"
#include "cpu-common.h"

/* some important defines:
 *
 * WORDS_ALIGNED : if defined, the host cpu can only make word aligned
 * memory accesses.
 *
 * HOST_WORDS_BIGENDIAN : if defined, the host cpu is big endian and
 * otherwise little endian.
 *
 * (TARGET_WORDS_ALIGNED : same for target cpu (not supported yet))
 *
 * TARGET_WORDS_BIGENDIAN : same for target cpu
 */

#include "softfloat.h"

#if defined(HOST_WORDS_BIGENDIAN) != defined(TARGET_WORDS_BIGENDIAN)
#define BSWAP_NEEDED
#endif

#ifdef BSWAP_NEEDED

static inline uint16_t tswap16(uint16_t s)
{
    return bswap16(s);
}

static inline uint32_t tswap32(uint32_t s)
{
    return bswap32(s);
}

static inline uint64_t tswap64(uint64_t s)
{
    return bswap64(s);
}

static inline void tswap16s(uint16_t *s)
{
    *s = bswap16(*s);
}

static inline void tswap32s(uint32_t *s)
{
    *s = bswap32(*s);
}

static inline void tswap64s(uint64_t *s)
{
    *s = bswap64(*s);
}

#else

static inline uint16_t tswap16(uint16_t s)
{
    return s;
}

static inline uint32_t tswap32(uint32_t s)
{
    return s;
}

static inline uint64_t tswap64(uint64_t s)
{
    return s;
}

static inline void tswap16s(uint16_t *s)
{
}

static inline void tswap32s(uint32_t *s)
{
}

static inline void tswap64s(uint64_t *s)
{
}

#endif

#if TARGET_LONG_SIZE == 4
#define tswapl(s) tswap32(s)
#define tswapls(s) tswap32s((uint32_t *)(s))
#define bswaptls(s) bswap32s(s)
#else
#define tswapl(s) tswap64(s)
#define tswapls(s) tswap64s((uint64_t *)(s))
#define bswaptls(s) bswap64s(s)
#endif

typedef union {
    float32 f;
    uint32_t l;
} CPU_FloatU;

/* NOTE: arm FPA is horrible as double 32 bit words are stored in big
   endian ! */
typedef union {
    float64 d;
#if defined(HOST_WORDS_BIGENDIAN) \
    || (defined(__arm__) && !defined(__VFP_FP__) && !defined(CONFIG_SOFTFLOAT))
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

#if defined(CONFIG_SOFTFLOAT)
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
#endif

/* CPU memory access without any memory or io remapping */

/*
 * the generic syntax for the memory accesses is:
 *
 * load: ld{type}{sign}{size}{endian}_{access_type}(ptr)
 *
 * store: st{type}{size}{endian}_{access_type}(ptr, val)
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
 * (empty): target cpu endianness or 8 bit access
 *   r    : reversed target cpu endianness (not implemented yet)
 *   be   : big endian (not implemented yet)
 *   le   : little endian (not implemented yet)
 *
 * access_type is:
 *   raw    : host memory access
 *   user   : user mode access using soft MMU
 *   kernel : kernel mode access using soft MMU
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

/* target CPU memory access functions */
#if defined(TARGET_WORDS_BIGENDIAN)
#define lduw_p(p) lduw_be_p(p)
#define ldsw_p(p) ldsw_be_p(p)
#define ldl_p(p) ldl_be_p(p)
#define ldq_p(p) ldq_be_p(p)
#define ldfl_p(p) ldfl_be_p(p)
#define ldfq_p(p) ldfq_be_p(p)
#define stw_p(p, v) stw_be_p(p, v)
#define stl_p(p, v) stl_be_p(p, v)
#define stq_p(p, v) stq_be_p(p, v)
#define stfl_p(p, v) stfl_be_p(p, v)
#define stfq_p(p, v) stfq_be_p(p, v)
#else
#define lduw_p(p) lduw_le_p(p)
#define ldsw_p(p) ldsw_le_p(p)
#define ldl_p(p) ldl_le_p(p)
#define ldq_p(p) ldq_le_p(p)
#define ldfl_p(p) ldfl_le_p(p)
#define ldfq_p(p) ldfq_le_p(p)
#define stw_p(p, v) stw_le_p(p, v)
#define stl_p(p, v) stl_le_p(p, v)
#define stq_p(p, v) stq_le_p(p, v)
#define stfl_p(p, v) stfl_le_p(p, v)
#define stfq_p(p, v) stfq_le_p(p, v)
#endif

/* MMU memory access macros */

#if defined(CONFIG_USER_ONLY)
#include <assert.h>
#include "qemu-types.h"

/* On some host systems the guest address space is reserved on the host.
 * This allows the guest address space to be offset to a convenient location.
 */
#if defined(CONFIG_USE_GUEST_BASE)
extern unsigned long guest_base;
extern int have_guest_base;
extern unsigned long reserved_va;
#define GUEST_BASE guest_base
#define RESERVED_VA reserved_va
#else
#define GUEST_BASE 0ul
#define RESERVED_VA 0ul
#endif

/* All direct uses of g2h and h2g need to go away for usermode softmmu.  */
#define g2h(x) ((void *)((unsigned long)(x) + GUEST_BASE))

#if HOST_LONG_BITS <= TARGET_VIRT_ADDR_SPACE_BITS
#define h2g_valid(x) 1
#else
#define h2g_valid(x) ({ \
    unsigned long __guest = (unsigned long)(x) - GUEST_BASE; \
    __guest < (1ul << TARGET_VIRT_ADDR_SPACE_BITS); \
})
#endif

#define h2g(x) ({ \
    unsigned long __ret = (unsigned long)(x) - GUEST_BASE; \
    /* Check if given address fits target address space */ \
    assert(h2g_valid(x)); \
    (abi_ulong)__ret; \
})

#define saddr(x) g2h(x)
#define laddr(x) g2h(x)

#else /* !CONFIG_USER_ONLY */
/* NOTE: we use double casts if pointers and target_ulong have
   different sizes */
#define saddr(x) (uint8_t *)(long)(x)
#define laddr(x) (uint8_t *)(long)(x)
#endif

#define ldub_raw(p) ldub_p(laddr((p)))
#define ldsb_raw(p) ldsb_p(laddr((p)))
#define lduw_raw(p) lduw_p(laddr((p)))
#define ldsw_raw(p) ldsw_p(laddr((p)))
#define ldl_raw(p) ldl_p(laddr((p)))
#define ldq_raw(p) ldq_p(laddr((p)))
#define ldfl_raw(p) ldfl_p(laddr((p)))
#define ldfq_raw(p) ldfq_p(laddr((p)))
#define stb_raw(p, v) stb_p(saddr((p)), v)
#define stw_raw(p, v) stw_p(saddr((p)), v)
#define stl_raw(p, v) stl_p(saddr((p)), v)
#define stq_raw(p, v) stq_p(saddr((p)), v)
#define stfl_raw(p, v) stfl_p(saddr((p)), v)
#define stfq_raw(p, v) stfq_p(saddr((p)), v)


#if defined(CONFIG_USER_ONLY)

/* if user mode, no other memory access functions */
#define ldub(p) ldub_raw(p)
#define ldsb(p) ldsb_raw(p)
#define lduw(p) lduw_raw(p)
#define ldsw(p) ldsw_raw(p)
#define ldl(p) ldl_raw(p)
#define ldq(p) ldq_raw(p)
#define ldfl(p) ldfl_raw(p)
#define ldfq(p) ldfq_raw(p)
#define stb(p, v) stb_raw(p, v)
#define stw(p, v) stw_raw(p, v)
#define stl(p, v) stl_raw(p, v)
#define stq(p, v) stq_raw(p, v)
#define stfl(p, v) stfl_raw(p, v)
#define stfq(p, v) stfq_raw(p, v)

#define ldub_code(p) ldub_raw(p)
#define ldsb_code(p) ldsb_raw(p)
#define lduw_code(p) lduw_raw(p)
#define ldsw_code(p) ldsw_raw(p)
#define ldl_code(p) ldl_raw(p)
#define ldq_code(p) ldq_raw(p)

#define ldub_kernel(p) ldub_raw(p)
#define ldsb_kernel(p) ldsb_raw(p)
#define lduw_kernel(p) lduw_raw(p)
#define ldsw_kernel(p) ldsw_raw(p)
#define ldl_kernel(p) ldl_raw(p)
#define ldq_kernel(p) ldq_raw(p)
#define ldfl_kernel(p) ldfl_raw(p)
#define ldfq_kernel(p) ldfq_raw(p)
#define stb_kernel(p, v) stb_raw(p, v)
#define stw_kernel(p, v) stw_raw(p, v)
#define stl_kernel(p, v) stl_raw(p, v)
#define stq_kernel(p, v) stq_raw(p, v)
#define stfl_kernel(p, v) stfl_raw(p, v)
#define stfq_kernel(p, vt) stfq_raw(p, v)

#endif /* defined(CONFIG_USER_ONLY) */

/* page related stuff */

#define TARGET_PAGE_SIZE (1 << TARGET_PAGE_BITS)
#define TARGET_PAGE_MASK ~(TARGET_PAGE_SIZE - 1)
#define TARGET_PAGE_ALIGN(addr) (((addr) + TARGET_PAGE_SIZE - 1) & TARGET_PAGE_MASK)

/* ??? These should be the larger of unsigned long and target_ulong.  */
extern unsigned long qemu_real_host_page_size;
extern unsigned long qemu_host_page_bits;
extern unsigned long qemu_host_page_size;
extern unsigned long qemu_host_page_mask;

#define HOST_PAGE_ALIGN(addr) (((addr) + qemu_host_page_size - 1) & qemu_host_page_mask)

/* same as PROT_xxx */
#define PAGE_READ      0x0001
#define PAGE_WRITE     0x0002
#define PAGE_EXEC      0x0004
#define PAGE_BITS      (PAGE_READ | PAGE_WRITE | PAGE_EXEC)
#define PAGE_VALID     0x0008
/* original state of the write flag (used when tracking self-modifying
   code */
#define PAGE_WRITE_ORG 0x0010
#if defined(CONFIG_BSD) && defined(CONFIG_USER_ONLY)
/* FIXME: Code that sets/uses this is broken and needs to go away.  */
#define PAGE_RESERVED  0x0020
#endif

#if defined(CONFIG_USER_ONLY)
void page_dump(FILE *f);

typedef int (*walk_memory_regions_fn)(void *, abi_ulong,
                                      abi_ulong, unsigned long);
int walk_memory_regions(void *, walk_memory_regions_fn);

int page_get_flags(target_ulong address);
void page_set_flags(target_ulong start, target_ulong end, int flags);
int page_check_range(target_ulong start, target_ulong len, int flags);
#endif

CPUState *cpu_copy(CPUState *env);
CPUState *qemu_get_cpu(int cpu);

#define CPU_DUMP_CODE 0x00010000

void cpu_dump_state(CPUState *env, FILE *f, fprintf_function cpu_fprintf,
                    int flags);
void cpu_dump_statistics(CPUState *env, FILE *f, fprintf_function cpu_fprintf,
                         int flags);

void QEMU_NORETURN cpu_abort(CPUState *env, const char *fmt, ...)
    GCC_FMT_ATTR(2, 3);
extern CPUState *first_cpu;
extern CPUState *cpu_single_env;

#define CPU_INTERRUPT_HARD   0x02 /* hardware interrupt pending */
#define CPU_INTERRUPT_EXITTB 0x04 /* exit the current TB (use for x86 a20 case) */
#define CPU_INTERRUPT_TIMER  0x08 /* internal timer exception pending */
#define CPU_INTERRUPT_FIQ    0x10 /* Fast interrupt pending.  */
#define CPU_INTERRUPT_HALT   0x20 /* CPU halt wanted */
#define CPU_INTERRUPT_SMI    0x40 /* (x86 only) SMI interrupt pending */
#define CPU_INTERRUPT_DEBUG  0x80 /* Debug event occured.  */
#define CPU_INTERRUPT_VIRQ   0x100 /* virtual interrupt pending.  */
#define CPU_INTERRUPT_NMI    0x200 /* NMI pending. */
#define CPU_INTERRUPT_INIT   0x400 /* INIT pending. */
#define CPU_INTERRUPT_SIPI   0x800 /* SIPI pending. */
#define CPU_INTERRUPT_MCE    0x1000 /* (x86 only) MCE pending. */

void cpu_interrupt(CPUState *s, int mask);
void cpu_reset_interrupt(CPUState *env, int mask);

void cpu_exit(CPUState *s);

int qemu_cpu_has_work(CPUState *env);

/* Breakpoint/watchpoint flags */
#define BP_MEM_READ           0x01
#define BP_MEM_WRITE          0x02
#define BP_MEM_ACCESS         (BP_MEM_READ | BP_MEM_WRITE)
#define BP_STOP_BEFORE_ACCESS 0x04
#define BP_WATCHPOINT_HIT     0x08
#define BP_GDB                0x10
#define BP_CPU                0x20

int cpu_breakpoint_insert(CPUState *env, target_ulong pc, int flags,
                          CPUBreakpoint **breakpoint);
int cpu_breakpoint_remove(CPUState *env, target_ulong pc, int flags);
void cpu_breakpoint_remove_by_ref(CPUState *env, CPUBreakpoint *breakpoint);
void cpu_breakpoint_remove_all(CPUState *env, int mask);
int cpu_watchpoint_insert(CPUState *env, target_ulong addr, target_ulong len,
                          int flags, CPUWatchpoint **watchpoint);
int cpu_watchpoint_remove(CPUState *env, target_ulong addr,
                          target_ulong len, int flags);
void cpu_watchpoint_remove_by_ref(CPUState *env, CPUWatchpoint *watchpoint);
void cpu_watchpoint_remove_all(CPUState *env, int mask);

#define SSTEP_ENABLE  0x1  /* Enable simulated HW single stepping */
#define SSTEP_NOIRQ   0x2  /* Do not use IRQ while single stepping */
#define SSTEP_NOTIMER 0x4  /* Do not Timers while single stepping */

void cpu_single_step(CPUState *env, int enabled);
void cpu_reset(CPUState *s);
int cpu_is_stopped(CPUState *env);
void run_on_cpu(CPUState *env, void (*func)(void *data), void *data);

#define CPU_LOG_TB_OUT_ASM (1 << 0)
#define CPU_LOG_TB_IN_ASM  (1 << 1)
#define CPU_LOG_TB_OP      (1 << 2)
#define CPU_LOG_TB_OP_OPT  (1 << 3)
#define CPU_LOG_INT        (1 << 4)
#define CPU_LOG_EXEC       (1 << 5)
#define CPU_LOG_PCALL      (1 << 6)
#define CPU_LOG_IOPORT     (1 << 7)
#define CPU_LOG_TB_CPU     (1 << 8)
#define CPU_LOG_RESET      (1 << 9)

/* define log items */
typedef struct CPULogItem {
    int mask;
    const char *name;
    const char *help;
} CPULogItem;

extern const CPULogItem cpu_log_items[];

void cpu_set_log(int log_flags);
void cpu_set_log_filename(const char *filename);
int cpu_str_to_log_mask(const char *str);

#if !defined(CONFIG_USER_ONLY)

/* Return the physical page corresponding to a virtual one. Use it
   only for debugging because no protection checks are done. Return -1
   if no page found. */
target_phys_addr_t cpu_get_phys_page_debug(CPUState *env, target_ulong addr);

/* memory API */

extern int phys_ram_fd;
extern ram_addr_t ram_size;

/* RAM is pre-allocated and passed into qemu_ram_alloc_from_ptr */
#define RAM_PREALLOC_MASK   (1 << 0)

typedef struct RAMBlock {
    uint8_t *host;
    ram_addr_t offset;
    ram_addr_t length;
    uint32_t flags;
    char idstr[256];
    QLIST_ENTRY(RAMBlock) next;
#if defined(__linux__) && !defined(TARGET_S390X)
    int fd;
#endif
} RAMBlock;

typedef struct RAMList {
    uint8_t *phys_dirty;
    QLIST_HEAD(ram, RAMBlock) blocks;
} RAMList;
extern RAMList ram_list;

extern const char *mem_path;
extern int mem_prealloc;

/* physical memory access */

/* MMIO pages are identified by a combination of an IO device index and
   3 flags.  The ROMD code stores the page ram offset in iotlb entry, 
   so only a limited number of ids are avaiable.  */

#define IO_MEM_NB_ENTRIES  (1 << (TARGET_PAGE_BITS  - IO_MEM_SHIFT))

/* Flags stored in the low bits of the TLB virtual address.  These are
   defined so that fast path ram access is all zeros.  */
/* Zero if TLB entry is valid.  */
#define TLB_INVALID_MASK   (1 << 3)
/* Set if TLB entry references a clean RAM page.  The iotlb entry will
   contain the page physical address.  */
#define TLB_NOTDIRTY    (1 << 4)
/* Set if TLB entry is an IO callback.  */
#define TLB_MMIO        (1 << 5)

#define VGA_DIRTY_FLAG       0x01
#define CODE_DIRTY_FLAG      0x02
#define MIGRATION_DIRTY_FLAG 0x08

/* read dirty bit (return 0 or 1) */
static inline int cpu_physical_memory_is_dirty(ram_addr_t addr)
{
    return ram_list.phys_dirty[addr >> TARGET_PAGE_BITS] == 0xff;
}

static inline int cpu_physical_memory_get_dirty_flags(ram_addr_t addr)
{
    return ram_list.phys_dirty[addr >> TARGET_PAGE_BITS];
}

static inline int cpu_physical_memory_get_dirty(ram_addr_t addr,
                                                int dirty_flags)
{
    return ram_list.phys_dirty[addr >> TARGET_PAGE_BITS] & dirty_flags;
}

static inline void cpu_physical_memory_set_dirty(ram_addr_t addr)
{
    ram_list.phys_dirty[addr >> TARGET_PAGE_BITS] = 0xff;
}

static inline int cpu_physical_memory_set_dirty_flags(ram_addr_t addr,
                                                      int dirty_flags)
{
    return ram_list.phys_dirty[addr >> TARGET_PAGE_BITS] |= dirty_flags;
}

static inline void cpu_physical_memory_mask_dirty_range(ram_addr_t start,
                                                        int length,
                                                        int dirty_flags)
{
    int i, mask, len;
    uint8_t *p;

    len = length >> TARGET_PAGE_BITS;
    mask = ~dirty_flags;
    p = ram_list.phys_dirty + (start >> TARGET_PAGE_BITS);
    for (i = 0; i < len; i++) {
        p[i] &= mask;
    }
}

void cpu_physical_memory_reset_dirty(ram_addr_t start, ram_addr_t end,
                                     int dirty_flags);
void cpu_tlb_update_dirty(CPUState *env);

int cpu_physical_memory_set_dirty_tracking(int enable);

int cpu_physical_memory_get_dirty_tracking(void);

int cpu_physical_sync_dirty_bitmap(target_phys_addr_t start_addr,
                                   target_phys_addr_t end_addr);

int cpu_physical_log_start(target_phys_addr_t start_addr,
                           ram_addr_t size);

int cpu_physical_log_stop(target_phys_addr_t start_addr,
                          ram_addr_t size);

void dump_exec_info(FILE *f, fprintf_function cpu_fprintf);
#endif /* !CONFIG_USER_ONLY */

int cpu_memory_rw_debug(CPUState *env, target_ulong addr,
                        uint8_t *buf, int len, int is_write);

#endif /* CPU_ALL_H */
