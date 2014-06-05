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
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _EXEC_ALL_H_
#define _EXEC_ALL_H_

#include "qemu-common.h"

/* allow to see translation results - the slowdown should be negligible, so we leave it */
#define DEBUG_DISAS

/* Page tracking code uses ram addresses in system mode, and virtual
   addresses in userspace mode.  Define tb_page_addr_t to be an appropriate
   type.  */
#if defined(CONFIG_USER_ONLY)
typedef abi_ulong tb_page_addr_t;
#else
typedef ram_addr_t tb_page_addr_t;
#endif

/* is_jmp field values */
#define DISAS_NEXT    0 /* next instruction can be analyzed */
#define DISAS_JUMP    1 /* only pc was modified dynamically */
#define DISAS_UPDATE  2 /* cpu state was modified dynamically */
#define DISAS_TB_JUMP 3 /* only pc was modified statically */

struct TranslationBlock;
typedef struct TranslationBlock TranslationBlock;

/* XXX: make safe guess about sizes */
#define MAX_OP_PER_INSTR 266

#if HOST_LONG_BITS == 32
#define MAX_OPC_PARAM_PER_ARG 2
#else
#define MAX_OPC_PARAM_PER_ARG 1
#endif
#define MAX_OPC_PARAM_IARGS 5
#define MAX_OPC_PARAM_OARGS 1
#define MAX_OPC_PARAM_ARGS (MAX_OPC_PARAM_IARGS + MAX_OPC_PARAM_OARGS)

/* A Call op needs up to 4 + 2N parameters on 32-bit archs,
 * and up to 4 + N parameters on 64-bit archs
 * (N = number of input arguments + output arguments).  */
#define MAX_OPC_PARAM (4 + (MAX_OPC_PARAM_PER_ARG * MAX_OPC_PARAM_ARGS))
#define OPC_BUF_SIZE 640
#define OPC_MAX_SIZE (OPC_BUF_SIZE - MAX_OP_PER_INSTR)

/* Maximum size a TCG op can expand to.  This is complicated because a
   single op may require several host instructions and register reloads.
   For now take a wild guess at 192 bytes, which should allow at least
   a couple of fixup instructions per argument.  */
#define TCG_MAX_OP_SIZE 192

#define OPPARAM_BUF_SIZE (OPC_BUF_SIZE * MAX_OPC_PARAM)

#include "qemu/log.h"

void gen_intermediate_code(CPUArchState *env, struct TranslationBlock *tb);
void gen_intermediate_code_pc(CPUArchState *env, struct TranslationBlock *tb);
void restore_state_to_opc(CPUArchState *env, struct TranslationBlock *tb,
                          int pc_pos);

void cpu_gen_init(void);
int cpu_gen_code(CPUArchState *env, struct TranslationBlock *tb,
                 int *gen_code_size_ptr);
bool cpu_restore_state(CPUState *cpu, uintptr_t searched_pc);
void page_size_init(void);

void QEMU_NORETURN cpu_resume_from_signal(CPUState *cpu, void *puc);
void QEMU_NORETURN cpu_io_recompile(CPUState *cpu, uintptr_t retaddr);
TranslationBlock *tb_gen_code(CPUState *cpu,
                              target_ulong pc, target_ulong cs_base, int flags,
                              int cflags);
void cpu_exec_init(CPUArchState *env);
void QEMU_NORETURN cpu_loop_exit(CPUState *cpu);
int page_unprotect(target_ulong address, uintptr_t pc, void *puc);
void tb_invalidate_phys_page_range(tb_page_addr_t start, tb_page_addr_t end,
                                   int is_cpu_write_access);
void tb_invalidate_phys_range(tb_page_addr_t start, tb_page_addr_t end,
                              int is_cpu_write_access);
#if !defined(CONFIG_USER_ONLY)
void tcg_cpu_address_space_init(CPUState *cpu, AddressSpace *as);
/* cputlb.c */
void tlb_flush_page(CPUState *cpu, target_ulong addr);
void tlb_flush(CPUState *cpu, int flush_global);
void tlb_set_page(CPUState *cpu, target_ulong vaddr,
                  hwaddr paddr, int prot,
                  int mmu_idx, target_ulong size);
void tb_invalidate_phys_addr(AddressSpace *as, hwaddr addr);
#else
static inline void tlb_flush_page(CPUState *cpu, target_ulong addr)
{
}

static inline void tlb_flush(CPUState *cpu, int flush_global)
{
}
#endif

#define CODE_GEN_ALIGN           16 /* must be >= of the size of a icache line */

#define CODE_GEN_PHYS_HASH_BITS     15
#define CODE_GEN_PHYS_HASH_SIZE     (1 << CODE_GEN_PHYS_HASH_BITS)

/* estimated block size for TB allocation */
/* XXX: use a per code average code fragment size and modulate it
   according to the host CPU */
#if defined(CONFIG_SOFTMMU)
#define CODE_GEN_AVG_BLOCK_SIZE 128
#else
#define CODE_GEN_AVG_BLOCK_SIZE 64
#endif

#if defined(__arm__) || defined(_ARCH_PPC) \
    || defined(__x86_64__) || defined(__i386__) \
    || defined(__sparc__) || defined(__aarch64__) \
    || defined(__s390x__) || defined(__mips__) \
    || defined(CONFIG_TCG_INTERPRETER)
#define USE_DIRECT_JUMP
#endif

struct TranslationBlock {
    target_ulong pc;   /* simulated PC corresponding to this block (EIP + CS base) */
    target_ulong cs_base; /* CS base for this block */
    uint64_t flags; /* flags defining in which context the code was generated */
    uint16_t size;      /* size of target code for this block (1 <=
                           size <= TARGET_PAGE_SIZE) */
    uint16_t cflags;    /* compile flags */
#define CF_COUNT_MASK  0x7fff
#define CF_LAST_IO     0x8000 /* Last insn may be an IO access.  */

    void *tc_ptr;    /* pointer to the translated code */
    /* next matching tb for physical address. */
    struct TranslationBlock *phys_hash_next;
    /* first and second physical page containing code. The lower bit
       of the pointer tells the index in page_next[] */
    struct TranslationBlock *page_next[2];
    tb_page_addr_t page_addr[2];

    /* the following data are used to directly call another TB from
       the code of this one. */
    uint16_t tb_next_offset[2]; /* offset of original jump target */
#ifdef USE_DIRECT_JUMP
    uint16_t tb_jmp_offset[2]; /* offset of jump instruction */
#else
    uintptr_t tb_next[2]; /* address of jump generated code */
#endif
    /* list of TBs jumping to this one. This is a circular list using
       the two least significant bits of the pointers to tell what is
       the next pointer: 0 = jmp_next[0], 1 = jmp_next[1], 2 =
       jmp_first */
    struct TranslationBlock *jmp_next[2];
    struct TranslationBlock *jmp_first;
    uint32_t icount;
};

#include "exec/spinlock.h"

typedef struct TBContext TBContext;

struct TBContext {

    TranslationBlock *tbs;
    TranslationBlock *tb_phys_hash[CODE_GEN_PHYS_HASH_SIZE];
    int nb_tbs;
    /* any access to the tbs or the page table must use this lock */
    spinlock_t tb_lock;

    /* statistics */
    int tb_flush_count;
    int tb_phys_invalidate_count;

    int tb_invalidated_flag;
};

static inline unsigned int tb_jmp_cache_hash_page(target_ulong pc)
{
    target_ulong tmp;
    tmp = pc ^ (pc >> (TARGET_PAGE_BITS - TB_JMP_PAGE_BITS));
    return (tmp >> (TARGET_PAGE_BITS - TB_JMP_PAGE_BITS)) & TB_JMP_PAGE_MASK;
}

static inline unsigned int tb_jmp_cache_hash_func(target_ulong pc)
{
    target_ulong tmp;
    tmp = pc ^ (pc >> (TARGET_PAGE_BITS - TB_JMP_PAGE_BITS));
    return (((tmp >> (TARGET_PAGE_BITS - TB_JMP_PAGE_BITS)) & TB_JMP_PAGE_MASK)
	    | (tmp & TB_JMP_ADDR_MASK));
}

static inline unsigned int tb_phys_hash_func(tb_page_addr_t pc)
{
    return (pc >> 2) & (CODE_GEN_PHYS_HASH_SIZE - 1);
}

void tb_free(TranslationBlock *tb);
void tb_flush(CPUArchState *env);
void tb_phys_invalidate(TranslationBlock *tb, tb_page_addr_t page_addr);

#if defined(USE_DIRECT_JUMP)

#if defined(CONFIG_TCG_INTERPRETER)
static inline void tb_set_jmp_target1(uintptr_t jmp_addr, uintptr_t addr)
{
    /* patch the branch destination */
    *(uint32_t *)jmp_addr = addr - (jmp_addr + 4);
    /* no need to flush icache explicitly */
}
#elif defined(_ARCH_PPC)
void ppc_tb_set_jmp_target(unsigned long jmp_addr, unsigned long addr);
#define tb_set_jmp_target1 ppc_tb_set_jmp_target
#elif defined(__i386__) || defined(__x86_64__)
static inline void tb_set_jmp_target1(uintptr_t jmp_addr, uintptr_t addr)
{
    /* patch the branch destination */
    stl_le_p((void*)jmp_addr, addr - (jmp_addr + 4));
    /* no need to flush icache explicitly */
}
#elif defined(__s390x__)
static inline void tb_set_jmp_target1(uintptr_t jmp_addr, uintptr_t addr)
{
    /* patch the branch destination */
    intptr_t disp = addr - (jmp_addr - 2);
    stl_be_p((void*)jmp_addr, disp / 2);
    /* no need to flush icache explicitly */
}
#elif defined(__aarch64__)
void aarch64_tb_set_jmp_target(uintptr_t jmp_addr, uintptr_t addr);
#define tb_set_jmp_target1 aarch64_tb_set_jmp_target
#elif defined(__arm__)
static inline void tb_set_jmp_target1(uintptr_t jmp_addr, uintptr_t addr)
{
#if !QEMU_GNUC_PREREQ(4, 1)
    register unsigned long _beg __asm ("a1");
    register unsigned long _end __asm ("a2");
    register unsigned long _flg __asm ("a3");
#endif

    /* we could use a ldr pc, [pc, #-4] kind of branch and avoid the flush */
    *(uint32_t *)jmp_addr =
        (*(uint32_t *)jmp_addr & ~0xffffff)
        | (((addr - (jmp_addr + 8)) >> 2) & 0xffffff);

#if QEMU_GNUC_PREREQ(4, 1)
    __builtin___clear_cache((char *) jmp_addr, (char *) jmp_addr + 4);
#else
    /* flush icache */
    _beg = jmp_addr;
    _end = jmp_addr + 4;
    _flg = 0;
    __asm __volatile__ ("swi 0x9f0002" : : "r" (_beg), "r" (_end), "r" (_flg));
#endif
}
#elif defined(__sparc__) || defined(__mips__)
void tb_set_jmp_target1(uintptr_t jmp_addr, uintptr_t addr);
#else
#error tb_set_jmp_target1 is missing
#endif

static inline void tb_set_jmp_target(TranslationBlock *tb,
                                     int n, uintptr_t addr)
{
    uint16_t offset = tb->tb_jmp_offset[n];
    tb_set_jmp_target1((uintptr_t)(tb->tc_ptr + offset), addr);
}

#else

/* set the jump target */
static inline void tb_set_jmp_target(TranslationBlock *tb,
                                     int n, uintptr_t addr)
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
        tb_set_jmp_target(tb, n, (uintptr_t)tb_next->tc_ptr);

        /* add in TB jmp circular list */
        tb->jmp_next[n] = tb_next->jmp_first;
        tb_next->jmp_first = (TranslationBlock *)((uintptr_t)(tb) | (n));
    }
}

/* GETRA is the true target of the return instruction that we'll execute,
   defined here for simplicity of defining the follow-up macros.  */
#if defined(CONFIG_TCG_INTERPRETER)
extern uintptr_t tci_tb_ptr;
# define GETRA() tci_tb_ptr
#else
# define GETRA() \
    ((uintptr_t)__builtin_extract_return_addr(__builtin_return_address(0)))
#endif

/* The true return address will often point to a host insn that is part of
   the next translated guest insn.  Adjust the address backward to point to
   the middle of the call insn.  Subtracting one would do the job except for
   several compressed mode architectures (arm, mips) which set the low bit
   to indicate the compressed mode; subtracting two works around that.  It
   is also the case that there are no host isas that contain a call insn
   smaller than 4 bytes, so we don't worry about special-casing this.  */
#if defined(CONFIG_TCG_INTERPRETER)
# define GETPC_ADJ   0
#else
# define GETPC_ADJ   2
#endif

#define GETPC()  (GETRA() - GETPC_ADJ)

#if !defined(CONFIG_USER_ONLY)

void phys_mem_set_alloc(void *(*alloc)(size_t));

struct MemoryRegion *iotlb_to_region(AddressSpace *as, hwaddr index);
bool io_mem_read(struct MemoryRegion *mr, hwaddr addr,
                 uint64_t *pvalue, unsigned size);
bool io_mem_write(struct MemoryRegion *mr, hwaddr addr,
                  uint64_t value, unsigned size);

void tlb_fill(CPUState *cpu, target_ulong addr, int is_write, int mmu_idx,
              uintptr_t retaddr);

#endif

#if defined(CONFIG_USER_ONLY)
static inline tb_page_addr_t get_page_addr_code(CPUArchState *env1, target_ulong addr)
{
    return addr;
}
#else
/* cputlb.c */
tb_page_addr_t get_page_addr_code(CPUArchState *env1, target_ulong addr);
#endif

typedef void (CPUDebugExcpHandler)(CPUArchState *env);

void cpu_set_debug_excp_handler(CPUDebugExcpHandler *handler);

/* vl.c */
extern int singlestep;

/* cpu-exec.c */
extern volatile sig_atomic_t exit_request;

/**
 * cpu_can_do_io:
 * @cpu: The CPU for which to check IO.
 *
 * Deterministic execution requires that IO only be performed on the last
 * instruction of a TB so that interrupts take effect immediately.
 *
 * Returns: %true if memory-mapped IO is safe, %false otherwise.
 */
static inline bool cpu_can_do_io(CPUState *cpu)
{
    if (!use_icount) {
        return true;
    }
    /* If not executing code then assume we are ok.  */
    if (cpu->current_tb == NULL) {
        return true;
    }
    return cpu->can_do_io != 0;
}

#endif
