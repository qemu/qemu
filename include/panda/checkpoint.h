#include "panda/rr/rr_log.h"

typedef struct Checkpoint {
    uint64_t guest_instr_count;
    size_t nondet_log_position;

    unsigned long long number_of_log_entries[RR_LAST];
    unsigned long long size_of_log_entries[RR_LAST];
    unsigned long long max_num_queue_entries;

    unsigned next_progress;

    int memfd;

    size_t memfd_usage;

    QLIST_ENTRY(Checkpoint) next;
} Checkpoint;

#define MAX_CHECKPOINTS 256
extern Checkpoint* checkpoints[MAX_CHECKPOINTS];

/*void* search_checkpoints(uint64_t target_instr);*/

/**
 * get_num_checkpoints() - Get number of checkpoints in current checkpointed recording.
 * 
 * Return: Number of checkpoints.
 */
size_t get_num_checkpoints(void);

/**
 * get_closest_checkpoint_num() - Determine checkpoint closest to this instruction count.
 * @instr_count: Instruction count we'd like to get closest to.
 *
 * Return: Checkpoint number.
 */
int get_closest_checkpoint_num(uint64_t instr_count);

/**
 * get_checkpoint() - Get this checkpoint, by number.
 * @num: The number.
 *
 * Return: Pointer to the checkpoint.
 */
Checkpoint* get_checkpoint(int num);

/** 
 * panda_checkpoint() - Take a checkpoint.
 *
 * This has to happen whilst we are in replay.  Take a checkpoint
 * we'll later be able to return to using panda_restore_by_num.
 */
void* panda_checkpoint(void);

/**
 * panda_restore_by_num() - Restore to a particular checkpoint
 * @num: Checkpoint number.
 *
 * Restore to this numbered checkpoint. 
 */
void panda_restore_by_num(int num);

// Not an API fn I think.
void panda_restore(void *opaque);
