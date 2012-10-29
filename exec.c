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
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */
#include "config.h"
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/types.h>
#include <sys/mman.h>
#endif

#include "qemu-common.h"
#include "cpu.h"
#include "tcg.h"
#include "hw/hw.h"
#include "hw/qdev.h"
#include "osdep.h"
#include "kvm.h"
#include "hw/xen.h"
#include "qemu-timer.h"
#include "memory.h"
#include "dma.h"
#include "exec-memory.h"
#if defined(CONFIG_USER_ONLY)
#include <qemu.h>
#if defined(__FreeBSD__) || defined(__FreeBSD_kernel__)
#include <sys/param.h>
#if __FreeBSD_version >= 700104
#define HAVE_KINFO_GETVMMAP
#define sigqueue sigqueue_freebsd  /* avoid redefinition */
#include <sys/time.h>
#include <sys/proc.h>
#include <machine/profile.h>
#define _KERNEL
#include <sys/user.h>
#undef _KERNEL
#undef sigqueue
#include <libutil.h>
#endif
#endif
#else /* !CONFIG_USER_ONLY */
#include "xen-mapcache.h"
#include "trace.h"
#endif

#include "cputlb.h"

#include "memory-internal.h"

//#define DEBUG_TB_INVALIDATE
//#define DEBUG_FLUSH
//#define DEBUG_UNASSIGNED

/* make various TB consistency checks */
//#define DEBUG_TB_CHECK

//#define DEBUG_IOPORT
//#define DEBUG_SUBPAGE

#if !defined(CONFIG_USER_ONLY)
/* TB consistency checks only implemented for usermode emulation.  */
#undef DEBUG_TB_CHECK
#endif

#define SMC_BITMAP_USE_THRESHOLD 10

static TranslationBlock *tbs;
static int code_gen_max_blocks;
TranslationBlock *tb_phys_hash[CODE_GEN_PHYS_HASH_SIZE];
static int nb_tbs;
/* any access to the tbs or the page table must use this lock */
spinlock_t tb_lock = SPIN_LOCK_UNLOCKED;

uint8_t *code_gen_prologue;
static uint8_t *code_gen_buffer;
static size_t code_gen_buffer_size;
/* threshold to flush the translated code buffer */
static size_t code_gen_buffer_max_size;
static uint8_t *code_gen_ptr;

#if !defined(CONFIG_USER_ONLY)
int phys_ram_fd;
static int in_migration;

RAMList ram_list = { .blocks = QLIST_HEAD_INITIALIZER(ram_list.blocks) };

static MemoryRegion *system_memory;
static MemoryRegion *system_io;

AddressSpace address_space_io;
AddressSpace address_space_memory;
DMAContext dma_context_memory;

MemoryRegion io_mem_ram, io_mem_rom, io_mem_unassigned, io_mem_notdirty;
static MemoryRegion io_mem_subpage_ram;

#endif

CPUArchState *first_cpu;
/* current CPU in the current thread. It is only valid inside
   cpu_exec() */
DEFINE_TLS(CPUArchState *,cpu_single_env);
/* 0 = Do not count executed instructions.
   1 = Precise instruction counting.
   2 = Adaptive rate instruction counting.  */
int use_icount = 0;

typedef struct PageDesc {
    /* list of TBs intersecting this ram page */
    TranslationBlock *first_tb;
    /* in order to optimize self modifying code, we count the number
       of lookups we do to a given page to use a bitmap */
    unsigned int code_write_count;
    uint8_t *code_bitmap;
#if defined(CONFIG_USER_ONLY)
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
#define L2_BITS 10
#define L2_SIZE (1 << L2_BITS)

#define P_L2_LEVELS \
    (((TARGET_PHYS_ADDR_SPACE_BITS - TARGET_PAGE_BITS - 1) / L2_BITS) + 1)

/* The bits remaining after N lower levels of page tables.  */
#define V_L1_BITS_REM \
    ((L1_MAP_ADDR_SPACE_BITS - TARGET_PAGE_BITS) % L2_BITS)

#if V_L1_BITS_REM < 4
#define V_L1_BITS  (V_L1_BITS_REM + L2_BITS)
#else
#define V_L1_BITS  V_L1_BITS_REM
#endif

#define V_L1_SIZE  ((target_ulong)1 << V_L1_BITS)

#define V_L1_SHIFT (L1_MAP_ADDR_SPACE_BITS - TARGET_PAGE_BITS - V_L1_BITS)

uintptr_t qemu_real_host_page_size;
uintptr_t qemu_host_page_size;
uintptr_t qemu_host_page_mask;

/* This is a multi-level map on the virtual address space.
   The bottom level has pointers to PageDesc.  */
static void *l1_map[V_L1_SIZE];

#if !defined(CONFIG_USER_ONLY)

static MemoryRegionSection *phys_sections;
static unsigned phys_sections_nb, phys_sections_nb_alloc;
static uint16_t phys_section_unassigned;
static uint16_t phys_section_notdirty;
static uint16_t phys_section_rom;
static uint16_t phys_section_watch;

/* Simple allocator for PhysPageEntry nodes */
static PhysPageEntry (*phys_map_nodes)[L2_SIZE];
static unsigned phys_map_nodes_nb, phys_map_nodes_nb_alloc;

#define PHYS_MAP_NODE_NIL (((uint16_t)~0) >> 1)

static void io_mem_init(void);
static void memory_map_init(void);
static void *qemu_safe_ram_ptr(ram_addr_t addr);

static MemoryRegion io_mem_watch;
#endif
static void tb_link_page(TranslationBlock *tb, tb_page_addr_t phys_pc,
                         tb_page_addr_t phys_page2);

/* statistics */
static int tb_flush_count;
static int tb_phys_invalidate_count;

#ifdef _WIN32
static inline void map_exec(void *addr, long size)
{
    DWORD old_protect;
    VirtualProtect(addr, size,
                   PAGE_EXECUTE_READWRITE, &old_protect);
    
}
#else
static inline void map_exec(void *addr, long size)
{
    unsigned long start, end, page_size;
    
    page_size = getpagesize();
    start = (unsigned long)addr;
    start &= ~(page_size - 1);
    
    end = (unsigned long)addr + size;
    end += page_size - 1;
    end &= ~(page_size - 1);
    
    mprotect((void *)start, end - start,
             PROT_READ | PROT_WRITE | PROT_EXEC);
}
#endif

static void page_init(void)
{
    /* NOTE: we can always suppose that qemu_host_page_size >=
       TARGET_PAGE_SIZE */
#ifdef _WIN32
    {
        SYSTEM_INFO system_info;

        GetSystemInfo(&system_info);
        qemu_real_host_page_size = system_info.dwPageSize;
    }
#else
    qemu_real_host_page_size = getpagesize();
#endif
    if (qemu_host_page_size == 0)
        qemu_host_page_size = qemu_real_host_page_size;
    if (qemu_host_page_size < TARGET_PAGE_SIZE)
        qemu_host_page_size = TARGET_PAGE_SIZE;
    qemu_host_page_mask = ~(qemu_host_page_size - 1);

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

                n = fscanf (f, "%lx-%lx %*[^\n]\n", &startaddr, &endaddr);

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

static PageDesc *page_find_alloc(tb_page_addr_t index, int alloc)
{
    PageDesc *pd;
    void **lp;
    int i;

#if defined(CONFIG_USER_ONLY)
    /* We can't use g_malloc because it may recurse into a locked mutex. */
# define ALLOC(P, SIZE)                                 \
    do {                                                \
        P = mmap(NULL, SIZE, PROT_READ | PROT_WRITE,    \
                 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);   \
    } while (0)
#else
# define ALLOC(P, SIZE) \
    do { P = g_malloc0(SIZE); } while (0)
#endif

    /* Level 1.  Always allocated.  */
    lp = l1_map + ((index >> V_L1_SHIFT) & (V_L1_SIZE - 1));

    /* Level 2..N-1.  */
    for (i = V_L1_SHIFT / L2_BITS - 1; i > 0; i--) {
        void **p = *lp;

        if (p == NULL) {
            if (!alloc) {
                return NULL;
            }
            ALLOC(p, sizeof(void *) * L2_SIZE);
            *lp = p;
        }

        lp = p + ((index >> (i * L2_BITS)) & (L2_SIZE - 1));
    }

    pd = *lp;
    if (pd == NULL) {
        if (!alloc) {
            return NULL;
        }
        ALLOC(pd, sizeof(PageDesc) * L2_SIZE);
        *lp = pd;
    }

#undef ALLOC

    return pd + (index & (L2_SIZE - 1));
}

static inline PageDesc *page_find(tb_page_addr_t index)
{
    return page_find_alloc(index, 0);
}

#if !defined(CONFIG_USER_ONLY)

static void phys_map_node_reserve(unsigned nodes)
{
    if (phys_map_nodes_nb + nodes > phys_map_nodes_nb_alloc) {
        typedef PhysPageEntry Node[L2_SIZE];
        phys_map_nodes_nb_alloc = MAX(phys_map_nodes_nb_alloc * 2, 16);
        phys_map_nodes_nb_alloc = MAX(phys_map_nodes_nb_alloc,
                                      phys_map_nodes_nb + nodes);
        phys_map_nodes = g_renew(Node, phys_map_nodes,
                                 phys_map_nodes_nb_alloc);
    }
}

static uint16_t phys_map_node_alloc(void)
{
    unsigned i;
    uint16_t ret;

    ret = phys_map_nodes_nb++;
    assert(ret != PHYS_MAP_NODE_NIL);
    assert(ret != phys_map_nodes_nb_alloc);
    for (i = 0; i < L2_SIZE; ++i) {
        phys_map_nodes[ret][i].is_leaf = 0;
        phys_map_nodes[ret][i].ptr = PHYS_MAP_NODE_NIL;
    }
    return ret;
}

static void phys_map_nodes_reset(void)
{
    phys_map_nodes_nb = 0;
}


static void phys_page_set_level(PhysPageEntry *lp, hwaddr *index,
                                hwaddr *nb, uint16_t leaf,
                                int level)
{
    PhysPageEntry *p;
    int i;
    hwaddr step = (hwaddr)1 << (level * L2_BITS);

    if (!lp->is_leaf && lp->ptr == PHYS_MAP_NODE_NIL) {
        lp->ptr = phys_map_node_alloc();
        p = phys_map_nodes[lp->ptr];
        if (level == 0) {
            for (i = 0; i < L2_SIZE; i++) {
                p[i].is_leaf = 1;
                p[i].ptr = phys_section_unassigned;
            }
        }
    } else {
        p = phys_map_nodes[lp->ptr];
    }
    lp = &p[(*index >> (level * L2_BITS)) & (L2_SIZE - 1)];

    while (*nb && lp < &p[L2_SIZE]) {
        if ((*index & (step - 1)) == 0 && *nb >= step) {
            lp->is_leaf = true;
            lp->ptr = leaf;
            *index += step;
            *nb -= step;
        } else {
            phys_page_set_level(lp, index, nb, leaf, level - 1);
        }
        ++lp;
    }
}

static void phys_page_set(AddressSpaceDispatch *d,
                          hwaddr index, hwaddr nb,
                          uint16_t leaf)
{
    /* Wildly overreserve - it doesn't matter much. */
    phys_map_node_reserve(3 * P_L2_LEVELS);

    phys_page_set_level(&d->phys_map, &index, &nb, leaf, P_L2_LEVELS - 1);
}

MemoryRegionSection *phys_page_find(AddressSpaceDispatch *d, hwaddr index)
{
    PhysPageEntry lp = d->phys_map;
    PhysPageEntry *p;
    int i;
    uint16_t s_index = phys_section_unassigned;

    for (i = P_L2_LEVELS - 1; i >= 0 && !lp.is_leaf; i--) {
        if (lp.ptr == PHYS_MAP_NODE_NIL) {
            goto not_found;
        }
        p = phys_map_nodes[lp.ptr];
        lp = p[(index >> (i * L2_BITS)) & (L2_SIZE - 1)];
    }

    s_index = lp.ptr;
not_found:
    return &phys_sections[s_index];
}

bool memory_region_is_unassigned(MemoryRegion *mr)
{
    return mr != &io_mem_ram && mr != &io_mem_rom
        && mr != &io_mem_notdirty && !mr->rom_device
        && mr != &io_mem_watch;
}

#define mmap_lock() do { } while(0)
#define mmap_unlock() do { } while(0)
#endif

#if defined(CONFIG_USER_ONLY)
/* Currently it is not recommended to allocate big chunks of data in
   user mode. It will change when a dedicated libc will be used.  */
/* ??? 64-bit hosts ought to have no problem mmaping data outside the
   region in which the guest needs to run.  Revisit this.  */
#define USE_STATIC_CODE_GEN_BUFFER
#endif

/* ??? Should configure for this, not list operating systems here.  */
#if (defined(__linux__) \
    || defined(__FreeBSD__) || defined(__FreeBSD_kernel__) \
    || defined(__DragonFly__) || defined(__OpenBSD__) \
    || defined(__NetBSD__))
# define USE_MMAP
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
#elif defined(__arm__)
# define MAX_CODE_GEN_BUFFER_SIZE  (16u * 1024 * 1024)
#elif defined(__s390x__)
  /* We have a +- 4GB range on the branches; leave some slop.  */
# define MAX_CODE_GEN_BUFFER_SIZE  (3ul * 1024 * 1024 * 1024)
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
    code_gen_buffer_size = tb_size;
    return tb_size;
}

#ifdef USE_STATIC_CODE_GEN_BUFFER
static uint8_t static_code_gen_buffer[DEFAULT_CODE_GEN_BUFFER_SIZE]
    __attribute__((aligned(CODE_GEN_ALIGN)));

static inline void *alloc_code_gen_buffer(void)
{
    map_exec(static_code_gen_buffer, code_gen_buffer_size);
    return static_code_gen_buffer;
}
#elif defined(USE_MMAP)
static inline void *alloc_code_gen_buffer(void)
{
    int flags = MAP_PRIVATE | MAP_ANONYMOUS;
    uintptr_t start = 0;
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
    if (code_gen_buffer_size > 800u * 1024 * 1024) {
        code_gen_buffer_size = 800u * 1024 * 1024;
    }
# elif defined(__sparc__)
    start = 0x40000000ul;
# elif defined(__s390x__)
    start = 0x90000000ul;
# endif

    buf = mmap((void *)start, code_gen_buffer_size,
               PROT_WRITE | PROT_READ | PROT_EXEC, flags, -1, 0);
    return buf == MAP_FAILED ? NULL : buf;
}
#else
static inline void *alloc_code_gen_buffer(void)
{
    void *buf = g_malloc(code_gen_buffer_size);
    if (buf) {
        map_exec(buf, code_gen_buffer_size);
    }
    return buf;
}
#endif /* USE_STATIC_CODE_GEN_BUFFER, USE_MMAP */

static inline void code_gen_alloc(size_t tb_size)
{
    code_gen_buffer_size = size_code_gen_buffer(tb_size);
    code_gen_buffer = alloc_code_gen_buffer();
    if (code_gen_buffer == NULL) {
        fprintf(stderr, "Could not allocate dynamic translator buffer\n");
        exit(1);
    }

    /* Steal room for the prologue at the end of the buffer.  This ensures
       (via the MAX_CODE_GEN_BUFFER_SIZE limits above) that direct branches
       from TB's to the prologue are going to be in range.  It also means
       that we don't need to mark (additional) portions of the data segment
       as executable.  */
    code_gen_prologue = code_gen_buffer + code_gen_buffer_size - 1024;
    code_gen_buffer_size -= 1024;

    code_gen_buffer_max_size = code_gen_buffer_size -
        (TCG_MAX_OP_SIZE * OPC_BUF_SIZE);
    code_gen_max_blocks = code_gen_buffer_size / CODE_GEN_AVG_BLOCK_SIZE;
    tbs = g_malloc(code_gen_max_blocks * sizeof(TranslationBlock));
}

/* Must be called before using the QEMU cpus. 'tb_size' is the size
   (in bytes) allocated to the translation buffer. Zero means default
   size. */
void tcg_exec_init(unsigned long tb_size)
{
    cpu_gen_init();
    code_gen_alloc(tb_size);
    code_gen_ptr = code_gen_buffer;
    tcg_register_jit(code_gen_buffer, code_gen_buffer_size);
    page_init();
#if !defined(CONFIG_USER_ONLY) || !defined(CONFIG_USE_GUEST_BASE)
    /* There's no guest base to take into account, so go ahead and
       initialize the prologue now.  */
    tcg_prologue_init(&tcg_ctx);
#endif
}

bool tcg_enabled(void)
{
    return code_gen_buffer != NULL;
}

void cpu_exec_init_all(void)
{
#if !defined(CONFIG_USER_ONLY)
    memory_map_init();
    io_mem_init();
#endif
}

#if defined(CPU_SAVE_VERSION) && !defined(CONFIG_USER_ONLY)

static int cpu_common_post_load(void *opaque, int version_id)
{
    CPUArchState *env = opaque;

    /* 0x01 was CPU_INTERRUPT_EXIT. This line can be removed when the
       version_id is increased. */
    env->interrupt_request &= ~0x01;
    tlb_flush(env, 1);

    return 0;
}

static const VMStateDescription vmstate_cpu_common = {
    .name = "cpu_common",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .post_load = cpu_common_post_load,
    .fields      = (VMStateField []) {
        VMSTATE_UINT32(halted, CPUArchState),
        VMSTATE_UINT32(interrupt_request, CPUArchState),
        VMSTATE_END_OF_LIST()
    }
};
#endif

CPUArchState *qemu_get_cpu(int cpu)
{
    CPUArchState *env = first_cpu;

    while (env) {
        if (env->cpu_index == cpu)
            break;
        env = env->next_cpu;
    }

    return env;
}

void cpu_exec_init(CPUArchState *env)
{
#ifndef CONFIG_USER_ONLY
    CPUState *cpu = ENV_GET_CPU(env);
#endif
    CPUArchState **penv;
    int cpu_index;

#if defined(CONFIG_USER_ONLY)
    cpu_list_lock();
#endif
    env->next_cpu = NULL;
    penv = &first_cpu;
    cpu_index = 0;
    while (*penv != NULL) {
        penv = &(*penv)->next_cpu;
        cpu_index++;
    }
    env->cpu_index = cpu_index;
    env->numa_node = 0;
    QTAILQ_INIT(&env->breakpoints);
    QTAILQ_INIT(&env->watchpoints);
#ifndef CONFIG_USER_ONLY
    cpu->thread_id = qemu_get_thread_id();
#endif
    *penv = env;
#if defined(CONFIG_USER_ONLY)
    cpu_list_unlock();
#endif
#if defined(CPU_SAVE_VERSION) && !defined(CONFIG_USER_ONLY)
    vmstate_register(NULL, cpu_index, &vmstate_cpu_common, env);
    register_savevm(NULL, "cpu", cpu_index, CPU_SAVE_VERSION,
                    cpu_save, cpu_load, env);
#endif
}

/* Allocate a new translation block. Flush the translation buffer if
   too many translation blocks or too much generated code. */
static TranslationBlock *tb_alloc(target_ulong pc)
{
    TranslationBlock *tb;

    if (nb_tbs >= code_gen_max_blocks ||
        (code_gen_ptr - code_gen_buffer) >= code_gen_buffer_max_size)
        return NULL;
    tb = &tbs[nb_tbs++];
    tb->pc = pc;
    tb->cflags = 0;
    return tb;
}

void tb_free(TranslationBlock *tb)
{
    /* In practice this is mostly used for single use temporary TB
       Ignore the hard cases and just back up if this TB happens to
       be the last one generated.  */
    if (nb_tbs > 0 && tb == &tbs[nb_tbs - 1]) {
        code_gen_ptr = tb->tc_ptr;
        nb_tbs--;
    }
}

static inline void invalidate_page_bitmap(PageDesc *p)
{
    if (p->code_bitmap) {
        g_free(p->code_bitmap);
        p->code_bitmap = NULL;
    }
    p->code_write_count = 0;
}

/* Set to NULL all the 'first_tb' fields in all PageDescs. */

static void page_flush_tb_1 (int level, void **lp)
{
    int i;

    if (*lp == NULL) {
        return;
    }
    if (level == 0) {
        PageDesc *pd = *lp;
        for (i = 0; i < L2_SIZE; ++i) {
            pd[i].first_tb = NULL;
            invalidate_page_bitmap(pd + i);
        }
    } else {
        void **pp = *lp;
        for (i = 0; i < L2_SIZE; ++i) {
            page_flush_tb_1 (level - 1, pp + i);
        }
    }
}

static void page_flush_tb(void)
{
    int i;
    for (i = 0; i < V_L1_SIZE; i++) {
        page_flush_tb_1(V_L1_SHIFT / L2_BITS - 1, l1_map + i);
    }
}

/* flush all the translation blocks */
/* XXX: tb_flush is currently not thread safe */
void tb_flush(CPUArchState *env1)
{
    CPUArchState *env;
#if defined(DEBUG_FLUSH)
    printf("qemu: flush code_size=%ld nb_tbs=%d avg_tb_size=%ld\n",
           (unsigned long)(code_gen_ptr - code_gen_buffer),
           nb_tbs, nb_tbs > 0 ?
           ((unsigned long)(code_gen_ptr - code_gen_buffer)) / nb_tbs : 0);
#endif
    if ((unsigned long)(code_gen_ptr - code_gen_buffer) > code_gen_buffer_size)
        cpu_abort(env1, "Internal error: code buffer overflow\n");

    nb_tbs = 0;

    for(env = first_cpu; env != NULL; env = env->next_cpu) {
        memset (env->tb_jmp_cache, 0, TB_JMP_CACHE_SIZE * sizeof (void *));
    }

    memset (tb_phys_hash, 0, CODE_GEN_PHYS_HASH_SIZE * sizeof (void *));
    page_flush_tb();

    code_gen_ptr = code_gen_buffer;
    /* XXX: flush processor icache at this point if cache flush is
       expensive */
    tb_flush_count++;
}

#ifdef DEBUG_TB_CHECK

static void tb_invalidate_check(target_ulong address)
{
    TranslationBlock *tb;
    int i;
    address &= TARGET_PAGE_MASK;
    for(i = 0;i < CODE_GEN_PHYS_HASH_SIZE; i++) {
        for(tb = tb_phys_hash[i]; tb != NULL; tb = tb->phys_hash_next) {
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

    for(i = 0;i < CODE_GEN_PHYS_HASH_SIZE; i++) {
        for(tb = tb_phys_hash[i]; tb != NULL; tb = tb->phys_hash_next) {
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
        n1 = (uintptr_t)tb1 & 3;
        tb1 = (TranslationBlock *)((uintptr_t)tb1 & ~3);
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
            n1 = (uintptr_t)tb1 & 3;
            tb1 = (TranslationBlock *)((uintptr_t)tb1 & ~3);
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
    tb_set_jmp_target(tb, n, (uintptr_t)(tb->tc_ptr + tb->tb_next_offset[n]));
}

void tb_phys_invalidate(TranslationBlock *tb, tb_page_addr_t page_addr)
{
    CPUArchState *env;
    PageDesc *p;
    unsigned int h, n1;
    tb_page_addr_t phys_pc;
    TranslationBlock *tb1, *tb2;

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

    tb_invalidated_flag = 1;

    /* remove the TB from the hash list */
    h = tb_jmp_cache_hash_func(tb->pc);
    for(env = first_cpu; env != NULL; env = env->next_cpu) {
        if (env->tb_jmp_cache[h] == tb)
            env->tb_jmp_cache[h] = NULL;
    }

    /* suppress this TB from the two jump lists */
    tb_jmp_remove(tb, 0);
    tb_jmp_remove(tb, 1);

    /* suppress any remaining jumps to this TB */
    tb1 = tb->jmp_first;
    for(;;) {
        n1 = (uintptr_t)tb1 & 3;
        if (n1 == 2)
            break;
        tb1 = (TranslationBlock *)((uintptr_t)tb1 & ~3);
        tb2 = tb1->jmp_next[n1];
        tb_reset_jump(tb1, n1);
        tb1->jmp_next[n1] = NULL;
        tb1 = tb2;
    }
    tb->jmp_first = (TranslationBlock *)((uintptr_t)tb | 2); /* fail safe */

    tb_phys_invalidate_count++;
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

    p->code_bitmap = g_malloc0(TARGET_PAGE_SIZE / 8);

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

TranslationBlock *tb_gen_code(CPUArchState *env,
                              target_ulong pc, target_ulong cs_base,
                              int flags, int cflags)
{
    TranslationBlock *tb;
    uint8_t *tc_ptr;
    tb_page_addr_t phys_pc, phys_page2;
    target_ulong virt_page2;
    int code_gen_size;

    phys_pc = get_page_addr_code(env, pc);
    tb = tb_alloc(pc);
    if (!tb) {
        /* flush must be done */
        tb_flush(env);
        /* cannot fail at this point */
        tb = tb_alloc(pc);
        /* Don't forget to invalidate previous TB info.  */
        tb_invalidated_flag = 1;
    }
    tc_ptr = code_gen_ptr;
    tb->tc_ptr = tc_ptr;
    tb->cs_base = cs_base;
    tb->flags = flags;
    tb->cflags = cflags;
    cpu_gen_code(env, tb, &code_gen_size);
    code_gen_ptr = (void *)(((uintptr_t)code_gen_ptr + code_gen_size +
                             CODE_GEN_ALIGN - 1) & ~(CODE_GEN_ALIGN - 1));

    /* check next page if needed */
    virt_page2 = (pc + tb->size - 1) & TARGET_PAGE_MASK;
    phys_page2 = -1;
    if ((pc & TARGET_PAGE_MASK) != virt_page2) {
        phys_page2 = get_page_addr_code(env, virt_page2);
    }
    tb_link_page(tb, phys_pc, phys_page2);
    return tb;
}

/*
 * Invalidate all TBs which intersect with the target physical address range
 * [start;end[. NOTE: start and end may refer to *different* physical pages.
 * 'is_cpu_write_access' should be true if called from a real cpu write
 * access: the virtual CPU will exit the current TB if code is modified inside
 * this TB.
 */
void tb_invalidate_phys_range(tb_page_addr_t start, tb_page_addr_t end,
                              int is_cpu_write_access)
{
    while (start < end) {
        tb_invalidate_phys_page_range(start, end, is_cpu_write_access);
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
 */
void tb_invalidate_phys_page_range(tb_page_addr_t start, tb_page_addr_t end,
                                   int is_cpu_write_access)
{
    TranslationBlock *tb, *tb_next, *saved_tb;
    CPUArchState *env = cpu_single_env;
    tb_page_addr_t tb_start, tb_end;
    PageDesc *p;
    int n;
#ifdef TARGET_HAS_PRECISE_SMC
    int current_tb_not_found = is_cpu_write_access;
    TranslationBlock *current_tb = NULL;
    int current_tb_modified = 0;
    target_ulong current_pc = 0;
    target_ulong current_cs_base = 0;
    int current_flags = 0;
#endif /* TARGET_HAS_PRECISE_SMC */

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
                if (env->mem_io_pc) {
                    /* now we have a real cpu fault */
                    current_tb = tb_find_pc(env->mem_io_pc);
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
                cpu_restore_state(current_tb, env, env->mem_io_pc);
                cpu_get_tb_cpu_state(env, &current_pc, &current_cs_base,
                                     &current_flags);
            }
#endif /* TARGET_HAS_PRECISE_SMC */
            /* we need to do that to handle the case where a signal
               occurs while doing tb_phys_invalidate() */
            saved_tb = NULL;
            if (env) {
                saved_tb = env->current_tb;
                env->current_tb = NULL;
            }
            tb_phys_invalidate(tb, -1);
            if (env) {
                env->current_tb = saved_tb;
                if (env->interrupt_request && env->current_tb)
                    cpu_interrupt(env, env->interrupt_request);
            }
        }
        tb = tb_next;
    }
#if !defined(CONFIG_USER_ONLY)
    /* if no code remaining, no need to continue to use slow writes */
    if (!p->first_tb) {
        invalidate_page_bitmap(p);
        if (is_cpu_write_access) {
            tlb_unprotect_code_phys(env, start, env->mem_io_vaddr);
        }
    }
#endif
#ifdef TARGET_HAS_PRECISE_SMC
    if (current_tb_modified) {
        /* we generate a block containing just the instruction
           modifying the memory. It will ensure that it cannot modify
           itself */
        env->current_tb = NULL;
        tb_gen_code(env, current_pc, current_cs_base, current_flags, 1);
        cpu_resume_from_signal(env, NULL);
    }
#endif
}

/* len must be <= 8 and start must be a multiple of len */
static inline void tb_invalidate_phys_page_fast(tb_page_addr_t start, int len)
{
    PageDesc *p;
    int offset, b;
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
static void tb_invalidate_phys_page(tb_page_addr_t addr,
                                    uintptr_t pc, void *puc)
{
    TranslationBlock *tb;
    PageDesc *p;
    int n;
#ifdef TARGET_HAS_PRECISE_SMC
    TranslationBlock *current_tb = NULL;
    CPUArchState *env = cpu_single_env;
    int current_tb_modified = 0;
    target_ulong current_pc = 0;
    target_ulong current_cs_base = 0;
    int current_flags = 0;
#endif

    addr &= TARGET_PAGE_MASK;
    p = page_find(addr >> TARGET_PAGE_BITS);
    if (!p)
        return;
    tb = p->first_tb;
#ifdef TARGET_HAS_PRECISE_SMC
    if (tb && pc != 0) {
        current_tb = tb_find_pc(pc);
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
            cpu_restore_state(current_tb, env, pc);
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
        env->current_tb = NULL;
        tb_gen_code(env, current_pc, current_cs_base, current_flags, 1);
        cpu_resume_from_signal(env, puc);
    }
#endif
}
#endif

/* add the tb in the target page and protect it if necessary */
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

#if defined(TARGET_HAS_SMC) || 1

#if defined(CONFIG_USER_ONLY)
    if (p->flags & PAGE_WRITE) {
        target_ulong addr;
        PageDesc *p2;
        int prot;

        /* force the host page as non writable (writes will have a
           page fault + mprotect overhead) */
        page_addr &= qemu_host_page_mask;
        prot = 0;
        for(addr = page_addr; addr < page_addr + qemu_host_page_size;
            addr += TARGET_PAGE_SIZE) {

            p2 = page_find (addr >> TARGET_PAGE_BITS);
            if (!p2)
                continue;
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

#endif /* TARGET_HAS_SMC */
}

/* add a new TB and link it to the physical page tables. phys_page2 is
   (-1) to indicate that only one page contains the TB. */
static void tb_link_page(TranslationBlock *tb, tb_page_addr_t phys_pc,
                         tb_page_addr_t phys_page2)
{
    unsigned int h;
    TranslationBlock **ptb;

    /* Grab the mmap lock to stop another thread invalidating this TB
       before we are done.  */
    mmap_lock();
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

    tb->jmp_first = (TranslationBlock *)((uintptr_t)tb | 2);
    tb->jmp_next[0] = NULL;
    tb->jmp_next[1] = NULL;

    /* init original jump addresses */
    if (tb->tb_next_offset[0] != 0xffff)
        tb_reset_jump(tb, 0);
    if (tb->tb_next_offset[1] != 0xffff)
        tb_reset_jump(tb, 1);

#ifdef DEBUG_TB_CHECK
    tb_page_check();
#endif
    mmap_unlock();
}

#if defined(CONFIG_QEMU_LDST_OPTIMIZATION) && defined(CONFIG_SOFTMMU)
/* check whether the given addr is in TCG generated code buffer or not */
bool is_tcg_gen_code(uintptr_t tc_ptr)
{
    /* This can be called during code generation, code_gen_buffer_max_size
       is used instead of code_gen_ptr for upper boundary checking */
    return (tc_ptr >= (uintptr_t)code_gen_buffer &&
            tc_ptr < (uintptr_t)(code_gen_buffer + code_gen_buffer_max_size));
}
#endif

/* find the TB 'tb' such that tb[0].tc_ptr <= tc_ptr <
   tb[1].tc_ptr. Return NULL if not found */
TranslationBlock *tb_find_pc(uintptr_t tc_ptr)
{
    int m_min, m_max, m;
    uintptr_t v;
    TranslationBlock *tb;

    if (nb_tbs <= 0)
        return NULL;
    if (tc_ptr < (uintptr_t)code_gen_buffer ||
        tc_ptr >= (uintptr_t)code_gen_ptr) {
        return NULL;
    }
    /* binary search (cf Knuth) */
    m_min = 0;
    m_max = nb_tbs - 1;
    while (m_min <= m_max) {
        m = (m_min + m_max) >> 1;
        tb = &tbs[m];
        v = (uintptr_t)tb->tc_ptr;
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
            n1 = (uintptr_t)tb1 & 3;
            tb1 = (TranslationBlock *)((uintptr_t)tb1 & ~3);
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
            n1 = (uintptr_t)tb1 & 3;
            tb1 = (TranslationBlock *)((uintptr_t)tb1 & ~3);
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

#if defined(TARGET_HAS_ICE)
#if defined(CONFIG_USER_ONLY)
static void breakpoint_invalidate(CPUArchState *env, target_ulong pc)
{
    tb_invalidate_phys_page_range(pc, pc + 1, 0);
}
#else
void tb_invalidate_phys_addr(hwaddr addr)
{
    ram_addr_t ram_addr;
    MemoryRegionSection *section;

    section = phys_page_find(address_space_memory.dispatch, addr >> TARGET_PAGE_BITS);
    if (!(memory_region_is_ram(section->mr)
          || (section->mr->rom_device && section->mr->readable))) {
        return;
    }
    ram_addr = (memory_region_get_ram_addr(section->mr) & TARGET_PAGE_MASK)
        + memory_region_section_addr(section, addr);
    tb_invalidate_phys_page_range(ram_addr, ram_addr + 1, 0);
}

static void breakpoint_invalidate(CPUArchState *env, target_ulong pc)
{
    tb_invalidate_phys_addr(cpu_get_phys_page_debug(env, pc) |
            (pc & ~TARGET_PAGE_MASK));
}
#endif
#endif /* TARGET_HAS_ICE */

#if defined(CONFIG_USER_ONLY)
void cpu_watchpoint_remove_all(CPUArchState *env, int mask)

{
}

int cpu_watchpoint_insert(CPUArchState *env, target_ulong addr, target_ulong len,
                          int flags, CPUWatchpoint **watchpoint)
{
    return -ENOSYS;
}
#else
/* Add a watchpoint.  */
int cpu_watchpoint_insert(CPUArchState *env, target_ulong addr, target_ulong len,
                          int flags, CPUWatchpoint **watchpoint)
{
    target_ulong len_mask = ~(len - 1);
    CPUWatchpoint *wp;

    /* sanity checks: allow power-of-2 lengths, deny unaligned watchpoints */
    if ((len & (len - 1)) || (addr & ~len_mask) ||
            len == 0 || len > TARGET_PAGE_SIZE) {
        fprintf(stderr, "qemu: tried to set invalid watchpoint at "
                TARGET_FMT_lx ", len=" TARGET_FMT_lu "\n", addr, len);
        return -EINVAL;
    }
    wp = g_malloc(sizeof(*wp));

    wp->vaddr = addr;
    wp->len_mask = len_mask;
    wp->flags = flags;

    /* keep all GDB-injected watchpoints in front */
    if (flags & BP_GDB)
        QTAILQ_INSERT_HEAD(&env->watchpoints, wp, entry);
    else
        QTAILQ_INSERT_TAIL(&env->watchpoints, wp, entry);

    tlb_flush_page(env, addr);

    if (watchpoint)
        *watchpoint = wp;
    return 0;
}

/* Remove a specific watchpoint.  */
int cpu_watchpoint_remove(CPUArchState *env, target_ulong addr, target_ulong len,
                          int flags)
{
    target_ulong len_mask = ~(len - 1);
    CPUWatchpoint *wp;

    QTAILQ_FOREACH(wp, &env->watchpoints, entry) {
        if (addr == wp->vaddr && len_mask == wp->len_mask
                && flags == (wp->flags & ~BP_WATCHPOINT_HIT)) {
            cpu_watchpoint_remove_by_ref(env, wp);
            return 0;
        }
    }
    return -ENOENT;
}

/* Remove a specific watchpoint by reference.  */
void cpu_watchpoint_remove_by_ref(CPUArchState *env, CPUWatchpoint *watchpoint)
{
    QTAILQ_REMOVE(&env->watchpoints, watchpoint, entry);

    tlb_flush_page(env, watchpoint->vaddr);

    g_free(watchpoint);
}

/* Remove all matching watchpoints.  */
void cpu_watchpoint_remove_all(CPUArchState *env, int mask)
{
    CPUWatchpoint *wp, *next;

    QTAILQ_FOREACH_SAFE(wp, &env->watchpoints, entry, next) {
        if (wp->flags & mask)
            cpu_watchpoint_remove_by_ref(env, wp);
    }
}
#endif

/* Add a breakpoint.  */
int cpu_breakpoint_insert(CPUArchState *env, target_ulong pc, int flags,
                          CPUBreakpoint **breakpoint)
{
#if defined(TARGET_HAS_ICE)
    CPUBreakpoint *bp;

    bp = g_malloc(sizeof(*bp));

    bp->pc = pc;
    bp->flags = flags;

    /* keep all GDB-injected breakpoints in front */
    if (flags & BP_GDB)
        QTAILQ_INSERT_HEAD(&env->breakpoints, bp, entry);
    else
        QTAILQ_INSERT_TAIL(&env->breakpoints, bp, entry);

    breakpoint_invalidate(env, pc);

    if (breakpoint)
        *breakpoint = bp;
    return 0;
#else
    return -ENOSYS;
#endif
}

/* Remove a specific breakpoint.  */
int cpu_breakpoint_remove(CPUArchState *env, target_ulong pc, int flags)
{
#if defined(TARGET_HAS_ICE)
    CPUBreakpoint *bp;

    QTAILQ_FOREACH(bp, &env->breakpoints, entry) {
        if (bp->pc == pc && bp->flags == flags) {
            cpu_breakpoint_remove_by_ref(env, bp);
            return 0;
        }
    }
    return -ENOENT;
#else
    return -ENOSYS;
#endif
}

/* Remove a specific breakpoint by reference.  */
void cpu_breakpoint_remove_by_ref(CPUArchState *env, CPUBreakpoint *breakpoint)
{
#if defined(TARGET_HAS_ICE)
    QTAILQ_REMOVE(&env->breakpoints, breakpoint, entry);

    breakpoint_invalidate(env, breakpoint->pc);

    g_free(breakpoint);
#endif
}

/* Remove all matching breakpoints. */
void cpu_breakpoint_remove_all(CPUArchState *env, int mask)
{
#if defined(TARGET_HAS_ICE)
    CPUBreakpoint *bp, *next;

    QTAILQ_FOREACH_SAFE(bp, &env->breakpoints, entry, next) {
        if (bp->flags & mask)
            cpu_breakpoint_remove_by_ref(env, bp);
    }
#endif
}

/* enable or disable single step mode. EXCP_DEBUG is returned by the
   CPU loop after each instruction */
void cpu_single_step(CPUArchState *env, int enabled)
{
#if defined(TARGET_HAS_ICE)
    if (env->singlestep_enabled != enabled) {
        env->singlestep_enabled = enabled;
        if (kvm_enabled())
            kvm_update_guest_debug(env, 0);
        else {
            /* must flush all the translated code to avoid inconsistencies */
            /* XXX: only flush what is necessary */
            tb_flush(env);
        }
    }
#endif
}

static void cpu_unlink_tb(CPUArchState *env)
{
    /* FIXME: TB unchaining isn't SMP safe.  For now just ignore the
       problem and hope the cpu will stop of its own accord.  For userspace
       emulation this often isn't actually as bad as it sounds.  Often
       signals are used primarily to interrupt blocking syscalls.  */
    TranslationBlock *tb;
    static spinlock_t interrupt_lock = SPIN_LOCK_UNLOCKED;

    spin_lock(&interrupt_lock);
    tb = env->current_tb;
    /* if the cpu is currently executing code, we must unlink it and
       all the potentially executing TB */
    if (tb) {
        env->current_tb = NULL;
        tb_reset_jump_recursive(tb);
    }
    spin_unlock(&interrupt_lock);
}

#ifndef CONFIG_USER_ONLY
/* mask must never be zero, except for A20 change call */
static void tcg_handle_interrupt(CPUArchState *env, int mask)
{
    CPUState *cpu = ENV_GET_CPU(env);
    int old_mask;

    old_mask = env->interrupt_request;
    env->interrupt_request |= mask;

    /*
     * If called from iothread context, wake the target cpu in
     * case its halted.
     */
    if (!qemu_cpu_is_self(cpu)) {
        qemu_cpu_kick(cpu);
        return;
    }

    if (use_icount) {
        env->icount_decr.u16.high = 0xffff;
        if (!can_do_io(env)
            && (mask & ~old_mask) != 0) {
            cpu_abort(env, "Raised interrupt while not in I/O function");
        }
    } else {
        cpu_unlink_tb(env);
    }
}

CPUInterruptHandler cpu_interrupt_handler = tcg_handle_interrupt;

#else /* CONFIG_USER_ONLY */

void cpu_interrupt(CPUArchState *env, int mask)
{
    env->interrupt_request |= mask;
    cpu_unlink_tb(env);
}
#endif /* CONFIG_USER_ONLY */

void cpu_reset_interrupt(CPUArchState *env, int mask)
{
    env->interrupt_request &= ~mask;
}

void cpu_exit(CPUArchState *env)
{
    env->exit_request = 1;
    cpu_unlink_tb(env);
}

void cpu_abort(CPUArchState *env, const char *fmt, ...)
{
    va_list ap;
    va_list ap2;

    va_start(ap, fmt);
    va_copy(ap2, ap);
    fprintf(stderr, "qemu: fatal: ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    cpu_dump_state(env, stderr, fprintf, CPU_DUMP_FPU | CPU_DUMP_CCOP);
    if (qemu_log_enabled()) {
        qemu_log("qemu: fatal: ");
        qemu_log_vprintf(fmt, ap2);
        qemu_log("\n");
        log_cpu_state(env, CPU_DUMP_FPU | CPU_DUMP_CCOP);
        qemu_log_flush();
        qemu_log_close();
    }
    va_end(ap2);
    va_end(ap);
#if defined(CONFIG_USER_ONLY)
    {
        struct sigaction act;
        sigfillset(&act.sa_mask);
        act.sa_handler = SIG_DFL;
        sigaction(SIGABRT, &act, NULL);
    }
#endif
    abort();
}

CPUArchState *cpu_copy(CPUArchState *env)
{
    CPUArchState *new_env = cpu_init(env->cpu_model_str);
    CPUArchState *next_cpu = new_env->next_cpu;
    int cpu_index = new_env->cpu_index;
#if defined(TARGET_HAS_ICE)
    CPUBreakpoint *bp;
    CPUWatchpoint *wp;
#endif

    memcpy(new_env, env, sizeof(CPUArchState));

    /* Preserve chaining and index. */
    new_env->next_cpu = next_cpu;
    new_env->cpu_index = cpu_index;

    /* Clone all break/watchpoints.
       Note: Once we support ptrace with hw-debug register access, make sure
       BP_CPU break/watchpoints are handled correctly on clone. */
    QTAILQ_INIT(&env->breakpoints);
    QTAILQ_INIT(&env->watchpoints);
#if defined(TARGET_HAS_ICE)
    QTAILQ_FOREACH(bp, &env->breakpoints, entry) {
        cpu_breakpoint_insert(new_env, bp->pc, bp->flags, NULL);
    }
    QTAILQ_FOREACH(wp, &env->watchpoints, entry) {
        cpu_watchpoint_insert(new_env, wp->vaddr, (~wp->len_mask) + 1,
                              wp->flags, NULL);
    }
#endif

    return new_env;
}

#if !defined(CONFIG_USER_ONLY)
void tb_flush_jmp_cache(CPUArchState *env, target_ulong addr)
{
    unsigned int i;

    /* Discard jump cache entries for any tb which might potentially
       overlap the flushed page.  */
    i = tb_jmp_cache_hash_page(addr - TARGET_PAGE_SIZE);
    memset (&env->tb_jmp_cache[i], 0, 
            TB_JMP_PAGE_SIZE * sizeof(TranslationBlock *));

    i = tb_jmp_cache_hash_page(addr);
    memset (&env->tb_jmp_cache[i], 0, 
            TB_JMP_PAGE_SIZE * sizeof(TranslationBlock *));
}

static void tlb_reset_dirty_range_all(ram_addr_t start, ram_addr_t end,
                                      uintptr_t length)
{
    uintptr_t start1;

    /* we modify the TLB cache so that the dirty bit will be set again
       when accessing the range */
    start1 = (uintptr_t)qemu_safe_ram_ptr(start);
    /* Check that we don't span multiple blocks - this breaks the
       address comparisons below.  */
    if ((uintptr_t)qemu_safe_ram_ptr(end - 1) - start1
            != (end - 1) - start) {
        abort();
    }
    cpu_tlb_reset_dirty_all(start1, length);

}

/* Note: start and end must be within the same ram block.  */
void cpu_physical_memory_reset_dirty(ram_addr_t start, ram_addr_t end,
                                     int dirty_flags)
{
    uintptr_t length;

    start &= TARGET_PAGE_MASK;
    end = TARGET_PAGE_ALIGN(end);

    length = end - start;
    if (length == 0)
        return;
    cpu_physical_memory_mask_dirty_range(start, length, dirty_flags);

    if (tcg_enabled()) {
        tlb_reset_dirty_range_all(start, end, length);
    }
}

static int cpu_physical_memory_set_dirty_tracking(int enable)
{
    int ret = 0;
    in_migration = enable;
    return ret;
}

hwaddr memory_region_section_get_iotlb(CPUArchState *env,
                                                   MemoryRegionSection *section,
                                                   target_ulong vaddr,
                                                   hwaddr paddr,
                                                   int prot,
                                                   target_ulong *address)
{
    hwaddr iotlb;
    CPUWatchpoint *wp;

    if (memory_region_is_ram(section->mr)) {
        /* Normal RAM.  */
        iotlb = (memory_region_get_ram_addr(section->mr) & TARGET_PAGE_MASK)
            + memory_region_section_addr(section, paddr);
        if (!section->readonly) {
            iotlb |= phys_section_notdirty;
        } else {
            iotlb |= phys_section_rom;
        }
    } else {
        /* IO handlers are currently passed a physical address.
           It would be nice to pass an offset from the base address
           of that region.  This would avoid having to special case RAM,
           and avoid full address decoding in every device.
           We can't use the high bits of pd for this because
           IO_MEM_ROMD uses these as a ram address.  */
        iotlb = section - phys_sections;
        iotlb += memory_region_section_addr(section, paddr);
    }

    /* Make accesses to pages with watchpoints go via the
       watchpoint trap routines.  */
    QTAILQ_FOREACH(wp, &env->watchpoints, entry) {
        if (vaddr == (wp->vaddr & TARGET_PAGE_MASK)) {
            /* Avoid trapping reads of pages with a write breakpoint. */
            if ((prot & PAGE_WRITE) || (wp->flags & BP_MEM_READ)) {
                iotlb = phys_section_watch + paddr;
                *address |= TLB_MMIO;
                break;
            }
        }
    }

    return iotlb;
}

#else
/*
 * Walks guest process memory "regions" one by one
 * and calls callback function 'fn' for each region.
 */

struct walk_memory_regions_data
{
    walk_memory_regions_fn fn;
    void *priv;
    uintptr_t start;
    int prot;
};

static int walk_memory_regions_end(struct walk_memory_regions_data *data,
                                   abi_ulong end, int new_prot)
{
    if (data->start != -1ul) {
        int rc = data->fn(data->priv, data->start, end, data->prot);
        if (rc != 0) {
            return rc;
        }
    }

    data->start = (new_prot ? end : -1ul);
    data->prot = new_prot;

    return 0;
}

static int walk_memory_regions_1(struct walk_memory_regions_data *data,
                                 abi_ulong base, int level, void **lp)
{
    abi_ulong pa;
    int i, rc;

    if (*lp == NULL) {
        return walk_memory_regions_end(data, base, 0);
    }

    if (level == 0) {
        PageDesc *pd = *lp;
        for (i = 0; i < L2_SIZE; ++i) {
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
        for (i = 0; i < L2_SIZE; ++i) {
            pa = base | ((abi_ulong)i <<
                (TARGET_PAGE_BITS + L2_BITS * level));
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
    data.start = -1ul;
    data.prot = 0;

    for (i = 0; i < V_L1_SIZE; i++) {
        int rc = walk_memory_regions_1(&data, (abi_ulong)i << V_L1_SHIFT,
                                       V_L1_SHIFT / L2_BITS - 1, l1_map + i);
        if (rc != 0) {
            return rc;
        }
    }

    return walk_memory_regions_end(&data, 0, 0);
}

static int dump_region(void *priv, abi_ulong start,
    abi_ulong end, unsigned long prot)
{
    FILE *f = (FILE *)priv;

    (void) fprintf(f, TARGET_ABI_FMT_lx"-"TARGET_ABI_FMT_lx
        " "TARGET_ABI_FMT_lx" %c%c%c\n",
        start, end, end - start,
        ((prot & PAGE_READ) ? 'r' : '-'),
        ((prot & PAGE_WRITE) ? 'w' : '-'),
        ((prot & PAGE_EXEC) ? 'x' : '-'));

    return (0);
}

/* dump memory mappings */
void page_dump(FILE *f)
{
    (void) fprintf(f, "%-8s %-8s %-8s %s\n",
            "start", "end", "size", "prot");
    walk_memory_regions(f, dump_region);
}

int page_get_flags(target_ulong address)
{
    PageDesc *p;

    p = page_find(address >> TARGET_PAGE_BITS);
    if (!p)
        return 0;
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
    assert(end < ((abi_ulong)1 << L1_MAP_ADDR_SPACE_BITS));
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
            tb_invalidate_phys_page(addr, 0, NULL);
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
    assert(start < ((abi_ulong)1 << L1_MAP_ADDR_SPACE_BITS));
#endif

    if (len == 0) {
        return 0;
    }
    if (start + len - 1 < start) {
        /* We've wrapped around.  */
        return -1;
    }

    end = TARGET_PAGE_ALIGN(start+len); /* must do before we loose bits in the next step */
    start = start & TARGET_PAGE_MASK;

    for (addr = start, len = end - start;
         len != 0;
         len -= TARGET_PAGE_SIZE, addr += TARGET_PAGE_SIZE) {
        p = page_find(addr >> TARGET_PAGE_BITS);
        if( !p )
            return -1;
        if( !(p->flags & PAGE_VALID) )
            return -1;

        if ((flags & PAGE_READ) && !(p->flags & PAGE_READ))
            return -1;
        if (flags & PAGE_WRITE) {
            if (!(p->flags & PAGE_WRITE_ORG))
                return -1;
            /* unprotect the page if it was put read-only because it
               contains translated code */
            if (!(p->flags & PAGE_WRITE)) {
                if (!page_unprotect(addr, 0, NULL))
                    return -1;
            }
            return 0;
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
            tb_invalidate_phys_page(addr, pc, puc);
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
#endif /* defined(CONFIG_USER_ONLY) */

#if !defined(CONFIG_USER_ONLY)

#define SUBPAGE_IDX(addr) ((addr) & ~TARGET_PAGE_MASK)
typedef struct subpage_t {
    MemoryRegion iomem;
    hwaddr base;
    uint16_t sub_section[TARGET_PAGE_SIZE];
} subpage_t;

static int subpage_register (subpage_t *mmio, uint32_t start, uint32_t end,
                             uint16_t section);
static subpage_t *subpage_init(hwaddr base);
static void destroy_page_desc(uint16_t section_index)
{
    MemoryRegionSection *section = &phys_sections[section_index];
    MemoryRegion *mr = section->mr;

    if (mr->subpage) {
        subpage_t *subpage = container_of(mr, subpage_t, iomem);
        memory_region_destroy(&subpage->iomem);
        g_free(subpage);
    }
}

static void destroy_l2_mapping(PhysPageEntry *lp, unsigned level)
{
    unsigned i;
    PhysPageEntry *p;

    if (lp->ptr == PHYS_MAP_NODE_NIL) {
        return;
    }

    p = phys_map_nodes[lp->ptr];
    for (i = 0; i < L2_SIZE; ++i) {
        if (!p[i].is_leaf) {
            destroy_l2_mapping(&p[i], level - 1);
        } else {
            destroy_page_desc(p[i].ptr);
        }
    }
    lp->is_leaf = 0;
    lp->ptr = PHYS_MAP_NODE_NIL;
}

static void destroy_all_mappings(AddressSpaceDispatch *d)
{
    destroy_l2_mapping(&d->phys_map, P_L2_LEVELS - 1);
    phys_map_nodes_reset();
}

static uint16_t phys_section_add(MemoryRegionSection *section)
{
    if (phys_sections_nb == phys_sections_nb_alloc) {
        phys_sections_nb_alloc = MAX(phys_sections_nb_alloc * 2, 16);
        phys_sections = g_renew(MemoryRegionSection, phys_sections,
                                phys_sections_nb_alloc);
    }
    phys_sections[phys_sections_nb] = *section;
    return phys_sections_nb++;
}

static void phys_sections_clear(void)
{
    phys_sections_nb = 0;
}

static void register_subpage(AddressSpaceDispatch *d, MemoryRegionSection *section)
{
    subpage_t *subpage;
    hwaddr base = section->offset_within_address_space
        & TARGET_PAGE_MASK;
    MemoryRegionSection *existing = phys_page_find(d, base >> TARGET_PAGE_BITS);
    MemoryRegionSection subsection = {
        .offset_within_address_space = base,
        .size = TARGET_PAGE_SIZE,
    };
    hwaddr start, end;

    assert(existing->mr->subpage || existing->mr == &io_mem_unassigned);

    if (!(existing->mr->subpage)) {
        subpage = subpage_init(base);
        subsection.mr = &subpage->iomem;
        phys_page_set(d, base >> TARGET_PAGE_BITS, 1,
                      phys_section_add(&subsection));
    } else {
        subpage = container_of(existing->mr, subpage_t, iomem);
    }
    start = section->offset_within_address_space & ~TARGET_PAGE_MASK;
    end = start + section->size - 1;
    subpage_register(subpage, start, end, phys_section_add(section));
}


static void register_multipage(AddressSpaceDispatch *d, MemoryRegionSection *section)
{
    hwaddr start_addr = section->offset_within_address_space;
    ram_addr_t size = section->size;
    hwaddr addr;
    uint16_t section_index = phys_section_add(section);

    assert(size);

    addr = start_addr;
    phys_page_set(d, addr >> TARGET_PAGE_BITS, size >> TARGET_PAGE_BITS,
                  section_index);
}

static void mem_add(MemoryListener *listener, MemoryRegionSection *section)
{
    AddressSpaceDispatch *d = container_of(listener, AddressSpaceDispatch, listener);
    MemoryRegionSection now = *section, remain = *section;

    if ((now.offset_within_address_space & ~TARGET_PAGE_MASK)
        || (now.size < TARGET_PAGE_SIZE)) {
        now.size = MIN(TARGET_PAGE_ALIGN(now.offset_within_address_space)
                       - now.offset_within_address_space,
                       now.size);
        register_subpage(d, &now);
        remain.size -= now.size;
        remain.offset_within_address_space += now.size;
        remain.offset_within_region += now.size;
    }
    while (remain.size >= TARGET_PAGE_SIZE) {
        now = remain;
        if (remain.offset_within_region & ~TARGET_PAGE_MASK) {
            now.size = TARGET_PAGE_SIZE;
            register_subpage(d, &now);
        } else {
            now.size &= TARGET_PAGE_MASK;
            register_multipage(d, &now);
        }
        remain.size -= now.size;
        remain.offset_within_address_space += now.size;
        remain.offset_within_region += now.size;
    }
    now = remain;
    if (now.size) {
        register_subpage(d, &now);
    }
}

void qemu_flush_coalesced_mmio_buffer(void)
{
    if (kvm_enabled())
        kvm_flush_coalesced_mmio_buffer();
}

#if defined(__linux__) && !defined(TARGET_S390X)

#include <sys/vfs.h>

#define HUGETLBFS_MAGIC       0x958458f6

static long gethugepagesize(const char *path)
{
    struct statfs fs;
    int ret;

    do {
        ret = statfs(path, &fs);
    } while (ret != 0 && errno == EINTR);

    if (ret != 0) {
        perror(path);
        return 0;
    }

    if (fs.f_type != HUGETLBFS_MAGIC)
        fprintf(stderr, "Warning: path not on HugeTLBFS: %s\n", path);

    return fs.f_bsize;
}

static void *file_ram_alloc(RAMBlock *block,
                            ram_addr_t memory,
                            const char *path)
{
    char *filename;
    void *area;
    int fd;
#ifdef MAP_POPULATE
    int flags;
#endif
    unsigned long hpagesize;

    hpagesize = gethugepagesize(path);
    if (!hpagesize) {
        return NULL;
    }

    if (memory < hpagesize) {
        return NULL;
    }

    if (kvm_enabled() && !kvm_has_sync_mmu()) {
        fprintf(stderr, "host lacks kvm mmu notifiers, -mem-path unsupported\n");
        return NULL;
    }

    if (asprintf(&filename, "%s/qemu_back_mem.XXXXXX", path) == -1) {
        return NULL;
    }

    fd = mkstemp(filename);
    if (fd < 0) {
        perror("unable to create backing store for hugepages");
        free(filename);
        return NULL;
    }
    unlink(filename);
    free(filename);

    memory = (memory+hpagesize-1) & ~(hpagesize-1);

    /*
     * ftruncate is not supported by hugetlbfs in older
     * hosts, so don't bother bailing out on errors.
     * If anything goes wrong with it under other filesystems,
     * mmap will fail.
     */
    if (ftruncate(fd, memory))
        perror("ftruncate");

#ifdef MAP_POPULATE
    /* NB: MAP_POPULATE won't exhaustively alloc all phys pages in the case
     * MAP_PRIVATE is requested.  For mem_prealloc we mmap as MAP_SHARED
     * to sidestep this quirk.
     */
    flags = mem_prealloc ? MAP_POPULATE | MAP_SHARED : MAP_PRIVATE;
    area = mmap(0, memory, PROT_READ | PROT_WRITE, flags, fd, 0);
#else
    area = mmap(0, memory, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
#endif
    if (area == MAP_FAILED) {
        perror("file_ram_alloc: can't mmap RAM pages");
        close(fd);
        return (NULL);
    }
    block->fd = fd;
    return area;
}
#endif

static ram_addr_t find_ram_offset(ram_addr_t size)
{
    RAMBlock *block, *next_block;
    ram_addr_t offset = RAM_ADDR_MAX, mingap = RAM_ADDR_MAX;

    if (QLIST_EMPTY(&ram_list.blocks))
        return 0;

    QLIST_FOREACH(block, &ram_list.blocks, next) {
        ram_addr_t end, next = RAM_ADDR_MAX;

        end = block->offset + block->length;

        QLIST_FOREACH(next_block, &ram_list.blocks, next) {
            if (next_block->offset >= end) {
                next = MIN(next, next_block->offset);
            }
        }
        if (next - end >= size && next - end < mingap) {
            offset = end;
            mingap = next - end;
        }
    }

    if (offset == RAM_ADDR_MAX) {
        fprintf(stderr, "Failed to find gap of requested size: %" PRIu64 "\n",
                (uint64_t)size);
        abort();
    }

    return offset;
}

ram_addr_t last_ram_offset(void)
{
    RAMBlock *block;
    ram_addr_t last = 0;

    QLIST_FOREACH(block, &ram_list.blocks, next)
        last = MAX(last, block->offset + block->length);

    return last;
}

static void qemu_ram_setup_dump(void *addr, ram_addr_t size)
{
    int ret;
    QemuOpts *machine_opts;

    /* Use MADV_DONTDUMP, if user doesn't want the guest memory in the core */
    machine_opts = qemu_opts_find(qemu_find_opts("machine"), 0);
    if (machine_opts &&
        !qemu_opt_get_bool(machine_opts, "dump-guest-core", true)) {
        ret = qemu_madvise(addr, size, QEMU_MADV_DONTDUMP);
        if (ret) {
            perror("qemu_madvise");
            fprintf(stderr, "madvise doesn't support MADV_DONTDUMP, "
                            "but dump_guest_core=off specified\n");
        }
    }
}

void qemu_ram_set_idstr(ram_addr_t addr, const char *name, DeviceState *dev)
{
    RAMBlock *new_block, *block;

    new_block = NULL;
    QLIST_FOREACH(block, &ram_list.blocks, next) {
        if (block->offset == addr) {
            new_block = block;
            break;
        }
    }
    assert(new_block);
    assert(!new_block->idstr[0]);

    if (dev) {
        char *id = qdev_get_dev_path(dev);
        if (id) {
            snprintf(new_block->idstr, sizeof(new_block->idstr), "%s/", id);
            g_free(id);
        }
    }
    pstrcat(new_block->idstr, sizeof(new_block->idstr), name);

    QLIST_FOREACH(block, &ram_list.blocks, next) {
        if (block != new_block && !strcmp(block->idstr, new_block->idstr)) {
            fprintf(stderr, "RAMBlock \"%s\" already registered, abort!\n",
                    new_block->idstr);
            abort();
        }
    }
}

static int memory_try_enable_merging(void *addr, size_t len)
{
    QemuOpts *opts;

    opts = qemu_opts_find(qemu_find_opts("machine"), 0);
    if (opts && !qemu_opt_get_bool(opts, "mem-merge", true)) {
        /* disabled by the user */
        return 0;
    }

    return qemu_madvise(addr, len, QEMU_MADV_MERGEABLE);
}

ram_addr_t qemu_ram_alloc_from_ptr(ram_addr_t size, void *host,
                                   MemoryRegion *mr)
{
    RAMBlock *new_block;

    size = TARGET_PAGE_ALIGN(size);
    new_block = g_malloc0(sizeof(*new_block));

    new_block->mr = mr;
    new_block->offset = find_ram_offset(size);
    if (host) {
        new_block->host = host;
        new_block->flags |= RAM_PREALLOC_MASK;
    } else {
        if (mem_path) {
#if defined (__linux__) && !defined(TARGET_S390X)
            new_block->host = file_ram_alloc(new_block, size, mem_path);
            if (!new_block->host) {
                new_block->host = qemu_vmalloc(size);
                memory_try_enable_merging(new_block->host, size);
            }
#else
            fprintf(stderr, "-mem-path option unsupported\n");
            exit(1);
#endif
        } else {
            if (xen_enabled()) {
                xen_ram_alloc(new_block->offset, size, mr);
            } else if (kvm_enabled()) {
                /* some s390/kvm configurations have special constraints */
                new_block->host = kvm_vmalloc(size);
            } else {
                new_block->host = qemu_vmalloc(size);
            }
            memory_try_enable_merging(new_block->host, size);
        }
    }
    new_block->length = size;

    QLIST_INSERT_HEAD(&ram_list.blocks, new_block, next);

    ram_list.phys_dirty = g_realloc(ram_list.phys_dirty,
                                       last_ram_offset() >> TARGET_PAGE_BITS);
    memset(ram_list.phys_dirty + (new_block->offset >> TARGET_PAGE_BITS),
           0, size >> TARGET_PAGE_BITS);
    cpu_physical_memory_set_dirty_range(new_block->offset, size, 0xff);

    qemu_ram_setup_dump(new_block->host, size);
    qemu_madvise(new_block->host, size, QEMU_MADV_HUGEPAGE);

    if (kvm_enabled())
        kvm_setup_guest_memory(new_block->host, size);

    return new_block->offset;
}

ram_addr_t qemu_ram_alloc(ram_addr_t size, MemoryRegion *mr)
{
    return qemu_ram_alloc_from_ptr(size, NULL, mr);
}

void qemu_ram_free_from_ptr(ram_addr_t addr)
{
    RAMBlock *block;

    QLIST_FOREACH(block, &ram_list.blocks, next) {
        if (addr == block->offset) {
            QLIST_REMOVE(block, next);
            g_free(block);
            return;
        }
    }
}

void qemu_ram_free(ram_addr_t addr)
{
    RAMBlock *block;

    QLIST_FOREACH(block, &ram_list.blocks, next) {
        if (addr == block->offset) {
            QLIST_REMOVE(block, next);
            if (block->flags & RAM_PREALLOC_MASK) {
                ;
            } else if (mem_path) {
#if defined (__linux__) && !defined(TARGET_S390X)
                if (block->fd) {
                    munmap(block->host, block->length);
                    close(block->fd);
                } else {
                    qemu_vfree(block->host);
                }
#else
                abort();
#endif
            } else {
#if defined(TARGET_S390X) && defined(CONFIG_KVM)
                munmap(block->host, block->length);
#else
                if (xen_enabled()) {
                    xen_invalidate_map_cache_entry(block->host);
                } else {
                    qemu_vfree(block->host);
                }
#endif
            }
            g_free(block);
            return;
        }
    }

}

#ifndef _WIN32
void qemu_ram_remap(ram_addr_t addr, ram_addr_t length)
{
    RAMBlock *block;
    ram_addr_t offset;
    int flags;
    void *area, *vaddr;

    QLIST_FOREACH(block, &ram_list.blocks, next) {
        offset = addr - block->offset;
        if (offset < block->length) {
            vaddr = block->host + offset;
            if (block->flags & RAM_PREALLOC_MASK) {
                ;
            } else {
                flags = MAP_FIXED;
                munmap(vaddr, length);
                if (mem_path) {
#if defined(__linux__) && !defined(TARGET_S390X)
                    if (block->fd) {
#ifdef MAP_POPULATE
                        flags |= mem_prealloc ? MAP_POPULATE | MAP_SHARED :
                            MAP_PRIVATE;
#else
                        flags |= MAP_PRIVATE;
#endif
                        area = mmap(vaddr, length, PROT_READ | PROT_WRITE,
                                    flags, block->fd, offset);
                    } else {
                        flags |= MAP_PRIVATE | MAP_ANONYMOUS;
                        area = mmap(vaddr, length, PROT_READ | PROT_WRITE,
                                    flags, -1, 0);
                    }
#else
                    abort();
#endif
                } else {
#if defined(TARGET_S390X) && defined(CONFIG_KVM)
                    flags |= MAP_SHARED | MAP_ANONYMOUS;
                    area = mmap(vaddr, length, PROT_EXEC|PROT_READ|PROT_WRITE,
                                flags, -1, 0);
#else
                    flags |= MAP_PRIVATE | MAP_ANONYMOUS;
                    area = mmap(vaddr, length, PROT_READ | PROT_WRITE,
                                flags, -1, 0);
#endif
                }
                if (area != vaddr) {
                    fprintf(stderr, "Could not remap addr: "
                            RAM_ADDR_FMT "@" RAM_ADDR_FMT "\n",
                            length, addr);
                    exit(1);
                }
                memory_try_enable_merging(vaddr, length);
                qemu_ram_setup_dump(vaddr, length);
            }
            return;
        }
    }
}
#endif /* !_WIN32 */

/* Return a host pointer to ram allocated with qemu_ram_alloc.
   With the exception of the softmmu code in this file, this should
   only be used for local memory (e.g. video ram) that the device owns,
   and knows it isn't going to access beyond the end of the block.

   It should not be used for general purpose DMA.
   Use cpu_physical_memory_map/cpu_physical_memory_rw instead.
 */
void *qemu_get_ram_ptr(ram_addr_t addr)
{
    RAMBlock *block;

    QLIST_FOREACH(block, &ram_list.blocks, next) {
        if (addr - block->offset < block->length) {
            /* Move this entry to to start of the list.  */
            if (block != QLIST_FIRST(&ram_list.blocks)) {
                QLIST_REMOVE(block, next);
                QLIST_INSERT_HEAD(&ram_list.blocks, block, next);
            }
            if (xen_enabled()) {
                /* We need to check if the requested address is in the RAM
                 * because we don't want to map the entire memory in QEMU.
                 * In that case just map until the end of the page.
                 */
                if (block->offset == 0) {
                    return xen_map_cache(addr, 0, 0);
                } else if (block->host == NULL) {
                    block->host =
                        xen_map_cache(block->offset, block->length, 1);
                }
            }
            return block->host + (addr - block->offset);
        }
    }

    fprintf(stderr, "Bad ram offset %" PRIx64 "\n", (uint64_t)addr);
    abort();

    return NULL;
}

/* Return a host pointer to ram allocated with qemu_ram_alloc.
 * Same as qemu_get_ram_ptr but avoid reordering ramblocks.
 */
static void *qemu_safe_ram_ptr(ram_addr_t addr)
{
    RAMBlock *block;

    QLIST_FOREACH(block, &ram_list.blocks, next) {
        if (addr - block->offset < block->length) {
            if (xen_enabled()) {
                /* We need to check if the requested address is in the RAM
                 * because we don't want to map the entire memory in QEMU.
                 * In that case just map until the end of the page.
                 */
                if (block->offset == 0) {
                    return xen_map_cache(addr, 0, 0);
                } else if (block->host == NULL) {
                    block->host =
                        xen_map_cache(block->offset, block->length, 1);
                }
            }
            return block->host + (addr - block->offset);
        }
    }

    fprintf(stderr, "Bad ram offset %" PRIx64 "\n", (uint64_t)addr);
    abort();

    return NULL;
}

/* Return a host pointer to guest's ram. Similar to qemu_get_ram_ptr
 * but takes a size argument */
static void *qemu_ram_ptr_length(ram_addr_t addr, ram_addr_t *size)
{
    if (*size == 0) {
        return NULL;
    }
    if (xen_enabled()) {
        return xen_map_cache(addr, *size, 1);
    } else {
        RAMBlock *block;

        QLIST_FOREACH(block, &ram_list.blocks, next) {
            if (addr - block->offset < block->length) {
                if (addr - block->offset + *size > block->length)
                    *size = block->length - addr + block->offset;
                return block->host + (addr - block->offset);
            }
        }

        fprintf(stderr, "Bad ram offset %" PRIx64 "\n", (uint64_t)addr);
        abort();
    }
}

void qemu_put_ram_ptr(void *addr)
{
    trace_qemu_put_ram_ptr(addr);
}

int qemu_ram_addr_from_host(void *ptr, ram_addr_t *ram_addr)
{
    RAMBlock *block;
    uint8_t *host = ptr;

    if (xen_enabled()) {
        *ram_addr = xen_ram_addr_from_mapcache(ptr);
        return 0;
    }

    QLIST_FOREACH(block, &ram_list.blocks, next) {
        /* This case append when the block is not mapped. */
        if (block->host == NULL) {
            continue;
        }
        if (host - block->host < block->length) {
            *ram_addr = block->offset + (host - block->host);
            return 0;
        }
    }

    return -1;
}

/* Some of the softmmu routines need to translate from a host pointer
   (typically a TLB entry) back to a ram offset.  */
ram_addr_t qemu_ram_addr_from_host_nofail(void *ptr)
{
    ram_addr_t ram_addr;

    if (qemu_ram_addr_from_host(ptr, &ram_addr)) {
        fprintf(stderr, "Bad ram pointer %p\n", ptr);
        abort();
    }
    return ram_addr;
}

static uint64_t unassigned_mem_read(void *opaque, hwaddr addr,
                                    unsigned size)
{
#ifdef DEBUG_UNASSIGNED
    printf("Unassigned mem read " TARGET_FMT_plx "\n", addr);
#endif
#if defined(TARGET_ALPHA) || defined(TARGET_SPARC) || defined(TARGET_MICROBLAZE)
    cpu_unassigned_access(cpu_single_env, addr, 0, 0, 0, size);
#endif
    return 0;
}

static void unassigned_mem_write(void *opaque, hwaddr addr,
                                 uint64_t val, unsigned size)
{
#ifdef DEBUG_UNASSIGNED
    printf("Unassigned mem write " TARGET_FMT_plx " = 0x%"PRIx64"\n", addr, val);
#endif
#if defined(TARGET_ALPHA) || defined(TARGET_SPARC) || defined(TARGET_MICROBLAZE)
    cpu_unassigned_access(cpu_single_env, addr, 1, 0, 0, size);
#endif
}

static const MemoryRegionOps unassigned_mem_ops = {
    .read = unassigned_mem_read,
    .write = unassigned_mem_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static uint64_t error_mem_read(void *opaque, hwaddr addr,
                               unsigned size)
{
    abort();
}

static void error_mem_write(void *opaque, hwaddr addr,
                            uint64_t value, unsigned size)
{
    abort();
}

static const MemoryRegionOps error_mem_ops = {
    .read = error_mem_read,
    .write = error_mem_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static const MemoryRegionOps rom_mem_ops = {
    .read = error_mem_read,
    .write = unassigned_mem_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void notdirty_mem_write(void *opaque, hwaddr ram_addr,
                               uint64_t val, unsigned size)
{
    int dirty_flags;
    dirty_flags = cpu_physical_memory_get_dirty_flags(ram_addr);
    if (!(dirty_flags & CODE_DIRTY_FLAG)) {
#if !defined(CONFIG_USER_ONLY)
        tb_invalidate_phys_page_fast(ram_addr, size);
        dirty_flags = cpu_physical_memory_get_dirty_flags(ram_addr);
#endif
    }
    switch (size) {
    case 1:
        stb_p(qemu_get_ram_ptr(ram_addr), val);
        break;
    case 2:
        stw_p(qemu_get_ram_ptr(ram_addr), val);
        break;
    case 4:
        stl_p(qemu_get_ram_ptr(ram_addr), val);
        break;
    default:
        abort();
    }
    dirty_flags |= (0xff & ~CODE_DIRTY_FLAG);
    cpu_physical_memory_set_dirty_flags(ram_addr, dirty_flags);
    /* we remove the notdirty callback only if the code has been
       flushed */
    if (dirty_flags == 0xff)
        tlb_set_dirty(cpu_single_env, cpu_single_env->mem_io_vaddr);
}

static const MemoryRegionOps notdirty_mem_ops = {
    .read = error_mem_read,
    .write = notdirty_mem_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

/* Generate a debug exception if a watchpoint has been hit.  */
static void check_watchpoint(int offset, int len_mask, int flags)
{
    CPUArchState *env = cpu_single_env;
    target_ulong pc, cs_base;
    TranslationBlock *tb;
    target_ulong vaddr;
    CPUWatchpoint *wp;
    int cpu_flags;

    if (env->watchpoint_hit) {
        /* We re-entered the check after replacing the TB. Now raise
         * the debug interrupt so that is will trigger after the
         * current instruction. */
        cpu_interrupt(env, CPU_INTERRUPT_DEBUG);
        return;
    }
    vaddr = (env->mem_io_vaddr & TARGET_PAGE_MASK) + offset;
    QTAILQ_FOREACH(wp, &env->watchpoints, entry) {
        if ((vaddr == (wp->vaddr & len_mask) ||
             (vaddr & wp->len_mask) == wp->vaddr) && (wp->flags & flags)) {
            wp->flags |= BP_WATCHPOINT_HIT;
            if (!env->watchpoint_hit) {
                env->watchpoint_hit = wp;
                tb = tb_find_pc(env->mem_io_pc);
                if (!tb) {
                    cpu_abort(env, "check_watchpoint: could not find TB for "
                              "pc=%p", (void *)env->mem_io_pc);
                }
                cpu_restore_state(tb, env, env->mem_io_pc);
                tb_phys_invalidate(tb, -1);
                if (wp->flags & BP_STOP_BEFORE_ACCESS) {
                    env->exception_index = EXCP_DEBUG;
                    cpu_loop_exit(env);
                } else {
                    cpu_get_tb_cpu_state(env, &pc, &cs_base, &cpu_flags);
                    tb_gen_code(env, pc, cs_base, cpu_flags, 1);
                    cpu_resume_from_signal(env, NULL);
                }
            }
        } else {
            wp->flags &= ~BP_WATCHPOINT_HIT;
        }
    }
}

/* Watchpoint access routines.  Watchpoints are inserted using TLB tricks,
   so these check for a hit then pass through to the normal out-of-line
   phys routines.  */
static uint64_t watch_mem_read(void *opaque, hwaddr addr,
                               unsigned size)
{
    check_watchpoint(addr & ~TARGET_PAGE_MASK, ~(size - 1), BP_MEM_READ);
    switch (size) {
    case 1: return ldub_phys(addr);
    case 2: return lduw_phys(addr);
    case 4: return ldl_phys(addr);
    default: abort();
    }
}

static void watch_mem_write(void *opaque, hwaddr addr,
                            uint64_t val, unsigned size)
{
    check_watchpoint(addr & ~TARGET_PAGE_MASK, ~(size - 1), BP_MEM_WRITE);
    switch (size) {
    case 1:
        stb_phys(addr, val);
        break;
    case 2:
        stw_phys(addr, val);
        break;
    case 4:
        stl_phys(addr, val);
        break;
    default: abort();
    }
}

static const MemoryRegionOps watch_mem_ops = {
    .read = watch_mem_read,
    .write = watch_mem_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static uint64_t subpage_read(void *opaque, hwaddr addr,
                             unsigned len)
{
    subpage_t *mmio = opaque;
    unsigned int idx = SUBPAGE_IDX(addr);
    MemoryRegionSection *section;
#if defined(DEBUG_SUBPAGE)
    printf("%s: subpage %p len %d addr " TARGET_FMT_plx " idx %d\n", __func__,
           mmio, len, addr, idx);
#endif

    section = &phys_sections[mmio->sub_section[idx]];
    addr += mmio->base;
    addr -= section->offset_within_address_space;
    addr += section->offset_within_region;
    return io_mem_read(section->mr, addr, len);
}

static void subpage_write(void *opaque, hwaddr addr,
                          uint64_t value, unsigned len)
{
    subpage_t *mmio = opaque;
    unsigned int idx = SUBPAGE_IDX(addr);
    MemoryRegionSection *section;
#if defined(DEBUG_SUBPAGE)
    printf("%s: subpage %p len %d addr " TARGET_FMT_plx
           " idx %d value %"PRIx64"\n",
           __func__, mmio, len, addr, idx, value);
#endif

    section = &phys_sections[mmio->sub_section[idx]];
    addr += mmio->base;
    addr -= section->offset_within_address_space;
    addr += section->offset_within_region;
    io_mem_write(section->mr, addr, value, len);
}

static const MemoryRegionOps subpage_ops = {
    .read = subpage_read,
    .write = subpage_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static uint64_t subpage_ram_read(void *opaque, hwaddr addr,
                                 unsigned size)
{
    ram_addr_t raddr = addr;
    void *ptr = qemu_get_ram_ptr(raddr);
    switch (size) {
    case 1: return ldub_p(ptr);
    case 2: return lduw_p(ptr);
    case 4: return ldl_p(ptr);
    default: abort();
    }
}

static void subpage_ram_write(void *opaque, hwaddr addr,
                              uint64_t value, unsigned size)
{
    ram_addr_t raddr = addr;
    void *ptr = qemu_get_ram_ptr(raddr);
    switch (size) {
    case 1: return stb_p(ptr, value);
    case 2: return stw_p(ptr, value);
    case 4: return stl_p(ptr, value);
    default: abort();
    }
}

static const MemoryRegionOps subpage_ram_ops = {
    .read = subpage_ram_read,
    .write = subpage_ram_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static int subpage_register (subpage_t *mmio, uint32_t start, uint32_t end,
                             uint16_t section)
{
    int idx, eidx;

    if (start >= TARGET_PAGE_SIZE || end >= TARGET_PAGE_SIZE)
        return -1;
    idx = SUBPAGE_IDX(start);
    eidx = SUBPAGE_IDX(end);
#if defined(DEBUG_SUBPAGE)
    printf("%s: %p start %08x end %08x idx %08x eidx %08x mem %ld\n", __func__,
           mmio, start, end, idx, eidx, memory);
#endif
    if (memory_region_is_ram(phys_sections[section].mr)) {
        MemoryRegionSection new_section = phys_sections[section];
        new_section.mr = &io_mem_subpage_ram;
        section = phys_section_add(&new_section);
    }
    for (; idx <= eidx; idx++) {
        mmio->sub_section[idx] = section;
    }

    return 0;
}

static subpage_t *subpage_init(hwaddr base)
{
    subpage_t *mmio;

    mmio = g_malloc0(sizeof(subpage_t));

    mmio->base = base;
    memory_region_init_io(&mmio->iomem, &subpage_ops, mmio,
                          "subpage", TARGET_PAGE_SIZE);
    mmio->iomem.subpage = true;
#if defined(DEBUG_SUBPAGE)
    printf("%s: %p base " TARGET_FMT_plx " len %08x %d\n", __func__,
           mmio, base, TARGET_PAGE_SIZE, subpage_memory);
#endif
    subpage_register(mmio, 0, TARGET_PAGE_SIZE-1, phys_section_unassigned);

    return mmio;
}

static uint16_t dummy_section(MemoryRegion *mr)
{
    MemoryRegionSection section = {
        .mr = mr,
        .offset_within_address_space = 0,
        .offset_within_region = 0,
        .size = UINT64_MAX,
    };

    return phys_section_add(&section);
}

MemoryRegion *iotlb_to_region(hwaddr index)
{
    return phys_sections[index & ~TARGET_PAGE_MASK].mr;
}

static void io_mem_init(void)
{
    memory_region_init_io(&io_mem_ram, &error_mem_ops, NULL, "ram", UINT64_MAX);
    memory_region_init_io(&io_mem_rom, &rom_mem_ops, NULL, "rom", UINT64_MAX);
    memory_region_init_io(&io_mem_unassigned, &unassigned_mem_ops, NULL,
                          "unassigned", UINT64_MAX);
    memory_region_init_io(&io_mem_notdirty, &notdirty_mem_ops, NULL,
                          "notdirty", UINT64_MAX);
    memory_region_init_io(&io_mem_subpage_ram, &subpage_ram_ops, NULL,
                          "subpage-ram", UINT64_MAX);
    memory_region_init_io(&io_mem_watch, &watch_mem_ops, NULL,
                          "watch", UINT64_MAX);
}

static void mem_begin(MemoryListener *listener)
{
    AddressSpaceDispatch *d = container_of(listener, AddressSpaceDispatch, listener);

    destroy_all_mappings(d);
    d->phys_map.ptr = PHYS_MAP_NODE_NIL;
}

static void core_begin(MemoryListener *listener)
{
    phys_sections_clear();
    phys_section_unassigned = dummy_section(&io_mem_unassigned);
    phys_section_notdirty = dummy_section(&io_mem_notdirty);
    phys_section_rom = dummy_section(&io_mem_rom);
    phys_section_watch = dummy_section(&io_mem_watch);
}

static void tcg_commit(MemoryListener *listener)
{
    CPUArchState *env;

    /* since each CPU stores ram addresses in its TLB cache, we must
       reset the modified entries */
    /* XXX: slow ! */
    for(env = first_cpu; env != NULL; env = env->next_cpu) {
        tlb_flush(env, 1);
    }
}

static void core_log_global_start(MemoryListener *listener)
{
    cpu_physical_memory_set_dirty_tracking(1);
}

static void core_log_global_stop(MemoryListener *listener)
{
    cpu_physical_memory_set_dirty_tracking(0);
}

static void io_region_add(MemoryListener *listener,
                          MemoryRegionSection *section)
{
    MemoryRegionIORange *mrio = g_new(MemoryRegionIORange, 1);

    mrio->mr = section->mr;
    mrio->offset = section->offset_within_region;
    iorange_init(&mrio->iorange, &memory_region_iorange_ops,
                 section->offset_within_address_space, section->size);
    ioport_register(&mrio->iorange);
}

static void io_region_del(MemoryListener *listener,
                          MemoryRegionSection *section)
{
    isa_unassign_ioport(section->offset_within_address_space, section->size);
}

static MemoryListener core_memory_listener = {
    .begin = core_begin,
    .log_global_start = core_log_global_start,
    .log_global_stop = core_log_global_stop,
    .priority = 1,
};

static MemoryListener io_memory_listener = {
    .region_add = io_region_add,
    .region_del = io_region_del,
    .priority = 0,
};

static MemoryListener tcg_memory_listener = {
    .commit = tcg_commit,
};

void address_space_init_dispatch(AddressSpace *as)
{
    AddressSpaceDispatch *d = g_new(AddressSpaceDispatch, 1);

    d->phys_map  = (PhysPageEntry) { .ptr = PHYS_MAP_NODE_NIL, .is_leaf = 0 };
    d->listener = (MemoryListener) {
        .begin = mem_begin,
        .region_add = mem_add,
        .region_nop = mem_add,
        .priority = 0,
    };
    as->dispatch = d;
    memory_listener_register(&d->listener, as);
}

void address_space_destroy_dispatch(AddressSpace *as)
{
    AddressSpaceDispatch *d = as->dispatch;

    memory_listener_unregister(&d->listener);
    destroy_l2_mapping(&d->phys_map, P_L2_LEVELS - 1);
    g_free(d);
    as->dispatch = NULL;
}

static void memory_map_init(void)
{
    system_memory = g_malloc(sizeof(*system_memory));
    memory_region_init(system_memory, "system", INT64_MAX);
    address_space_init(&address_space_memory, system_memory);
    address_space_memory.name = "memory";

    system_io = g_malloc(sizeof(*system_io));
    memory_region_init(system_io, "io", 65536);
    address_space_init(&address_space_io, system_io);
    address_space_io.name = "I/O";

    memory_listener_register(&core_memory_listener, &address_space_memory);
    memory_listener_register(&io_memory_listener, &address_space_io);
    memory_listener_register(&tcg_memory_listener, &address_space_memory);

    dma_context_init(&dma_context_memory, &address_space_memory,
                     NULL, NULL, NULL);
}

MemoryRegion *get_system_memory(void)
{
    return system_memory;
}

MemoryRegion *get_system_io(void)
{
    return system_io;
}

#endif /* !defined(CONFIG_USER_ONLY) */

/* physical memory access (slow version, mainly for debug) */
#if defined(CONFIG_USER_ONLY)
int cpu_memory_rw_debug(CPUArchState *env, target_ulong addr,
                        uint8_t *buf, int len, int is_write)
{
    int l, flags;
    target_ulong page;
    void * p;

    while (len > 0) {
        page = addr & TARGET_PAGE_MASK;
        l = (page + TARGET_PAGE_SIZE) - addr;
        if (l > len)
            l = len;
        flags = page_get_flags(page);
        if (!(flags & PAGE_VALID))
            return -1;
        if (is_write) {
            if (!(flags & PAGE_WRITE))
                return -1;
            /* XXX: this code should not depend on lock_user */
            if (!(p = lock_user(VERIFY_WRITE, addr, l, 0)))
                return -1;
            memcpy(p, buf, l);
            unlock_user(p, addr, l);
        } else {
            if (!(flags & PAGE_READ))
                return -1;
            /* XXX: this code should not depend on lock_user */
            if (!(p = lock_user(VERIFY_READ, addr, l, 1)))
                return -1;
            memcpy(buf, p, l);
            unlock_user(p, addr, 0);
        }
        len -= l;
        buf += l;
        addr += l;
    }
    return 0;
}

#else

static void invalidate_and_set_dirty(hwaddr addr,
                                     hwaddr length)
{
    if (!cpu_physical_memory_is_dirty(addr)) {
        /* invalidate code */
        tb_invalidate_phys_page_range(addr, addr + length, 0);
        /* set dirty bit */
        cpu_physical_memory_set_dirty_flags(addr, (0xff & ~CODE_DIRTY_FLAG));
    }
    xen_modified_memory(addr, length);
}

void address_space_rw(AddressSpace *as, hwaddr addr, uint8_t *buf,
                      int len, bool is_write)
{
    AddressSpaceDispatch *d = as->dispatch;
    int l;
    uint8_t *ptr;
    uint32_t val;
    hwaddr page;
    MemoryRegionSection *section;

    while (len > 0) {
        page = addr & TARGET_PAGE_MASK;
        l = (page + TARGET_PAGE_SIZE) - addr;
        if (l > len)
            l = len;
        section = phys_page_find(d, page >> TARGET_PAGE_BITS);

        if (is_write) {
            if (!memory_region_is_ram(section->mr)) {
                hwaddr addr1;
                addr1 = memory_region_section_addr(section, addr);
                /* XXX: could force cpu_single_env to NULL to avoid
                   potential bugs */
                if (l >= 4 && ((addr1 & 3) == 0)) {
                    /* 32 bit write access */
                    val = ldl_p(buf);
                    io_mem_write(section->mr, addr1, val, 4);
                    l = 4;
                } else if (l >= 2 && ((addr1 & 1) == 0)) {
                    /* 16 bit write access */
                    val = lduw_p(buf);
                    io_mem_write(section->mr, addr1, val, 2);
                    l = 2;
                } else {
                    /* 8 bit write access */
                    val = ldub_p(buf);
                    io_mem_write(section->mr, addr1, val, 1);
                    l = 1;
                }
            } else if (!section->readonly) {
                ram_addr_t addr1;
                addr1 = memory_region_get_ram_addr(section->mr)
                    + memory_region_section_addr(section, addr);
                /* RAM case */
                ptr = qemu_get_ram_ptr(addr1);
                memcpy(ptr, buf, l);
                invalidate_and_set_dirty(addr1, l);
                qemu_put_ram_ptr(ptr);
            }
        } else {
            if (!(memory_region_is_ram(section->mr) ||
                  memory_region_is_romd(section->mr))) {
                hwaddr addr1;
                /* I/O case */
                addr1 = memory_region_section_addr(section, addr);
                if (l >= 4 && ((addr1 & 3) == 0)) {
                    /* 32 bit read access */
                    val = io_mem_read(section->mr, addr1, 4);
                    stl_p(buf, val);
                    l = 4;
                } else if (l >= 2 && ((addr1 & 1) == 0)) {
                    /* 16 bit read access */
                    val = io_mem_read(section->mr, addr1, 2);
                    stw_p(buf, val);
                    l = 2;
                } else {
                    /* 8 bit read access */
                    val = io_mem_read(section->mr, addr1, 1);
                    stb_p(buf, val);
                    l = 1;
                }
            } else {
                /* RAM case */
                ptr = qemu_get_ram_ptr(section->mr->ram_addr
                                       + memory_region_section_addr(section,
                                                                    addr));
                memcpy(buf, ptr, l);
                qemu_put_ram_ptr(ptr);
            }
        }
        len -= l;
        buf += l;
        addr += l;
    }
}

void address_space_write(AddressSpace *as, hwaddr addr,
                         const uint8_t *buf, int len)
{
    address_space_rw(as, addr, (uint8_t *)buf, len, true);
}

/**
 * address_space_read: read from an address space.
 *
 * @as: #AddressSpace to be accessed
 * @addr: address within that address space
 * @buf: buffer with the data transferred
 */
void address_space_read(AddressSpace *as, hwaddr addr, uint8_t *buf, int len)
{
    address_space_rw(as, addr, buf, len, false);
}


void cpu_physical_memory_rw(hwaddr addr, uint8_t *buf,
                            int len, int is_write)
{
    return address_space_rw(&address_space_memory, addr, buf, len, is_write);
}

/* used for ROM loading : can write in RAM and ROM */
void cpu_physical_memory_write_rom(hwaddr addr,
                                   const uint8_t *buf, int len)
{
    AddressSpaceDispatch *d = address_space_memory.dispatch;
    int l;
    uint8_t *ptr;
    hwaddr page;
    MemoryRegionSection *section;

    while (len > 0) {
        page = addr & TARGET_PAGE_MASK;
        l = (page + TARGET_PAGE_SIZE) - addr;
        if (l > len)
            l = len;
        section = phys_page_find(d, page >> TARGET_PAGE_BITS);

        if (!(memory_region_is_ram(section->mr) ||
              memory_region_is_romd(section->mr))) {
            /* do nothing */
        } else {
            unsigned long addr1;
            addr1 = memory_region_get_ram_addr(section->mr)
                + memory_region_section_addr(section, addr);
            /* ROM/RAM case */
            ptr = qemu_get_ram_ptr(addr1);
            memcpy(ptr, buf, l);
            invalidate_and_set_dirty(addr1, l);
            qemu_put_ram_ptr(ptr);
        }
        len -= l;
        buf += l;
        addr += l;
    }
}

typedef struct {
    void *buffer;
    hwaddr addr;
    hwaddr len;
} BounceBuffer;

static BounceBuffer bounce;

typedef struct MapClient {
    void *opaque;
    void (*callback)(void *opaque);
    QLIST_ENTRY(MapClient) link;
} MapClient;

static QLIST_HEAD(map_client_list, MapClient) map_client_list
    = QLIST_HEAD_INITIALIZER(map_client_list);

void *cpu_register_map_client(void *opaque, void (*callback)(void *opaque))
{
    MapClient *client = g_malloc(sizeof(*client));

    client->opaque = opaque;
    client->callback = callback;
    QLIST_INSERT_HEAD(&map_client_list, client, link);
    return client;
}

static void cpu_unregister_map_client(void *_client)
{
    MapClient *client = (MapClient *)_client;

    QLIST_REMOVE(client, link);
    g_free(client);
}

static void cpu_notify_map_clients(void)
{
    MapClient *client;

    while (!QLIST_EMPTY(&map_client_list)) {
        client = QLIST_FIRST(&map_client_list);
        client->callback(client->opaque);
        cpu_unregister_map_client(client);
    }
}

/* Map a physical memory region into a host virtual address.
 * May map a subset of the requested range, given by and returned in *plen.
 * May return NULL if resources needed to perform the mapping are exhausted.
 * Use only for reads OR writes - not for read-modify-write operations.
 * Use cpu_register_map_client() to know when retrying the map operation is
 * likely to succeed.
 */
void *address_space_map(AddressSpace *as,
                        hwaddr addr,
                        hwaddr *plen,
                        bool is_write)
{
    AddressSpaceDispatch *d = as->dispatch;
    hwaddr len = *plen;
    hwaddr todo = 0;
    int l;
    hwaddr page;
    MemoryRegionSection *section;
    ram_addr_t raddr = RAM_ADDR_MAX;
    ram_addr_t rlen;
    void *ret;

    while (len > 0) {
        page = addr & TARGET_PAGE_MASK;
        l = (page + TARGET_PAGE_SIZE) - addr;
        if (l > len)
            l = len;
        section = phys_page_find(d, page >> TARGET_PAGE_BITS);

        if (!(memory_region_is_ram(section->mr) && !section->readonly)) {
            if (todo || bounce.buffer) {
                break;
            }
            bounce.buffer = qemu_memalign(TARGET_PAGE_SIZE, TARGET_PAGE_SIZE);
            bounce.addr = addr;
            bounce.len = l;
            if (!is_write) {
                address_space_read(as, addr, bounce.buffer, l);
            }

            *plen = l;
            return bounce.buffer;
        }
        if (!todo) {
            raddr = memory_region_get_ram_addr(section->mr)
                + memory_region_section_addr(section, addr);
        }

        len -= l;
        addr += l;
        todo += l;
    }
    rlen = todo;
    ret = qemu_ram_ptr_length(raddr, &rlen);
    *plen = rlen;
    return ret;
}

/* Unmaps a memory region previously mapped by address_space_map().
 * Will also mark the memory as dirty if is_write == 1.  access_len gives
 * the amount of memory that was actually read or written by the caller.
 */
void address_space_unmap(AddressSpace *as, void *buffer, hwaddr len,
                         int is_write, hwaddr access_len)
{
    if (buffer != bounce.buffer) {
        if (is_write) {
            ram_addr_t addr1 = qemu_ram_addr_from_host_nofail(buffer);
            while (access_len) {
                unsigned l;
                l = TARGET_PAGE_SIZE;
                if (l > access_len)
                    l = access_len;
                invalidate_and_set_dirty(addr1, l);
                addr1 += l;
                access_len -= l;
            }
        }
        if (xen_enabled()) {
            xen_invalidate_map_cache_entry(buffer);
        }
        return;
    }
    if (is_write) {
        address_space_write(as, bounce.addr, bounce.buffer, access_len);
    }
    qemu_vfree(bounce.buffer);
    bounce.buffer = NULL;
    cpu_notify_map_clients();
}

void *cpu_physical_memory_map(hwaddr addr,
                              hwaddr *plen,
                              int is_write)
{
    return address_space_map(&address_space_memory, addr, plen, is_write);
}

void cpu_physical_memory_unmap(void *buffer, hwaddr len,
                               int is_write, hwaddr access_len)
{
    return address_space_unmap(&address_space_memory, buffer, len, is_write, access_len);
}

/* warning: addr must be aligned */
static inline uint32_t ldl_phys_internal(hwaddr addr,
                                         enum device_endian endian)
{
    uint8_t *ptr;
    uint32_t val;
    MemoryRegionSection *section;

    section = phys_page_find(address_space_memory.dispatch, addr >> TARGET_PAGE_BITS);

    if (!(memory_region_is_ram(section->mr) ||
          memory_region_is_romd(section->mr))) {
        /* I/O case */
        addr = memory_region_section_addr(section, addr);
        val = io_mem_read(section->mr, addr, 4);
#if defined(TARGET_WORDS_BIGENDIAN)
        if (endian == DEVICE_LITTLE_ENDIAN) {
            val = bswap32(val);
        }
#else
        if (endian == DEVICE_BIG_ENDIAN) {
            val = bswap32(val);
        }
#endif
    } else {
        /* RAM case */
        ptr = qemu_get_ram_ptr((memory_region_get_ram_addr(section->mr)
                                & TARGET_PAGE_MASK)
                               + memory_region_section_addr(section, addr));
        switch (endian) {
        case DEVICE_LITTLE_ENDIAN:
            val = ldl_le_p(ptr);
            break;
        case DEVICE_BIG_ENDIAN:
            val = ldl_be_p(ptr);
            break;
        default:
            val = ldl_p(ptr);
            break;
        }
    }
    return val;
}

uint32_t ldl_phys(hwaddr addr)
{
    return ldl_phys_internal(addr, DEVICE_NATIVE_ENDIAN);
}

uint32_t ldl_le_phys(hwaddr addr)
{
    return ldl_phys_internal(addr, DEVICE_LITTLE_ENDIAN);
}

uint32_t ldl_be_phys(hwaddr addr)
{
    return ldl_phys_internal(addr, DEVICE_BIG_ENDIAN);
}

/* warning: addr must be aligned */
static inline uint64_t ldq_phys_internal(hwaddr addr,
                                         enum device_endian endian)
{
    uint8_t *ptr;
    uint64_t val;
    MemoryRegionSection *section;

    section = phys_page_find(address_space_memory.dispatch, addr >> TARGET_PAGE_BITS);

    if (!(memory_region_is_ram(section->mr) ||
          memory_region_is_romd(section->mr))) {
        /* I/O case */
        addr = memory_region_section_addr(section, addr);

        /* XXX This is broken when device endian != cpu endian.
               Fix and add "endian" variable check */
#ifdef TARGET_WORDS_BIGENDIAN
        val = io_mem_read(section->mr, addr, 4) << 32;
        val |= io_mem_read(section->mr, addr + 4, 4);
#else
        val = io_mem_read(section->mr, addr, 4);
        val |= io_mem_read(section->mr, addr + 4, 4) << 32;
#endif
    } else {
        /* RAM case */
        ptr = qemu_get_ram_ptr((memory_region_get_ram_addr(section->mr)
                                & TARGET_PAGE_MASK)
                               + memory_region_section_addr(section, addr));
        switch (endian) {
        case DEVICE_LITTLE_ENDIAN:
            val = ldq_le_p(ptr);
            break;
        case DEVICE_BIG_ENDIAN:
            val = ldq_be_p(ptr);
            break;
        default:
            val = ldq_p(ptr);
            break;
        }
    }
    return val;
}

uint64_t ldq_phys(hwaddr addr)
{
    return ldq_phys_internal(addr, DEVICE_NATIVE_ENDIAN);
}

uint64_t ldq_le_phys(hwaddr addr)
{
    return ldq_phys_internal(addr, DEVICE_LITTLE_ENDIAN);
}

uint64_t ldq_be_phys(hwaddr addr)
{
    return ldq_phys_internal(addr, DEVICE_BIG_ENDIAN);
}

/* XXX: optimize */
uint32_t ldub_phys(hwaddr addr)
{
    uint8_t val;
    cpu_physical_memory_read(addr, &val, 1);
    return val;
}

/* warning: addr must be aligned */
static inline uint32_t lduw_phys_internal(hwaddr addr,
                                          enum device_endian endian)
{
    uint8_t *ptr;
    uint64_t val;
    MemoryRegionSection *section;

    section = phys_page_find(address_space_memory.dispatch, addr >> TARGET_PAGE_BITS);

    if (!(memory_region_is_ram(section->mr) ||
          memory_region_is_romd(section->mr))) {
        /* I/O case */
        addr = memory_region_section_addr(section, addr);
        val = io_mem_read(section->mr, addr, 2);
#if defined(TARGET_WORDS_BIGENDIAN)
        if (endian == DEVICE_LITTLE_ENDIAN) {
            val = bswap16(val);
        }
#else
        if (endian == DEVICE_BIG_ENDIAN) {
            val = bswap16(val);
        }
#endif
    } else {
        /* RAM case */
        ptr = qemu_get_ram_ptr((memory_region_get_ram_addr(section->mr)
                                & TARGET_PAGE_MASK)
                               + memory_region_section_addr(section, addr));
        switch (endian) {
        case DEVICE_LITTLE_ENDIAN:
            val = lduw_le_p(ptr);
            break;
        case DEVICE_BIG_ENDIAN:
            val = lduw_be_p(ptr);
            break;
        default:
            val = lduw_p(ptr);
            break;
        }
    }
    return val;
}

uint32_t lduw_phys(hwaddr addr)
{
    return lduw_phys_internal(addr, DEVICE_NATIVE_ENDIAN);
}

uint32_t lduw_le_phys(hwaddr addr)
{
    return lduw_phys_internal(addr, DEVICE_LITTLE_ENDIAN);
}

uint32_t lduw_be_phys(hwaddr addr)
{
    return lduw_phys_internal(addr, DEVICE_BIG_ENDIAN);
}

/* warning: addr must be aligned. The ram page is not masked as dirty
   and the code inside is not invalidated. It is useful if the dirty
   bits are used to track modified PTEs */
void stl_phys_notdirty(hwaddr addr, uint32_t val)
{
    uint8_t *ptr;
    MemoryRegionSection *section;

    section = phys_page_find(address_space_memory.dispatch, addr >> TARGET_PAGE_BITS);

    if (!memory_region_is_ram(section->mr) || section->readonly) {
        addr = memory_region_section_addr(section, addr);
        if (memory_region_is_ram(section->mr)) {
            section = &phys_sections[phys_section_rom];
        }
        io_mem_write(section->mr, addr, val, 4);
    } else {
        unsigned long addr1 = (memory_region_get_ram_addr(section->mr)
                               & TARGET_PAGE_MASK)
            + memory_region_section_addr(section, addr);
        ptr = qemu_get_ram_ptr(addr1);
        stl_p(ptr, val);

        if (unlikely(in_migration)) {
            if (!cpu_physical_memory_is_dirty(addr1)) {
                /* invalidate code */
                tb_invalidate_phys_page_range(addr1, addr1 + 4, 0);
                /* set dirty bit */
                cpu_physical_memory_set_dirty_flags(
                    addr1, (0xff & ~CODE_DIRTY_FLAG));
            }
        }
    }
}

void stq_phys_notdirty(hwaddr addr, uint64_t val)
{
    uint8_t *ptr;
    MemoryRegionSection *section;

    section = phys_page_find(address_space_memory.dispatch, addr >> TARGET_PAGE_BITS);

    if (!memory_region_is_ram(section->mr) || section->readonly) {
        addr = memory_region_section_addr(section, addr);
        if (memory_region_is_ram(section->mr)) {
            section = &phys_sections[phys_section_rom];
        }
#ifdef TARGET_WORDS_BIGENDIAN
        io_mem_write(section->mr, addr, val >> 32, 4);
        io_mem_write(section->mr, addr + 4, (uint32_t)val, 4);
#else
        io_mem_write(section->mr, addr, (uint32_t)val, 4);
        io_mem_write(section->mr, addr + 4, val >> 32, 4);
#endif
    } else {
        ptr = qemu_get_ram_ptr((memory_region_get_ram_addr(section->mr)
                                & TARGET_PAGE_MASK)
                               + memory_region_section_addr(section, addr));
        stq_p(ptr, val);
    }
}

/* warning: addr must be aligned */
static inline void stl_phys_internal(hwaddr addr, uint32_t val,
                                     enum device_endian endian)
{
    uint8_t *ptr;
    MemoryRegionSection *section;

    section = phys_page_find(address_space_memory.dispatch, addr >> TARGET_PAGE_BITS);

    if (!memory_region_is_ram(section->mr) || section->readonly) {
        addr = memory_region_section_addr(section, addr);
        if (memory_region_is_ram(section->mr)) {
            section = &phys_sections[phys_section_rom];
        }
#if defined(TARGET_WORDS_BIGENDIAN)
        if (endian == DEVICE_LITTLE_ENDIAN) {
            val = bswap32(val);
        }
#else
        if (endian == DEVICE_BIG_ENDIAN) {
            val = bswap32(val);
        }
#endif
        io_mem_write(section->mr, addr, val, 4);
    } else {
        unsigned long addr1;
        addr1 = (memory_region_get_ram_addr(section->mr) & TARGET_PAGE_MASK)
            + memory_region_section_addr(section, addr);
        /* RAM case */
        ptr = qemu_get_ram_ptr(addr1);
        switch (endian) {
        case DEVICE_LITTLE_ENDIAN:
            stl_le_p(ptr, val);
            break;
        case DEVICE_BIG_ENDIAN:
            stl_be_p(ptr, val);
            break;
        default:
            stl_p(ptr, val);
            break;
        }
        invalidate_and_set_dirty(addr1, 4);
    }
}

void stl_phys(hwaddr addr, uint32_t val)
{
    stl_phys_internal(addr, val, DEVICE_NATIVE_ENDIAN);
}

void stl_le_phys(hwaddr addr, uint32_t val)
{
    stl_phys_internal(addr, val, DEVICE_LITTLE_ENDIAN);
}

void stl_be_phys(hwaddr addr, uint32_t val)
{
    stl_phys_internal(addr, val, DEVICE_BIG_ENDIAN);
}

/* XXX: optimize */
void stb_phys(hwaddr addr, uint32_t val)
{
    uint8_t v = val;
    cpu_physical_memory_write(addr, &v, 1);
}

/* warning: addr must be aligned */
static inline void stw_phys_internal(hwaddr addr, uint32_t val,
                                     enum device_endian endian)
{
    uint8_t *ptr;
    MemoryRegionSection *section;

    section = phys_page_find(address_space_memory.dispatch, addr >> TARGET_PAGE_BITS);

    if (!memory_region_is_ram(section->mr) || section->readonly) {
        addr = memory_region_section_addr(section, addr);
        if (memory_region_is_ram(section->mr)) {
            section = &phys_sections[phys_section_rom];
        }
#if defined(TARGET_WORDS_BIGENDIAN)
        if (endian == DEVICE_LITTLE_ENDIAN) {
            val = bswap16(val);
        }
#else
        if (endian == DEVICE_BIG_ENDIAN) {
            val = bswap16(val);
        }
#endif
        io_mem_write(section->mr, addr, val, 2);
    } else {
        unsigned long addr1;
        addr1 = (memory_region_get_ram_addr(section->mr) & TARGET_PAGE_MASK)
            + memory_region_section_addr(section, addr);
        /* RAM case */
        ptr = qemu_get_ram_ptr(addr1);
        switch (endian) {
        case DEVICE_LITTLE_ENDIAN:
            stw_le_p(ptr, val);
            break;
        case DEVICE_BIG_ENDIAN:
            stw_be_p(ptr, val);
            break;
        default:
            stw_p(ptr, val);
            break;
        }
        invalidate_and_set_dirty(addr1, 2);
    }
}

void stw_phys(hwaddr addr, uint32_t val)
{
    stw_phys_internal(addr, val, DEVICE_NATIVE_ENDIAN);
}

void stw_le_phys(hwaddr addr, uint32_t val)
{
    stw_phys_internal(addr, val, DEVICE_LITTLE_ENDIAN);
}

void stw_be_phys(hwaddr addr, uint32_t val)
{
    stw_phys_internal(addr, val, DEVICE_BIG_ENDIAN);
}

/* XXX: optimize */
void stq_phys(hwaddr addr, uint64_t val)
{
    val = tswap64(val);
    cpu_physical_memory_write(addr, &val, 8);
}

void stq_le_phys(hwaddr addr, uint64_t val)
{
    val = cpu_to_le64(val);
    cpu_physical_memory_write(addr, &val, 8);
}

void stq_be_phys(hwaddr addr, uint64_t val)
{
    val = cpu_to_be64(val);
    cpu_physical_memory_write(addr, &val, 8);
}

/* virtual memory access for debug (includes writing to ROM) */
int cpu_memory_rw_debug(CPUArchState *env, target_ulong addr,
                        uint8_t *buf, int len, int is_write)
{
    int l;
    hwaddr phys_addr;
    target_ulong page;

    while (len > 0) {
        page = addr & TARGET_PAGE_MASK;
        phys_addr = cpu_get_phys_page_debug(env, page);
        /* if no physical page mapped, return an error */
        if (phys_addr == -1)
            return -1;
        l = (page + TARGET_PAGE_SIZE) - addr;
        if (l > len)
            l = len;
        phys_addr += (addr & ~TARGET_PAGE_MASK);
        if (is_write)
            cpu_physical_memory_write_rom(phys_addr, buf, l);
        else
            cpu_physical_memory_rw(phys_addr, buf, l, is_write);
        len -= l;
        buf += l;
        addr += l;
    }
    return 0;
}
#endif

/* in deterministic execution mode, instructions doing device I/Os
   must be at the end of the TB */
void cpu_io_recompile(CPUArchState *env, uintptr_t retaddr)
{
    TranslationBlock *tb;
    uint32_t n, cflags;
    target_ulong pc, cs_base;
    uint64_t flags;

    tb = tb_find_pc(retaddr);
    if (!tb) {
        cpu_abort(env, "cpu_io_recompile: could not find TB for pc=%p", 
                  (void *)retaddr);
    }
    n = env->icount_decr.u16.low + tb->icount;
    cpu_restore_state(tb, env, retaddr);
    /* Calculate how many instructions had been executed before the fault
       occurred.  */
    n = n - env->icount_decr.u16.low;
    /* Generate a new TB ending on the I/O insn.  */
    n++;
    /* On MIPS and SH, delay slot instructions can only be restarted if
       they were already the first instruction in the TB.  If this is not
       the first instruction in a TB then re-execute the preceding
       branch.  */
#if defined(TARGET_MIPS)
    if ((env->hflags & MIPS_HFLAG_BMASK) != 0 && n > 1) {
        env->active_tc.PC -= 4;
        env->icount_decr.u16.low++;
        env->hflags &= ~MIPS_HFLAG_BMASK;
    }
#elif defined(TARGET_SH4)
    if ((env->flags & ((DELAY_SLOT | DELAY_SLOT_CONDITIONAL))) != 0
            && n > 1) {
        env->pc -= 2;
        env->icount_decr.u16.low++;
        env->flags &= ~(DELAY_SLOT | DELAY_SLOT_CONDITIONAL);
    }
#endif
    /* This should never happen.  */
    if (n > CF_COUNT_MASK)
        cpu_abort(env, "TB too big during recompile");

    cflags = n | CF_LAST_IO;
    pc = tb->pc;
    cs_base = tb->cs_base;
    flags = tb->flags;
    tb_phys_invalidate(tb, -1);
    /* FIXME: In theory this could raise an exception.  In practice
       we have already translated the block once so it's probably ok.  */
    tb_gen_code(env, pc, cs_base, flags, cflags);
    /* TODO: If env->pc != tb->pc (i.e. the faulting instruction was not
       the first in the TB) then we end up generating a whole new TB and
       repeating the fault, which is horribly inefficient.
       Better would be to execute just this insn uncached, or generate a
       second new TB.  */
    cpu_resume_from_signal(env, NULL);
}

#if !defined(CONFIG_USER_ONLY)

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
    for(i = 0; i < nb_tbs; i++) {
        tb = &tbs[i];
        target_code_size += tb->size;
        if (tb->size > max_target_code_size)
            max_target_code_size = tb->size;
        if (tb->page_addr[1] != -1)
            cross_page++;
        if (tb->tb_next_offset[0] != 0xffff) {
            direct_jmp_count++;
            if (tb->tb_next_offset[1] != 0xffff) {
                direct_jmp2_count++;
            }
        }
    }
    /* XXX: avoid using doubles ? */
    cpu_fprintf(f, "Translation buffer state:\n");
    cpu_fprintf(f, "gen code size       %td/%zd\n",
                code_gen_ptr - code_gen_buffer, code_gen_buffer_max_size);
    cpu_fprintf(f, "TB count            %d/%d\n", 
                nb_tbs, code_gen_max_blocks);
    cpu_fprintf(f, "TB avg target size  %d max=%d bytes\n",
                nb_tbs ? target_code_size / nb_tbs : 0,
                max_target_code_size);
    cpu_fprintf(f, "TB avg host size    %td bytes (expansion ratio: %0.1f)\n",
                nb_tbs ? (code_gen_ptr - code_gen_buffer) / nb_tbs : 0,
                target_code_size ? (double) (code_gen_ptr - code_gen_buffer) / target_code_size : 0);
    cpu_fprintf(f, "cross page TB count %d (%d%%)\n",
            cross_page,
            nb_tbs ? (cross_page * 100) / nb_tbs : 0);
    cpu_fprintf(f, "direct jump count   %d (%d%%) (2 jumps=%d %d%%)\n",
                direct_jmp_count,
                nb_tbs ? (direct_jmp_count * 100) / nb_tbs : 0,
                direct_jmp2_count,
                nb_tbs ? (direct_jmp2_count * 100) / nb_tbs : 0);
    cpu_fprintf(f, "\nStatistics:\n");
    cpu_fprintf(f, "TB flush count      %d\n", tb_flush_count);
    cpu_fprintf(f, "TB invalidate count %d\n", tb_phys_invalidate_count);
    cpu_fprintf(f, "TLB flush count     %d\n", tlb_flush_count);
    tcg_dump_info(f, cpu_fprintf);
}

/*
 * A helper function for the _utterly broken_ virtio device model to find out if
 * it's running on a big endian machine. Don't do this at home kids!
 */
bool virtio_is_big_endian(void);
bool virtio_is_big_endian(void)
{
#if defined(TARGET_WORDS_BIGENDIAN)
    return true;
#else
    return false;
#endif
}

#endif

#ifndef CONFIG_USER_ONLY
bool cpu_physical_memory_is_io(hwaddr phys_addr)
{
    MemoryRegionSection *section;

    section = phys_page_find(address_space_memory.dispatch,
                             phys_addr >> TARGET_PAGE_BITS);

    return !(memory_region_is_ram(section->mr) ||
             memory_region_is_romd(section->mr));
}
#endif
