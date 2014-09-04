/*
 * s390 adapter definitions
 *
 * Copyright 2013,2014 IBM Corp.
 * Author(s): Cornelia Huck <cornelia.huck@de.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */

#ifndef S390X_ADAPTER_H
#define S390X_ADAPTER_H

struct AdapterInfo {
    uint64_t ind_addr;
    uint64_t summary_addr;
    uint64_t ind_offset;
    uint32_t summary_offset;
    uint32_t adapter_id;
};

#endif
