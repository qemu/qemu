/*
 * internal execution defines for qemu
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

/* allow to see translation results - the slowdown should be negligible, so we leave it */
#define DEBUG_DISAS

#ifndef glue
#define xglue(x, y) x ## y
#define glue(x, y) xglue(x, y)
#define stringify(s)	tostring(s)
#define tostring(s)	#s
#endif

#if GCC_MAJOR < 3
#define __builtin_expect(x, n) (x)
#endif

#ifdef __i386__
#define REGPARM(n) __attribute((regparm(n)))
#else
#define REGPARM(n)
#endif

/* is_jmp field values */
#define DISAS_NEXT    0 /* next instruction can be analyzed */
#define DISAS_JUMP    1 /* only pc was modified dynamically */
#define DISAS_UPDATE  2 /* cpu state was modified dynamically */
#define DISAS_TB_JUMP 3 /* only pc was modified statically */

struct TranslationBlock;

/* XXX: make safe guess about sizes */
#define MAX_OP_PER_INSTR 32
#define OPC_BUF_SIZE 512
#define OPC_MAX_SIZE (OPC_BUF_SIZE - MAX_OP_PER_INSTR)

#define OPPARAM_BUF_SIZE (OPC_BUF_SIZE * 3)

extern uint16_t gen_opc_buf[OPC_BUF_SIZE];
extern uint32_t gen_opparam_buf[OPPARAM_BUF_SIZE];
extern uint32_t gen_opc_pc[OPC_BUF_SIZE];
extern uint8_t gen_opc_cc_op[OPC_BUF_SIZE];
extern uint8_t gen_opc_instr_start[OPC_BUF_SIZE];

typedef void (GenOpFunc)(void);
typedef void (GenOpFunc1)(long);
typedef void (GenOpFunc2)(long, long);
typedef void (GenOpFunc3)(long, long, long);
                    
#if defined(TARGET_I386)

void optimize_flags_init(void);

#endif

extern FILE *logfile;
extern int loglevel;

int gen_intermediate_code(CPUState *env, struct TranslationBlock *tb);
int gen_intermediate_code_pc(CPUState *env, struct TranslationBlock *tb);
void dump_ops(const uint16_t *opc_buf, const uint32_t *opparam_buf);
int cpu_gen_code(CPUState *env, struct TranslationBlock *tb,
                 int max_code_size, int *gen_code_size_ptr);
int cpu_restore_state(struct TranslationBlock *tb, 
                      CPUState *env, unsigned long searched_pc,
                      void *puc);
int cpu_gen_code_copy(CPUState *env, struct TranslationBlock *tb,
                      int max_code_size, int *gen_code_size_ptr);
int cpu_restore_state_copy(struct TranslationBlock *tb, 
                           CPUState *env, unsigned long searched_pc,
                           void *puc);
void cpu_exec_init(void);
int page_unprotect(unsigned long address);
void tb_invalidate_page_range(target_ulong start, target_ulong end);
void tlb_flush_page(CPUState *env, uint32_t addr);
void tlb_flush_page_write(CPUState *env, uint32_t addr);
void tlb_flush(CPUState *env, int flush_global);
int tlb_set_page(CPUState *env, uint32_t vaddr, uint32_t paddr, int prot, 
                 int is_user, int is_softmmu);

#define CODE_GEN_MAX_SIZE        65536
#define CODE_GEN_ALIGN           16 /* must be >= of the size of a icache line */

#define CODE_GEN_HASH_BITS     15
#define CODE_GEN_HASH_SIZE     (1 << CODE_GEN_HASH_BITS)

#define CODE_GEN_PHYS_HASH_BITS     15
#define CODE_GEN_PHYS_HASH_SIZE     (1 << CODE_GEN_PHYS_HASH_BITS)

/* maximum total translate dcode allocated */

/* NOTE: the translated code area cannot be too big because on some
   archs the range of "fast" function calls is limited. Here is a
   summary of the ranges:

   i386  : signed 32 bits
   arm   : signed 26 bits
   ppc   : signed 24 bits
   sparc : signed 32 bits
   alpha : signed 23 bits
*/

#if defined(__alpha__)
#define CODE_GEN_BUFFER_SIZE     (2 * 1024 * 1024)
#elif defined(__powerpc__)
#define CODE_GEN_BUFFER_SIZE     (6 * 1024 * 1024)
#else
#define CODE_GEN_BUFFER_SIZE     (8 * 1024 * 1024)
#endif

//#define CODE_GEN_BUFFER_SIZE     (128 * 1024)

/* estimated block size for TB allocation */
/* XXX: use a per code average code fragment size and modulate it
   according to the host CPU */
#if defined(CONFIG_SOFTMMU)
#define CODE_GEN_AVG_BLOCK_SIZE 128
#else
#define CODE_GEN_AVG_BLOCK_SIZE 64
#endif

#define CODE_GEN_MAX_BLOCKS    (CODE_GEN_BUFFER_SIZE / CODE_GEN_AVG_BLOCK_SIZE)

#if defined(__powerpc__) 
#define USE_DIRECT_JUMP
#endif
#if defined(__i386__) 
#define USE_DIRECT_JUMP
#endif

typedef struct TranslationBlock {
    unsigned long pc;   /* simulated PC corresponding to this block (EIP + CS base) */
    unsigned long cs_base; /* CS base for this block */
    unsigned int flags; /* flags defining in which context the code was generated */
    uint16_t size;      /* size of target code for this block (1 <=
                           size <= TARGET_PAGE_SIZE) */
    uint16_t cflags;    /* compile flags */
#define CF_CODE_COPY  0x0001 /* block was generated in code copy mode */

    uint8_t *tc_ptr;    /* pointer to the translated code */
    struct TranslationBlock *hash_next; /* next matching tb for virtual address */
    /* next matching tb for physical address. */
    struct TranslationBlock *phys_hash_next; 
    /* first and second physical page containing code. The lower bit
       of the pointer tells the index in page_next[] */
    struct TranslationBlock *page_next[2]; 
    target_ulong page_addr[2]; 

    /* the following data are used to directly call another TB from
       the code of this one. */
    uint16_t tb_next_offset[2]; /* offset of original jump target */
#ifdef USE_DIRECT_JUMP
    uint16_t tb_jmp_offset[4]; /* offset of jump instruction */
#else
    uint32_t tb_next[2]; /* address of jump generated code */
#endif
    /* list of TBs jumping to this one. This is a circular list using
       the two least significant bits of the pointers to tell what is
       the next pointer: 0 = jmp_next[0], 1 = jmp_next[1], 2 =
       jmp_first */
    struct TranslationBlock *jmp_next[2]; 
    struct TranslationBlock *jmp_first;
} TranslationBlock;

static inline unsigned int tb_hash_func(unsigned long pc)
{
    return pc & (CODE_GEN_HASH_SIZE - 1);
}

static inline unsigned int tb_phys_hash_func(unsigned long pc)
{
    return pc & (CODE_GEN_PHYS_HASH_SIZE - 1);
}

TranslationBlock *tb_alloc(unsigned long pc);
void tb_flush(CPUState *env);
void tb_link(TranslationBlock *tb);
void tb_link_phys(TranslationBlock *tb, 
                  target_ulong phys_pc, target_ulong phys_page2);

extern TranslationBlock *tb_hash[CODE_GEN_HASH_SIZE];
extern TranslationBlock *tb_phys_hash[CODE_GEN_PHYS_HASH_SIZE];

extern uint8_t code_gen_buffer[CODE_GEN_BUFFER_SIZE];
extern uint8_t *code_gen_ptr;

/* find a translation block in the translation cache. If not found,
   return NULL and the pointer to the last element of the list in pptb */
static inline TranslationBlock *tb_find(TranslationBlock ***pptb,
                                        unsigned long pc, 
                                        unsigned long cs_base,
                                        unsigned int flags)
{
    TranslationBlock **ptb, *tb;
    unsigned int h;
 
    h = tb_hash_func(pc);
    ptb = &tb_hash[h];
    for(;;) {
        tb = *ptb;
        if (!tb)
            break;
        if (tb->pc == pc && tb->cs_base == cs_base && tb->flags == flags)
            return tb;
        ptb = &tb->hash_next;
    }
    *pptb = ptb;
    return NULL;
}


#if defined(USE_DIRECT_JUMP)

#if defined(__powerpc__)
static inline void tb_set_jmp_target1(unsigned long jmp_addr, unsigned long addr)
{
    uint32_t val, *ptr;

    /* patch the branch destination */
    ptr = (uint32_t *)jmp_addr;
    val = *ptr;
    val = (val & ~0x03fffffc) | ((addr - jmp_addr) & 0x03fffffc);
    *ptr = val;
    /* flush icache */
    asm volatile ("dcbst 0,%0" : : "r"(ptr) : "memory");
    asm volatile ("sync" : : : "memory");
    asm volatile ("icbi 0,%0" : : "r"(ptr) : "memory");
    asm volatile ("sync" : : : "memory");
    asm volatile ("isync" : : : "memory");
}
#elif defined(__i386__)
static inline void tb_set_jmp_target1(unsigned long jmp_addr, unsigned long addr)
{
    /* patch the branch destination */
    *(uint32_t *)jmp_addr = addr - (jmp_addr + 4);
    /* no need to flush icache explicitely */
}
#endif

static inline void tb_set_jmp_target(TranslationBlock *tb, 
                                     int n, unsigned long addr)
{
    unsigned long offset;

    offset = tb->tb_jmp_offset[n];
    tb_set_jmp_target1((unsigned long)(tb->tc_ptr + offset), addr);
    offset = tb->tb_jmp_offset[n + 2];
    if (offset != 0xffff)
        tb_set_jmp_target1((unsigned long)(tb->tc_ptr + offset), addr);
}

#else

/* set the jump target */
static inline void tb_set_jmp_target(TranslationBlock *tb, 
                                     int n, unsigned long addr)
{
    tb->tb_next[n] = addr;
}

#endif

static inline void tb_add_jump(TranslationBlock *tb, int n, 
                               TranslationBlock *tb_next)
{
    /* NOTE: this test is only needed for thread safety */
    if (!tb->jmp_next[n]) {
        /* patch the native jump address */
        tb_set_jmp_target(tb, n, (unsigned long)tb_next->tc_ptr);
        
        /* add in TB jmp circular list */
        tb->jmp_next[n] = tb_next->jmp_first;
        tb_next->jmp_first = (TranslationBlock *)((long)(tb) | (n));
    }
}

TranslationBlock *tb_find_pc(unsigned long pc_ptr);

#ifndef offsetof
#define offsetof(type, field) ((size_t) &((type *)0)->field)
#endif

#if defined(__powerpc__)

/* we patch the jump instruction directly */
#define JUMP_TB(opname, tbparam, n, eip)\
do {\
    asm volatile (".section \".data\"\n"\
		  "__op_label" #n "." stringify(opname) ":\n"\
		  ".long 1f\n"\
		  ".previous\n"\
                  "b __op_jmp" #n "\n"\
		  "1:\n");\
    T0 = (long)(tbparam) + (n);\
    EIP = eip;\
    EXIT_TB();\
} while (0)

#define JUMP_TB2(opname, tbparam, n)\
do {\
    asm volatile ("b __op_jmp" #n "\n");\
} while (0)

#elif defined(__i386__) && defined(USE_DIRECT_JUMP)

/* we patch the jump instruction directly */
#define JUMP_TB(opname, tbparam, n, eip)\
do {\
    asm volatile (".section \".data\"\n"\
		  "__op_label" #n "." stringify(opname) ":\n"\
		  ".long 1f\n"\
		  ".previous\n"\
                  "jmp __op_jmp" #n "\n"\
		  "1:\n");\
    T0 = (long)(tbparam) + (n);\
    EIP = eip;\
    EXIT_TB();\
} while (0)

#define JUMP_TB2(opname, tbparam, n)\
do {\
    asm volatile ("jmp __op_jmp" #n "\n");\
} while (0)

#else

/* jump to next block operations (more portable code, does not need
   cache flushing, but slower because of indirect jump) */
#define JUMP_TB(opname, tbparam, n, eip)\
do {\
    static void __attribute__((unused)) *__op_label ## n = &&label ## n;\
    static void __attribute__((unused)) *dummy ## n = &&dummy_label ## n;\
    goto *(void *)(((TranslationBlock *)tbparam)->tb_next[n]);\
label ## n:\
    T0 = (long)(tbparam) + (n);\
    EIP = eip;\
dummy_label ## n:\
    EXIT_TB();\
} while (0)

/* second jump to same destination 'n' */
#define JUMP_TB2(opname, tbparam, n)\
do {\
    goto *(void *)(((TranslationBlock *)tbparam)->tb_next[n - 2]);\
} while (0)

#endif

extern CPUWriteMemoryFunc *io_mem_write[IO_MEM_NB_ENTRIES][4];
extern CPUReadMemoryFunc *io_mem_read[IO_MEM_NB_ENTRIES][4];

#ifdef __powerpc__
static inline int testandset (int *p)
{
    int ret;
    __asm__ __volatile__ (
                          "0:    lwarx %0,0,%1 ;"
                          "      xor. %0,%3,%0;"
                          "      bne 1f;"
                          "      stwcx. %2,0,%1;"
                          "      bne- 0b;"
                          "1:    "
                          : "=&r" (ret)
                          : "r" (p), "r" (1), "r" (0)
                          : "cr0", "memory");
    return ret;
}
#endif

#ifdef __i386__
static inline int testandset (int *p)
{
    char ret;
    long int readval;
    
    __asm__ __volatile__ ("lock; cmpxchgl %3, %1; sete %0"
                          : "=q" (ret), "=m" (*p), "=a" (readval)
                          : "r" (1), "m" (*p), "a" (0)
                          : "memory");
    return ret;
}
#endif

#ifdef __s390__
static inline int testandset (int *p)
{
    int ret;

    __asm__ __volatile__ ("0: cs    %0,%1,0(%2)\n"
			  "   jl    0b"
			  : "=&d" (ret)
			  : "r" (1), "a" (p), "0" (*p) 
			  : "cc", "memory" );
    return ret;
}
#endif

#ifdef __alpha__
static inline int testandset (int *p)
{
    int ret;
    unsigned long one;

    __asm__ __volatile__ ("0:	mov 1,%2\n"
			  "	ldl_l %0,%1\n"
			  "	stl_c %2,%1\n"
			  "	beq %2,1f\n"
			  ".subsection 2\n"
			  "1:	br 0b\n"
			  ".previous"
			  : "=r" (ret), "=m" (*p), "=r" (one)
			  : "m" (*p));
    return ret;
}
#endif

#ifdef __sparc__
static inline int testandset (int *p)
{
	int ret;

	__asm__ __volatile__("ldstub	[%1], %0"
			     : "=r" (ret)
			     : "r" (p)
			     : "memory");

	return (ret ? 1 : 0);
}
#endif

#ifdef __arm__
static inline int testandset (int *spinlock)
{
    register unsigned int ret;
    __asm__ __volatile__("swp %0, %1, [%2]"
                         : "=r"(ret)
                         : "0"(1), "r"(spinlock));
    
    return ret;
}
#endif

#ifdef __mc68000
static inline int testandset (int *p)
{
    char ret;
    __asm__ __volatile__("tas %1; sne %0"
                         : "=r" (ret)
                         : "m" (p)
                         : "cc","memory");
    return ret == 0;
}
#endif

typedef int spinlock_t;

#define SPIN_LOCK_UNLOCKED 0

#if defined(CONFIG_USER_ONLY)
static inline void spin_lock(spinlock_t *lock)
{
    while (testandset(lock));
}

static inline void spin_unlock(spinlock_t *lock)
{
    *lock = 0;
}

static inline int spin_trylock(spinlock_t *lock)
{
    return !testandset(lock);
}
#else
static inline void spin_lock(spinlock_t *lock)
{
}

static inline void spin_unlock(spinlock_t *lock)
{
}

static inline int spin_trylock(spinlock_t *lock)
{
    return 1;
}
#endif

extern spinlock_t tb_lock;

extern int tb_invalidated_flag;

#if (defined(TARGET_I386) || defined(TARGET_PPC)) && \
    !defined(CONFIG_USER_ONLY)

void tlb_fill(unsigned long addr, int is_write, int is_user, 
              void *retaddr);

#define ACCESS_TYPE 3
#define MEMSUFFIX _code
#define env cpu_single_env

#define DATA_SIZE 1
#include "softmmu_header.h"

#define DATA_SIZE 2
#include "softmmu_header.h"

#define DATA_SIZE 4
#include "softmmu_header.h"

#undef ACCESS_TYPE
#undef MEMSUFFIX
#undef env

#endif

#if defined(CONFIG_USER_ONLY)
static inline target_ulong get_phys_addr_code(CPUState *env, target_ulong addr)
{
    return addr;
}
#else
/* NOTE: this function can trigger an exception */
/* NOTE2: the returned address is not exactly the physical address: it
   is the offset relative to phys_ram_base */
/* XXX: i386 target specific */
static inline target_ulong get_phys_addr_code(CPUState *env, target_ulong addr)
{
    int is_user, index;

    index = (addr >> TARGET_PAGE_BITS) & (CPU_TLB_SIZE - 1);
#if defined(TARGET_I386)
    is_user = ((env->hflags & HF_CPL_MASK) == 3);
#elif defined (TARGET_PPC)
    is_user = msr_pr;
#else
#error "Unimplemented !"
#endif
    if (__builtin_expect(env->tlb_read[is_user][index].address != 
                         (addr & TARGET_PAGE_MASK), 0)) {
        ldub_code((void *)addr);
    }
    return addr + env->tlb_read[is_user][index].addend - (unsigned long)phys_ram_base;
}
#endif
