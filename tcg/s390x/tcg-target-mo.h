/* SPDX-License-Identifier: MIT */
/*
 * Define target-specific memory model
 * Copyright (c) 2009 Ulrich Hecht <uli@suse.de>
 */

#ifndef TCG_TARGET_MO_H
#define TCG_TARGET_MO_H

#define TCG_TARGET_DEFAULT_MO (TCG_MO_ALL & ~TCG_MO_ST_LD)

#endif
