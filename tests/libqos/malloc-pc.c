/*
 * libqos malloc support for PC
 *
 * Copyright IBM, Corp. 2012-2013
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "libqos/malloc-pc.h"
#include "libqos/fw_cfg.h"

#define NO_QEMU_PROTOS
#include "hw/nvram/fw_cfg.h"

#include "qemu-common.h"
#include "qemu/queue.h"
#include <glib.h>

#define PAGE_SIZE (4096)

#define MLIST_ENTNAME entries
typedef QTAILQ_HEAD(MemList, MemBlock) MemList;
typedef struct MemBlock {
    QTAILQ_ENTRY(MemBlock) MLIST_ENTNAME;
    uint64_t size;
    uint64_t addr;
} MemBlock;

typedef struct PCAlloc
{
    QGuestAllocator alloc;
    PCAllocOpts opts;
    uint64_t start;
    uint64_t end;

    MemList used;
    MemList free;
} PCAlloc;

static MemBlock *mlist_new(uint64_t addr, uint64_t size)
{
    MemBlock *block;

    if (!size) {
        return NULL;
    }
    block = g_malloc0(sizeof(MemBlock));

    block->addr = addr;
    block->size = size;

    return block;
}

static void mlist_delete(MemList *list, MemBlock *node)
{
    g_assert(list && node);
    QTAILQ_REMOVE(list, node, MLIST_ENTNAME);
    g_free(node);
}

static MemBlock *mlist_find_key(MemList *head, uint64_t addr)
{
    MemBlock *node;
    QTAILQ_FOREACH(node, head, MLIST_ENTNAME) {
        if (node->addr == addr) {
            return node;
        }
    }
    return NULL;
}

static MemBlock *mlist_find_space(MemList *head, uint64_t size)
{
    MemBlock *node;

    QTAILQ_FOREACH(node, head, MLIST_ENTNAME) {
        if (node->size >= size) {
            return node;
        }
    }
    return NULL;
}

static MemBlock *mlist_sort_insert(MemList *head, MemBlock *insr)
{
    MemBlock *node;
    g_assert(head && insr);

    QTAILQ_FOREACH(node, head, MLIST_ENTNAME) {
        if (insr->addr < node->addr) {
            QTAILQ_INSERT_BEFORE(node, insr, MLIST_ENTNAME);
            return insr;
        }
    }

    QTAILQ_INSERT_TAIL(head, insr, MLIST_ENTNAME);
    return insr;
}

static inline uint64_t mlist_boundary(MemBlock *node)
{
    return node->size + node->addr;
}

static MemBlock *mlist_join(MemList *head, MemBlock *left, MemBlock *right)
{
    g_assert(head && left && right);

    left->size += right->size;
    mlist_delete(head, right);
    return left;
}

static void mlist_coalesce(MemList *head, MemBlock *node)
{
    g_assert(node);
    MemBlock *left;
    MemBlock *right;
    char merge;

    do {
        merge = 0;
        left = QTAILQ_PREV(node, MemList, MLIST_ENTNAME);
        right = QTAILQ_NEXT(node, MLIST_ENTNAME);

        /* clowns to the left of me */
        if (left && mlist_boundary(left) == node->addr) {
            node = mlist_join(head, left, node);
            merge = 1;
        }

        /* jokers to the right */
        if (right && mlist_boundary(node) == right->addr) {
            node = mlist_join(head, node, right);
            merge = 1;
        }

    } while (merge);
}

static uint64_t pc_mlist_fulfill(PCAlloc *s, MemBlock *freenode, uint64_t size)
{
    uint64_t addr;
    MemBlock *usednode;

    g_assert(freenode);
    g_assert_cmpint(freenode->size, >=, size);

    addr = freenode->addr;
    if (freenode->size == size) {
        /* re-use this freenode as our used node */
        QTAILQ_REMOVE(&s->free, freenode, MLIST_ENTNAME);
        usednode = freenode;
    } else {
        /* adjust the free node and create a new used node */
        freenode->addr += size;
        freenode->size -= size;
        usednode = mlist_new(addr, size);
    }

    mlist_sort_insert(&s->used, usednode);
    return addr;
}

/* To assert the correctness of the list.
 * Used only if PC_ALLOC_PARANOID is set. */
static void pc_mlist_check(PCAlloc *s)
{
    MemBlock *node;
    uint64_t addr = s->start > 0 ? s->start - 1 : 0;
    uint64_t next = s->start;

    QTAILQ_FOREACH(node, &s->free, MLIST_ENTNAME) {
        g_assert_cmpint(node->addr, >, addr);
        g_assert_cmpint(node->addr, >=, next);
        addr = node->addr;
        next = node->addr + node->size;
    }

    addr = s->start > 0 ? s->start - 1 : 0;
    next = s->start;
    QTAILQ_FOREACH(node, &s->used, MLIST_ENTNAME) {
        g_assert_cmpint(node->addr, >, addr);
        g_assert_cmpint(node->addr, >=, next);
        addr = node->addr;
        next = node->addr + node->size;
    }
}

static uint64_t pc_mlist_alloc(PCAlloc *s, uint64_t size)
{
    MemBlock *node;

    node = mlist_find_space(&s->free, size);
    if (!node) {
        fprintf(stderr, "Out of guest memory.\n");
        g_assert_not_reached();
    }
    return pc_mlist_fulfill(s, node, size);
}

static void pc_mlist_free(PCAlloc *s, uint64_t addr)
{
    MemBlock *node;

    if (addr == 0) {
        return;
    }

    node = mlist_find_key(&s->used, addr);
    if (!node) {
        fprintf(stderr, "Error: no record found for an allocation at "
                "0x%016" PRIx64 ".\n",
                addr);
        g_assert_not_reached();
    }

    /* Rip it out of the used list and re-insert back into the free list. */
    QTAILQ_REMOVE(&s->used, node, MLIST_ENTNAME);
    mlist_sort_insert(&s->free, node);
    mlist_coalesce(&s->free, node);
}

static uint64_t pc_alloc(QGuestAllocator *allocator, size_t size)
{
    PCAlloc *s = container_of(allocator, PCAlloc, alloc);
    uint64_t rsize = size;
    uint64_t naddr;

    rsize += (PAGE_SIZE - 1);
    rsize &= -PAGE_SIZE;
    g_assert_cmpint((s->start + rsize), <=, s->end);
    g_assert_cmpint(rsize, >=, size);

    naddr = pc_mlist_alloc(s, rsize);
    if (s->opts & PC_ALLOC_PARANOID) {
        pc_mlist_check(s);
    }

    return naddr;
}

static void pc_free(QGuestAllocator *allocator, uint64_t addr)
{
    PCAlloc *s = container_of(allocator, PCAlloc, alloc);

    pc_mlist_free(s, addr);
    if (s->opts & PC_ALLOC_PARANOID) {
        pc_mlist_check(s);
    }
}

/*
 * Mostly for valgrind happiness, but it does offer
 * a chokepoint for debugging guest memory leaks, too.
 */
void pc_alloc_uninit(QGuestAllocator *allocator)
{
    PCAlloc *s = container_of(allocator, PCAlloc, alloc);
    MemBlock *node;
    MemBlock *tmp;
    PCAllocOpts mask;

    /* Check for guest leaks, and destroy the list. */
    QTAILQ_FOREACH_SAFE(node, &s->used, MLIST_ENTNAME, tmp) {
        if (s->opts & (PC_ALLOC_LEAK_WARN | PC_ALLOC_LEAK_ASSERT)) {
            fprintf(stderr, "guest malloc leak @ 0x%016" PRIx64 "; "
                    "size 0x%016" PRIx64 ".\n",
                    node->addr, node->size);
        }
        if (s->opts & (PC_ALLOC_LEAK_ASSERT)) {
            g_assert_not_reached();
        }
        g_free(node);
    }

    /* If we have previously asserted that there are no leaks, then there
     * should be only one node here with a specific address and size. */
    mask = PC_ALLOC_LEAK_ASSERT | PC_ALLOC_PARANOID;
    QTAILQ_FOREACH_SAFE(node, &s->free, MLIST_ENTNAME, tmp) {
        if ((s->opts & mask) == mask) {
            if ((node->addr != s->start) ||
                (node->size != s->end - s->start)) {
                fprintf(stderr, "Free list is corrupted.\n");
                g_assert_not_reached();
            }
        }

        g_free(node);
    }

    g_free(s);
}

QGuestAllocator *pc_alloc_init_flags(PCAllocOpts flags)
{
    PCAlloc *s = g_malloc0(sizeof(*s));
    uint64_t ram_size;
    QFWCFG *fw_cfg = pc_fw_cfg_init();
    MemBlock *node;

    s->opts = flags;
    s->alloc.alloc = pc_alloc;
    s->alloc.free = pc_free;

    ram_size = qfw_cfg_get_u64(fw_cfg, FW_CFG_RAM_SIZE);

    /* Start at 1MB */
    s->start = 1 << 20;

    /* Respect PCI hole */
    s->end = MIN(ram_size, 0xE0000000);

    /* clean-up */
    g_free(fw_cfg);

    QTAILQ_INIT(&s->used);
    QTAILQ_INIT(&s->free);

    node = mlist_new(s->start, s->end - s->start);
    QTAILQ_INSERT_HEAD(&s->free, node, MLIST_ENTNAME);

    return &s->alloc;
}

inline QGuestAllocator *pc_alloc_init(void)
{
    return pc_alloc_init_flags(PC_ALLOC_NO_FLAGS);
}
