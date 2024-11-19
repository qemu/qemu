/*
 * Block utility functions
 *
 * Copyright IBM, Corp. 2011
 * Copyright (c) 2020 Coiby Xu <coiby.xu@gmail.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "block-helpers.h"

/**
 * check_block_size:
 * @name: The name of the property being validated
 * @value: The block size in bytes
 * @errp: A pointer to an area to store an error
 *
 * This function checks that the block size meets the following conditions:
 * 1. At least MIN_BLOCK_SIZE
 * 2. No larger than MAX_BLOCK_SIZE
 * 3. A power of 2
 *
 * Returns: true on success, false on failure
 */
bool check_block_size(const char *name, int64_t value, Error **errp)
{
    if (!value) {
        /* unset */
        return true;
    }

    if (value < MIN_BLOCK_SIZE || value > MAX_BLOCK_SIZE
        || (value & (value - 1))) {
        error_setg(errp,
                   "parameter %s must be a power of 2 between %" PRId64
                   " and %" PRId64,
                   name, MIN_BLOCK_SIZE, MAX_BLOCK_SIZE);
        return false;
    }
    return true;
}
