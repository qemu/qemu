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
#include "config.h"
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <inttypes.h>
#if !defined(CONFIG_SOFTMMU)
#include <sys/mman.h>
#endif

#include "cpu.h"
#include "exec-all.h"

//#define DEBUG_TB_INVALIDATE
//#define DEBUG_FLUSH
//#define DEBUG_TLB

/* make various TB consistency checks */
//#define DEBUG_TB_CHECK 
//#define DEBUG_TLB_CHECK 

/* threshold to flush the translated code buffer */
#define CODE_GEN_BUFFER_MAX_SIZE (CODE_GEN_BUFFER_SIZE - CODE_GEN_MAX_SIZE)

#define SMC_BITMAP_USE_THRESHOLD 10

#define MMAP_AREA_START        0x00000000
#define MMAP_AREA_END          0xa8000000

TranslationBlock tbs[CODE_GEN_MAX_BLOCKS];
TranslationBlock *tb_hash[CODE_GEN_HASH_SIZE];
TranslationBlock *tb_phys_hash[CODE_GEN_PHYS_HASH_SIZE];
int nb_tbs;
/* any access to the tbs or the page table must use this lock */
spinlock_t tb_lock = SPIN_LOCK_UNLOCKED;

uint8_t code_gen_buffer[CODE_GEN_BUFFER_SIZE];
uint8_t *code_gen_ptr;

int phys_ram_size;
int phys_ram_fd;
uint8_t *phys_ram_base;
uint8_t *phys_ram_dirty;

typedef struct PageDesc {
    /* offset in host memory of the page + io_index in the low 12 bits */
    unsigned long phys_offset;
    /* list of TBs intersecting this physical page */
    TranslationBlock *first_tb;
    /* in order to optimize self modifying code, we count the number
       of lookups we do to a given page to use a bitmap */
    unsigned int code_write_count;
    uint8_t *code_bitmap;
#if defined(CONFIG_USER_ONLY)
    unsigned long flags;
#endif
} PageDesc;

typedef struct VirtPageDesc {
    /* physical address of code page. It is valid only if 'valid_tag'
       matches 'virt_valid_tag' */ 
    target_ulong phys_addr; 
    unsigned int valid_tag;
#if !defined(CONFIG_SOFTMMU)
    /* original page access rights. It is valid only if 'valid_tag'
       matches 'virt_valid_tag' */
    unsigned int prot;
#endif
} VirtPageDesc;

#define L2_BITS 10
#define L1_BITS (32 - L2_BITS - TARGET_PAGE_BITS)

#define L1_SIZE (1 << L1_BITS)
#define L2_SIZE (1 << L2_BITS)

static void io_mem_init(void);

unsigned long real_host_page_size;
unsigned long host_page_bits;
unsigned long host_page_size;
unsigned long host_page_mask;

static PageDesc *l1_map[L1_SIZE];

#if !defined(CONFIG_USER_ONLY)
static VirtPageDesc *l1_virt_map[L1_SIZE];
static unsigned int virt_valid_tag;
#endif

/* io memory support */
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
#ifdef _WIN32
    real_host_page_size = 4096;
#else
    real_host_page_size = getpagesize();
#endif
    if (host_page_size == 0)
        host_page_size = real_host_page_size;
    if (host_page_size < TARGET_PAGE_SIZE)
        host_page_size = TARGET_PAGE_SIZE;
    host_page_bits = 0;
    while ((1 << host_page_bits) < host_page_size)
        host_page_bits++;
    host_page_mask = ~(host_page_size - 1);
#if !defined(CONFIG_USER_ONLY)
    virt_valid_tag = 1;
#endif
}

static inline PageDesc *page_find_alloc(unsigned int index)
{
    PageDesc **lp, *p;

    lp = &l1_map[index >> L2_BITS];
    p = *lp;
    if (!p) {
        /* allocate if not found */
        p = qemu_malloc(sizeof(PageDesc) * L2_SIZE);
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

#if !defined(CONFIG_USER_ONLY)
static void tlb_protect_code(CPUState *env, target_ulong addr);
static void tlb_unprotect_code_phys(CPUState *env, unsigned long phys_addr, target_ulong vaddr);

static inline VirtPageDesc *virt_page_find_alloc(unsigned int index)
{
    VirtPageDesc **lp, *p;

    lp = &l1_virt_map[index >> L2_BITS];
    p = *lp;
    if (!p) {
        /* allocate if not found */
        p = qemu_malloc(sizeof(VirtPageDesc) * L2_SIZE);
        memset(p, 0, sizeof(VirtPageDesc) * L2_SIZE);
        *lp = p;
    }
    return p + (index & (L2_SIZE - 1));
}

static inline VirtPageDesc *virt_page_find(unsigned int index)
{
    VirtPageDesc *p;

    p = l1_virt_map[index >> L2_BITS];
    if (!p)
        return 0;
    return p + (index & (L2_SIZE - 1));
}

static void virt_page_flush(void)
{
    int i, j;
    VirtPageDesc *p;
    
    virt_valid_tag++;

    if (virt_valid_tag == 0) {
        virt_valid_tag = 1;
        for(i = 0; i < L1_SIZE; i++) {
            p = l1_virt_map[i];
            if (p) {
                for(j = 0; j < L2_SIZE; j++)
                    p[j].valid_tag = 0;
            }
        }
    }
}
#else
static void virt_page_flush(void)
{
}
#endif

void cpu_exec_init(void)
{
    if (!code_gen_ptr) {
        code_gen_ptr = code_gen_buffer;
        page_init();
        io_mem_init();
    }
}

static inline void invalidate_page_bitmap(PageDesc *p)
{
    if (p->code_bitmap) {
        qemu_free(p->code_bitmap);
        p->code_bitmap = NULL;
    }
    p->code_write_count = 0;
}

/* set to NULL all the 'first_tb' fields in all PageDescs */
static void page_flush_tb(void)
{
    int i, j;
    PageDesc *p;

    for(i = 0; i < L1_SIZE; i++) {
        p = l1_map[i];
        if (p) {
            for(j = 0; j < L2_SIZE; j++) {
                p->first_tb = NULL;
                invalidate_page_bitmap(p);
                p++;
            }
        }
    }
}

/* flush all the translation blocks */
/* XXX: tb_flush is currently not thread safe */
void tb_flush(CPUState *env)
{
    int i;
#if defined(DEBUG_FLUSH)
    printf("qemu: flush code_size=%d nb_tbs=%d avg_tb_size=%d\n", 
           code_gen_ptr - code_gen_buffer, 
           nb_tbs, 
           nb_tbs > 0 ? (code_gen_ptr - code_gen_buffer) / nb_tbs : 0);
#endif
    nb_tbs = 0;
    for(i = 0;i < CODE_GEN_HASH_SIZE; i++)
        tb_hash[i] = NULL;
    virt_page_flush();

    for(i = 0;i < CODE_GEN_PHYS_HASH_SIZE; i++)
        tb_phys_hash[i] = NULL;
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

static inline void tb_page_remove(TranslationBlock **ptb, TranslationBlock *tb)
{
    TranslationBlock *tb1;
    unsigned int n1;

    for(;;) {
        tb1 = *ptb;
        n1 = (long)tb1 & 3;
        tb1 = (TranslationBlock *)((long)tb1 & ~3);
        if (tb1 == tb) {
            *ptb = tb1->page_next[n1];
            break;
        }
        ptb = &tb1->page_next[n1];
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

static inline void tb_invalidate(TranslationBlock *tb)
{
    unsigned int h, n1;
    TranslationBlock *tb1, *tb2, **ptb;
    
    tb_invalidated_flag = 1;

    /* remove the TB from the hash list */
    h = tb_hash_func(tb->pc);
    ptb = &tb_hash[h];
    for(;;) {
        tb1 = *ptb;
        /* NOTE: the TB is not necessarily linked in the hash. It
           indicates that it is not currently used */
        if (tb1 == NULL)
            return;
        if (tb1 == tb) {
            *ptb = tb1->hash_next;
            break;
        }
        ptb = &tb1->hash_next;
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

static inline void tb_phys_invalidate(TranslationBlock *tb, unsigned int page_addr)
{
    PageDesc *p;
    unsigned int h;
    target_ulong phys_pc;
    
    /* remove the TB from the hash list */
    phys_pc = tb->page_addr[0] + (tb->pc & ~TARGET_PAGE_MASK);
    h = tb_phys_hash_func(phys_pc);
    tb_remove(&tb_phys_hash[h], tb, 
              offsetof(TranslationBlock, phys_hash_next));

    /* remove the TB from the page list */
    if (tb->page_addr[0] != page_addr) {
        p = page_find(tb->page_addr[0] >> TARGET_PAGE_BITS);
        tb_page_remove(&p->first_tb, tb);
        invalidate_page_bitmap(p);
    }
    if (tb->page_addr[1] != -1 && tb->page_addr[1] != page_addr) {
        p = page_find(tb->page_addr[1] >> TARGET_PAGE_BITS);
        tb_page_remove(&p->first_tb, tb);
        invalidate_page_bitmap(p);
    }

    tb_invalidate(tb);
}

static inline void set_bits(uint8_t *tab, int start, int len)
{
    int end, mask, end1;

    end = start + len;
    tab += start >> 3;
    mask = 0xff << (start & 7);
    if ((start & ~7) == (end & ~7)) {
        if (start < end) {
            mask &= ~(0xff << (end & 7));
            *tab |= mask;
        }
    } else {
        *tab++ |= mask;
        start = (start + 8) & ~7;
        end1 = end & ~7;
        while (start < end1) {
            *tab++ = 0xff;
            start += 8;
        }
        if (start < end) {
            mask = ~(0xff << (end & 7));
            *tab |= mask;
        }
    }
}

static void build_page_bitmap(PageDesc *p)
{
    int n, tb_start, tb_end;
    TranslationBlock *tb;
    
    p->code_bitmap = qemu_malloc(TARGET_PAGE_SIZE / 8);
    if (!p->code_bitmap)
        return;
    memset(p->code_bitmap, 0, TARGET_PAGE_SIZE / 8);

    tb = p->first_tb;
    while (tb != NULL) {
        n = (long)tb & 3;
        tb = (TranslationBlock *)((long)tb & ~3);
        /* NOTE: this is subtle as a TB may span two physical pages */
        if (n == 0) {
            /* NOTE: tb_end may be after the end of the page, but
               it is not a problem */
            tb_start = tb->pc & ~TARGET_PAGE_MASK;
            tb_end = tb_start + tb->size;
            if (tb_end > TARGET_PAGE_SIZE)
                tb_end = TARGET_PAGE_SIZE;
        } else {
            tb_start = 0;
            tb_end = ((tb->pc + tb->size) & ~TARGET_PAGE_MASK);
        }
        set_bits(p->code_bitmap, tb_start, tb_end - tb_start);
        tb = tb->page_next[n];
    }
}

#ifdef TARGET_HAS_PRECISE_SMC

static void tb_gen_code(CPUState *env, 
                        target_ulong pc, target_ulong cs_base, int flags,
                        int cflags)
{
    TranslationBlock *tb;
    uint8_t *tc_ptr;
    target_ulong phys_pc, phys_page2, virt_page2;
    int code_gen_size;

    phys_pc = get_phys_addr_code(env, (unsigned long)pc);
    tb = tb_alloc((unsigned long)pc);
    if (!tb) {
        /* flush must be done */
        tb_flush(env);
        /* cannot fail at this point */
        tb = tb_alloc((unsigned long)pc);
    }
    tc_ptr = code_gen_ptr;
    tb->tc_ptr = tc_ptr;
    tb->cs_base = cs_base;
    tb->flags = flags;
    tb->cflags = cflags;
    cpu_gen_code(env, tb, CODE_GEN_MAX_SIZE, &code_gen_size);
    code_gen_ptr = (void *)(((unsigned long)code_gen_ptr + code_gen_size + CODE_GEN_ALIGN - 1) & ~(CODE_GEN_ALIGN - 1));
    
    /* check next page if needed */
    virt_page2 = ((unsigned long)pc + tb->size - 1) & TARGET_PAGE_MASK;
    phys_page2 = -1;
    if (((unsigned long)pc & TARGET_PAGE_MASK) != virt_page2) {
        phys_page2 = get_phys_addr_code(env, virt_page2);
    }
    tb_link_phys(tb, phys_pc, phys_page2);
}
#endif
    
/* invalidate all TBs which intersect with the target physical page
   starting in range [start;end[. NOTE: start and end must refer to
   the same physical page. 'is_cpu_write_access' should be true if called
   from a real cpu write access: the virtual CPU will exit the current
   TB if code is modified inside this TB. */
void tb_invalidate_phys_page_range(target_ulong start, target_ulong end, 
                                   int is_cpu_write_access)
{
    int n, current_tb_modified, current_tb_not_found, current_flags;
#if defined(TARGET_HAS_PRECISE_SMC) || !defined(CONFIG_USER_ONLY)
    CPUState *env = cpu_single_env;
#endif
    PageDesc *p;
    TranslationBlock *tb, *tb_next, *current_tb;
    target_ulong tb_start, tb_end;
    target_ulong current_pc, current_cs_base;

    p = page_find(start >> TARGET_PAGE_BITS);
    if (!p) 
        return;
    if (!p->code_bitmap && 
        ++p->code_write_count >= SMC_BITMAP_USE_THRESHOLD &&
        is_cpu_write_access) {
        /* build code bitmap */
        build_page_bitmap(p);
    }

    /* we remove all the TBs in the range [start, end[ */
    /* XXX: see if in some cases it could be faster to invalidate all the code */
    current_tb_not_found = is_cpu_write_access;
    current_tb_modified = 0;
    current_tb = NULL; /* avoid warning */
    current_pc = 0; /* avoid warning */
    current_cs_base = 0; /* avoid warning */
    current_flags = 0; /* avoid warning */
    tb = p->first_tb;
    while (tb != NULL) {
        n = (long)tb & 3;
        tb = (TranslationBlock *)((long)tb & ~3);
        tb_next = tb->page_next[n];
        /* NOTE: this is subtle as a TB may span two physical pages */
        if (n == 0) {
            /* NOTE: tb_end may be after the end of the page, but
               it is not a problem */
            tb_start = tb->page_addr[0] + (tb->pc & ~TARGET_PAGE_MASK);
            tb_end = tb_start + tb->size;
        } else {
            tb_start = tb->page_addr[1];
            tb_end = tb_start + ((tb->pc + tb->size) & ~TARGET_PAGE_MASK);
        }
        if (!(tb_end <= start || tb_start >= end)) {
#ifdef TARGET_HAS_PRECISE_SMC
            if (current_tb_not_found) {
                current_tb_not_found = 0;
                current_tb = NULL;
                if (env->mem_write_pc) {
                    /* now we have a real cpu fault */
                    current_tb = tb_find_pc(env->mem_write_pc);
                }
            }
            if (current_tb == tb &&
                !(current_tb->cflags & CF_SINGLE_INSN)) {
                /* If we are modifying the current TB, we must stop
                its execution. We could be more precise by checking
                that the modification is after the current PC, but it
                would require a specialized function to partially
                restore the CPU state */
                
                current_tb_modified = 1;
                cpu_restore_state(current_tb, env, 
                                  env->mem_write_pc, NULL);
#if defined(TARGET_I386)
                current_flags = env->hflags;
                current_flags |= (env->eflags & (IOPL_MASK | TF_MASK | VM_MASK));
                current_cs_base = (target_ulong)env->segs[R_CS].base;
                current_pc = current_cs_base + env->eip;
#else
#error unsupported CPU
#endif
            }
#endif /* TARGET_HAS_PRECISE_SMC */
            tb_phys_invalidate(tb, -1);
        }
        tb = tb_next;
    }
#if !defined(CONFIG_USER_ONLY)
    /* if no code remaining, no need to continue to use slow writes */
    if (!p->first_tb) {
        invalidate_page_bitmap(p);
        if (is_cpu_write_access) {
            tlb_unprotect_code_phys(env, start, env->mem_write_vaddr);
        }
    }
#endif
#ifdef TARGET_HAS_PRECISE_SMC
    if (current_tb_modified) {
        /* we generate a block containing just the instruction
           modifying the memory. It will ensure that it cannot modify
           itself */
        tb_gen_code(env, current_pc, current_cs_base, current_flags, 
                    CF_SINGLE_INSN);
        cpu_resume_from_signal(env, NULL);
    }
#endif
}

/* len must be <= 8 and start must be a multiple of len */
static inline void tb_invalidate_phys_page_fast(target_ulong start, int len)
{
    PageDesc *p;
    int offset, b;
#if 0
    if (cpu_single_env->cr[0] & CR0_PE_MASK) {
        printf("modifying code at 0x%x size=%d EIP=%x\n", 
               (vaddr & TARGET_PAGE_MASK) | (start & ~TARGET_PAGE_MASK), len, 
               cpu_single_env->eip);
    }
#endif
    p = page_find(start >> TARGET_PAGE_BITS);
    if (!p) 
        return;
    if (p->code_bitmap) {
        offset = start & ~TARGET_PAGE_MASK;
        b = p->code_bitmap[offset >> 3] >> (offset & 7);
        if (b & ((1 << len) - 1))
            goto do_invalidate;
    } else {
    do_invalidate:
        tb_invalidate_phys_page_range(start, start + len, 1);
    }
}

#if !defined(CONFIG_SOFTMMU)
static void tb_invalidate_phys_page(target_ulong addr, 
                                    unsigned long pc, void *puc)
{
    int n, current_flags, current_tb_modified;
    target_ulong current_pc, current_cs_base;
    PageDesc *p;
    TranslationBlock *tb, *current_tb;
#ifdef TARGET_HAS_PRECISE_SMC
    CPUState *env = cpu_single_env;
#endif

    addr &= TARGET_PAGE_MASK;
    p = page_find(addr >> TARGET_PAGE_BITS);
    if (!p) 
        return;
    tb = p->first_tb;
    current_tb_modified = 0;
    current_tb = NULL;
    current_pc = 0; /* avoid warning */
    current_cs_base = 0; /* avoid warning */
    current_flags = 0; /* avoid warning */
#ifdef TARGET_HAS_PRECISE_SMC
    if (tb && pc != 0) {
        current_tb = tb_find_pc(pc);
    }
#endif
    while (tb != NULL) {
        n = (long)tb & 3;
        tb = (TranslationBlock *)((long)tb & ~3);
#ifdef TARGET_HAS_PRECISE_SMC
        if (current_tb == tb &&
            !(current_tb->cflags & CF_SINGLE_INSN)) {
                /* If we are modifying the current TB, we must stop
                   its execution. We could be more precise by checking
                   that the modification is after the current PC, but it
                   would require a specialized function to partially
                   restore the CPU state */
            
            current_tb_modified = 1;
            cpu_restore_state(current_tb, env, pc, puc);
#if defined(TARGET_I386)
            current_flags = env->hflags;
            current_flags |= (env->eflags & (IOPL_MASK | TF_MASK | VM_MASK));
            current_cs_base = (target_ulong)env->segs[R_CS].base;
            current_pc = current_cs_base + env->eip;
#else
#error unsupported CPU
#endif
        }
#endif /* TARGET_HAS_PRECISE_SMC */
        tb_phys_invalidate(tb, addr);
        tb = tb->page_next[n];
    }
    p->first_tb = NULL;
#ifdef TARGET_HAS_PRECISE_SMC
    if (current_tb_modified) {
        /* we generate a block containing just the instruction
           modifying the memory. It will ensure that it cannot modify
           itself */
        tb_gen_code(env, current_pc, current_cs_base, current_flags, 
                    CF_SINGLE_INSN);
        cpu_resume_from_signal(env, puc);
    }
#endif
}
#endif

/* add the tb in the target page and protect it if necessary */
static inline void tb_alloc_page(TranslationBlock *tb, 
                                 unsigned int n, unsigned int page_addr)
{
    PageDesc *p;
    TranslationBlock *last_first_tb;

    tb->page_addr[n] = page_addr;
    p = page_find(page_addr >> TARGET_PAGE_BITS);
    tb->page_next[n] = p->first_tb;
    last_first_tb = p->first_tb;
    p->first_tb = (TranslationBlock *)((long)tb | n);
    invalidate_page_bitmap(p);

#ifdef TARGET_HAS_SMC

#if defined(CONFIG_USER_ONLY)
    if (p->flags & PAGE_WRITE) {
        unsigned long host_start, host_end, addr;
        int prot;

        /* force the host page as non writable (writes will have a
           page fault + mprotect overhead) */
        host_start = page_addr & host_page_mask;
        host_end = host_start + host_page_size;
        prot = 0;
        for(addr = host_start; addr < host_end; addr += TARGET_PAGE_SIZE)
            prot |= page_get_flags(addr);
        mprotect((void *)host_start, host_page_size, 
                 (prot & PAGE_BITS) & ~PAGE_WRITE);
#ifdef DEBUG_TB_INVALIDATE
        printf("protecting code page: 0x%08lx\n", 
               host_start);
#endif
        p->flags &= ~PAGE_WRITE;
    }
#else
    /* if some code is already present, then the pages are already
       protected. So we handle the case where only the first TB is
       allocated in a physical page */
    if (!last_first_tb) {
        target_ulong virt_addr;

        virt_addr = (tb->pc & TARGET_PAGE_MASK) + (n << TARGET_PAGE_BITS);
        tlb_protect_code(cpu_single_env, virt_addr);        
    }
#endif

#endif /* TARGET_HAS_SMC */
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
    tb->cflags = 0;
    return tb;
}

/* add a new TB and link it to the physical page tables. phys_page2 is
   (-1) to indicate that only one page contains the TB. */
void tb_link_phys(TranslationBlock *tb, 
                  target_ulong phys_pc, target_ulong phys_page2)
{
    unsigned int h;
    TranslationBlock **ptb;

    /* add in the physical hash table */
    h = tb_phys_hash_func(phys_pc);
    ptb = &tb_phys_hash[h];
    tb->phys_hash_next = *ptb;
    *ptb = tb;

    /* add in the page list */
    tb_alloc_page(tb, 0, phys_pc & TARGET_PAGE_MASK);
    if (phys_page2 != -1)
        tb_alloc_page(tb, 1, phys_page2);
    else
        tb->page_addr[1] = -1;
#ifdef DEBUG_TB_CHECK
    tb_page_check();
#endif
}

/* link the tb with the other TBs */
void tb_link(TranslationBlock *tb)
{
#if !defined(CONFIG_USER_ONLY)
    {
        VirtPageDesc *vp;
        target_ulong addr;
        
        /* save the code memory mappings (needed to invalidate the code) */
        addr = tb->pc & TARGET_PAGE_MASK;
        vp = virt_page_find_alloc(addr >> TARGET_PAGE_BITS);
#ifdef DEBUG_TLB_CHECK 
        if (vp->valid_tag == virt_valid_tag &&
            vp->phys_addr != tb->page_addr[0]) {
            printf("Error tb addr=0x%x phys=0x%x vp->phys_addr=0x%x\n",
                   addr, tb->page_addr[0], vp->phys_addr);
        }
#endif
        vp->phys_addr = tb->page_addr[0];
        if (vp->valid_tag != virt_valid_tag) {
            vp->valid_tag = virt_valid_tag;
#if !defined(CONFIG_SOFTMMU)
            vp->prot = 0;
#endif
        }
        
        if (tb->page_addr[1] != -1) {
            addr += TARGET_PAGE_SIZE;
            vp = virt_page_find_alloc(addr >> TARGET_PAGE_BITS);
#ifdef DEBUG_TLB_CHECK 
            if (vp->valid_tag == virt_valid_tag &&
                vp->phys_addr != tb->page_addr[1]) { 
                printf("Error tb addr=0x%x phys=0x%x vp->phys_addr=0x%x\n",
                       addr, tb->page_addr[1], vp->phys_addr);
            }
#endif
            vp->phys_addr = tb->page_addr[1];
            if (vp->valid_tag != virt_valid_tag) {
                vp->valid_tag = virt_valid_tag;
#if !defined(CONFIG_SOFTMMU)
                vp->prot = 0;
#endif
            }
        }
    }
#endif

    tb->jmp_first = (TranslationBlock *)((long)tb | 2);
    tb->jmp_next[0] = NULL;
    tb->jmp_next[1] = NULL;
#ifdef USE_CODE_COPY
    tb->cflags &= ~CF_FP_USED;
    if (tb->cflags & CF_TB_FP_USED)
        tb->cflags |= CF_FP_USED;
#endif

    /* init original jump addresses */
    if (tb->tb_next_offset[0] != 0xffff)
        tb_reset_jump(tb, 0);
    if (tb->tb_next_offset[1] != 0xffff)
        tb_reset_jump(tb, 1);
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

        /* suppress jumps in the tb on which we could have jumped */
        tb_reset_jump_recursive(tb_next);
    }
}

static void tb_reset_jump_recursive(TranslationBlock *tb)
{
    tb_reset_jump_recursive2(tb, 0);
    tb_reset_jump_recursive2(tb, 1);
}

static void breakpoint_invalidate(CPUState *env, target_ulong pc)
{
    target_ulong phys_addr;

    phys_addr = cpu_get_phys_page_debug(env, pc);
    tb_invalidate_phys_page_range(phys_addr, phys_addr + 1, 0);
}

/* add a breakpoint. EXCP_DEBUG is returned by the CPU loop if a
   breakpoint is reached */
int cpu_breakpoint_insert(CPUState *env, target_ulong pc)
{
#if defined(TARGET_I386) || defined(TARGET_PPC)
    int i;
    
    for(i = 0; i < env->nb_breakpoints; i++) {
        if (env->breakpoints[i] == pc)
            return 0;
    }

    if (env->nb_breakpoints >= MAX_BREAKPOINTS)
        return -1;
    env->breakpoints[env->nb_breakpoints++] = pc;
    
    breakpoint_invalidate(env, pc);
    return 0;
#else
    return -1;
#endif
}

/* remove a breakpoint */
int cpu_breakpoint_remove(CPUState *env, target_ulong pc)
{
#if defined(TARGET_I386) || defined(TARGET_PPC)
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

    breakpoint_invalidate(env, pc);
    return 0;
#else
    return -1;
#endif
}

/* enable or disable single step mode. EXCP_DEBUG is returned by the
   CPU loop after each instruction */
void cpu_single_step(CPUState *env, int enabled)
{
#if defined(TARGET_I386) || defined(TARGET_PPC)
    if (env->singlestep_enabled != enabled) {
        env->singlestep_enabled = enabled;
        /* must flush all the translated code to avoid inconsistancies */
        /* XXX: only flush what is necessary */
        tb_flush(env);
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
#if !defined(CONFIG_SOFTMMU)
        /* must avoid mmap() usage of glibc by setting a buffer "by hand" */
        {
            static uint8_t logfile_buf[4096];
            setvbuf(logfile, logfile_buf, _IOLBF, sizeof(logfile_buf));
        }
#else
        setvbuf(logfile, NULL, _IOLBF, 0);
#endif
    }
}

void cpu_set_log_filename(const char *filename)
{
    logfilename = strdup(filename);
}

/* mask must never be zero, except for A20 change call */
void cpu_interrupt(CPUState *env, int mask)
{
    TranslationBlock *tb;
    static int interrupt_lock;

    env->interrupt_request |= mask;
    /* if the cpu is currently executing code, we must unlink it and
       all the potentially executing TB */
    tb = env->current_tb;
    if (tb && !testandset(&interrupt_lock)) {
        env->current_tb = NULL;
        tb_reset_jump_recursive(tb);
        interrupt_lock = 0;
    }
}

void cpu_reset_interrupt(CPUState *env, int mask)
{
    env->interrupt_request &= ~mask;
}

CPULogItem cpu_log_items[] = {
    { CPU_LOG_TB_OUT_ASM, "out_asm", 
      "show generated host assembly code for each compiled TB" },
    { CPU_LOG_TB_IN_ASM, "in_asm",
      "show target assembly code for each compiled TB" },
    { CPU_LOG_TB_OP, "op", 
      "show micro ops for each compiled TB (only usable if 'in_asm' used)" },
#ifdef TARGET_I386
    { CPU_LOG_TB_OP_OPT, "op_opt",
      "show micro ops after optimization for each compiled TB" },
#endif
    { CPU_LOG_INT, "int",
      "show interrupts/exceptions in short format" },
    { CPU_LOG_EXEC, "exec",
      "show trace before each executed TB (lots of logs)" },
#ifdef TARGET_I386
    { CPU_LOG_PCALL, "pcall",
      "show protected mode far calls/returns/exceptions" },
#endif
    { CPU_LOG_IOPORT, "ioport",
      "show all i/o ports accesses" },
    { 0, NULL, NULL },
};

static int cmp1(const char *s1, int n, const char *s2)
{
    if (strlen(s2) != n)
        return 0;
    return memcmp(s1, s2, n) == 0;
}
      
/* takes a comma separated list of log masks. Return 0 if error. */
int cpu_str_to_log_mask(const char *str)
{
    CPULogItem *item;
    int mask;
    const char *p, *p1;

    p = str;
    mask = 0;
    for(;;) {
        p1 = strchr(p, ',');
        if (!p1)
            p1 = p + strlen(p);
        for(item = cpu_log_items; item->mask != 0; item++) {
            if (cmp1(p, p1 - p, item->name))
                goto found;
        }
        return 0;
    found:
        mask |= item->mask;
        if (*p1 != ',')
            break;
        p = p1 + 1;
    }
    return mask;
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

#if !defined(CONFIG_USER_ONLY)

/* NOTE: if flush_global is true, also flush global entries (not
   implemented yet) */
void tlb_flush(CPUState *env, int flush_global)
{
    int i;

#if defined(DEBUG_TLB)
    printf("tlb_flush:\n");
#endif
    /* must reset current TB so that interrupts cannot modify the
       links while we are modifying them */
    env->current_tb = NULL;

    for(i = 0; i < CPU_TLB_SIZE; i++) {
        env->tlb_read[0][i].address = -1;
        env->tlb_write[0][i].address = -1;
        env->tlb_read[1][i].address = -1;
        env->tlb_write[1][i].address = -1;
    }

    virt_page_flush();
    for(i = 0;i < CODE_GEN_HASH_SIZE; i++)
        tb_hash[i] = NULL;

#if !defined(CONFIG_SOFTMMU)
    munmap((void *)MMAP_AREA_START, MMAP_AREA_END - MMAP_AREA_START);
#endif
}

static inline void tlb_flush_entry(CPUTLBEntry *tlb_entry, uint32_t addr)
{
    if (addr == (tlb_entry->address & 
                 (TARGET_PAGE_MASK | TLB_INVALID_MASK)))
        tlb_entry->address = -1;
}

void tlb_flush_page(CPUState *env, target_ulong addr)
{
    int i, n;
    VirtPageDesc *vp;
    PageDesc *p;
    TranslationBlock *tb;

#if defined(DEBUG_TLB)
    printf("tlb_flush_page: 0x%08x\n", addr);
#endif
    /* must reset current TB so that interrupts cannot modify the
       links while we are modifying them */
    env->current_tb = NULL;

    addr &= TARGET_PAGE_MASK;
    i = (addr >> TARGET_PAGE_BITS) & (CPU_TLB_SIZE - 1);
    tlb_flush_entry(&env->tlb_read[0][i], addr);
    tlb_flush_entry(&env->tlb_write[0][i], addr);
    tlb_flush_entry(&env->tlb_read[1][i], addr);
    tlb_flush_entry(&env->tlb_write[1][i], addr);

    /* remove from the virtual pc hash table all the TB at this
       virtual address */
    
    vp = virt_page_find(addr >> TARGET_PAGE_BITS);
    if (vp && vp->valid_tag == virt_valid_tag) {
        p = page_find(vp->phys_addr >> TARGET_PAGE_BITS);
        if (p) {
            /* we remove all the links to the TBs in this virtual page */
            tb = p->first_tb;
            while (tb != NULL) {
                n = (long)tb & 3;
                tb = (TranslationBlock *)((long)tb & ~3);
                if ((tb->pc & TARGET_PAGE_MASK) == addr ||
                    ((tb->pc + tb->size - 1) & TARGET_PAGE_MASK) == addr) {
                    tb_invalidate(tb);
                }
                tb = tb->page_next[n];
            }
        }
        vp->valid_tag = 0;
    }

#if !defined(CONFIG_SOFTMMU)
    if (addr < MMAP_AREA_END)
        munmap((void *)addr, TARGET_PAGE_SIZE);
#endif
}

static inline void tlb_protect_code1(CPUTLBEntry *tlb_entry, target_ulong addr)
{
    if (addr == (tlb_entry->address & 
                 (TARGET_PAGE_MASK | TLB_INVALID_MASK)) &&
        (tlb_entry->address & ~TARGET_PAGE_MASK) != IO_MEM_CODE &&
        (tlb_entry->address & ~TARGET_PAGE_MASK) != IO_MEM_ROM) {
        tlb_entry->address = (tlb_entry->address & TARGET_PAGE_MASK) | IO_MEM_CODE;
    }
}

/* update the TLBs so that writes to code in the virtual page 'addr'
   can be detected */
static void tlb_protect_code(CPUState *env, target_ulong addr)
{
    int i;

    addr &= TARGET_PAGE_MASK;
    i = (addr >> TARGET_PAGE_BITS) & (CPU_TLB_SIZE - 1);
    tlb_protect_code1(&env->tlb_write[0][i], addr);
    tlb_protect_code1(&env->tlb_write[1][i], addr);
#if !defined(CONFIG_SOFTMMU)
    /* NOTE: as we generated the code for this page, it is already at
       least readable */
    if (addr < MMAP_AREA_END)
        mprotect((void *)addr, TARGET_PAGE_SIZE, PROT_READ);
#endif
}

static inline void tlb_unprotect_code2(CPUTLBEntry *tlb_entry, 
                                       unsigned long phys_addr)
{
    if ((tlb_entry->address & ~TARGET_PAGE_MASK) == IO_MEM_CODE &&
        ((tlb_entry->address & TARGET_PAGE_MASK) + tlb_entry->addend) == phys_addr) {
        tlb_entry->address = (tlb_entry->address & TARGET_PAGE_MASK) | IO_MEM_NOTDIRTY;
    }
}

/* update the TLB so that writes in physical page 'phys_addr' are no longer
   tested self modifying code */
static void tlb_unprotect_code_phys(CPUState *env, unsigned long phys_addr, target_ulong vaddr)
{
    int i;

    phys_addr &= TARGET_PAGE_MASK;
    phys_addr += (long)phys_ram_base;
    i = (vaddr >> TARGET_PAGE_BITS) & (CPU_TLB_SIZE - 1);
    tlb_unprotect_code2(&env->tlb_write[0][i], phys_addr);
    tlb_unprotect_code2(&env->tlb_write[1][i], phys_addr);
}

static inline void tlb_reset_dirty_range(CPUTLBEntry *tlb_entry, 
                                         unsigned long start, unsigned long length)
{
    unsigned long addr;
    if ((tlb_entry->address & ~TARGET_PAGE_MASK) == IO_MEM_RAM) {
        addr = (tlb_entry->address & TARGET_PAGE_MASK) + tlb_entry->addend;
        if ((addr - start) < length) {
            tlb_entry->address = (tlb_entry->address & TARGET_PAGE_MASK) | IO_MEM_NOTDIRTY;
        }
    }
}

void cpu_physical_memory_reset_dirty(target_ulong start, target_ulong end)
{
    CPUState *env;
    unsigned long length, start1;
    int i;

    start &= TARGET_PAGE_MASK;
    end = TARGET_PAGE_ALIGN(end);

    length = end - start;
    if (length == 0)
        return;
    memset(phys_ram_dirty + (start >> TARGET_PAGE_BITS), 0, length >> TARGET_PAGE_BITS);

    env = cpu_single_env;
    /* we modify the TLB cache so that the dirty bit will be set again
       when accessing the range */
    start1 = start + (unsigned long)phys_ram_base;
    for(i = 0; i < CPU_TLB_SIZE; i++)
        tlb_reset_dirty_range(&env->tlb_write[0][i], start1, length);
    for(i = 0; i < CPU_TLB_SIZE; i++)
        tlb_reset_dirty_range(&env->tlb_write[1][i], start1, length);

#if !defined(CONFIG_SOFTMMU)
    /* XXX: this is expensive */
    {
        VirtPageDesc *p;
        int j;
        target_ulong addr;

        for(i = 0; i < L1_SIZE; i++) {
            p = l1_virt_map[i];
            if (p) {
                addr = i << (TARGET_PAGE_BITS + L2_BITS);
                for(j = 0; j < L2_SIZE; j++) {
                    if (p->valid_tag == virt_valid_tag &&
                        p->phys_addr >= start && p->phys_addr < end &&
                        (p->prot & PROT_WRITE)) {
                        if (addr < MMAP_AREA_END) {
                            mprotect((void *)addr, TARGET_PAGE_SIZE, 
                                     p->prot & ~PROT_WRITE);
                        }
                    }
                    addr += TARGET_PAGE_SIZE;
                    p++;
                }
            }
        }
    }
#endif
}

static inline void tlb_set_dirty1(CPUTLBEntry *tlb_entry, 
                                    unsigned long start)
{
    unsigned long addr;
    if ((tlb_entry->address & ~TARGET_PAGE_MASK) == IO_MEM_NOTDIRTY) {
        addr = (tlb_entry->address & TARGET_PAGE_MASK) + tlb_entry->addend;
        if (addr == start) {
            tlb_entry->address = (tlb_entry->address & TARGET_PAGE_MASK) | IO_MEM_RAM;
        }
    }
}

/* update the TLB corresponding to virtual page vaddr and phys addr
   addr so that it is no longer dirty */
static inline void tlb_set_dirty(unsigned long addr, target_ulong vaddr)
{
    CPUState *env = cpu_single_env;
    int i;

    phys_ram_dirty[(addr - (unsigned long)phys_ram_base) >> TARGET_PAGE_BITS] = 1;

    addr &= TARGET_PAGE_MASK;
    i = (vaddr >> TARGET_PAGE_BITS) & (CPU_TLB_SIZE - 1);
    tlb_set_dirty1(&env->tlb_write[0][i], addr);
    tlb_set_dirty1(&env->tlb_write[1][i], addr);
}

/* add a new TLB entry. At most one entry for a given virtual address
   is permitted. Return 0 if OK or 2 if the page could not be mapped
   (can only happen in non SOFTMMU mode for I/O pages or pages
   conflicting with the host address space). */
int tlb_set_page(CPUState *env, target_ulong vaddr, 
                 target_phys_addr_t paddr, int prot, 
                 int is_user, int is_softmmu)
{
    PageDesc *p;
    unsigned long pd;
    TranslationBlock *first_tb;
    unsigned int index;
    target_ulong address;
    unsigned long addend;
    int ret;

    p = page_find(paddr >> TARGET_PAGE_BITS);
    if (!p) {
        pd = IO_MEM_UNASSIGNED;
        first_tb = NULL;
    } else {
        pd = p->phys_offset;
        first_tb = p->first_tb;
    }
#if defined(DEBUG_TLB)
    printf("tlb_set_page: vaddr=0x%08x paddr=0x%08x prot=%x u=%d c=%d smmu=%d pd=0x%08x\n",
           vaddr, paddr, prot, is_user, (first_tb != NULL), is_softmmu, pd);
#endif

    ret = 0;
#if !defined(CONFIG_SOFTMMU)
    if (is_softmmu) 
#endif
    {
        if ((pd & ~TARGET_PAGE_MASK) > IO_MEM_ROM) {
            /* IO memory case */
            address = vaddr | pd;
            addend = paddr;
        } else {
            /* standard memory */
            address = vaddr;
            addend = (unsigned long)phys_ram_base + (pd & TARGET_PAGE_MASK);
        }
        
        index = (vaddr >> 12) & (CPU_TLB_SIZE - 1);
        addend -= vaddr;
        if (prot & PAGE_READ) {
            env->tlb_read[is_user][index].address = address;
            env->tlb_read[is_user][index].addend = addend;
        } else {
            env->tlb_read[is_user][index].address = -1;
            env->tlb_read[is_user][index].addend = -1;
        }
        if (prot & PAGE_WRITE) {
            if ((pd & ~TARGET_PAGE_MASK) == IO_MEM_ROM) {
                /* ROM: access is ignored (same as unassigned) */
                env->tlb_write[is_user][index].address = vaddr | IO_MEM_ROM;
                env->tlb_write[is_user][index].addend = addend;
            } else 
                /* XXX: the PowerPC code seems not ready to handle
                   self modifying code with DCBI */
#if defined(TARGET_HAS_SMC) || 1
            if (first_tb) {
                /* if code is present, we use a specific memory
                   handler. It works only for physical memory access */
                env->tlb_write[is_user][index].address = vaddr | IO_MEM_CODE;
                env->tlb_write[is_user][index].addend = addend;
            } else 
#endif
            if ((pd & ~TARGET_PAGE_MASK) == IO_MEM_RAM && 
                       !cpu_physical_memory_is_dirty(pd)) {
                env->tlb_write[is_user][index].address = vaddr | IO_MEM_NOTDIRTY;
                env->tlb_write[is_user][index].addend = addend;
            } else {
                env->tlb_write[is_user][index].address = address;
                env->tlb_write[is_user][index].addend = addend;
            }
        } else {
            env->tlb_write[is_user][index].address = -1;
            env->tlb_write[is_user][index].addend = -1;
        }
    }
#if !defined(CONFIG_SOFTMMU)
    else {
        if ((pd & ~TARGET_PAGE_MASK) > IO_MEM_ROM) {
            /* IO access: no mapping is done as it will be handled by the
               soft MMU */
            if (!(env->hflags & HF_SOFTMMU_MASK))
                ret = 2;
        } else {
            void *map_addr;

            if (vaddr >= MMAP_AREA_END) {
                ret = 2;
            } else {
                if (prot & PROT_WRITE) {
                    if ((pd & ~TARGET_PAGE_MASK) == IO_MEM_ROM || 
#if defined(TARGET_HAS_SMC) || 1
                        first_tb ||
#endif
                        ((pd & ~TARGET_PAGE_MASK) == IO_MEM_RAM && 
                         !cpu_physical_memory_is_dirty(pd))) {
                        /* ROM: we do as if code was inside */
                        /* if code is present, we only map as read only and save the
                           original mapping */
                        VirtPageDesc *vp;
                        
                        vp = virt_page_find_alloc(vaddr >> TARGET_PAGE_BITS);
                        vp->phys_addr = pd;
                        vp->prot = prot;
                        vp->valid_tag = virt_valid_tag;
                        prot &= ~PAGE_WRITE;
                    }
                }
                map_addr = mmap((void *)vaddr, TARGET_PAGE_SIZE, prot, 
                                MAP_SHARED | MAP_FIXED, phys_ram_fd, (pd & TARGET_PAGE_MASK));
                if (map_addr == MAP_FAILED) {
                    cpu_abort(env, "mmap failed when mapped physical address 0x%08x to virtual address 0x%08x\n",
                              paddr, vaddr);
                }
            }
        }
    }
#endif
    return ret;
}

/* called from signal handler: invalidate the code and unprotect the
   page. Return TRUE if the fault was succesfully handled. */
int page_unprotect(unsigned long addr, unsigned long pc, void *puc)
{
#if !defined(CONFIG_SOFTMMU)
    VirtPageDesc *vp;

#if defined(DEBUG_TLB)
    printf("page_unprotect: addr=0x%08x\n", addr);
#endif
    addr &= TARGET_PAGE_MASK;

    /* if it is not mapped, no need to worry here */
    if (addr >= MMAP_AREA_END)
        return 0;
    vp = virt_page_find(addr >> TARGET_PAGE_BITS);
    if (!vp)
        return 0;
    /* NOTE: in this case, validate_tag is _not_ tested as it
       validates only the code TLB */
    if (vp->valid_tag != virt_valid_tag)
        return 0;
    if (!(vp->prot & PAGE_WRITE))
        return 0;
#if defined(DEBUG_TLB)
    printf("page_unprotect: addr=0x%08x phys_addr=0x%08x prot=%x\n", 
           addr, vp->phys_addr, vp->prot);
#endif
    if (mprotect((void *)addr, TARGET_PAGE_SIZE, vp->prot) < 0)
        cpu_abort(cpu_single_env, "error mprotect addr=0x%lx prot=%d\n",
                  (unsigned long)addr, vp->prot);
    /* set the dirty bit */
    phys_ram_dirty[vp->phys_addr >> TARGET_PAGE_BITS] = 1;
    /* flush the code inside */
    tb_invalidate_phys_page(vp->phys_addr, pc, puc);
    return 1;
#else
    return 0;
#endif
}

#else

void tlb_flush(CPUState *env, int flush_global)
{
}

void tlb_flush_page(CPUState *env, target_ulong addr)
{
}

int tlb_set_page(CPUState *env, target_ulong vaddr, 
                 target_phys_addr_t paddr, int prot, 
                 int is_user, int is_softmmu)
{
    return 0;
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
            tb_invalidate_phys_page(addr, 0, NULL);
        }
        p->flags = flags;
    }
    spin_unlock(&tb_lock);
}

/* called from signal handler: invalidate the code and unprotect the
   page. Return TRUE if the fault was succesfully handled. */
int page_unprotect(unsigned long address, unsigned long pc, void *puc)
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
            mprotect((void *)host_start, host_page_size, 
                     (prot & PAGE_BITS) | PAGE_WRITE);
            p1[pindex].flags |= PAGE_WRITE;
            /* and since the content will be modified, we must invalidate
               the corresponding translated code. */
            tb_invalidate_phys_page(address, pc, puc);
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
        page_unprotect(addr, 0, NULL);
    }
}

static inline void tlb_set_dirty(unsigned long addr, target_ulong vaddr)
{
}
#endif /* defined(CONFIG_USER_ONLY) */

/* register physical memory. 'size' must be a multiple of the target
   page size. If (phys_offset & ~TARGET_PAGE_MASK) != 0, then it is an
   io memory page */
void cpu_register_physical_memory(target_phys_addr_t start_addr, 
                                  unsigned long size,
                                  unsigned long phys_offset)
{
    unsigned long addr, end_addr;
    PageDesc *p;

    end_addr = start_addr + size;
    for(addr = start_addr; addr < end_addr; addr += TARGET_PAGE_SIZE) {
        p = page_find_alloc(addr >> TARGET_PAGE_BITS);
        p->phys_offset = phys_offset;
        if ((phys_offset & ~TARGET_PAGE_MASK) <= IO_MEM_ROM)
            phys_offset += TARGET_PAGE_SIZE;
    }
}

static uint32_t unassigned_mem_readb(target_phys_addr_t addr)
{
    return 0;
}

static void unassigned_mem_writeb(target_phys_addr_t addr, uint32_t val)
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

/* self modifying code support in soft mmu mode : writing to a page
   containing code comes to these functions */

static void code_mem_writeb(target_phys_addr_t addr, uint32_t val)
{
    unsigned long phys_addr;

    phys_addr = addr - (long)phys_ram_base;
#if !defined(CONFIG_USER_ONLY)
    tb_invalidate_phys_page_fast(phys_addr, 1);
#endif
    stb_raw((uint8_t *)addr, val);
    phys_ram_dirty[phys_addr >> TARGET_PAGE_BITS] = 1;
}

static void code_mem_writew(target_phys_addr_t addr, uint32_t val)
{
    unsigned long phys_addr;

    phys_addr = addr - (long)phys_ram_base;
#if !defined(CONFIG_USER_ONLY)
    tb_invalidate_phys_page_fast(phys_addr, 2);
#endif
    stw_raw((uint8_t *)addr, val);
    phys_ram_dirty[phys_addr >> TARGET_PAGE_BITS] = 1;
}

static void code_mem_writel(target_phys_addr_t addr, uint32_t val)
{
    unsigned long phys_addr;

    phys_addr = addr - (long)phys_ram_base;
#if !defined(CONFIG_USER_ONLY)
    tb_invalidate_phys_page_fast(phys_addr, 4);
#endif
    stl_raw((uint8_t *)addr, val);
    phys_ram_dirty[phys_addr >> TARGET_PAGE_BITS] = 1;
}

static CPUReadMemoryFunc *code_mem_read[3] = {
    NULL, /* never used */
    NULL, /* never used */
    NULL, /* never used */
};

static CPUWriteMemoryFunc *code_mem_write[3] = {
    code_mem_writeb,
    code_mem_writew,
    code_mem_writel,
};

static void notdirty_mem_writeb(target_phys_addr_t addr, uint32_t val)
{
    stb_raw((uint8_t *)addr, val);
    tlb_set_dirty(addr, cpu_single_env->mem_write_vaddr);
}

static void notdirty_mem_writew(target_phys_addr_t addr, uint32_t val)
{
    stw_raw((uint8_t *)addr, val);
    tlb_set_dirty(addr, cpu_single_env->mem_write_vaddr);
}

static void notdirty_mem_writel(target_phys_addr_t addr, uint32_t val)
{
    stl_raw((uint8_t *)addr, val);
    tlb_set_dirty(addr, cpu_single_env->mem_write_vaddr);
}

static CPUWriteMemoryFunc *notdirty_mem_write[3] = {
    notdirty_mem_writeb,
    notdirty_mem_writew,
    notdirty_mem_writel,
};

static void io_mem_init(void)
{
    cpu_register_io_memory(IO_MEM_ROM >> IO_MEM_SHIFT, code_mem_read, unassigned_mem_write);
    cpu_register_io_memory(IO_MEM_UNASSIGNED >> IO_MEM_SHIFT, unassigned_mem_read, unassigned_mem_write);
    cpu_register_io_memory(IO_MEM_CODE >> IO_MEM_SHIFT, code_mem_read, code_mem_write);
    cpu_register_io_memory(IO_MEM_NOTDIRTY >> IO_MEM_SHIFT, code_mem_read, notdirty_mem_write);
    io_mem_nb = 5;

    /* alloc dirty bits array */
    phys_ram_dirty = qemu_malloc(phys_ram_size >> TARGET_PAGE_BITS);
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

/* physical memory access (slow version, mainly for debug) */
#if defined(CONFIG_USER_ONLY)
void cpu_physical_memory_rw(target_phys_addr_t addr, uint8_t *buf, 
                            int len, int is_write)
{
    int l, flags;
    target_ulong page;

    while (len > 0) {
        page = addr & TARGET_PAGE_MASK;
        l = (page + TARGET_PAGE_SIZE) - addr;
        if (l > len)
            l = len;
        flags = page_get_flags(page);
        if (!(flags & PAGE_VALID))
            return;
        if (is_write) {
            if (!(flags & PAGE_WRITE))
                return;
            memcpy((uint8_t *)addr, buf, len);
        } else {
            if (!(flags & PAGE_READ))
                return;
            memcpy(buf, (uint8_t *)addr, len);
        }
        len -= l;
        buf += l;
        addr += l;
    }
}
#else
void cpu_physical_memory_rw(target_phys_addr_t addr, uint8_t *buf, 
                            int len, int is_write)
{
    int l, io_index;
    uint8_t *ptr;
    uint32_t val;
    target_phys_addr_t page;
    unsigned long pd;
    PageDesc *p;
    
    while (len > 0) {
        page = addr & TARGET_PAGE_MASK;
        l = (page + TARGET_PAGE_SIZE) - addr;
        if (l > len)
            l = len;
        p = page_find(page >> TARGET_PAGE_BITS);
        if (!p) {
            pd = IO_MEM_UNASSIGNED;
        } else {
            pd = p->phys_offset;
        }
        
        if (is_write) {
            if ((pd & ~TARGET_PAGE_MASK) != 0) {
                io_index = (pd >> IO_MEM_SHIFT) & (IO_MEM_NB_ENTRIES - 1);
                if (l >= 4 && ((addr & 3) == 0)) {
                    /* 32 bit read access */
                    val = ldl_raw(buf);
                    io_mem_write[io_index][2](addr, val);
                    l = 4;
                } else if (l >= 2 && ((addr & 1) == 0)) {
                    /* 16 bit read access */
                    val = lduw_raw(buf);
                    io_mem_write[io_index][1](addr, val);
                    l = 2;
                } else {
                    /* 8 bit access */
                    val = ldub_raw(buf);
                    io_mem_write[io_index][0](addr, val);
                    l = 1;
                }
            } else {
                unsigned long addr1;
                addr1 = (pd & TARGET_PAGE_MASK) + (addr & ~TARGET_PAGE_MASK);
                /* RAM case */
                ptr = phys_ram_base + addr1;
                memcpy(ptr, buf, l);
                /* invalidate code */
                tb_invalidate_phys_page_range(addr1, addr1 + l, 0);
                /* set dirty bit */
                phys_ram_dirty[page >> TARGET_PAGE_BITS] = 1;                
            }
        } else {
            if ((pd & ~TARGET_PAGE_MASK) > IO_MEM_ROM &&
                (pd & ~TARGET_PAGE_MASK) != IO_MEM_CODE) {
                /* I/O case */
                io_index = (pd >> IO_MEM_SHIFT) & (IO_MEM_NB_ENTRIES - 1);
                if (l >= 4 && ((addr & 3) == 0)) {
                    /* 32 bit read access */
                    val = io_mem_read[io_index][2](addr);
                    stl_raw(buf, val);
                    l = 4;
                } else if (l >= 2 && ((addr & 1) == 0)) {
                    /* 16 bit read access */
                    val = io_mem_read[io_index][1](addr);
                    stw_raw(buf, val);
                    l = 2;
                } else {
                    /* 8 bit access */
                    val = io_mem_read[io_index][0](addr);
                    stb_raw(buf, val);
                    l = 1;
                }
            } else {
                /* RAM case */
                ptr = phys_ram_base + (pd & TARGET_PAGE_MASK) + 
                    (addr & ~TARGET_PAGE_MASK);
                memcpy(buf, ptr, l);
            }
        }
        len -= l;
        buf += l;
        addr += l;
    }
}
#endif

/* virtual memory access for debug */
int cpu_memory_rw_debug(CPUState *env, target_ulong addr, 
                        uint8_t *buf, int len, int is_write)
{
    int l;
    target_ulong page, phys_addr;

    while (len > 0) {
        page = addr & TARGET_PAGE_MASK;
        phys_addr = cpu_get_phys_page_debug(env, page);
        /* if no physical page mapped, return an error */
        if (phys_addr == -1)
            return -1;
        l = (page + TARGET_PAGE_SIZE) - addr;
        if (l > len)
            l = len;
        cpu_physical_memory_rw(phys_addr + (addr & ~TARGET_PAGE_MASK), 
                               buf, l, is_write);
        len -= l;
        buf += l;
        addr += l;
    }
    return 0;
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
