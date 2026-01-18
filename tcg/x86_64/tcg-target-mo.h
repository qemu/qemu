/* SPDX-License-Identifier: MIT */
/*
 * Define target-specific memory model
 * Copyright (c) 2008 Fabrice Bellard
 */

#ifndef TCG_TARGET_MO_H
#define TCG_TARGET_MO_H

/*
 * This defines the natural memory order supported by this architecture
 * before guarantees made by various barrier instructions.
 *
 * The x86 has a pretty strong memory ordering which only really
 * allows for some stores to be re-ordered after loads.
 */
#define TCG_TARGET_DEFAULT_MO (TCG_MO_ALL & ~TCG_MO_ST_LD)

#endif
