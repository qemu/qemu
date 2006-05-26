/*
 *  Software MMU support
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
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#if DATA_SIZE == 8
#define SUFFIX q
#define USUFFIX q
#define DATA_TYPE uint64_t
#elif DATA_SIZE == 4
#define SUFFIX l
#define USUFFIX l
#define DATA_TYPE uint32_t
#elif DATA_SIZE == 2
#define SUFFIX w
#define USUFFIX uw
#define DATA_TYPE uint16_t
#define DATA_STYPE int16_t
#elif DATA_SIZE == 1
#define SUFFIX b
#define USUFFIX ub
#define DATA_TYPE uint8_t
#define DATA_STYPE int8_t
#else
#error unsupported data size
#endif

#if ACCESS_TYPE == 0

#define CPU_MEM_INDEX 0
#define MMUSUFFIX _mmu

#elif ACCESS_TYPE == 1

#define CPU_MEM_INDEX 1
#define MMUSUFFIX _mmu

#elif ACCESS_TYPE == 2

#ifdef TARGET_I386
#define CPU_MEM_INDEX ((env->hflags & HF_CPL_MASK) == 3)
#elif defined (TARGET_PPC)
#define CPU_MEM_INDEX (msr_pr)
#elif defined (TARGET_MIPS)
#define CPU_MEM_INDEX ((env->hflags & MIPS_HFLAG_MODE) == MIPS_HFLAG_UM)
#elif defined (TARGET_SPARC)
#define CPU_MEM_INDEX ((env->psrs) == 0)
#elif defined (TARGET_ARM)
#define CPU_MEM_INDEX ((env->uncached_cpsr & CPSR_M) == ARM_CPU_MODE_USR)
#elif defined (TARGET_SH4)
#define CPU_MEM_INDEX ((env->sr & SR_MD) == 0)
#else
#error unsupported CPU
#endif
#define MMUSUFFIX _mmu

#elif ACCESS_TYPE == 3

#ifdef TARGET_I386
#define CPU_MEM_INDEX ((env->hflags & HF_CPL_MASK) == 3)
#elif defined (TARGET_PPC)
#define CPU_MEM_INDEX (msr_pr)
#elif defined (TARGET_MIPS)
#define CPU_MEM_INDEX ((env->hflags & MIPS_HFLAG_MODE) == MIPS_HFLAG_UM)
#elif defined (TARGET_SPARC)
#define CPU_MEM_INDEX ((env->psrs) == 0)
#elif defined (TARGET_ARM)
#define CPU_MEM_INDEX ((env->uncached_cpsr & CPSR_M) == ARM_CPU_MODE_USR)
#elif defined (TARGET_SH4)
#define CPU_MEM_INDEX ((env->sr & SR_MD) == 0)
#else
#error unsupported CPU
#endif
#define MMUSUFFIX _cmmu

#else
#error invalid ACCESS_TYPE
#endif

#if DATA_SIZE == 8
#define RES_TYPE uint64_t
#else
#define RES_TYPE int
#endif

#if ACCESS_TYPE == 3
#define ADDR_READ addr_code
#else
#define ADDR_READ addr_read
#endif

DATA_TYPE REGPARM(1) glue(glue(__ld, SUFFIX), MMUSUFFIX)(target_ulong addr,
                                                         int is_user);
void REGPARM(2) glue(glue(__st, SUFFIX), MMUSUFFIX)(target_ulong addr, DATA_TYPE v, int is_user);

#if (DATA_SIZE <= 4) && (TARGET_LONG_BITS == 32) && defined(__i386__) && \
    (ACCESS_TYPE <= 1) && defined(ASM_SOFTMMU)

#define CPU_TLB_ENTRY_BITS 4

static inline RES_TYPE glue(glue(ld, USUFFIX), MEMSUFFIX)(target_ulong ptr)
{
    int res;

    asm volatile ("movl %1, %%edx\n"
                  "movl %1, %%eax\n"
                  "shrl %3, %%edx\n"
                  "andl %4, %%eax\n"
                  "andl %2, %%edx\n"
                  "leal %5(%%edx, %%ebp), %%edx\n"
                  "cmpl (%%edx), %%eax\n"
                  "movl %1, %%eax\n"
                  "je 1f\n"
                  "pushl %6\n"
                  "call %7\n"
                  "popl %%edx\n"
                  "movl %%eax, %0\n"
                  "jmp 2f\n"
                  "1:\n"
                  "addl 12(%%edx), %%eax\n"
#if DATA_SIZE == 1
                  "movzbl (%%eax), %0\n"
#elif DATA_SIZE == 2
                  "movzwl (%%eax), %0\n"
#elif DATA_SIZE == 4
                  "movl (%%eax), %0\n"
#else
#error unsupported size
#endif
                  "2:\n"
                  : "=r" (res)
                  : "r" (ptr), 
                  "i" ((CPU_TLB_SIZE - 1) << CPU_TLB_ENTRY_BITS), 
                  "i" (TARGET_PAGE_BITS - CPU_TLB_ENTRY_BITS), 
                  "i" (TARGET_PAGE_MASK | (DATA_SIZE - 1)),
                  "m" (*(uint32_t *)offsetof(CPUState, tlb_table[CPU_MEM_INDEX][0].addr_read)),
                  "i" (CPU_MEM_INDEX),
                  "m" (*(uint8_t *)&glue(glue(__ld, SUFFIX), MMUSUFFIX))
                  : "%eax", "%ecx", "%edx", "memory", "cc");
    return res;
}

#if DATA_SIZE <= 2
static inline int glue(glue(lds, SUFFIX), MEMSUFFIX)(target_ulong ptr)
{
    int res;

    asm volatile ("movl %1, %%edx\n"
                  "movl %1, %%eax\n"
                  "shrl %3, %%edx\n"
                  "andl %4, %%eax\n"
                  "andl %2, %%edx\n"
                  "leal %5(%%edx, %%ebp), %%edx\n"
                  "cmpl (%%edx), %%eax\n"
                  "movl %1, %%eax\n"
                  "je 1f\n"
                  "pushl %6\n"
                  "call %7\n"
                  "popl %%edx\n"
#if DATA_SIZE == 1
                  "movsbl %%al, %0\n"
#elif DATA_SIZE == 2
                  "movswl %%ax, %0\n"
#else
#error unsupported size
#endif
                  "jmp 2f\n"
                  "1:\n"
                  "addl 12(%%edx), %%eax\n"
#if DATA_SIZE == 1
                  "movsbl (%%eax), %0\n"
#elif DATA_SIZE == 2
                  "movswl (%%eax), %0\n"
#else
#error unsupported size
#endif
                  "2:\n"
                  : "=r" (res)
                  : "r" (ptr), 
                  "i" ((CPU_TLB_SIZE - 1) << CPU_TLB_ENTRY_BITS), 
                  "i" (TARGET_PAGE_BITS - CPU_TLB_ENTRY_BITS), 
                  "i" (TARGET_PAGE_MASK | (DATA_SIZE - 1)),
                  "m" (*(uint32_t *)offsetof(CPUState, tlb_table[CPU_MEM_INDEX][0].addr_read)),
                  "i" (CPU_MEM_INDEX),
                  "m" (*(uint8_t *)&glue(glue(__ld, SUFFIX), MMUSUFFIX))
                  : "%eax", "%ecx", "%edx", "memory", "cc");
    return res;
}
#endif

static inline void glue(glue(st, SUFFIX), MEMSUFFIX)(target_ulong ptr, RES_TYPE v)
{
    asm volatile ("movl %0, %%edx\n"
                  "movl %0, %%eax\n"
                  "shrl %3, %%edx\n"
                  "andl %4, %%eax\n"
                  "andl %2, %%edx\n"
                  "leal %5(%%edx, %%ebp), %%edx\n"
                  "cmpl (%%edx), %%eax\n"
                  "movl %0, %%eax\n"
                  "je 1f\n"
#if DATA_SIZE == 1
                  "movzbl %b1, %%edx\n"
#elif DATA_SIZE == 2
                  "movzwl %w1, %%edx\n"
#elif DATA_SIZE == 4
                  "movl %1, %%edx\n"
#else
#error unsupported size
#endif
                  "pushl %6\n"
                  "call %7\n"
                  "popl %%eax\n"
                  "jmp 2f\n"
                  "1:\n"
                  "addl 8(%%edx), %%eax\n"
#if DATA_SIZE == 1
                  "movb %b1, (%%eax)\n"
#elif DATA_SIZE == 2
                  "movw %w1, (%%eax)\n"
#elif DATA_SIZE == 4
                  "movl %1, (%%eax)\n"
#else
#error unsupported size
#endif
                  "2:\n"
                  : 
                  : "r" (ptr), 
/* NOTE: 'q' would be needed as constraint, but we could not use it
   with T1 ! */
                  "r" (v), 
                  "i" ((CPU_TLB_SIZE - 1) << CPU_TLB_ENTRY_BITS), 
                  "i" (TARGET_PAGE_BITS - CPU_TLB_ENTRY_BITS), 
                  "i" (TARGET_PAGE_MASK | (DATA_SIZE - 1)),
                  "m" (*(uint32_t *)offsetof(CPUState, tlb_table[CPU_MEM_INDEX][0].addr_write)),
                  "i" (CPU_MEM_INDEX),
                  "m" (*(uint8_t *)&glue(glue(__st, SUFFIX), MMUSUFFIX))
                  : "%eax", "%ecx", "%edx", "memory", "cc");
}

#else

/* generic load/store macros */

static inline RES_TYPE glue(glue(ld, USUFFIX), MEMSUFFIX)(target_ulong ptr)
{
    int index;
    RES_TYPE res;
    target_ulong addr;
    unsigned long physaddr;
    int is_user;

    addr = ptr;
    index = (addr >> TARGET_PAGE_BITS) & (CPU_TLB_SIZE - 1);
    is_user = CPU_MEM_INDEX;
    if (__builtin_expect(env->tlb_table[is_user][index].ADDR_READ != 
                         (addr & (TARGET_PAGE_MASK | (DATA_SIZE - 1))), 0)) {
        res = glue(glue(__ld, SUFFIX), MMUSUFFIX)(addr, is_user);
    } else {
        physaddr = addr + env->tlb_table[is_user][index].addend;
        res = glue(glue(ld, USUFFIX), _raw)((uint8_t *)physaddr);
    }
    return res;
}

#if DATA_SIZE <= 2
static inline int glue(glue(lds, SUFFIX), MEMSUFFIX)(target_ulong ptr)
{
    int res, index;
    target_ulong addr;
    unsigned long physaddr;
    int is_user;

    addr = ptr;
    index = (addr >> TARGET_PAGE_BITS) & (CPU_TLB_SIZE - 1);
    is_user = CPU_MEM_INDEX;
    if (__builtin_expect(env->tlb_table[is_user][index].ADDR_READ != 
                         (addr & (TARGET_PAGE_MASK | (DATA_SIZE - 1))), 0)) {
        res = (DATA_STYPE)glue(glue(__ld, SUFFIX), MMUSUFFIX)(addr, is_user);
    } else {
        physaddr = addr + env->tlb_table[is_user][index].addend;
        res = glue(glue(lds, SUFFIX), _raw)((uint8_t *)physaddr);
    }
    return res;
}
#endif

#if ACCESS_TYPE != 3

/* generic store macro */

static inline void glue(glue(st, SUFFIX), MEMSUFFIX)(target_ulong ptr, RES_TYPE v)
{
    int index;
    target_ulong addr;
    unsigned long physaddr;
    int is_user;

    addr = ptr;
    index = (addr >> TARGET_PAGE_BITS) & (CPU_TLB_SIZE - 1);
    is_user = CPU_MEM_INDEX;
    if (__builtin_expect(env->tlb_table[is_user][index].addr_write != 
                         (addr & (TARGET_PAGE_MASK | (DATA_SIZE - 1))), 0)) {
        glue(glue(__st, SUFFIX), MMUSUFFIX)(addr, v, is_user);
    } else {
        physaddr = addr + env->tlb_table[is_user][index].addend;
        glue(glue(st, SUFFIX), _raw)((uint8_t *)physaddr, v);
    }
}

#endif /* ACCESS_TYPE != 3 */

#endif /* !asm */

#if ACCESS_TYPE != 3

#if DATA_SIZE == 8
static inline float64 glue(ldfq, MEMSUFFIX)(target_ulong ptr)
{
    union {
        float64 d;
        uint64_t i;
    } u;
    u.i = glue(ldq, MEMSUFFIX)(ptr);
    return u.d;
}

static inline void glue(stfq, MEMSUFFIX)(target_ulong ptr, float64 v)
{
    union {
        float64 d;
        uint64_t i;
    } u;
    u.d = v;
    glue(stq, MEMSUFFIX)(ptr, u.i);
}
#endif /* DATA_SIZE == 8 */

#if DATA_SIZE == 4
static inline float32 glue(ldfl, MEMSUFFIX)(target_ulong ptr)
{
    union {
        float32 f;
        uint32_t i;
    } u;
    u.i = glue(ldl, MEMSUFFIX)(ptr);
    return u.f;
}

static inline void glue(stfl, MEMSUFFIX)(target_ulong ptr, float32 v)
{
    union {
        float32 f;
        uint32_t i;
    } u;
    u.f = v;
    glue(stl, MEMSUFFIX)(ptr, u.i);
}
#endif /* DATA_SIZE == 4 */

#endif /* ACCESS_TYPE != 3 */

#undef RES_TYPE
#undef DATA_TYPE
#undef DATA_STYPE
#undef SUFFIX
#undef USUFFIX
#undef DATA_SIZE
#undef CPU_MEM_INDEX
#undef MMUSUFFIX
#undef ADDR_READ
