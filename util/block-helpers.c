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
#include "qapi/qmp/qerror.h"
#include "block-helpers.h"

/**
 * check_block_size:
 * @id: The unique ID of the object
 * @name: The name of the property being validated
 * @value: The block size in bytes
 * @errp: A pointer to an area to store an error
 *
 * This function checks that the block size meets the following conditions:
 * 1. At least MIN_BLOCK_SIZE
 * 2. No larger than MAX_BLOCK_SIZE
 * 3. A power of 2
 */
void check_block_size(const char *id, const char *name, int64_t value,
                      Error **errp)
{
    /* value of 0 means "unset" */
    if (value && (value < MIN_BLOCK_SIZE || value > MAX_BLOCK_SIZE)) {
        error_setg(errp, QERR_PROPERTY_VALUE_OUT_OF_RANGE,
                   id, name, value, MIN_BLOCK_SIZE, MAX_BLOCK_SIZE);
        return;
    }

    /* We rely on power-of-2 blocksizes for bitmasks */
    if ((value & (value - 1)) != 0) {
        error_setg(errp,
                   "Property %s.%s doesn't take value '%" PRId64
                   "', it's not a power of 2",
                   id, name, value);
        return;
    }
}
