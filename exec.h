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

#if defined(TARGET_I386)

#define GEN_FLAG_CODE32_SHIFT 0
#define GEN_FLAG_ADDSEG_SHIFT 1
#define GEN_FLAG_SS32_SHIFT   2
#define GEN_FLAG_VM_SHIFT     3
#define GEN_FLAG_ST_SHIFT     4
#define GEN_FLAG_TF_SHIFT     8 /* same position as eflags */
#define GEN_FLAG_CPL_SHIFT    9
#define GEN_FLAG_IOPL_SHIFT   12 /* same position as eflags */

#endif

extern FILE *logfile;
extern int loglevel;

int gen_intermediate_code(struct TranslationBlock *tb);
int gen_intermediate_code_pc(struct TranslationBlock *tb);
void dump_ops(const uint16_t *opc_buf, const uint32_t *opparam_buf);
int cpu_gen_code(struct TranslationBlock *tb,
                 int max_code_size, int *gen_code_size_ptr);
int cpu_restore_state(struct TranslationBlock *tb, 
                      CPUState *env, unsigned long searched_pc);
void cpu_exec_init(void);
int page_unprotect(unsigned long address);
void page_unmap(void);

#define CODE_GEN_MAX_SIZE        65536
#define CODE_GEN_ALIGN           16 /* must be >= of the size of a icache line */

#define CODE_GEN_HASH_BITS     15
#define CODE_GEN_HASH_SIZE     (1 << CODE_GEN_HASH_BITS)

/* maximum total translate dcode allocated */
#define CODE_GEN_BUFFER_SIZE     (2048 * 1024)
//#define CODE_GEN_BUFFER_SIZE     (128 * 1024)

#if defined(__powerpc__)
#define USE_DIRECT_JUMP
#endif

typedef struct TranslationBlock {
    unsigned long pc;   /* simulated PC corresponding to this block (EIP + CS base) */
    unsigned long cs_base; /* CS base for this block */
    unsigned int flags; /* flags defining in which context the code was generated */
    uint16_t size;      /* size of target code for this block (1 <=
                           size <= TARGET_PAGE_SIZE) */
    uint8_t *tc_ptr;    /* pointer to the translated code */
    struct TranslationBlock *hash_next; /* next matching block */
    struct TranslationBlock *page_next[2]; /* next blocks in even/odd page */
    /* the following data are used to directly call another TB from
       the code of this one. */
    uint16_t tb_next_offset[2]; /* offset of original jump target */
#ifdef USE_DIRECT_JUMP
    uint16_t tb_jmp_offset[2]; /* offset of jump instruction */
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

TranslationBlock *tb_alloc(unsigned long pc);
void tb_flush(void);
void tb_link(TranslationBlock *tb);

extern TranslationBlock *tb_hash[CODE_GEN_HASH_SIZE];

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

#if defined(__powerpc__)

static inline void tb_set_jmp_target(TranslationBlock *tb, 
                                     int n, unsigned long addr)
{
    uint32_t val, *ptr;
    unsigned long offset;

    offset = (unsigned long)(tb->tc_ptr + tb->tb_jmp_offset[n]);

    /* patch the branch destination */
    ptr = (uint32_t *)offset;
    val = *ptr;
    val = (val & ~0x03fffffc) | ((addr - offset) & 0x03fffffc);
    *ptr = val;
    /* flush icache */
    asm volatile ("dcbst 0,%0" : : "r"(ptr) : "memory");
    asm volatile ("sync" : : : "memory");
    asm volatile ("icbi 0,%0" : : "r"(ptr) : "memory");
    asm volatile ("sync" : : : "memory");
    asm volatile ("isync" : : : "memory");
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

/* on PowerPC we patch the jump instruction directly */
#define JUMP_TB(tbparam, n, eip)\
do {\
    static void __attribute__((unused)) *__op_label ## n = &&label ## n;\
    asm volatile ("b %0" : : "i" (&__op_jmp ## n));\
label ## n:\
    T0 = (long)(tbparam) + (n);\
    EIP = eip;\
} while (0)

#else

/* jump to next block operations (more portable code, does not need
   cache flushing, but slower because of indirect jump) */
#define JUMP_TB(tbparam, n, eip)\
do {\
    static void __attribute__((unused)) *__op_label ## n = &&label ## n;\
    static void __attribute__((unused)) *dummy ## n = &&dummy_label ## n;\
    goto *(void *)(((TranslationBlock *)tbparam)->tb_next[n]);\
label ## n:\
    T0 = (long)(tbparam) + (n);\
    EIP = eip;\
dummy_label ## n:\
} while (0)

#endif

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

typedef int spinlock_t;

#define SPIN_LOCK_UNLOCKED 0

#if 1
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

