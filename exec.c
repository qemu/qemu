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

#include "cpu-i386.h"

//#define DEBUG_TB_INVALIDATE
#define DEBUG_FLUSH

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

unsigned long real_host_page_size;
unsigned long host_page_bits;
unsigned long host_page_size;
unsigned long host_page_mask;

static PageDesc *l1_map[L1_SIZE];

void page_init(void)
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

void cpu_x86_tblocks_init(void)
{
    if (!code_gen_ptr) {
        code_gen_ptr = code_gen_buffer;
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
    /* XXX: flush processor icache at this point */
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

static inline void tb_invalidate(TranslationBlock *tb, int parity)
{
    PageDesc *p;
    unsigned int page_index1, page_index2;
    unsigned int h;

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
        mprotect((void *)host_start, host_page_size, 
                 (prot & PAGE_BITS) & ~PAGE_WRITE);
#ifdef DEBUG_TB_INVALIDATE
        printf("protecting code page: 0x%08lx\n", 
               host_start);
#endif
        p->flags &= ~PAGE_WRITE;
#ifdef DEBUG_TB_CHECK
        tb_page_check();
#endif
    }
}

/* Allocate a new translation block. Flush the translation buffer if
   too many translation blocks or too much generated code. */
TranslationBlock *tb_alloc(unsigned long pc, 
                           unsigned long size)
{
    TranslationBlock *tb;
    unsigned int page_index1, page_index2;

    if (nb_tbs >= CODE_GEN_MAX_BLOCKS || 
        (code_gen_ptr - code_gen_buffer) >= CODE_GEN_BUFFER_MAX_SIZE)
        tb_flush();
    tb = &tbs[nb_tbs++];
    tb->pc = pc;
    tb->size = size;

    /* add in the page list */
    page_index1 = pc >> TARGET_PAGE_BITS;
    tb_alloc_page(tb, page_index1);
    page_index2 = (pc + size - 1) >> TARGET_PAGE_BITS;
    if (page_index2 != page_index1) {
        tb_alloc_page(tb, page_index2);
    }
    return tb;
}

/* called from signal handler: invalidate the code and unprotect the
   page. Return TRUE if the fault was succesfully handled. */
int page_unprotect(unsigned long address)
{
    unsigned int page_index, prot;
    PageDesc *p;
    unsigned long host_start, host_end, addr;

    page_index = address >> TARGET_PAGE_BITS;
    p = page_find(page_index);
    if (!p)
        return 0;
    if ((p->flags & (PAGE_WRITE_ORG | PAGE_WRITE)) == PAGE_WRITE_ORG) {
        /* if the page was really writable, then we change its
           protection back to writable */
        host_start = address & host_page_mask;
        host_end = host_start + host_page_size;
        prot = 0;
        for(addr = host_start; addr < host_end; addr += TARGET_PAGE_SIZE)
            prot |= page_get_flags(addr);
        mprotect((void *)host_start, host_page_size, 
                 (prot & PAGE_BITS) | PAGE_WRITE);
        p->flags |= PAGE_WRITE;

        /* and since the content will be modified, we must invalidate
           the corresponding translated code. */
        tb_invalidate_page(address);
#ifdef DEBUG_TB_CHECK
        tb_invalidate_check(address);
#endif
        return 1;
    } else {
        return 0;
    }
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
