/*
 * QEMU Random Number Generator Backend
 *
 * Copyright IBM, Corp. 2012
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#ifndef QEMU_RNG_RANDOM_H
#define QEMU_RNG_RANDOM_H

#include "qom/object.h"

#define TYPE_RNG_RANDOM "rng-random"
typedef struct RngRandom RngRandom;
DECLARE_INSTANCE_CHECKER(RngRandom, RNG_RANDOM,
                         TYPE_RNG_RANDOM)


#endif
