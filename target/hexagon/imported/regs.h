/*
 *  Copyright (c) 2019 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

/*
 * There are 32 general user regs and up to 32 user control regs.
 */


#ifndef _REGS_H
#define _REGS_H

#include "arch_types.h"

#define NUM_GEN_REGS 32
#define NUM_PER_THREAD_CR (32 + 32 + 16 + 48)	/* user + guest + per-thread supervisor + A regs */
#define NUM_GLOBAL_REGS (128 + 32) /* + A regs */
#define NUM_PMU_REGS 8

/* L2 registers info. They are here, because we want them architected. */
#define BASE_L2_REGS 0x0
/* do nothing on read for now  - FIXME*/
#define READ_L2REG(...)  0

enum regs_enum {
#define DEF_REG_FIELD(TAG,NAME,START,WIDTH,DESCRIPTION)
#define DEF_GLOBAL_REG(TAG,NAME,SYMBOL,NUM,OFFSET)
#define DEF_MMAP_REG(TAG,NAME,SYMBOL,NUM,OFFSET)
#define DEF_REG(TAG,NAME,SYMBOL,NUM,OFFSET)\
  TAG=OFFSET,
#include "regs_def.h"
#undef DEF_REG_FIELD
#undef DEF_GLOBAL_REG
#undef DEF_MMAP_REG
#undef DEF_REG
};

typedef struct {
	const char *name;
	int offset;
	int width;
	const char *description;
} reg_field_t;

extern reg_field_t reg_field_info[];



#define INTERRUPT_MAX 32
#define INT_NUMTOMASK(N) (0x00000001U << (N))



enum global_regs_enum {
#define DEF_REG_FIELD(TAG,NAME,START,WIDTH,DESCRIPTION)
#define DEF_REG(TAG,NAME,SYMBOL,NUM,OFFSET)
#define DEF_MMAP_REG(TAG,NAME,SYMBOL,NUM,OFFSET)
#define DEF_GLOBAL_REG(TAG,NAME,SYMBOL,NUM,OFFSET) \
    TAG = OFFSET,
#include "regs_def.h"
    END_GLOBAL_REGS
};

#undef DEF_REG
#undef DEF_MMAP_REG
#undef DEF_GLOBAL_REG
#undef DEF_REG_FIELD


enum reg_fields_enum {
#define DEF_REG(TAG,NAME,SYMBOL,NUM,OFFSET)
#define DEF_GLOBAL_REG(TAG,NAME,SYMBOL,NUM,OFFSET)
#define DEF_MMAP_REG(TAG,NAME,SYMBOL,NUM,OFFSET)
#define DEF_REG_FIELD(TAG,NAME,START,WIDTH,DESCRIPTION) \
    TAG,
#include "regs_def.h"
    NUM_REG_FIELDS
};

#undef DEF_REG
#undef DEF_GLOBAL_REG
#undef DEF_MMAP_REG
#undef DEF_REG_FIELD

#endif
