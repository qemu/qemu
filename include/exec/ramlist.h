#ifndef RAMLIST_H
#define RAMLIST_H

#include "qemu/queue.h"
#include "qemu/thread.h"
#include "qemu/rcu.h"
#include "qemu/rcu_queue.h"

typedef struct RAMBlockNotifier RAMBlockNotifier;

#define DIRTY_MEMORY_VGA       0
#define DIRTY_MEMORY_CODE      1
#define DIRTY_MEMORY_MIGRATION 2
#define DIRTY_MEMORY_NUM       3        /* num of dirty bits */

/* The dirty memory bitmap is split into fixed-size blocks to allow growth
 * under RCU.  The bitmap for a block can be accessed as follows:
 *
 *   rcu_read_lock();
 *
 *   DirtyMemoryBlocks *blocks =
 *       qatomic_rcu_read(&ram_list.dirty_memory[DIRTY_MEMORY_MIGRATION]);
 *
 *   ram_addr_t idx = (addr >> TARGET_PAGE_BITS) / DIRTY_MEMORY_BLOCK_SIZE;
 *   unsigned long *block = blocks.blocks[idx];
 *   ...access block bitmap...
 *
 *   rcu_read_unlock();
 *
 * Remember to check for the end of the block when accessing a range of
 * addresses.  Move on to the next block if you reach the end.
 *
 * Organization into blocks allows dirty memory to grow (but not shrink) under
 * RCU.  When adding new RAMBlocks requires the dirty memory to grow, a new
 * DirtyMemoryBlocks array is allocated with pointers to existing blocks kept
 * the same.  Other threads can safely access existing blocks while dirty
 * memory is being grown.  When no threads are using the old DirtyMemoryBlocks
 * anymore it is freed by RCU (but the underlying blocks stay because they are
 * pointed to from the new DirtyMemoryBlocks).
 */
#define DIRTY_MEMORY_BLOCK_SIZE ((ram_addr_t)256 * 1024 * 8)
typedef struct {
    struct rcu_head rcu;
    unsigned long *blocks[];
} DirtyMemoryBlocks;

typedef struct RAMList {
    QemuMutex mutex;
    RAMBlock *mru_block;
    /* RCU-enabled, writes protected by the ramlist lock. */
    QLIST_HEAD(, RAMBlock) blocks;
    DirtyMemoryBlocks *dirty_memory[DIRTY_MEMORY_NUM];
    uint32_t version;
    QLIST_HEAD(, RAMBlockNotifier) ramblock_notifiers;
} RAMList;
extern RAMList ram_list;

/* Should be holding either ram_list.mutex, or the RCU lock. */
#define  INTERNAL_RAMBLOCK_FOREACH(block)  \
    QLIST_FOREACH_RCU(block, &ram_list.blocks, next)
/* Never use the INTERNAL_ version except for defining other macros */
#define RAMBLOCK_FOREACH(block) INTERNAL_RAMBLOCK_FOREACH(block)

void qemu_mutex_lock_ramlist(void);
void qemu_mutex_unlock_ramlist(void);

struct RAMBlockNotifier {
    void (*ram_block_added)(RAMBlockNotifier *n, void *host, size_t size,
                            size_t max_size);
    void (*ram_block_removed)(RAMBlockNotifier *n, void *host, size_t size,
                              size_t max_size);
    void (*ram_block_resized)(RAMBlockNotifier *n, void *host, size_t old_size,
                              size_t new_size);
    QLIST_ENTRY(RAMBlockNotifier) next;
};

void ram_block_notifier_add(RAMBlockNotifier *n);
void ram_block_notifier_remove(RAMBlockNotifier *n);
void ram_block_notify_add(void *host, size_t size, size_t max_size);
void ram_block_notify_remove(void *host, size_t size, size_t max_size);
void ram_block_notify_resize(void *host, size_t old_size, size_t new_size);

void ram_block_dump(Monitor *mon);

#endif /* RAMLIST_H */
