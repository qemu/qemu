/*
 * BCM2835 CPRMAN clock manager
 *
 * Copyright (c) 2020 Luc Michel <luc@lmichel.fr>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_CPRMAN_INTERNALS_H
#define HW_MISC_CPRMAN_INTERNALS_H

#include "hw/registerfields.h"
#include "hw/misc/bcm2835_cprman.h"

/* Register map */

/*
 * This field is common to all registers. Each register write value must match
 * the CPRMAN_PASSWORD magic value in its 8 MSB.
 */
FIELD(CPRMAN, PASSWORD, 24, 8)
#define CPRMAN_PASSWORD 0x5a

#endif
