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
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#ifndef CPU_ALL_H
#define CPU_ALL_H

/* CPU memory access without any memory or io remapping */

static inline int ldub_raw(void *ptr)
{
    return *(uint8_t *)ptr;
}

static inline int ldsb_raw(void *ptr)
{
    return *(int8_t *)ptr;
}

static inline void stb_raw(void *ptr, int v)
{
    *(uint8_t *)ptr = v;
}

/* NOTE: on arm, putting 2 in /proc/sys/debug/alignment so that the
   kernel handles unaligned load/stores may give better results, but
   it is a system wide setting : bad */
#if defined(WORDS_BIGENDIAN) || defined(__arm__)

/* conservative code for little endian unaligned accesses */
static inline int lduw_raw(void *ptr)
{
#ifdef __powerpc__
    int val;
    __asm__ __volatile__ ("lhbrx %0,0,%1" : "=r" (val) : "r" (ptr));
    return val;
#else
    uint8_t *p = ptr;
    return p[0] | (p[1] << 8);
#endif
}

static inline int ldsw_raw(void *ptr)
{
#ifdef __powerpc__
    int val;
    __asm__ __volatile__ ("lhbrx %0,0,%1" : "=r" (val) : "r" (ptr));
    return (int16_t)val;
#else
    uint8_t *p = ptr;
    return (int16_t)(p[0] | (p[1] << 8));
#endif
}

static inline int ldl_raw(void *ptr)
{
#ifdef __powerpc__
    int val;
    __asm__ __volatile__ ("lwbrx %0,0,%1" : "=r" (val) : "r" (ptr));
    return val;
#else
    uint8_t *p = ptr;
    return p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24);
#endif
}

static inline uint64_t ldq_raw(void *ptr)
{
    uint8_t *p = ptr;
    uint32_t v1, v2;
    v1 = ldl_raw(p);
    v2 = ldl_raw(p + 4);
    return v1 | ((uint64_t)v2 << 32);
}

static inline void stw_raw(void *ptr, int v)
{
#ifdef __powerpc__
    __asm__ __volatile__ ("sthbrx %1,0,%2" : "=m" (*(uint16_t *)ptr) : "r" (v), "r" (ptr));
#else
    uint8_t *p = ptr;
    p[0] = v;
    p[1] = v >> 8;
#endif
}

static inline void stl_raw(void *ptr, int v)
{
#ifdef __powerpc__
    __asm__ __volatile__ ("stwbrx %1,0,%2" : "=m" (*(uint32_t *)ptr) : "r" (v), "r" (ptr));
#else
    uint8_t *p = ptr;
    p[0] = v;
    p[1] = v >> 8;
    p[2] = v >> 16;
    p[3] = v >> 24;
#endif
}

static inline void stq_raw(void *ptr, uint64_t v)
{
    uint8_t *p = ptr;
    stl_raw(p, (uint32_t)v);
    stl_raw(p + 4, v >> 32);
}

/* float access */

static inline float ldfl_raw(void *ptr)
{
    union {
        float f;
        uint32_t i;
    } u;
    u.i = ldl_raw(ptr);
    return u.f;
}

static inline void stfl_raw(void *ptr, float v)
{
    union {
        float f;
        uint32_t i;
    } u;
    u.f = v;
    stl_raw(ptr, u.i);
}


#if defined(__arm__) && !defined(WORDS_BIGENDIAN)

/* NOTE: arm is horrible as double 32 bit words are stored in big endian ! */
static inline double ldfq_raw(void *ptr)
{
    union {
        double d;
        uint32_t tab[2];
    } u;
    u.tab[1] = ldl_raw(ptr);
    u.tab[0] = ldl_raw(ptr + 4);
    return u.d;
}

static inline void stfq_raw(void *ptr, double v)
{
    union {
        double d;
        uint32_t tab[2];
    } u;
    u.d = v;
    stl_raw(ptr, u.tab[1]);
    stl_raw(ptr + 4, u.tab[0]);
}

#else
static inline double ldfq_raw(void *ptr)
{
    union {
        double d;
        uint64_t i;
    } u;
    u.i = ldq_raw(ptr);
    return u.d;
}

static inline void stfq_raw(void *ptr, double v)
{
    union {
        double d;
        uint64_t i;
    } u;
    u.d = v;
    stq_raw(ptr, u.i);
}
#endif

#elif defined(TARGET_WORDS_BIGENDIAN) && !defined(WORDS_BIGENDIAN)

static inline int lduw_raw(void *ptr)
{
    uint8_t *b = (uint8_t *) ptr;
    return (b[0]<<8|b[1]);
}

static inline int ldsw_raw(void *ptr)
{
    int8_t *b = (int8_t *) ptr;
    return (b[0]<<8|b[1]);
}

static inline int ldl_raw(void *ptr)
{
    uint8_t *b = (uint8_t *) ptr;
    return (b[0]<<24|b[1]<<16|b[2]<<8|b[3]);
}

static inline uint64_t ldq_raw(void *ptr)
{
    uint32_t a,b;
    a = ldl_raw(ptr);
    b = ldl_raw(ptr+4);
    return (((uint64_t)a<<32)|b);
}

static inline void stw_raw(void *ptr, int v)
{
    uint8_t *d = (uint8_t *) ptr;
    d[0] = v >> 8;
    d[1] = v;
}

static inline void stl_raw(void *ptr, int v)
{
    uint8_t *d = (uint8_t *) ptr;
    d[0] = v >> 24;
    d[1] = v >> 16;
    d[2] = v >> 8;
    d[3] = v;
}

static inline void stq_raw(void *ptr, uint64_t v)
{
    stl_raw(ptr, v);
    stl_raw(ptr+4, v >> 32);
}

#else

static inline int lduw_raw(void *ptr)
{
    return *(uint16_t *)ptr;
}

static inline int ldsw_raw(void *ptr)
{
    return *(int16_t *)ptr;
}

static inline int ldl_raw(void *ptr)
{
    return *(uint32_t *)ptr;
}

static inline uint64_t ldq_raw(void *ptr)
{
    return *(uint64_t *)ptr;
}

static inline void stw_raw(void *ptr, int v)
{
    *(uint16_t *)ptr = v;
}

static inline void stl_raw(void *ptr, int v)
{
    *(uint32_t *)ptr = v;
}

static inline void stq_raw(void *ptr, uint64_t v)
{
    *(uint64_t *)ptr = v;
}

/* float access */

static inline float ldfl_raw(void *ptr)
{
    return *(float *)ptr;
}

static inline double ldfq_raw(void *ptr)
{
    return *(double *)ptr;
}

static inline void stfl_raw(void *ptr, float v)
{
    *(float *)ptr = v;
}

static inline void stfq_raw(void *ptr, double v)
{
    *(double *)ptr = v;
}
#endif

/* MMU memory access macros */

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

#define ldub_kernel(p) ldub_raw(p)
#define ldsb_kernel(p) ldsb_raw(p)
#define lduw_kernel(p) lduw_raw(p)
#define ldsw_kernel(p) ldsw_raw(p)
#define ldl_kernel(p) ldl_raw(p)
#define stb_kernel(p, v) stb_raw(p, v)
#define stw_kernel(p, v) stw_raw(p, v)
#define stl_kernel(p, v) stl_raw(p, v)
#define stq_kernel(p, v) stq_raw(p, v)

#endif /* defined(CONFIG_USER_ONLY) */

/* page related stuff */

#define TARGET_PAGE_SIZE (1 << TARGET_PAGE_BITS)
#define TARGET_PAGE_MASK ~(TARGET_PAGE_SIZE - 1)
#define TARGET_PAGE_ALIGN(addr) (((addr) + TARGET_PAGE_SIZE - 1) & TARGET_PAGE_MASK)

extern unsigned long real_host_page_size;
extern unsigned long host_page_bits;
extern unsigned long host_page_size;
extern unsigned long host_page_mask;

#define HOST_PAGE_ALIGN(addr) (((addr) + host_page_size - 1) & host_page_mask)

/* same as PROT_xxx */
#define PAGE_READ      0x0001
#define PAGE_WRITE     0x0002
#define PAGE_EXEC      0x0004
#define PAGE_BITS      (PAGE_READ | PAGE_WRITE | PAGE_EXEC)
#define PAGE_VALID     0x0008
/* original state of the write flag (used when tracking self-modifying
   code */
#define PAGE_WRITE_ORG 0x0010 

void page_dump(FILE *f);
int page_get_flags(unsigned long address);
void page_set_flags(unsigned long start, unsigned long end, int flags);
void page_unprotect_range(uint8_t *data, unsigned long data_size);

#define SINGLE_CPU_DEFINES
#ifdef SINGLE_CPU_DEFINES

#if defined(TARGET_I386)

#define CPUState CPUX86State
#define cpu_init cpu_x86_init
#define cpu_exec cpu_x86_exec
#define cpu_gen_code cpu_x86_gen_code
#define cpu_interrupt cpu_x86_interrupt
#define cpu_signal_handler cpu_x86_signal_handler

#elif defined(TARGET_ARM)

#define CPUState CPUARMState
#define cpu_init cpu_arm_init
#define cpu_exec cpu_arm_exec
#define cpu_gen_code cpu_arm_gen_code
#define cpu_interrupt cpu_arm_interrupt
#define cpu_signal_handler cpu_arm_signal_handler

#elif defined(TARGET_SPARC)

#define CPUState CPUSPARCState
#define cpu_init cpu_sparc_init
#define cpu_exec cpu_sparc_exec
#define cpu_gen_code cpu_sparc_gen_code
#define cpu_interrupt cpu_sparc_interrupt
#define cpu_signal_handler cpu_sparc_signal_handler

#elif defined(TARGET_PPC)

#define CPUState CPUPPCState
#define cpu_init cpu_ppc_init
#define cpu_exec cpu_ppc_exec
#define cpu_gen_code cpu_ppc_gen_code
#define cpu_interrupt cpu_ppc_interrupt
#define cpu_signal_handler cpu_ppc_signal_handler

#else

#error unsupported target CPU

#endif

#endif /* SINGLE_CPU_DEFINES */

#define DEFAULT_GDBSTUB_PORT 1234

void cpu_abort(CPUState *env, const char *fmt, ...);
extern CPUState *cpu_single_env;

#define CPU_INTERRUPT_EXIT 0x01 /* wants exit from main loop */
#define CPU_INTERRUPT_HARD 0x02 /* hardware interrupt pending */
void cpu_interrupt(CPUState *s, int mask);

int cpu_breakpoint_insert(CPUState *env, uint32_t pc);
int cpu_breakpoint_remove(CPUState *env, uint32_t pc);
void cpu_single_step(CPUState *env, int enabled);

#define CPU_LOG_ALL 1
void cpu_set_log(int log_flags);
void cpu_set_log_filename(const char *filename);

/* memory API */

typedef void CPUWriteMemoryFunc(uint32_t addr, uint32_t value);
typedef uint32_t CPUReadMemoryFunc(uint32_t addr);

void cpu_register_physical_memory(unsigned long start_addr, unsigned long size,
                                  long phys_offset);
int cpu_register_io_memory(int io_index,
                           CPUReadMemoryFunc **mem_read,
                           CPUWriteMemoryFunc **mem_write);

/* gdb stub API */
extern int gdbstub_fd;
CPUState *cpu_gdbstub_get_env(void *opaque);
int cpu_gdbstub(void *opaque, int (*main_loop)(void *opaque), int port);

#endif /* CPU_ALL_H */
