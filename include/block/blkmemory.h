#ifndef BLKMEMORY_H
#define BLKMEMORY_H

#include "block/block_int.h"
#include "exec/memory.h"

int bdrv_memory_open(BlockDriverState *bs, AddressSpace *as, uint64_t size);

#endif