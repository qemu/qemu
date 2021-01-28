#ifndef BLOCK_HELPERS_H
#define BLOCK_HELPERS_H

#include "qemu/units.h"

/* lower limit is sector size */
#define MIN_BLOCK_SIZE          INT64_C(512)
#define MIN_BLOCK_SIZE_STR      "512 B"
/*
 * upper limit is arbitrary, 2 MiB looks sufficient for all sensible uses, and
 * matches qcow2 cluster size limit
 */
#define MAX_BLOCK_SIZE          (2 * MiB)
#define MAX_BLOCK_SIZE_STR      "2 MiB"

void check_block_size(const char *id, const char *name, int64_t value,
                      Error **errp);

#endif /* BLOCK_HELPERS_H */
