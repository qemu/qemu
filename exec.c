/*
 *  virtual page mapping and translated block handling
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
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <inttypes.h>
#include <sys/mman.h>

#include "config.h"
#include "cpu.h"
#include "exec-all.h"

//#define DEBUG_TB_INVALIDATE
//#define DEBUG_FLUSH

/* make various TB consistency checks */
//#define DEBUG_TB_CHECK 

/* threshold to flush the translated code buffer */
#define CODE_GEN_BUFFER_MAX_SIZE (CODE_GEN_BUFFER_SIZE - CODE_GEN_MAX_SIZE)

#define CODE_GEN_MAX_BLOCKS    (CODE_GEN_BUFFER_SIZE / 64)

TranslationBlock tbs[CODE_GEN_MAX_BLOCKS];
TranslationBlock *tb_hash[CODE_GEN_HASH_SIZE];
int nb_tbs;
/* any access to the tbs or the page table must use this lock */
spinlock_t tb_lock = SPIN_LOCK_UNLOCKED;

uint8_t code_gen_buffer[CODE_GEN_BUFFER_SIZE];
uint8_t *code_gen_ptr;

/* XXX: pack the flags in the low bits of the pointer ? */
typedef struct PageDesc {
    unsigned long flags;
    TranslationBlock *first_tb;
} PageDesc;

#define L2_BITS 10
#define L1_BITS (32 - L2_BITS - TARGET_PAGE_BITS)

#define L1_SIZE (1 << L1_BITS)
#define L2_SIZE (1 << L2_BITS)

static void tb_invalidate_page(unsigned long address);
static void io_mem_init(void);

unsigned long real_host_page_size;
unsigned long host_page_bits;
unsigned long host_page_size;
unsigned long host_page_mask;

static PageDesc *l1_map[L1_SIZE];

/* io memory support */
static unsigned long *l1_physmap[L1_SIZE];
CPUWriteMemoryFunc *io_mem_write[IO_MEM_NB_ENTRIES][4];
CPUReadMemoryFunc *io_mem_read[IO_MEM_NB_ENTRIES][4];
static int io_mem_nb;

/* log support */
char *logfilename = "/tmp/qemu.log";
FILE *logfile;
int loglevel;

static void page_init(void)
{
    /* NOTE: we can always suppose that host_page_size >=
       TARGET_PAGE_SIZE */
    real_host_page_size = getpagesize();
    if (host_page_size == 0)
        host_page_size = real_host_page_size;
    if (host_page_size < TARGET_PAGE_SIZE)
        host_page_size = TARGET_PAGE_SIZE;
    host_page_bits = 0;
    while ((1 << host_page_bits) < host_page_size)
        host_page_bits++;
    host_page_mask = ~(host_page_size - 1);
}

/* dump memory mappings */
void page_dump(FILE *f)
{
    unsigned long start, end;
    int i, j, prot, prot1;
    PageDesc *p;

    fprintf(f, "%-8s %-8s %-8s %s\n",
            "start", "end", "size", "prot");
    start = -1;
    end = -1;
    prot = 0;
    for(i = 0; i <= L1_SIZE; i++) {
        if (i < L1_SIZE)
            p = l1_map[i];
        else
            p = NULL;
        for(j = 0;j < L2_SIZE; j++) {
            if (!p)
                prot1 = 0;
            else
                prot1 = p[j].flags;
            if (prot1 != prot) {
                end = (i << (32 - L1_BITS)) | (j << TARGET_PAGE_BITS);
                if (start != -1) {
                    fprintf(f, "%08lx-%08lx %08lx %c%c%c\n",
                            start, end, end - start, 
                            prot & PAGE_READ ? 'r' : '-',
                            prot & PAGE_WRITE ? 'w' : '-',
                            prot & PAGE_EXEC ? 'x' : '-');
                }
                if (prot1 != 0)
                    start = end;
                else
                    start = -1;
                prot = prot1;
            }
            if (!p)
                break;
        }
    }
}

static inline PageDesc *page_find_alloc(unsigned int index)
{
    PageDesc **lp, *p;

    lp = &l1_map[index >> L2_BITS];
    p = *lp;
    if (!p) {
        /* allocate if not found */
        p = malloc(sizeof(PageDesc) * L2_SIZE);
        memset(p, 0, sizeof(PageDesc) * L2_SIZE);
        *lp = p;
    }
    return p + (index & (L2_SIZE - 1));
}

static inline PageDesc *page_find(unsigned int index)
{
    PageDesc *p;

    p = l1_map[index >> L2_BITS];
    if (!p)
        return 0;
    return p + (index & (L2_SIZE - 1));
}

int page_get_flags(unsigned long address)
{
    PageDesc *p;

    p = page_find(address >> TARGET_PAGE_BITS);
    if (!p)
        return 0;
    return p->flags;
}

/* modify the flags of a page and invalidate the code if
   necessary. The flag PAGE_WRITE_ORG is positionned automatically
   depending on PAGE_WRITE */
void page_set_flags(unsigned long start, unsigned long end, int flags)
{
    PageDesc *p;
    unsigned long addr;

    start = start & TARGET_PAGE_MASK;
    end = TARGET_PAGE_ALIGN(end);
    if (flags & PAGE_WRITE)
        flags |= PAGE_WRITE_ORG;
    spin_lock(&tb_lock);
    for(addr = start; addr < end; addr += TARGET_PAGE_SIZE) {
        p = page_find_alloc(addr >> TARGET_PAGE_BITS);
        /* if the write protection is set, then we invalidate the code
           inside */
        if (!(p->flags & PAGE_WRITE) && 
            (flags & PAGE_WRITE) &&
            p->first_tb) {
            tb_invalidate_page(addr);
        }
        p->flags = flags;
    }
    spin_unlock(&tb_lock);
}

void cpu_exec_init(void)
{
    if (!code_gen_ptr) {
        code_gen_ptr = code_gen_buffer;
        page_init();
        io_mem_init();
    }
}

/* set to NULL all the 'first_tb' fields in all PageDescs */
static void page_flush_tb(void)
{
    int i, j;
    PageDesc *p;

    for(i = 0; i < L1_SIZE; i++) {
        p = l1_map[i];
        if (p) {
            for(j = 0; j < L2_SIZE; j++)
                p[j].first_tb = NULL;
        }
    }
}

/* flush all the translation blocks */
/* XXX: tb_flush is currently not thread safe */
void tb_flush(void)
{
    int i;
#ifdef DEBUG_FLUSH
    printf("qemu: flush code_size=%d nb_tbs=%d avg_tb_size=%d\n", 
           code_gen_ptr - code_gen_buffer, 
           nb_tbs, 
           (code_gen_ptr - code_gen_buffer) / nb_tbs);
#endif
    nb_tbs = 0;
    for(i = 0;i < CODE_GEN_HASH_SIZE; i++)
        tb_hash[i] = NULL;
    page_flush_tb();
    code_gen_ptr = code_gen_buffer;
    /* XXX: flush processor icache at this point if cache flush is
       expensive */
}

#ifdef DEBUG_TB_CHECK

static void tb_invalidate_check(unsigned long address)
{
    TranslationBlock *tb;
    int i;
    address &= TARGET_PAGE_MASK;
    for(i = 0;i < CODE_GEN_HASH_SIZE; i++) {
        for(tb = tb_hash[i]; tb != NULL; tb = tb->hash_next) {
            if (!(address + TARGET_PAGE_SIZE <= tb->pc ||
                  address >= tb->pc + tb->size)) {
                printf("ERROR invalidate: address=%08lx PC=%08lx size=%04x\n",
                       address, tb->pc, tb->size);
            }
        }
    }
}

/* verify that all the pages have correct rights for code */
static void tb_page_check(void)
{
    TranslationBlock *tb;
    int i, flags1, flags2;
    
    for(i = 0;i < CODE_GEN_HASH_SIZE; i++) {
        for(tb = tb_hash[i]; tb != NULL; tb = tb->hash_next) {
            flags1 = page_get_flags(tb->pc);
            flags2 = page_get_flags(tb->pc + tb->size - 1);
            if ((flags1 & PAGE_WRITE) || (flags2 & PAGE_WRITE)) {
                printf("ERROR page flags: PC=%08lx size=%04x f1=%x f2=%x\n",
                       tb->pc, tb->size, flags1, flags2);
            }
        }
    }
}

void tb_jmp_check(TranslationBlock *tb)
{
    TranslationBlock *tb1;
    unsigned int n1;

    /* suppress any remaining jumps to this TB */
    tb1 = tb->jmp_first;
    for(;;) {
        n1 = (long)tb1 & 3;
        tb1 = (TranslationBlock *)((long)tb1 & ~3);
        if (n1 == 2)
            break;
        tb1 = tb1->jmp_next[n1];
    }
    /* check end of list */
    if (tb1 != tb) {
        printf("ERROR: jmp_list from 0x%08lx\n", (long)tb);
    }
}

#endif

/* invalidate one TB */
static inline void tb_remove(TranslationBlock **ptb, TranslationBlock *tb,
                             int next_offset)
{
    TranslationBlock *tb1;
    for(;;) {
        tb1 = *ptb;
        if (tb1 == tb) {
            *ptb = *(TranslationBlock **)((char *)tb1 + next_offset);
            break;
        }
        ptb = (TranslationBlock **)((char *)tb1 + next_offset);
    }
}

static inline void tb_jmp_remove(TranslationBlock *tb, int n)
{
    TranslationBlock *tb1, **ptb;
    unsigned int n1;

    ptb = &tb->jmp_next[n];
    tb1 = *ptb;
    if (tb1) {
        /* find tb(n) in circular list */
        for(;;) {
            tb1 = *ptb;
            n1 = (long)tb1 & 3;
            tb1 = (TranslationBlock *)((long)tb1 & ~3);
            if (n1 == n && tb1 == tb)
                break;
            if (n1 == 2) {
                ptb = &tb1->jmp_first;
            } else {
                ptb = &tb1->jmp_next[n1];
            }
        }
        /* now we can suppress tb(n) from the list */
        *ptb = tb->jmp_next[n];

        tb->jmp_next[n] = NULL;
    }
}

/* reset the jump entry 'n' of a TB so that it is not chained to
   another TB */
static inline void tb_reset_jump(TranslationBlock *tb, int n)
{
    tb_set_jmp_target(tb, n, (unsigned long)(tb->tc_ptr + tb->tb_next_offset[n]));
}

static inline void tb_invalidate(TranslationBlock *tb, int parity)
{
    PageDesc *p;
    unsigned int page_index1, page_index2;
    unsigned int h, n1;
    TranslationBlock *tb1, *tb2;
    
    /* remove the TB from the hash list */
    h = tb_hash_func(tb->pc);
    tb_remove(&tb_hash[h], tb, 
              offsetof(TranslationBlock, hash_next));
    /* remove the TB from the page list */
    page_index1 = tb->pc >> TARGET_PAGE_BITS;
    if ((page_index1 & 1) == parity) {
        p = page_find(page_index1);
        tb_remove(&p->first_tb, tb, 
                  offsetof(TranslationBlock, page_next[page_index1 & 1]));
    }
    page_index2 = (tb->pc + tb->size - 1) >> TARGET_PAGE_BITS;
    if ((page_index2 & 1) == parity) {
        p = page_find(page_index2);
        tb_remove(&p->first_tb, tb, 
                  offsetof(TranslationBlock, page_next[page_index2 & 1]));
    }

    /* suppress this TB from the two jump lists */
    tb_jmp_remove(tb, 0);
    tb_jmp_remove(tb, 1);

    /* suppress any remaining jumps to this TB */
    tb1 = tb->jmp_first;
    for(;;) {
        n1 = (long)tb1 & 3;
        if (n1 == 2)
            break;
        tb1 = (TranslationBlock *)((long)tb1 & ~3);
        tb2 = tb1->jmp_next[n1];
        tb_reset_jump(tb1, n1);
        tb1->jmp_next[n1] = NULL;
        tb1 = tb2;
    }
    tb->jmp_first = (TranslationBlock *)((long)tb | 2); /* fail safe */
}

/* invalidate all TBs which intersect with the target page starting at addr */
static void tb_invalidate_page(unsigned long address)
{
    TranslationBlock *tb_next, *tb;
    unsigned int page_index;
    int parity1, parity2;
    PageDesc *p;
#ifdef DEBUG_TB_INVALIDATE
    printf("tb_invalidate_page: %lx\n", address);
#endif

    page_index = address >> TARGET_PAGE_BITS;
    p = page_find(page_index);
    if (!p)
        return;
    tb = p->first_tb;
    parity1 = page_index & 1;
    parity2 = parity1 ^ 1;
    while (tb != NULL) {
        tb_next = tb->page_next[parity1];
        tb_invalidate(tb, parity2);
        tb = tb_next;
    }
    p->first_tb = NULL;
}

/* add the tb in the target page and protect it if necessary */
static inline void tb_alloc_page(TranslationBlock *tb, unsigned int page_index)
{
    PageDesc *p;
    unsigned long host_start, host_end, addr, page_addr;
    int prot;

    p = page_find_alloc(page_index);
    tb->page_next[page_index & 1] = p->first_tb;
    p->first_tb = tb;
    if (p->flags & PAGE_WRITE) {
        /* force the host page as non writable (writes will have a
           page fault + mprotect overhead) */
        page_addr = (page_index << TARGET_PAGE_BITS);
        host_start = page_addr & host_page_mask;
        host_end = host_start + host_page_size;
        prot = 0;
        for(addr = host_start; addr < host_end; addr += TARGET_PAGE_SIZE)
            prot |= page_get_flags(addr);
#if !defined(CONFIG_SOFTMMU)
        mprotect((void *)host_start, host_page_size, 
                 (prot & PAGE_BITS) & ~PAGE_WRITE);
#endif
#if !defined(CONFIG_USER_ONLY)
        /* suppress soft TLB */
        /* XXX: must flush on all processor with same address space */
        tlb_flush_page_write(cpu_single_env, host_start);
#endif
#ifdef DEBUG_TB_INVALIDATE
        printf("protecting code page: 0x%08lx\n", 
               host_start);
#endif
        p->flags &= ~PAGE_WRITE;
    }
}

/* Allocate a new translation block. Flush the translation buffer if
   too many translation blocks or too much generated code. */
TranslationBlock *tb_alloc(unsigned long pc)
{
    TranslationBlock *tb;

    if (nb_tbs >= CODE_GEN_MAX_BLOCKS || 
        (code_gen_ptr - code_gen_buffer) >= CODE_GEN_BUFFER_MAX_SIZE)
        return NULL;
    tb = &tbs[nb_tbs++];
    tb->pc = pc;
    return tb;
}

/* link the tb with the other TBs */
void tb_link(TranslationBlock *tb)
{
    unsigned int page_index1, page_index2;

    /* add in the page list */
    page_index1 = tb->pc >> TARGET_PAGE_BITS;
    tb_alloc_page(tb, page_index1);
    page_index2 = (tb->pc + tb->size - 1) >> TARGET_PAGE_BITS;
    if (page_index2 != page_index1) {
        tb_alloc_page(tb, page_index2);
    }
#ifdef DEBUG_TB_CHECK
    tb_page_check();
#endif
    tb->jmp_first = (TranslationBlock *)((long)tb | 2);
    tb->jmp_next[0] = NULL;
    tb->jmp_next[1] = NULL;

    /* init original jump addresses */
    if (tb->tb_next_offset[0] != 0xffff)
        tb_reset_jump(tb, 0);
    if (tb->tb_next_offset[1] != 0xffff)
        tb_reset_jump(tb, 1);
}

/* called from signal handler: invalidate the code and unprotect the
   page. Return TRUE if the fault was succesfully handled. */
int page_unprotect(unsigned long address)
{
    unsigned int page_index, prot, pindex;
    PageDesc *p, *p1;
    unsigned long host_start, host_end, addr;

    host_start = address & host_page_mask;
    page_index = host_start >> TARGET_PAGE_BITS;
    p1 = page_find(page_index);
    if (!p1)
        return 0;
    host_end = host_start + host_page_size;
    p = p1;
    prot = 0;
    for(addr = host_start;addr < host_end; addr += TARGET_PAGE_SIZE) {
        prot |= p->flags;
        p++;
    }
    /* if the page was really writable, then we change its
       protection back to writable */
    if (prot & PAGE_WRITE_ORG) {
        pindex = (address - host_start) >> TARGET_PAGE_BITS;
        if (!(p1[pindex].flags & PAGE_WRITE)) {
#if !defined(CONFIG_SOFTMMU)
            mprotect((void *)host_start, host_page_size, 
                     (prot & PAGE_BITS) | PAGE_WRITE);
#endif
            p1[pindex].flags |= PAGE_WRITE;
            /* and since the content will be modified, we must invalidate
               the corresponding translated code. */
            tb_invalidate_page(address);
#ifdef DEBUG_TB_CHECK
            tb_invalidate_check(address);
#endif
            return 1;
        }
    }
    return 0;
}

/* call this function when system calls directly modify a memory area */
void page_unprotect_range(uint8_t *data, unsigned long data_size)
{
    unsigned long start, end, addr;

    start = (unsigned long)data;
    end = start + data_size;
    start &= TARGET_PAGE_MASK;
    end = TARGET_PAGE_ALIGN(end);
    for(addr = start; addr < end; addr += TARGET_PAGE_SIZE) {
        page_unprotect(addr);
    }
}

/* find the TB 'tb' such that tb[0].tc_ptr <= tc_ptr <
   tb[1].tc_ptr. Return NULL if not found */
TranslationBlock *tb_find_pc(unsigned long tc_ptr)
{
    int m_min, m_max, m;
    unsigned long v;
    TranslationBlock *tb;

    if (nb_tbs <= 0)
        return NULL;
    if (tc_ptr < (unsigned long)code_gen_buffer ||
        tc_ptr >= (unsigned long)code_gen_ptr)
        return NULL;
    /* binary search (cf Knuth) */
    m_min = 0;
    m_max = nb_tbs - 1;
    while (m_min <= m_max) {
        m = (m_min + m_max) >> 1;
        tb = &tbs[m];
        v = (unsigned long)tb->tc_ptr;
        if (v == tc_ptr)
            return tb;
        else if (tc_ptr < v) {
            m_max = m - 1;
        } else {
            m_min = m + 1;
        }
    } 
    return &tbs[m_max];
}

static void tb_reset_jump_recursive(TranslationBlock *tb);

static inline void tb_reset_jump_recursive2(TranslationBlock *tb, int n)
{
    TranslationBlock *tb1, *tb_next, **ptb;
    unsigned int n1;

    tb1 = tb->jmp_next[n];
    if (tb1 != NULL) {
        /* find head of list */
        for(;;) {
            n1 = (long)tb1 & 3;
            tb1 = (TranslationBlock *)((long)tb1 & ~3);
            if (n1 == 2)
                break;
            tb1 = tb1->jmp_next[n1];
        }
        /* we are now sure now that tb jumps to tb1 */
        tb_next = tb1;

        /* remove tb from the jmp_first list */
        ptb = &tb_next->jmp_first;
        for(;;) {
            tb1 = *ptb;
            n1 = (long)tb1 & 3;
            tb1 = (TranslationBlock *)((long)tb1 & ~3);
            if (n1 == n && tb1 == tb)
                break;
            ptb = &tb1->jmp_next[n1];
        }
        *ptb = tb->jmp_next[n];
        tb->jmp_next[n] = NULL;
        
        /* suppress the jump to next tb in generated code */
        tb_reset_jump(tb, n);

        /* suppress jumps in the tb on which we could have jump */
        tb_reset_jump_recursive(tb_next);
    }
}

static void tb_reset_jump_recursive(TranslationBlock *tb)
{
    tb_reset_jump_recursive2(tb, 0);
    tb_reset_jump_recursive2(tb, 1);
}

/* add a breakpoint. EXCP_DEBUG is returned by the CPU loop if a
   breakpoint is reached */
int cpu_breakpoint_insert(CPUState *env, uint32_t pc)
{
#if defined(TARGET_I386)
    int i;

    for(i = 0; i < env->nb_breakpoints; i++) {
        if (env->breakpoints[i] == pc)
            return 0;
    }

    if (env->nb_breakpoints >= MAX_BREAKPOINTS)
        return -1;
    env->breakpoints[env->nb_breakpoints++] = pc;
    tb_invalidate_page(pc);
    return 0;
#else
    return -1;
#endif
}

/* remove a breakpoint */
int cpu_breakpoint_remove(CPUState *env, uint32_t pc)
{
#if defined(TARGET_I386)
    int i;
    for(i = 0; i < env->nb_breakpoints; i++) {
        if (env->breakpoints[i] == pc)
            goto found;
    }
    return -1;
 found:
    memmove(&env->breakpoints[i], &env->breakpoints[i + 1],
            (env->nb_breakpoints - (i + 1)) * sizeof(env->breakpoints[0]));
    env->nb_breakpoints--;
    tb_invalidate_page(pc);
    return 0;
#else
    return -1;
#endif
}

/* enable or disable single step mode. EXCP_DEBUG is returned by the
   CPU loop after each instruction */
void cpu_single_step(CPUState *env, int enabled)
{
#if defined(TARGET_I386)
    if (env->singlestep_enabled != enabled) {
        env->singlestep_enabled = enabled;
        /* must flush all the translated code to avoid inconsistancies */
        tb_flush();
    }
#endif
}

/* enable or disable low levels log */
void cpu_set_log(int log_flags)
{
    loglevel = log_flags;
    if (loglevel && !logfile) {
        logfile = fopen(logfilename, "w");
        if (!logfile) {
            perror(logfilename);
            _exit(1);
        }
        setvbuf(logfile, NULL, _IOLBF, 0);
    }
}

void cpu_set_log_filename(const char *filename)
{
    logfilename = strdup(filename);
}

/* mask must never be zero */
void cpu_interrupt(CPUState *env, int mask)
{
    TranslationBlock *tb;
    
    env->interrupt_request |= mask;
    /* if the cpu is currently executing code, we must unlink it and
       all the potentially executing TB */
    tb = env->current_tb;
    if (tb) {
        tb_reset_jump_recursive(tb);
    }
}


void cpu_abort(CPUState *env, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    fprintf(stderr, "qemu: fatal: ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
#ifdef TARGET_I386
    cpu_x86_dump_state(env, stderr, X86_DUMP_FPU | X86_DUMP_CCOP);
#endif
    va_end(ap);
    abort();
}

#ifdef TARGET_I386
/* unmap all maped pages and flush all associated code */
void page_unmap(void)
{
    PageDesc *pmap;
    int i;

    for(i = 0; i < L1_SIZE; i++) {
        pmap = l1_map[i];
        if (pmap) {
#if !defined(CONFIG_SOFTMMU)
            PageDesc *p;
            unsigned long addr;
            int j, ret, j1;
            
            p = pmap;
            for(j = 0;j < L2_SIZE;) {
                if (p->flags & PAGE_VALID) {
                    addr = (i << (32 - L1_BITS)) | (j << TARGET_PAGE_BITS);
                    /* we try to find a range to make less syscalls */
                    j1 = j;
                    p++;
                    j++;
                    while (j < L2_SIZE && (p->flags & PAGE_VALID)) {
                        p++;
                        j++;
                    }
                    ret = munmap((void *)addr, (j - j1) << TARGET_PAGE_BITS);
                    if (ret != 0) {
                        fprintf(stderr, "Could not unmap page 0x%08lx\n", addr);
                        exit(1);
                    }
                } else {
                    p++;
                    j++;
                }
            }
#endif
            free(pmap);
            l1_map[i] = NULL;
        }
    }
    tb_flush();
}
#endif

void tlb_flush(CPUState *env)
{
#if !defined(CONFIG_USER_ONLY)
    int i;
    for(i = 0; i < CPU_TLB_SIZE; i++) {
        env->tlb_read[0][i].address = -1;
        env->tlb_write[0][i].address = -1;
        env->tlb_read[1][i].address = -1;
        env->tlb_write[1][i].address = -1;
    }
#endif
}

static inline void tlb_flush_entry(CPUTLBEntry *tlb_entry, uint32_t addr)
{
    if (addr == (tlb_entry->address & 
                 (TARGET_PAGE_MASK | TLB_INVALID_MASK)))
        tlb_entry->address = -1;
}

void tlb_flush_page(CPUState *env, uint32_t addr)
{
#if !defined(CONFIG_USER_ONLY)
    int i;

    addr &= TARGET_PAGE_MASK;
    i = (addr >> TARGET_PAGE_BITS) & (CPU_TLB_SIZE - 1);
    tlb_flush_entry(&env->tlb_read[0][i], addr);
    tlb_flush_entry(&env->tlb_write[0][i], addr);
    tlb_flush_entry(&env->tlb_read[1][i], addr);
    tlb_flush_entry(&env->tlb_write[1][i], addr);
#endif
}

/* make all write to page 'addr' trigger a TLB exception to detect
   self modifying code */
void tlb_flush_page_write(CPUState *env, uint32_t addr)
{
#if !defined(CONFIG_USER_ONLY)
    int i;

    addr &= TARGET_PAGE_MASK;
    i = (addr >> TARGET_PAGE_BITS) & (CPU_TLB_SIZE - 1);
    tlb_flush_entry(&env->tlb_write[0][i], addr);
    tlb_flush_entry(&env->tlb_write[1][i], addr);
#endif
}

static inline unsigned long *physpage_find_alloc(unsigned int page)
{
    unsigned long **lp, *p;
    unsigned int index, i;

    index = page >> TARGET_PAGE_BITS;
    lp = &l1_physmap[index >> L2_BITS];
    p = *lp;
    if (!p) {
        /* allocate if not found */
        p = malloc(sizeof(unsigned long) * L2_SIZE);
        for(i = 0; i < L2_SIZE; i++)
            p[i] = IO_MEM_UNASSIGNED;
        *lp = p;
    }
    return p + (index & (L2_SIZE - 1));
}

/* return NULL if no page defined (unused memory) */
unsigned long physpage_find(unsigned long page)
{
    unsigned long *p;
    unsigned int index;
    index = page >> TARGET_PAGE_BITS;
    p = l1_physmap[index >> L2_BITS];
    if (!p)
        return IO_MEM_UNASSIGNED;
    return p[index & (L2_SIZE - 1)];
}

/* register physical memory. 'size' must be a multiple of the target
   page size. If (phys_offset & ~TARGET_PAGE_MASK) != 0, then it is an
   io memory page */
void cpu_register_physical_memory(unsigned long start_addr, unsigned long size,
                                  long phys_offset)
{
    unsigned long addr, end_addr;
    unsigned long *p;

    end_addr = start_addr + size;
    for(addr = start_addr; addr < end_addr; addr += TARGET_PAGE_SIZE) {
        p = physpage_find_alloc(addr);
        *p = phys_offset;
        if ((phys_offset & ~TARGET_PAGE_MASK) == 0)
            phys_offset += TARGET_PAGE_SIZE;
    }
}

static uint32_t unassigned_mem_readb(uint32_t addr)
{
    return 0;
}

static void unassigned_mem_writeb(uint32_t addr, uint32_t val)
{
}

static CPUReadMemoryFunc *unassigned_mem_read[3] = {
    unassigned_mem_readb,
    unassigned_mem_readb,
    unassigned_mem_readb,
};

static CPUWriteMemoryFunc *unassigned_mem_write[3] = {
    unassigned_mem_writeb,
    unassigned_mem_writeb,
    unassigned_mem_writeb,
};


static void io_mem_init(void)
{
    io_mem_nb = 1;
    cpu_register_io_memory(0, unassigned_mem_read, unassigned_mem_write);
}

/* mem_read and mem_write are arrays of functions containing the
   function to access byte (index 0), word (index 1) and dword (index
   2). All functions must be supplied. If io_index is non zero, the
   corresponding io zone is modified. If it is zero, a new io zone is
   allocated. The return value can be used with
   cpu_register_physical_memory(). (-1) is returned if error. */
int cpu_register_io_memory(int io_index,
                           CPUReadMemoryFunc **mem_read,
                           CPUWriteMemoryFunc **mem_write)
{
    int i;

    if (io_index <= 0) {
        if (io_index >= IO_MEM_NB_ENTRIES)
            return -1;
        io_index = io_mem_nb++;
    } else {
        if (io_index >= IO_MEM_NB_ENTRIES)
            return -1;
    }
    
    for(i = 0;i < 3; i++) {
        io_mem_read[io_index][i] = mem_read[i];
        io_mem_write[io_index][i] = mem_write[i];
    }
    return io_index << IO_MEM_SHIFT;
}

#if !defined(CONFIG_USER_ONLY) 

#define MMUSUFFIX _cmmu
#define GETPC() NULL
#define env cpu_single_env

#define SHIFT 0
#include "softmmu_template.h"

#define SHIFT 1
#include "softmmu_template.h"

#define SHIFT 2
#include "softmmu_template.h"

#define SHIFT 3
#include "softmmu_template.h"

#undef env

#endif
