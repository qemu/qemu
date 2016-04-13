/*
 *  Host code generation
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
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif
#include "qemu/osdep.h"


#include "qemu-common.h"
#define NO_CPU_IO_DEFS
#include "cpu.h"
#include "trace.h"
#include "disas/disas.h"
#include "tcg.h"
#if defined(CONFIG_USER_ONLY)
#include "qemu.h"
#if defined(__FreeBSD__) || defined(__FreeBSD_kernel__)
#include <sys/param.h>
#if __FreeBSD_version >= 700104
#define HAVE_KINFO_GETVMMAP
#define sigqueue sigqueue_freebsd  /* avoid redefinition */
#include <sys/proc.h>
#include <machine/profile.h>
#define _KERNEL
#include <sys/user.h>
#undef _KERNEL
#undef sigqueue
#include <libutil.h>
#endif
#endif
#else
#include "exec/address-spaces.h"
#endif

#include "exec/cputlb.h"
#include "exec/tb-hash.h"
#include "translate-all.h"
#include "qemu/bitmap.h"
#include "qemu/timer.h"
#include "exec/log.h"

//#define DEBUG_TB_INVALIDATE
//#define DEBUG_FLUSH
/* make various TB consistency checks */
//#define DEBUG_TB_CHECK

#if !defined(CONFIG_USER_ONLY)
/* TB consistency checks only implemented for usermode emulation.  */
#undef DEBUG_TB_CHECK
#endif

#define SMC_BITMAP_USE_THRESHOLD 10

typedef struct PageDesc {
    /* list of TBs intersecting this ram page */
    TranslationBlock *first_tb;
#ifdef CONFIG_SOFTMMU
    /* in order to optimize self modifying code, we count the number
       of lookups we do to a given page to use a bitmap */
    unsigned int code_write_count;
    unsigned long *code_bitmap;
#else
    unsigned long flags;
#endif
} PageDesc;

/* In system mode we want L1_MAP to be based on ram offsets,
   while in user mode we want it to be based on virtual addresses.  */
#if !defined(CONFIG_USER_ONLY)
#if HOST_LONG_BITS < TARGET_PHYS_ADDR_SPACE_BITS
# define L1_MAP_ADDR_SPACE_BITS  HOST_LONG_BITS
#else
# define L1_MAP_ADDR_SPACE_BITS  TARGET_PHYS_ADDR_SPACE_BITS
#endif
#else
# define L1_MAP_ADDR_SPACE_BITS  TARGET_VIRT_ADDR_SPACE_BITS
#endif

/* Size of the L2 (and L3, etc) page tables.  */
#define V_L2_BITS 10
#define V_L2_SIZE (1 << V_L2_BITS)

/* The bits remaining after N lower levels of page tables.  */
#define V_L1_BITS_REM \
    ((L1_MAP_ADDR_SPACE_BITS - TARGET_PAGE_BITS) % V_L2_BITS)

#if V_L1_BITS_REM < 4
#define V_L1_BITS  (V_L1_BITS_REM + V_L2_BITS)
#else
#define V_L1_BITS  V_L1_BITS_REM
#endif

#define V_L1_SIZE  ((target_ulong)1 << V_L1_BITS)

#define V_L1_SHIFT (L1_MAP_ADDR_SPACE_BITS - TARGET_PAGE_BITS - V_L1_BITS)

uintptr_t qemu_host_page_size;
intptr_t qemu_host_page_mask;

/* The bottom level has pointers to PageDesc */
static void *l1_map[V_L1_SIZE];

/* code generation context */
TCGContext tcg_ctx;

/* translation block context */
#ifdef CONFIG_USER_ONLY
__thread int have_tb_lock;
#endif

void tb_lock(void)
{
#ifdef CONFIG_USER_ONLY
    assert(!have_tb_lock);
    qemu_mutex_lock(&tcg_ctx.tb_ctx.tb_lock);
    have_tb_lock++;
#endif
}

void tb_unlock(void)
{
#ifdef CONFIG_USER_ONLY
    assert(have_tb_lock);
    have_tb_lock--;
    qemu_mutex_unlock(&tcg_ctx.tb_ctx.tb_lock);
#endif
}

void tb_lock_reset(void)
{
#ifdef CONFIG_USER_ONLY
    if (have_tb_lock) {
        qemu_mutex_unlock(&tcg_ctx.tb_ctx.tb_lock);
        have_tb_lock = 0;
    }
#endif
}

static TranslationBlock *tb_find_pc(uintptr_t tc_ptr);

void cpu_gen_init(void)
{
    tcg_context_init(&tcg_ctx); 
}

/* Encode VAL as a signed leb128 sequence at P.
   Return P incremented past the encoded value.  */
static uint8_t *encode_sleb128(uint8_t *p, target_long val)
{
    int more, byte;

    do {
        byte = val & 0x7f;
        val >>= 7;
        more = !((val == 0 && (byte & 0x40) == 0)
                 || (val == -1 && (byte & 0x40) != 0));
        if (more) {
            byte |= 0x80;
        }
        *p++ = byte;
    } while (more);

    return p;
}

/* Decode a signed leb128 sequence at *PP; increment *PP past the
   decoded value.  Return the decoded value.  */
static target_long decode_sleb128(uint8_t **pp)
{
    uint8_t *p = *pp;
    target_long val = 0;
    int byte, shift = 0;

    do {
        byte = *p++;
        val |= (target_ulong)(byte & 0x7f) << shift;
        shift += 7;
    } while (byte & 0x80);
    if (shift < TARGET_LONG_BITS && (byte & 0x40)) {
        val |= -(target_ulong)1 << shift;
    }

    *pp = p;
    return val;
}

/* Encode the data collected about the instructions while compiling TB.
   Place the data at BLOCK, and return the number of bytes consumed.

   The logical table consisits of TARGET_INSN_START_WORDS target_ulong's,
   which come from the target's insn_start data, followed by a uintptr_t
   which comes from the host pc of the end of the code implementing the insn.

   Each line of the table is encoded as sleb128 deltas from the previous
   line.  The seed for the first line is { tb->pc, 0..., tb->tc_ptr }.
   That is, the first column is seeded with the guest pc, the last column
   with the host pc, and the middle columns with zeros.  */

static int encode_search(TranslationBlock *tb, uint8_t *block)
{
    uint8_t *highwater = tcg_ctx.code_gen_highwater;
    uint8_t *p = block;
    int i, j, n;

    tb->tc_search = block;

    for (i = 0, n = tb->icount; i < n; ++i) {
        target_ulong prev;

        for (j = 0; j < TARGET_INSN_START_WORDS; ++j) {
            if (i == 0) {
                prev = (j == 0 ? tb->pc : 0);
            } else {
                prev = tcg_ctx.gen_insn_data[i - 1][j];
            }
            p = encode_sleb128(p, tcg_ctx.gen_insn_data[i][j] - prev);
        }
        prev = (i == 0 ? 0 : tcg_ctx.gen_insn_end_off[i - 1]);
        p = encode_sleb128(p, tcg_ctx.gen_insn_end_off[i] - prev);

        /* Test for (pending) buffer overflow.  The assumption is that any
           one row beginning below the high water mark cannot overrun
           the buffer completely.  Thus we can test for overflow after
           encoding a row without having to check during encoding.  */
        if (unlikely(p > highwater)) {
            return -1;
        }
    }

    return p - block;
}

/* The cpu state corresponding to 'searched_pc' is restored.  */
static int cpu_restore_state_from_tb(CPUState *cpu, TranslationBlock *tb,
                                     uintptr_t searched_pc)
{
    target_ulong data[TARGET_INSN_START_WORDS] = { tb->pc };
    uintptr_t host_pc = (uintptr_t)tb->tc_ptr;
    CPUArchState *env = cpu->env_ptr;
    uint8_t *p = tb->tc_search;
    int i, j, num_insns = tb->icount;
#ifdef CONFIG_PROFILER
    int64_t ti = profile_getclock();
#endif

    if (searched_pc < host_pc) {
        return -1;
    }

    /* Reconstruct the stored insn data while looking for the point at
       which the end of the insn exceeds the searched_pc.  */
    for (i = 0; i < num_insns; ++i) {
        for (j = 0; j < TARGET_INSN_START_WORDS; ++j) {
            data[j] += decode_sleb128(&p);
        }
        host_pc += decode_sleb128(&p);
        if (host_pc > searched_pc) {
            goto found;
        }
    }
    return -1;

 found:
    if (tb->cflags & CF_USE_ICOUNT) {
        assert(use_icount);
        /* Reset the cycle counter to the start of the block.  */
        cpu->icount_decr.u16.low += num_insns;
        /* Clear the IO flag.  */
        cpu->can_do_io = 0;
    }
    cpu->icount_decr.u16.low -= i;
    restore_state_to_opc(env, tb, data);

#ifdef CONFIG_PROFILER
    tcg_ctx.restore_time += profile_getclock() - ti;
    tcg_ctx.restore_count++;
#endif
    return 0;
}

bool cpu_restore_state(CPUState *cpu, uintptr_t retaddr)
{
    TranslationBlock *tb;

    tb = tb_find_pc(retaddr);
    if (tb) {
        cpu_restore_state_from_tb(cpu, tb, retaddr);
        if (tb->cflags & CF_NOCACHE) {
            /* one-shot translation, invalidate it immediately */
            cpu->current_tb = NULL;
            tb_phys_invalidate(tb, -1);
            tb_free(tb);
        }
        return true;
    }
    return false;
}

void page_size_init(void)
{
    /* NOTE: we can always suppose that qemu_host_page_size >=
       TARGET_PAGE_SIZE */
    qemu_real_host_page_size = getpagesize();
    qemu_real_host_page_mask = -(intptr_t)qemu_real_host_page_size;
    if (qemu_host_page_size == 0) {
        qemu_host_page_size = qemu_real_host_page_size;
    }
    if (qemu_host_page_size < TARGET_PAGE_SIZE) {
        qemu_host_page_size = TARGET_PAGE_SIZE;
    }
    qemu_host_page_mask = -(intptr_t)qemu_host_page_size;
}

static void page_init(void)
{
    page_size_init();
#if defined(CONFIG_BSD) && defined(CONFIG_USER_ONLY)
    {
#ifdef HAVE_KINFO_GETVMMAP
        struct kinfo_vmentry *freep;
        int i, cnt;

        freep = kinfo_getvmmap(getpid(), &cnt);
        if (freep) {
            mmap_lock();
            for (i = 0; i < cnt; i++) {
                unsigned long startaddr, endaddr;

                startaddr = freep[i].kve_start;
                endaddr = freep[i].kve_end;
                if (h2g_valid(startaddr)) {
                    startaddr = h2g(startaddr) & TARGET_PAGE_MASK;

                    if (h2g_valid(endaddr)) {
                        endaddr = h2g(endaddr);
                        page_set_flags(startaddr, endaddr, PAGE_RESERVED);
                    } else {
#if TARGET_ABI_BITS <= L1_MAP_ADDR_SPACE_BITS
                        endaddr = ~0ul;
                        page_set_flags(startaddr, endaddr, PAGE_RESERVED);
#endif
                    }
                }
            }
            free(freep);
            mmap_unlock();
        }
#else
        FILE *f;

        last_brk = (unsigned long)sbrk(0);

        f = fopen("/compat/linux/proc/self/maps", "r");
        if (f) {
            mmap_lock();

            do {
                unsigned long startaddr, endaddr;
                int n;

                n = fscanf(f, "%lx-%lx %*[^\n]\n", &startaddr, &endaddr);

                if (n == 2 && h2g_valid(startaddr)) {
                    startaddr = h2g(startaddr) & TARGET_PAGE_MASK;

                    if (h2g_valid(endaddr)) {
                        endaddr = h2g(endaddr);
                    } else {
                        endaddr = ~0ul;
                    }
                    page_set_flags(startaddr, endaddr, PAGE_RESERVED);
                }
            } while (!feof(f));

            fclose(f);
            mmap_unlock();
        }
#endif
    }
#endif
}

/* If alloc=1:
 * Called with mmap_lock held for user-mode emulation.
 */
static PageDesc *page_find_alloc(tb_page_addr_t index, int alloc)
{
    PageDesc *pd;
    void **lp;
    int i;

    /* Level 1.  Always allocated.  */
    lp = l1_map + ((index >> V_L1_SHIFT) & (V_L1_SIZE - 1));

    /* Level 2..N-1.  */
    for (i = V_L1_SHIFT / V_L2_BITS - 1; i > 0; i--) {
        void **p = atomic_rcu_read(lp);

        if (p == NULL) {
            if (!alloc) {
                return NULL;
            }
            p = g_new0(void *, V_L2_SIZE);
            atomic_rcu_set(lp, p);
        }

        lp = p + ((index >> (i * V_L2_BITS)) & (V_L2_SIZE - 1));
    }

    pd = atomic_rcu_read(lp);
    if (pd == NULL) {
        if (!alloc) {
            return NULL;
        }
        pd = g_new0(PageDesc, V_L2_SIZE);
        atomic_rcu_set(lp, pd);
    }

    return pd + (index & (V_L2_SIZE - 1));
}

static inline PageDesc *page_find(tb_page_addr_t index)
{
    return page_find_alloc(index, 0);
}

#if defined(CONFIG_USER_ONLY)
/* Currently it is not recommended to allocate big chunks of data in
   user mode. It will change when a dedicated libc will be used.  */
/* ??? 64-bit hosts ought to have no problem mmaping data outside the
   region in which the guest needs to run.  Revisit this.  */
#define USE_STATIC_CODE_GEN_BUFFER
#endif

/* Minimum size of the code gen buffer.  This number is randomly chosen,
   but not so small that we can't have a fair number of TB's live.  */
#define MIN_CODE_GEN_BUFFER_SIZE     (1024u * 1024)

/* Maximum size of the code gen buffer we'd like to use.  Unless otherwise
   indicated, this is constrained by the range of direct branches on the
   host cpu, as used by the TCG implementation of goto_tb.  */
#if defined(__x86_64__)
# define MAX_CODE_GEN_BUFFER_SIZE  (2ul * 1024 * 1024 * 1024)
#elif defined(__sparc__)
# define MAX_CODE_GEN_BUFFER_SIZE  (2ul * 1024 * 1024 * 1024)
#elif defined(__powerpc64__)
# define MAX_CODE_GEN_BUFFER_SIZE  (2ul * 1024 * 1024 * 1024)
#elif defined(__powerpc__)
# define MAX_CODE_GEN_BUFFER_SIZE  (32u * 1024 * 1024)
#elif defined(__aarch64__)
# define MAX_CODE_GEN_BUFFER_SIZE  (128ul * 1024 * 1024)
#elif defined(__arm__)
# define MAX_CODE_GEN_BUFFER_SIZE  (16u * 1024 * 1024)
#elif defined(__s390x__)
  /* We have a +- 4GB range on the branches; leave some slop.  */
# define MAX_CODE_GEN_BUFFER_SIZE  (3ul * 1024 * 1024 * 1024)
#elif defined(__mips__)
  /* We have a 256MB branch region, but leave room to make sure the
     main executable is also within that region.  */
# define MAX_CODE_GEN_BUFFER_SIZE  (128ul * 1024 * 1024)
#else
# define MAX_CODE_GEN_BUFFER_SIZE  ((size_t)-1)
#endif

#define DEFAULT_CODE_GEN_BUFFER_SIZE_1 (32u * 1024 * 1024)

#define DEFAULT_CODE_GEN_BUFFER_SIZE \
  (DEFAULT_CODE_GEN_BUFFER_SIZE_1 < MAX_CODE_GEN_BUFFER_SIZE \
   ? DEFAULT_CODE_GEN_BUFFER_SIZE_1 : MAX_CODE_GEN_BUFFER_SIZE)

static inline size_t size_code_gen_buffer(size_t tb_size)
{
    /* Size the buffer.  */
    if (tb_size == 0) {
#ifdef USE_STATIC_CODE_GEN_BUFFER
        tb_size = DEFAULT_CODE_GEN_BUFFER_SIZE;
#else
        /* ??? Needs adjustments.  */
        /* ??? If we relax the requirement that CONFIG_USER_ONLY use the
           static buffer, we could size this on RESERVED_VA, on the text
           segment size of the executable, or continue to use the default.  */
        tb_size = (unsigned long)(ram_size / 4);
#endif
    }
    if (tb_size < MIN_CODE_GEN_BUFFER_SIZE) {
        tb_size = MIN_CODE_GEN_BUFFER_SIZE;
    }
    if (tb_size > MAX_CODE_GEN_BUFFER_SIZE) {
        tb_size = MAX_CODE_GEN_BUFFER_SIZE;
    }
    return tb_size;
}

#ifdef __mips__
/* In order to use J and JAL within the code_gen_buffer, we require
   that the buffer not cross a 256MB boundary.  */
static inline bool cross_256mb(void *addr, size_t size)
{
    return ((uintptr_t)addr ^ ((uintptr_t)addr + size)) & ~0x0ffffffful;
}

/* We weren't able to allocate a buffer without crossing that boundary,
   so make do with the larger portion of the buffer that doesn't cross.
   Returns the new base of the buffer, and adjusts code_gen_buffer_size.  */
static inline void *split_cross_256mb(void *buf1, size_t size1)
{
    void *buf2 = (void *)(((uintptr_t)buf1 + size1) & ~0x0ffffffful);
    size_t size2 = buf1 + size1 - buf2;

    size1 = buf2 - buf1;
    if (size1 < size2) {
        size1 = size2;
        buf1 = buf2;
    }

    tcg_ctx.code_gen_buffer_size = size1;
    return buf1;
}
#endif

#ifdef USE_STATIC_CODE_GEN_BUFFER
static uint8_t static_code_gen_buffer[DEFAULT_CODE_GEN_BUFFER_SIZE]
    __attribute__((aligned(CODE_GEN_ALIGN)));

# ifdef _WIN32
static inline void do_protect(void *addr, long size, int prot)
{
    DWORD old_protect;
    VirtualProtect(addr, size, prot, &old_protect);
}

static inline void map_exec(void *addr, long size)
{
    do_protect(addr, size, PAGE_EXECUTE_READWRITE);
}

static inline void map_none(void *addr, long size)
{
    do_protect(addr, size, PAGE_NOACCESS);
}
# else
static inline void do_protect(void *addr, long size, int prot)
{
    uintptr_t start, end;

    start = (uintptr_t)addr;
    start &= qemu_real_host_page_mask;

    end = (uintptr_t)addr + size;
    end = ROUND_UP(end, qemu_real_host_page_size);

    mprotect((void *)start, end - start, prot);
}

static inline void map_exec(void *addr, long size)
{
    do_protect(addr, size, PROT_READ | PROT_WRITE | PROT_EXEC);
}

static inline void map_none(void *addr, long size)
{
    do_protect(addr, size, PROT_NONE);
}
# endif /* WIN32 */

static inline void *alloc_code_gen_buffer(void)
{
    void *buf = static_code_gen_buffer;
    size_t full_size, size;

    /* The size of the buffer, rounded down to end on a page boundary.  */
    full_size = (((uintptr_t)buf + sizeof(static_code_gen_buffer))
                 & qemu_real_host_page_mask) - (uintptr_t)buf;

    /* Reserve a guard page.  */
    size = full_size - qemu_real_host_page_size;

    /* Honor a command-line option limiting the size of the buffer.  */
    if (size > tcg_ctx.code_gen_buffer_size) {
        size = (((uintptr_t)buf + tcg_ctx.code_gen_buffer_size)
                & qemu_real_host_page_mask) - (uintptr_t)buf;
    }
    tcg_ctx.code_gen_buffer_size = size;

#ifdef __mips__
    if (cross_256mb(buf, size)) {
        buf = split_cross_256mb(buf, size);
        size = tcg_ctx.code_gen_buffer_size;
    }
#endif

    map_exec(buf, size);
    map_none(buf + size, qemu_real_host_page_size);
    qemu_madvise(buf, size, QEMU_MADV_HUGEPAGE);

    return buf;
}
#elif defined(_WIN32)
static inline void *alloc_code_gen_buffer(void)
{
    size_t size = tcg_ctx.code_gen_buffer_size;
    void *buf1, *buf2;

    /* Perform the allocation in two steps, so that the guard page
       is reserved but uncommitted.  */
    buf1 = VirtualAlloc(NULL, size + qemu_real_host_page_size,
                        MEM_RESERVE, PAGE_NOACCESS);
    if (buf1 != NULL) {
        buf2 = VirtualAlloc(buf1, size, MEM_COMMIT, PAGE_EXECUTE_READWRITE);
        assert(buf1 == buf2);
    }

    return buf1;
}
#else
static inline void *alloc_code_gen_buffer(void)
{
    int flags = MAP_PRIVATE | MAP_ANONYMOUS;
    uintptr_t start = 0;
    size_t size = tcg_ctx.code_gen_buffer_size;
    void *buf;

    /* Constrain the position of the buffer based on the host cpu.
       Note that these addresses are chosen in concert with the
       addresses assigned in the relevant linker script file.  */
# if defined(__PIE__) || defined(__PIC__)
    /* Don't bother setting a preferred location if we're building
       a position-independent executable.  We're more likely to get
       an address near the main executable if we let the kernel
       choose the address.  */
# elif defined(__x86_64__) && defined(MAP_32BIT)
    /* Force the memory down into low memory with the executable.
       Leave the choice of exact location with the kernel.  */
    flags |= MAP_32BIT;
    /* Cannot expect to map more than 800MB in low memory.  */
    if (size > 800u * 1024 * 1024) {
        tcg_ctx.code_gen_buffer_size = size = 800u * 1024 * 1024;
    }
# elif defined(__sparc__)
    start = 0x40000000ul;
# elif defined(__s390x__)
    start = 0x90000000ul;
# elif defined(__mips__)
#  if _MIPS_SIM == _ABI64
    start = 0x128000000ul;
#  else
    start = 0x08000000ul;
#  endif
# endif

    buf = mmap((void *)start, size + qemu_real_host_page_size,
               PROT_NONE, flags, -1, 0);
    if (buf == MAP_FAILED) {
        return NULL;
    }

#ifdef __mips__
    if (cross_256mb(buf, size)) {
        /* Try again, with the original still mapped, to avoid re-acquiring
           that 256mb crossing.  This time don't specify an address.  */
        size_t size2;
        void *buf2 = mmap(NULL, size + qemu_real_host_page_size,
                          PROT_NONE, flags, -1, 0);
        switch (buf2 != MAP_FAILED) {
        case 1:
            if (!cross_256mb(buf2, size)) {
                /* Success!  Use the new buffer.  */
                munmap(buf, size + qemu_real_host_page_size);
                break;
            }
            /* Failure.  Work with what we had.  */
            munmap(buf2, size + qemu_real_host_page_size);
            /* fallthru */
        default:
            /* Split the original buffer.  Free the smaller half.  */
            buf2 = split_cross_256mb(buf, size);
            size2 = tcg_ctx.code_gen_buffer_size;
            if (buf == buf2) {
                munmap(buf + size2 + qemu_real_host_page_size, size - size2);
            } else {
                munmap(buf, size - size2);
            }
            size = size2;
            break;
        }
        buf = buf2;
    }
#endif

    /* Make the final buffer accessible.  The guard page at the end
       will remain inaccessible with PROT_NONE.  */
    mprotect(buf, size, PROT_WRITE | PROT_READ | PROT_EXEC);

    /* Request large pages for the buffer.  */
    qemu_madvise(buf, size, QEMU_MADV_HUGEPAGE);

    return buf;
}
#endif /* USE_STATIC_CODE_GEN_BUFFER, WIN32, POSIX */

static inline void code_gen_alloc(size_t tb_size)
{
    tcg_ctx.code_gen_buffer_size = size_code_gen_buffer(tb_size);
    tcg_ctx.code_gen_buffer = alloc_code_gen_buffer();
    if (tcg_ctx.code_gen_buffer == NULL) {
        fprintf(stderr, "Could not allocate dynamic translator buffer\n");
        exit(1);
    }

    /* Estimate a good size for the number of TBs we can support.  We
       still haven't deducted the prologue from the buffer size here,
       but that's minimal and won't affect the estimate much.  */
    tcg_ctx.code_gen_max_blocks
        = tcg_ctx.code_gen_buffer_size / CODE_GEN_AVG_BLOCK_SIZE;
    tcg_ctx.tb_ctx.tbs = g_new(TranslationBlock, tcg_ctx.code_gen_max_blocks);

    qemu_mutex_init(&tcg_ctx.tb_ctx.tb_lock);
}

/* Must be called before using the QEMU cpus. 'tb_size' is the size
   (in bytes) allocated to the translation buffer. Zero means default
   size. */
void tcg_exec_init(unsigned long tb_size)
{
    cpu_gen_init();
    page_init();
    code_gen_alloc(tb_size);
#if defined(CONFIG_SOFTMMU)
    /* There's no guest base to take into account, so go ahead and
       initialize the prologue now.  */
    tcg_prologue_init(&tcg_ctx);
#endif
}

bool tcg_enabled(void)
{
    return tcg_ctx.code_gen_buffer != NULL;
}

/* Allocate a new translation block. Flush the translation buffer if
   too many translation blocks or too much generated code. */
static TranslationBlock *tb_alloc(target_ulong pc)
{
    TranslationBlock *tb;

    if (tcg_ctx.tb_ctx.nb_tbs >= tcg_ctx.code_gen_max_blocks) {
        return NULL;
    }
    tb = &tcg_ctx.tb_ctx.tbs[tcg_ctx.tb_ctx.nb_tbs++];
    tb->pc = pc;
    tb->cflags = 0;
    return tb;
}

void tb_free(TranslationBlock *tb)
{
    /* In practice this is mostly used for single use temporary TB
       Ignore the hard cases and just back up if this TB happens to
       be the last one generated.  */
    if (tcg_ctx.tb_ctx.nb_tbs > 0 &&
            tb == &tcg_ctx.tb_ctx.tbs[tcg_ctx.tb_ctx.nb_tbs - 1]) {
        tcg_ctx.code_gen_ptr = tb->tc_ptr;
        tcg_ctx.tb_ctx.nb_tbs--;
    }
}

static inline void invalidate_page_bitmap(PageDesc *p)
{
#ifdef CONFIG_SOFTMMU
    g_free(p->code_bitmap);
    p->code_bitmap = NULL;
    p->code_write_count = 0;
#endif
}

/* Set to NULL all the 'first_tb' fields in all PageDescs. */
static void page_flush_tb_1(int level, void **lp)
{
    int i;

    if (*lp == NULL) {
        return;
    }
    if (level == 0) {
        PageDesc *pd = *lp;

        for (i = 0; i < V_L2_SIZE; ++i) {
            pd[i].first_tb = NULL;
            invalidate_page_bitmap(pd + i);
        }
    } else {
        void **pp = *lp;

        for (i = 0; i < V_L2_SIZE; ++i) {
            page_flush_tb_1(level - 1, pp + i);
        }
    }
}

static void page_flush_tb(void)
{
    int i;

    for (i = 0; i < V_L1_SIZE; i++) {
        page_flush_tb_1(V_L1_SHIFT / V_L2_BITS - 1, l1_map + i);
    }
}

/* flush all the translation blocks */
/* XXX: tb_flush is currently not thread safe */
void tb_flush(CPUState *cpu)
{
#if defined(DEBUG_FLUSH)
    printf("qemu: flush code_size=%ld nb_tbs=%d avg_tb_size=%ld\n",
           (unsigned long)(tcg_ctx.code_gen_ptr - tcg_ctx.code_gen_buffer),
           tcg_ctx.tb_ctx.nb_tbs, tcg_ctx.tb_ctx.nb_tbs > 0 ?
           ((unsigned long)(tcg_ctx.code_gen_ptr - tcg_ctx.code_gen_buffer)) /
           tcg_ctx.tb_ctx.nb_tbs : 0);
#endif
    if ((unsigned long)(tcg_ctx.code_gen_ptr - tcg_ctx.code_gen_buffer)
        > tcg_ctx.code_gen_buffer_size) {
        cpu_abort(cpu, "Internal error: code buffer overflow\n");
    }
    tcg_ctx.tb_ctx.nb_tbs = 0;

    CPU_FOREACH(cpu) {
        memset(cpu->tb_jmp_cache, 0, sizeof(cpu->tb_jmp_cache));
        cpu->tb_flushed = true;
    }

    memset(tcg_ctx.tb_ctx.tb_phys_hash, 0, sizeof(tcg_ctx.tb_ctx.tb_phys_hash));
    page_flush_tb();

    tcg_ctx.code_gen_ptr = tcg_ctx.code_gen_buffer;
    /* XXX: flush processor icache at this point if cache flush is
       expensive */
    tcg_ctx.tb_ctx.tb_flush_count++;
}

#ifdef DEBUG_TB_CHECK

static void tb_invalidate_check(target_ulong address)
{
    TranslationBlock *tb;
    int i;

    address &= TARGET_PAGE_MASK;
    for (i = 0; i < CODE_GEN_PHYS_HASH_SIZE; i++) {
        for (tb = tcg_ctx.tb_ctx.tb_phys_hash[i]; tb != NULL;
             tb = tb->phys_hash_next) {
            if (!(address + TARGET_PAGE_SIZE <= tb->pc ||
                  address >= tb->pc + tb->size)) {
                printf("ERROR invalidate: address=" TARGET_FMT_lx
                       " PC=%08lx size=%04x\n",
                       address, (long)tb->pc, tb->size);
            }
        }
    }
}

/* verify that all the pages have correct rights for code */
static void tb_page_check(void)
{
    TranslationBlock *tb;
    int i, flags1, flags2;

    for (i = 0; i < CODE_GEN_PHYS_HASH_SIZE; i++) {
        for (tb = tcg_ctx.tb_ctx.tb_phys_hash[i]; tb != NULL;
                tb = tb->phys_hash_next) {
            flags1 = page_get_flags(tb->pc);
            flags2 = page_get_flags(tb->pc + tb->size - 1);
            if ((flags1 & PAGE_WRITE) || (flags2 & PAGE_WRITE)) {
                printf("ERROR page flags: PC=%08lx size=%04x f1=%x f2=%x\n",
                       (long)tb->pc, tb->size, flags1, flags2);
            }
        }
    }
}

#endif

static inline void tb_hash_remove(TranslationBlock **ptb, TranslationBlock *tb)
{
    TranslationBlock *tb1;

    for (;;) {
        tb1 = *ptb;
        if (tb1 == tb) {
            *ptb = tb1->phys_hash_next;
            break;
        }
        ptb = &tb1->phys_hash_next;
    }
}

static inline void tb_page_remove(TranslationBlock **ptb, TranslationBlock *tb)
{
    TranslationBlock *tb1;
    unsigned int n1;

    for (;;) {
        tb1 = *ptb;
        n1 = (uintptr_t)tb1 & 3;
        tb1 = (TranslationBlock *)((uintptr_t)tb1 & ~3);
        if (tb1 == tb) {
            *ptb = tb1->page_next[n1];
            break;
        }
        ptb = &tb1->page_next[n1];
    }
}

/* remove the TB from a list of TBs jumping to the n-th jump target of the TB */
static inline void tb_remove_from_jmp_list(TranslationBlock *tb, int n)
{
    TranslationBlock *tb1;
    uintptr_t *ptb, ntb;
    unsigned int n1;

    ptb = &tb->jmp_list_next[n];
    if (*ptb) {
        /* find tb(n) in circular list */
        for (;;) {
            ntb = *ptb;
            n1 = ntb & 3;
            tb1 = (TranslationBlock *)(ntb & ~3);
            if (n1 == n && tb1 == tb) {
                break;
            }
            if (n1 == 2) {
                ptb = &tb1->jmp_list_first;
            } else {
                ptb = &tb1->jmp_list_next[n1];
            }
        }
        /* now we can suppress tb(n) from the list */
        *ptb = tb->jmp_list_next[n];

        tb->jmp_list_next[n] = (uintptr_t)NULL;
    }
}

/* reset the jump entry 'n' of a TB so that it is not chained to
   another TB */
static inline void tb_reset_jump(TranslationBlock *tb, int n)
{
    uintptr_t addr = (uintptr_t)(tb->tc_ptr + tb->jmp_reset_offset[n]);
    tb_set_jmp_target(tb, n, addr);
}

/* remove any jumps to the TB */
static inline void tb_jmp_unlink(TranslationBlock *tb)
{
    TranslationBlock *tb1;
    uintptr_t *ptb, ntb;
    unsigned int n1;

    ptb = &tb->jmp_list_first;
    for (;;) {
        ntb = *ptb;
        n1 = ntb & 3;
        tb1 = (TranslationBlock *)(ntb & ~3);
        if (n1 == 2) {
            break;
        }
        tb_reset_jump(tb1, n1);
        *ptb = tb1->jmp_list_next[n1];
        tb1->jmp_list_next[n1] = (uintptr_t)NULL;
    }
}

/* invalidate one TB */
void tb_phys_invalidate(TranslationBlock *tb, tb_page_addr_t page_addr)
{
    CPUState *cpu;
    PageDesc *p;
    unsigned int h;
    tb_page_addr_t phys_pc;

    /* remove the TB from the hash list */
    phys_pc = tb->page_addr[0] + (tb->pc & ~TARGET_PAGE_MASK);
    h = tb_phys_hash_func(phys_pc);
    tb_hash_remove(&tcg_ctx.tb_ctx.tb_phys_hash[h], tb);

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

    /* remove the TB from the hash list */
    h = tb_jmp_cache_hash_func(tb->pc);
    CPU_FOREACH(cpu) {
        if (cpu->tb_jmp_cache[h] == tb) {
            cpu->tb_jmp_cache[h] = NULL;
        }
    }

    /* suppress this TB from the two jump lists */
    tb_remove_from_jmp_list(tb, 0);
    tb_remove_from_jmp_list(tb, 1);

    /* suppress any remaining jumps to this TB */
    tb_jmp_unlink(tb);

    tcg_ctx.tb_ctx.tb_phys_invalidate_count++;
}

#ifdef CONFIG_SOFTMMU
static void build_page_bitmap(PageDesc *p)
{
    int n, tb_start, tb_end;
    TranslationBlock *tb;

    p->code_bitmap = bitmap_new(TARGET_PAGE_SIZE);

    tb = p->first_tb;
    while (tb != NULL) {
        n = (uintptr_t)tb & 3;
        tb = (TranslationBlock *)((uintptr_t)tb & ~3);
        /* NOTE: this is subtle as a TB may span two physical pages */
        if (n == 0) {
            /* NOTE: tb_end may be after the end of the page, but
               it is not a problem */
            tb_start = tb->pc & ~TARGET_PAGE_MASK;
            tb_end = tb_start + tb->size;
            if (tb_end > TARGET_PAGE_SIZE) {
                tb_end = TARGET_PAGE_SIZE;
            }
        } else {
            tb_start = 0;
            tb_end = ((tb->pc + tb->size) & ~TARGET_PAGE_MASK);
        }
        bitmap_set(p->code_bitmap, tb_start, tb_end - tb_start);
        tb = tb->page_next[n];
    }
}
#endif

/* add the tb in the target page and protect it if necessary
 *
 * Called with mmap_lock held for user-mode emulation.
 */
static inline void tb_alloc_page(TranslationBlock *tb,
                                 unsigned int n, tb_page_addr_t page_addr)
{
    PageDesc *p;
#ifndef CONFIG_USER_ONLY
    bool page_already_protected;
#endif

    tb->page_addr[n] = page_addr;
    p = page_find_alloc(page_addr >> TARGET_PAGE_BITS, 1);
    tb->page_next[n] = p->first_tb;
#ifndef CONFIG_USER_ONLY
    page_already_protected = p->first_tb != NULL;
#endif
    p->first_tb = (TranslationBlock *)((uintptr_t)tb | n);
    invalidate_page_bitmap(p);

#if defined(CONFIG_USER_ONLY)
    if (p->flags & PAGE_WRITE) {
        target_ulong addr;
        PageDesc *p2;
        int prot;

        /* force the host page as non writable (writes will have a
           page fault + mprotect overhead) */
        page_addr &= qemu_host_page_mask;
        prot = 0;
        for (addr = page_addr; addr < page_addr + qemu_host_page_size;
            addr += TARGET_PAGE_SIZE) {

            p2 = page_find(addr >> TARGET_PAGE_BITS);
            if (!p2) {
                continue;
            }
            prot |= p2->flags;
            p2->flags &= ~PAGE_WRITE;
          }
        mprotect(g2h(page_addr), qemu_host_page_size,
                 (prot & PAGE_BITS) & ~PAGE_WRITE);
#ifdef DEBUG_TB_INVALIDATE
        printf("protecting code page: 0x" TARGET_FMT_lx "\n",
               page_addr);
#endif
    }
#else
    /* if some code is already present, then the pages are already
       protected. So we handle the case where only the first TB is
       allocated in a physical page */
    if (!page_already_protected) {
        tlb_protect_code(page_addr);
    }
#endif
}

/* add a new TB and link it to the physical page tables. phys_page2 is
 * (-1) to indicate that only one page contains the TB.
 *
 * Called with mmap_lock held for user-mode emulation.
 */
static void tb_link_page(TranslationBlock *tb, tb_page_addr_t phys_pc,
                         tb_page_addr_t phys_page2)
{
    unsigned int h;
    TranslationBlock **ptb;

    /* add in the physical hash table */
    h = tb_phys_hash_func(phys_pc);
    ptb = &tcg_ctx.tb_ctx.tb_phys_hash[h];
    tb->phys_hash_next = *ptb;
    *ptb = tb;

    /* add in the page list */
    tb_alloc_page(tb, 0, phys_pc & TARGET_PAGE_MASK);
    if (phys_page2 != -1) {
        tb_alloc_page(tb, 1, phys_page2);
    } else {
        tb->page_addr[1] = -1;
    }

#ifdef DEBUG_TB_CHECK
    tb_page_check();
#endif
}

/* Called with mmap_lock held for user mode emulation.  */
TranslationBlock *tb_gen_code(CPUState *cpu,
                              target_ulong pc, target_ulong cs_base,
                              uint32_t flags, int cflags)
{
    CPUArchState *env = cpu->env_ptr;
    TranslationBlock *tb;
    tb_page_addr_t phys_pc, phys_page2;
    target_ulong virt_page2;
    tcg_insn_unit *gen_code_buf;
    int gen_code_size, search_size;
#ifdef CONFIG_PROFILER
    int64_t ti;
#endif

    phys_pc = get_page_addr_code(env, pc);
    if (use_icount && !(cflags & CF_IGNORE_ICOUNT)) {
        cflags |= CF_USE_ICOUNT;
    }

    tb = tb_alloc(pc);
    if (unlikely(!tb)) {
 buffer_overflow:
        /* flush must be done */
        tb_flush(cpu);
        /* cannot fail at this point */
        tb = tb_alloc(pc);
        assert(tb != NULL);
    }

    gen_code_buf = tcg_ctx.code_gen_ptr;
    tb->tc_ptr = gen_code_buf;
    tb->cs_base = cs_base;
    tb->flags = flags;
    tb->cflags = cflags;

#ifdef CONFIG_PROFILER
    tcg_ctx.tb_count1++; /* includes aborted translations because of
                       exceptions */
    ti = profile_getclock();
#endif

    tcg_func_start(&tcg_ctx);

    gen_intermediate_code(env, tb);

    trace_translate_block(tb, tb->pc, tb->tc_ptr);

    /* generate machine code */
    tb->jmp_reset_offset[0] = TB_JMP_RESET_OFFSET_INVALID;
    tb->jmp_reset_offset[1] = TB_JMP_RESET_OFFSET_INVALID;
    tcg_ctx.tb_jmp_reset_offset = tb->jmp_reset_offset;
#ifdef USE_DIRECT_JUMP
    tcg_ctx.tb_jmp_insn_offset = tb->jmp_insn_offset;
    tcg_ctx.tb_jmp_target_addr = NULL;
#else
    tcg_ctx.tb_jmp_insn_offset = NULL;
    tcg_ctx.tb_jmp_target_addr = tb->jmp_target_addr;
#endif

#ifdef CONFIG_PROFILER
    tcg_ctx.tb_count++;
    tcg_ctx.interm_time += profile_getclock() - ti;
    tcg_ctx.code_time -= profile_getclock();
#endif

    /* ??? Overflow could be handled better here.  In particular, we
       don't need to re-do gen_intermediate_code, nor should we re-do
       the tcg optimization currently hidden inside tcg_gen_code.  All
       that should be required is to flush the TBs, allocate a new TB,
       re-initialize it per above, and re-do the actual code generation.  */
    gen_code_size = tcg_gen_code(&tcg_ctx, tb);
    if (unlikely(gen_code_size < 0)) {
        goto buffer_overflow;
    }
    search_size = encode_search(tb, (void *)gen_code_buf + gen_code_size);
    if (unlikely(search_size < 0)) {
        goto buffer_overflow;
    }

#ifdef CONFIG_PROFILER
    tcg_ctx.code_time += profile_getclock();
    tcg_ctx.code_in_len += tb->size;
    tcg_ctx.code_out_len += gen_code_size;
    tcg_ctx.search_out_len += search_size;
#endif

#ifdef DEBUG_DISAS
    if (qemu_loglevel_mask(CPU_LOG_TB_OUT_ASM) &&
        qemu_log_in_addr_range(tb->pc)) {
        qemu_log("OUT: [size=%d]\n", gen_code_size);
        log_disas(tb->tc_ptr, gen_code_size);
        qemu_log("\n");
        qemu_log_flush();
    }
#endif

    tcg_ctx.code_gen_ptr = (void *)
        ROUND_UP((uintptr_t)gen_code_buf + gen_code_size + search_size,
                 CODE_GEN_ALIGN);

    /* init jump list */
    assert(((uintptr_t)tb & 3) == 0);
    tb->jmp_list_first = (uintptr_t)tb | 2;
    tb->jmp_list_next[0] = (uintptr_t)NULL;
    tb->jmp_list_next[1] = (uintptr_t)NULL;

    /* init original jump addresses wich has been set during tcg_gen_code() */
    if (tb->jmp_reset_offset[0] != TB_JMP_RESET_OFFSET_INVALID) {
        tb_reset_jump(tb, 0);
    }
    if (tb->jmp_reset_offset[1] != TB_JMP_RESET_OFFSET_INVALID) {
        tb_reset_jump(tb, 1);
    }

    /* check next page if needed */
    virt_page2 = (pc + tb->size - 1) & TARGET_PAGE_MASK;
    phys_page2 = -1;
    if ((pc & TARGET_PAGE_MASK) != virt_page2) {
        phys_page2 = get_page_addr_code(env, virt_page2);
    }
    /* As long as consistency of the TB stuff is provided by tb_lock in user
     * mode and is implicit in single-threaded softmmu emulation, no explicit
     * memory barrier is required before tb_link_page() makes the TB visible
     * through the physical hash table and physical page list.
     */
    tb_link_page(tb, phys_pc, phys_page2);
    return tb;
}

/*
 * Invalidate all TBs which intersect with the target physical address range
 * [start;end[. NOTE: start and end may refer to *different* physical pages.
 * 'is_cpu_write_access' should be true if called from a real cpu write
 * access: the virtual CPU will exit the current TB if code is modified inside
 * this TB.
 *
 * Called with mmap_lock held for user-mode emulation
 */
void tb_invalidate_phys_range(tb_page_addr_t start, tb_page_addr_t end)
{
    while (start < end) {
        tb_invalidate_phys_page_range(start, end, 0);
        start &= TARGET_PAGE_MASK;
        start += TARGET_PAGE_SIZE;
    }
}

/*
 * Invalidate all TBs which intersect with the target physical address range
 * [start;end[. NOTE: start and end must refer to the *same* physical page.
 * 'is_cpu_write_access' should be true if called from a real cpu write
 * access: the virtual CPU will exit the current TB if code is modified inside
 * this TB.
 *
 * Called with mmap_lock held for user-mode emulation
 */
void tb_invalidate_phys_page_range(tb_page_addr_t start, tb_page_addr_t end,
                                   int is_cpu_write_access)
{
    TranslationBlock *tb, *tb_next, *saved_tb;
    CPUState *cpu = current_cpu;
#if defined(TARGET_HAS_PRECISE_SMC)
    CPUArchState *env = NULL;
#endif
    tb_page_addr_t tb_start, tb_end;
    PageDesc *p;
    int n;
#ifdef TARGET_HAS_PRECISE_SMC
    int current_tb_not_found = is_cpu_write_access;
    TranslationBlock *current_tb = NULL;
    int current_tb_modified = 0;
    target_ulong current_pc = 0;
    target_ulong current_cs_base = 0;
    uint32_t current_flags = 0;
#endif /* TARGET_HAS_PRECISE_SMC */

    p = page_find(start >> TARGET_PAGE_BITS);
    if (!p) {
        return;
    }
#if defined(TARGET_HAS_PRECISE_SMC)
    if (cpu != NULL) {
        env = cpu->env_ptr;
    }
#endif

    /* we remove all the TBs in the range [start, end[ */
    /* XXX: see if in some cases it could be faster to invalidate all
       the code */
    tb = p->first_tb;
    while (tb != NULL) {
        n = (uintptr_t)tb & 3;
        tb = (TranslationBlock *)((uintptr_t)tb & ~3);
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
                if (cpu->mem_io_pc) {
                    /* now we have a real cpu fault */
                    current_tb = tb_find_pc(cpu->mem_io_pc);
                }
            }
            if (current_tb == tb &&
                (current_tb->cflags & CF_COUNT_MASK) != 1) {
                /* If we are modifying the current TB, we must stop
                its execution. We could be more precise by checking
                that the modification is after the current PC, but it
                would require a specialized function to partially
                restore the CPU state */

                current_tb_modified = 1;
                cpu_restore_state_from_tb(cpu, current_tb, cpu->mem_io_pc);
                cpu_get_tb_cpu_state(env, &current_pc, &current_cs_base,
                                     &current_flags);
            }
#endif /* TARGET_HAS_PRECISE_SMC */
            /* we need to do that to handle the case where a signal
               occurs while doing tb_phys_invalidate() */
            saved_tb = NULL;
            if (cpu != NULL) {
                saved_tb = cpu->current_tb;
                cpu->current_tb = NULL;
            }
            tb_phys_invalidate(tb, -1);
            if (cpu != NULL) {
                cpu->current_tb = saved_tb;
                if (cpu->interrupt_request && cpu->current_tb) {
                    cpu_interrupt(cpu, cpu->interrupt_request);
                }
            }
        }
        tb = tb_next;
    }
#if !defined(CONFIG_USER_ONLY)
    /* if no code remaining, no need to continue to use slow writes */
    if (!p->first_tb) {
        invalidate_page_bitmap(p);
        tlb_unprotect_code(start);
    }
#endif
#ifdef TARGET_HAS_PRECISE_SMC
    if (current_tb_modified) {
        /* we generate a block containing just the instruction
           modifying the memory. It will ensure that it cannot modify
           itself */
        cpu->current_tb = NULL;
        tb_gen_code(cpu, current_pc, current_cs_base, current_flags, 1);
        cpu_resume_from_signal(cpu, NULL);
    }
#endif
}

#ifdef CONFIG_SOFTMMU
/* len must be <= 8 and start must be a multiple of len */
void tb_invalidate_phys_page_fast(tb_page_addr_t start, int len)
{
    PageDesc *p;

#if 0
    if (1) {
        qemu_log("modifying code at 0x%x size=%d EIP=%x PC=%08x\n",
                  cpu_single_env->mem_io_vaddr, len,
                  cpu_single_env->eip,
                  cpu_single_env->eip +
                  (intptr_t)cpu_single_env->segs[R_CS].base);
    }
#endif
    p = page_find(start >> TARGET_PAGE_BITS);
    if (!p) {
        return;
    }
    if (!p->code_bitmap &&
        ++p->code_write_count >= SMC_BITMAP_USE_THRESHOLD) {
        /* build code bitmap */
        build_page_bitmap(p);
    }
    if (p->code_bitmap) {
        unsigned int nr;
        unsigned long b;

        nr = start & ~TARGET_PAGE_MASK;
        b = p->code_bitmap[BIT_WORD(nr)] >> (nr & (BITS_PER_LONG - 1));
        if (b & ((1 << len) - 1)) {
            goto do_invalidate;
        }
    } else {
    do_invalidate:
        tb_invalidate_phys_page_range(start, start + len, 1);
    }
}
#else
/* Called with mmap_lock held.  */
static void tb_invalidate_phys_page(tb_page_addr_t addr,
                                    uintptr_t pc, void *puc,
                                    bool locked)
{
    TranslationBlock *tb;
    PageDesc *p;
    int n;
#ifdef TARGET_HAS_PRECISE_SMC
    TranslationBlock *current_tb = NULL;
    CPUState *cpu = current_cpu;
    CPUArchState *env = NULL;
    int current_tb_modified = 0;
    target_ulong current_pc = 0;
    target_ulong current_cs_base = 0;
    uint32_t current_flags = 0;
#endif

    addr &= TARGET_PAGE_MASK;
    p = page_find(addr >> TARGET_PAGE_BITS);
    if (!p) {
        return;
    }
    tb = p->first_tb;
#ifdef TARGET_HAS_PRECISE_SMC
    if (tb && pc != 0) {
        current_tb = tb_find_pc(pc);
    }
    if (cpu != NULL) {
        env = cpu->env_ptr;
    }
#endif
    while (tb != NULL) {
        n = (uintptr_t)tb & 3;
        tb = (TranslationBlock *)((uintptr_t)tb & ~3);
#ifdef TARGET_HAS_PRECISE_SMC
        if (current_tb == tb &&
            (current_tb->cflags & CF_COUNT_MASK) != 1) {
                /* If we are modifying the current TB, we must stop
                   its execution. We could be more precise by checking
                   that the modification is after the current PC, but it
                   would require a specialized function to partially
                   restore the CPU state */

            current_tb_modified = 1;
            cpu_restore_state_from_tb(cpu, current_tb, pc);
            cpu_get_tb_cpu_state(env, &current_pc, &current_cs_base,
                                 &current_flags);
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
        cpu->current_tb = NULL;
        tb_gen_code(cpu, current_pc, current_cs_base, current_flags, 1);
        if (locked) {
            mmap_unlock();
        }
        cpu_resume_from_signal(cpu, puc);
    }
#endif
}
#endif

/* find the TB 'tb' such that tb[0].tc_ptr <= tc_ptr <
   tb[1].tc_ptr. Return NULL if not found */
static TranslationBlock *tb_find_pc(uintptr_t tc_ptr)
{
    int m_min, m_max, m;
    uintptr_t v;
    TranslationBlock *tb;

    if (tcg_ctx.tb_ctx.nb_tbs <= 0) {
        return NULL;
    }
    if (tc_ptr < (uintptr_t)tcg_ctx.code_gen_buffer ||
        tc_ptr >= (uintptr_t)tcg_ctx.code_gen_ptr) {
        return NULL;
    }
    /* binary search (cf Knuth) */
    m_min = 0;
    m_max = tcg_ctx.tb_ctx.nb_tbs - 1;
    while (m_min <= m_max) {
        m = (m_min + m_max) >> 1;
        tb = &tcg_ctx.tb_ctx.tbs[m];
        v = (uintptr_t)tb->tc_ptr;
        if (v == tc_ptr) {
            return tb;
        } else if (tc_ptr < v) {
            m_max = m - 1;
        } else {
            m_min = m + 1;
        }
    }
    return &tcg_ctx.tb_ctx.tbs[m_max];
}

#if !defined(CONFIG_USER_ONLY)
void tb_invalidate_phys_addr(AddressSpace *as, hwaddr addr)
{
    ram_addr_t ram_addr;
    MemoryRegion *mr;
    hwaddr l = 1;

    rcu_read_lock();
    mr = address_space_translate(as, addr, &addr, &l, false);
    if (!(memory_region_is_ram(mr)
          || memory_region_is_romd(mr))) {
        rcu_read_unlock();
        return;
    }
    ram_addr = (memory_region_get_ram_addr(mr) & TARGET_PAGE_MASK)
        + addr;
    tb_invalidate_phys_page_range(ram_addr, ram_addr + 1, 0);
    rcu_read_unlock();
}
#endif /* !defined(CONFIG_USER_ONLY) */

void tb_check_watchpoint(CPUState *cpu)
{
    TranslationBlock *tb;

    tb = tb_find_pc(cpu->mem_io_pc);
    if (tb) {
        /* We can use retranslation to find the PC.  */
        cpu_restore_state_from_tb(cpu, tb, cpu->mem_io_pc);
        tb_phys_invalidate(tb, -1);
    } else {
        /* The exception probably happened in a helper.  The CPU state should
           have been saved before calling it. Fetch the PC from there.  */
        CPUArchState *env = cpu->env_ptr;
        target_ulong pc, cs_base;
        tb_page_addr_t addr;
        uint32_t flags;

        cpu_get_tb_cpu_state(env, &pc, &cs_base, &flags);
        addr = get_page_addr_code(env, pc);
        tb_invalidate_phys_range(addr, addr + 1);
    }
}

#ifndef CONFIG_USER_ONLY
/* in deterministic execution mode, instructions doing device I/Os
   must be at the end of the TB */
void cpu_io_recompile(CPUState *cpu, uintptr_t retaddr)
{
#if defined(TARGET_MIPS) || defined(TARGET_SH4)
    CPUArchState *env = cpu->env_ptr;
#endif
    TranslationBlock *tb;
    uint32_t n, cflags;
    target_ulong pc, cs_base;
    uint32_t flags;

    tb = tb_find_pc(retaddr);
    if (!tb) {
        cpu_abort(cpu, "cpu_io_recompile: could not find TB for pc=%p",
                  (void *)retaddr);
    }
    n = cpu->icount_decr.u16.low + tb->icount;
    cpu_restore_state_from_tb(cpu, tb, retaddr);
    /* Calculate how many instructions had been executed before the fault
       occurred.  */
    n = n - cpu->icount_decr.u16.low;
    /* Generate a new TB ending on the I/O insn.  */
    n++;
    /* On MIPS and SH, delay slot instructions can only be restarted if
       they were already the first instruction in the TB.  If this is not
       the first instruction in a TB then re-execute the preceding
       branch.  */
#if defined(TARGET_MIPS)
    if ((env->hflags & MIPS_HFLAG_BMASK) != 0 && n > 1) {
        env->active_tc.PC -= (env->hflags & MIPS_HFLAG_B16 ? 2 : 4);
        cpu->icount_decr.u16.low++;
        env->hflags &= ~MIPS_HFLAG_BMASK;
    }
#elif defined(TARGET_SH4)
    if ((env->flags & ((DELAY_SLOT | DELAY_SLOT_CONDITIONAL))) != 0
            && n > 1) {
        env->pc -= 2;
        cpu->icount_decr.u16.low++;
        env->flags &= ~(DELAY_SLOT | DELAY_SLOT_CONDITIONAL);
    }
#endif
    /* This should never happen.  */
    if (n > CF_COUNT_MASK) {
        cpu_abort(cpu, "TB too big during recompile");
    }

    cflags = n | CF_LAST_IO;
    pc = tb->pc;
    cs_base = tb->cs_base;
    flags = tb->flags;
    tb_phys_invalidate(tb, -1);
    if (tb->cflags & CF_NOCACHE) {
        if (tb->orig_tb) {
            /* Invalidate original TB if this TB was generated in
             * cpu_exec_nocache() */
            tb_phys_invalidate(tb->orig_tb, -1);
        }
        tb_free(tb);
    }
    /* FIXME: In theory this could raise an exception.  In practice
       we have already translated the block once so it's probably ok.  */
    tb_gen_code(cpu, pc, cs_base, flags, cflags);
    /* TODO: If env->pc != tb->pc (i.e. the faulting instruction was not
       the first in the TB) then we end up generating a whole new TB and
       repeating the fault, which is horribly inefficient.
       Better would be to execute just this insn uncached, or generate a
       second new TB.  */
    cpu_resume_from_signal(cpu, NULL);
}

void tb_flush_jmp_cache(CPUState *cpu, target_ulong addr)
{
    unsigned int i;

    /* Discard jump cache entries for any tb which might potentially
       overlap the flushed page.  */
    i = tb_jmp_cache_hash_page(addr - TARGET_PAGE_SIZE);
    memset(&cpu->tb_jmp_cache[i], 0,
           TB_JMP_PAGE_SIZE * sizeof(TranslationBlock *));

    i = tb_jmp_cache_hash_page(addr);
    memset(&cpu->tb_jmp_cache[i], 0,
           TB_JMP_PAGE_SIZE * sizeof(TranslationBlock *));
}

void dump_exec_info(FILE *f, fprintf_function cpu_fprintf)
{
    int i, target_code_size, max_target_code_size;
    int direct_jmp_count, direct_jmp2_count, cross_page;
    TranslationBlock *tb;

    target_code_size = 0;
    max_target_code_size = 0;
    cross_page = 0;
    direct_jmp_count = 0;
    direct_jmp2_count = 0;
    for (i = 0; i < tcg_ctx.tb_ctx.nb_tbs; i++) {
        tb = &tcg_ctx.tb_ctx.tbs[i];
        target_code_size += tb->size;
        if (tb->size > max_target_code_size) {
            max_target_code_size = tb->size;
        }
        if (tb->page_addr[1] != -1) {
            cross_page++;
        }
        if (tb->jmp_reset_offset[0] != TB_JMP_RESET_OFFSET_INVALID) {
            direct_jmp_count++;
            if (tb->jmp_reset_offset[1] != TB_JMP_RESET_OFFSET_INVALID) {
                direct_jmp2_count++;
            }
        }
    }
    /* XXX: avoid using doubles ? */
    cpu_fprintf(f, "Translation buffer state:\n");
    cpu_fprintf(f, "gen code size       %td/%zd\n",
                tcg_ctx.code_gen_ptr - tcg_ctx.code_gen_buffer,
                tcg_ctx.code_gen_highwater - tcg_ctx.code_gen_buffer);
    cpu_fprintf(f, "TB count            %d/%d\n",
            tcg_ctx.tb_ctx.nb_tbs, tcg_ctx.code_gen_max_blocks);
    cpu_fprintf(f, "TB avg target size  %d max=%d bytes\n",
            tcg_ctx.tb_ctx.nb_tbs ? target_code_size /
                    tcg_ctx.tb_ctx.nb_tbs : 0,
            max_target_code_size);
    cpu_fprintf(f, "TB avg host size    %td bytes (expansion ratio: %0.1f)\n",
            tcg_ctx.tb_ctx.nb_tbs ? (tcg_ctx.code_gen_ptr -
                                     tcg_ctx.code_gen_buffer) /
                                     tcg_ctx.tb_ctx.nb_tbs : 0,
                target_code_size ? (double) (tcg_ctx.code_gen_ptr -
                                             tcg_ctx.code_gen_buffer) /
                                             target_code_size : 0);
    cpu_fprintf(f, "cross page TB count %d (%d%%)\n", cross_page,
            tcg_ctx.tb_ctx.nb_tbs ? (cross_page * 100) /
                                    tcg_ctx.tb_ctx.nb_tbs : 0);
    cpu_fprintf(f, "direct jump count   %d (%d%%) (2 jumps=%d %d%%)\n",
                direct_jmp_count,
                tcg_ctx.tb_ctx.nb_tbs ? (direct_jmp_count * 100) /
                        tcg_ctx.tb_ctx.nb_tbs : 0,
                direct_jmp2_count,
                tcg_ctx.tb_ctx.nb_tbs ? (direct_jmp2_count * 100) /
                        tcg_ctx.tb_ctx.nb_tbs : 0);
    cpu_fprintf(f, "\nStatistics:\n");
    cpu_fprintf(f, "TB flush count      %d\n", tcg_ctx.tb_ctx.tb_flush_count);
    cpu_fprintf(f, "TB invalidate count %d\n",
            tcg_ctx.tb_ctx.tb_phys_invalidate_count);
    cpu_fprintf(f, "TLB flush count     %d\n", tlb_flush_count);
    tcg_dump_info(f, cpu_fprintf);
}

void dump_opcount_info(FILE *f, fprintf_function cpu_fprintf)
{
    tcg_dump_op_count(f, cpu_fprintf);
}

#else /* CONFIG_USER_ONLY */

void cpu_interrupt(CPUState *cpu, int mask)
{
    cpu->interrupt_request |= mask;
    cpu->tcg_exit_req = 1;
}

/*
 * Walks guest process memory "regions" one by one
 * and calls callback function 'fn' for each region.
 */
struct walk_memory_regions_data {
    walk_memory_regions_fn fn;
    void *priv;
    target_ulong start;
    int prot;
};

static int walk_memory_regions_end(struct walk_memory_regions_data *data,
                                   target_ulong end, int new_prot)
{
    if (data->start != -1u) {
        int rc = data->fn(data->priv, data->start, end, data->prot);
        if (rc != 0) {
            return rc;
        }
    }

    data->start = (new_prot ? end : -1u);
    data->prot = new_prot;

    return 0;
}

static int walk_memory_regions_1(struct walk_memory_regions_data *data,
                                 target_ulong base, int level, void **lp)
{
    target_ulong pa;
    int i, rc;

    if (*lp == NULL) {
        return walk_memory_regions_end(data, base, 0);
    }

    if (level == 0) {
        PageDesc *pd = *lp;

        for (i = 0; i < V_L2_SIZE; ++i) {
            int prot = pd[i].flags;

            pa = base | (i << TARGET_PAGE_BITS);
            if (prot != data->prot) {
                rc = walk_memory_regions_end(data, pa, prot);
                if (rc != 0) {
                    return rc;
                }
            }
        }
    } else {
        void **pp = *lp;

        for (i = 0; i < V_L2_SIZE; ++i) {
            pa = base | ((target_ulong)i <<
                (TARGET_PAGE_BITS + V_L2_BITS * level));
            rc = walk_memory_regions_1(data, pa, level - 1, pp + i);
            if (rc != 0) {
                return rc;
            }
        }
    }

    return 0;
}

int walk_memory_regions(void *priv, walk_memory_regions_fn fn)
{
    struct walk_memory_regions_data data;
    uintptr_t i;

    data.fn = fn;
    data.priv = priv;
    data.start = -1u;
    data.prot = 0;

    for (i = 0; i < V_L1_SIZE; i++) {
        int rc = walk_memory_regions_1(&data, (target_ulong)i << (V_L1_SHIFT + TARGET_PAGE_BITS),
                                       V_L1_SHIFT / V_L2_BITS - 1, l1_map + i);
        if (rc != 0) {
            return rc;
        }
    }

    return walk_memory_regions_end(&data, 0, 0);
}

static int dump_region(void *priv, target_ulong start,
    target_ulong end, unsigned long prot)
{
    FILE *f = (FILE *)priv;

    (void) fprintf(f, TARGET_FMT_lx"-"TARGET_FMT_lx
        " "TARGET_FMT_lx" %c%c%c\n",
        start, end, end - start,
        ((prot & PAGE_READ) ? 'r' : '-'),
        ((prot & PAGE_WRITE) ? 'w' : '-'),
        ((prot & PAGE_EXEC) ? 'x' : '-'));

    return 0;
}

/* dump memory mappings */
void page_dump(FILE *f)
{
    const int length = sizeof(target_ulong) * 2;
    (void) fprintf(f, "%-*s %-*s %-*s %s\n",
            length, "start", length, "end", length, "size", "prot");
    walk_memory_regions(f, dump_region);
}

int page_get_flags(target_ulong address)
{
    PageDesc *p;

    p = page_find(address >> TARGET_PAGE_BITS);
    if (!p) {
        return 0;
    }
    return p->flags;
}

/* Modify the flags of a page and invalidate the code if necessary.
   The flag PAGE_WRITE_ORG is positioned automatically depending
   on PAGE_WRITE.  The mmap_lock should already be held.  */
void page_set_flags(target_ulong start, target_ulong end, int flags)
{
    target_ulong addr, len;

    /* This function should never be called with addresses outside the
       guest address space.  If this assert fires, it probably indicates
       a missing call to h2g_valid.  */
#if TARGET_ABI_BITS > L1_MAP_ADDR_SPACE_BITS
    assert(end < ((target_ulong)1 << L1_MAP_ADDR_SPACE_BITS));
#endif
    assert(start < end);

    start = start & TARGET_PAGE_MASK;
    end = TARGET_PAGE_ALIGN(end);

    if (flags & PAGE_WRITE) {
        flags |= PAGE_WRITE_ORG;
    }

    for (addr = start, len = end - start;
         len != 0;
         len -= TARGET_PAGE_SIZE, addr += TARGET_PAGE_SIZE) {
        PageDesc *p = page_find_alloc(addr >> TARGET_PAGE_BITS, 1);

        /* If the write protection bit is set, then we invalidate
           the code inside.  */
        if (!(p->flags & PAGE_WRITE) &&
            (flags & PAGE_WRITE) &&
            p->first_tb) {
            tb_invalidate_phys_page(addr, 0, NULL, false);
        }
        p->flags = flags;
    }
}

int page_check_range(target_ulong start, target_ulong len, int flags)
{
    PageDesc *p;
    target_ulong end;
    target_ulong addr;

    /* This function should never be called with addresses outside the
       guest address space.  If this assert fires, it probably indicates
       a missing call to h2g_valid.  */
#if TARGET_ABI_BITS > L1_MAP_ADDR_SPACE_BITS
    assert(start < ((target_ulong)1 << L1_MAP_ADDR_SPACE_BITS));
#endif

    if (len == 0) {
        return 0;
    }
    if (start + len - 1 < start) {
        /* We've wrapped around.  */
        return -1;
    }

    /* must do before we loose bits in the next step */
    end = TARGET_PAGE_ALIGN(start + len);
    start = start & TARGET_PAGE_MASK;

    for (addr = start, len = end - start;
         len != 0;
         len -= TARGET_PAGE_SIZE, addr += TARGET_PAGE_SIZE) {
        p = page_find(addr >> TARGET_PAGE_BITS);
        if (!p) {
            return -1;
        }
        if (!(p->flags & PAGE_VALID)) {
            return -1;
        }

        if ((flags & PAGE_READ) && !(p->flags & PAGE_READ)) {
            return -1;
        }
        if (flags & PAGE_WRITE) {
            if (!(p->flags & PAGE_WRITE_ORG)) {
                return -1;
            }
            /* unprotect the page if it was put read-only because it
               contains translated code */
            if (!(p->flags & PAGE_WRITE)) {
                if (!page_unprotect(addr, 0, NULL)) {
                    return -1;
                }
            }
        }
    }
    return 0;
}

/* called from signal handler: invalidate the code and unprotect the
   page. Return TRUE if the fault was successfully handled. */
int page_unprotect(target_ulong address, uintptr_t pc, void *puc)
{
    unsigned int prot;
    PageDesc *p;
    target_ulong host_start, host_end, addr;

    /* Technically this isn't safe inside a signal handler.  However we
       know this only ever happens in a synchronous SEGV handler, so in
       practice it seems to be ok.  */
    mmap_lock();

    p = page_find(address >> TARGET_PAGE_BITS);
    if (!p) {
        mmap_unlock();
        return 0;
    }

    /* if the page was really writable, then we change its
       protection back to writable */
    if ((p->flags & PAGE_WRITE_ORG) && !(p->flags & PAGE_WRITE)) {
        host_start = address & qemu_host_page_mask;
        host_end = host_start + qemu_host_page_size;

        prot = 0;
        for (addr = host_start ; addr < host_end ; addr += TARGET_PAGE_SIZE) {
            p = page_find(addr >> TARGET_PAGE_BITS);
            p->flags |= PAGE_WRITE;
            prot |= p->flags;

            /* and since the content will be modified, we must invalidate
               the corresponding translated code. */
            tb_invalidate_phys_page(addr, pc, puc, true);
#ifdef DEBUG_TB_CHECK
            tb_invalidate_check(addr);
#endif
        }
        mprotect((void *)g2h(host_start), qemu_host_page_size,
                 prot & PAGE_BITS);

        mmap_unlock();
        return 1;
    }
    mmap_unlock();
    return 0;
}
#endif /* CONFIG_USER_ONLY */
