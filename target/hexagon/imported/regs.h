/* Copyright (c) 2019 Qualcomm Innovation Center, Inc. All Rights Reserved. */


/*
 * There are 32 general user regs and up to 32 user control regs.
 */


#ifndef _REGS_H
#define _REGS_H

#include "arch_types.h"

#define DEF_SUBSYS_REG(NAME,DESC,OFFSET)

#define NUM_GEN_REGS 32
#define NUM_PREGS 4
#define NUM_PER_THREAD_CR (32 + 32 + 16 + 48)	/* user + guest + per-thread supervisor + A regs */
#ifdef CONFIG_USER_ONLY
#define TOTAL_PER_THREAD_REGS 64
#else
#define TOTAL_PER_THREAD_REGS (NUM_GEN_REGS+NUM_PER_THREAD_CR)
#endif
#define NUM_GLOBAL_REGS (128 + 32) /* + A regs */
#define NUM_PMU_REGS 8

/* L2 registers info. They are here, because we want them architected. */
#define BASE_L2_REGS 0x0
/* do nothing on read for now  - FIXME*/
#define READ_L2REG(...)  0

enum regs_enum {
#define DEF_REG_MUTABILITY(REG,MASK)
#define DEF_GLOBAL_REG_MUTABILITY(REG,MASK)
#define DEF_REG_FIELD(TAG,NAME,START,WIDTH,DESCRIPTION)
#define DEF_GLOBAL_REG(TAG,NAME,SYMBOL,NUM,OFFSET)
#define DEF_MMAP_REG(TAG,NAME,SYMBOL,NUM,OFFSET)
#define DEF_REG(TAG,NAME,SYMBOL,NUM,OFFSET)\
  TAG=OFFSET,
#include "regs.def"
#undef DEF_REG_MUTABILITY
#undef DEF_GLOBAL_REG_MUTABILITY
#undef DEF_REG_FIELD
#undef DEF_GLOBAL_REG
#undef DEF_MMAP_REG
#undef DEF_REG
};

enum last_regs_enum {
#define DEF_REG_MUTABILITY(REG,MASK)
#define DEF_GLOBAL_REG_MUTABILITY(REG,MASK)
#define DEF_REG_FIELD(TAG,NAME,START,WIDTH,DESCRIPTION)
#define DEF_GLOBAL_REG(TAG,NAME,SYMBOL,NUM,OFFSET)
#define DEF_MMAP_REG(TAG,NAME,SYMBOL,NUM,OFFSET)
#define DEF_REG(TAG,NAME,SYMBOL,NUM,OFFSET)\
  LAST_REG_FOR_##TAG=(OFFSET+NUM-1),
#include "regs.def"
	ENDOFREGS
#undef DEF_REG_MUTABILITY
#undef DEF_GLOBAL_REG_MUTABILITY
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
#define DEF_REG_MUTABILITY(REG,MASK)
#define DEF_GLOBAL_REG_MUTABILITY(REG,MASK)
#define DEF_REG_FIELD(TAG,NAME,START,WIDTH,DESCRIPTION)
#define DEF_REG(TAG,NAME,SYMBOL,NUM,OFFSET)
#define DEF_MMAP_REG(TAG,NAME,SYMBOL,NUM,OFFSET)
#define DEF_GLOBAL_REG(TAG,NAME,SYMBOL,NUM,OFFSET) \
    TAG = OFFSET,
#include "regs.def"
    END_GLOBAL_REGS
};

#undef DEF_REG
#undef DEF_MMAP_REG
#undef DEF_GLOBAL_REG
#undef DEF_REG_FIELD
#undef DEF_REG_MUTABILITY
#undef DEF_GLOBAL_REG_MUTABILITY


enum reg_fields_enum {
#define DEF_REG_MUTABILITY(REG,MASK)
#define DEF_GLOBAL_REG_MUTABILITY(REG,MASK)
#define DEF_REG(TAG,NAME,SYMBOL,NUM,OFFSET)
#define DEF_GLOBAL_REG(TAG,NAME,SYMBOL,NUM,OFFSET)
#define DEF_MMAP_REG(TAG,NAME,SYMBOL,NUM,OFFSET)
#define DEF_REG_FIELD(TAG,NAME,START,WIDTH,DESCRIPTION) \
    TAG,
#include "regs.def"
    NUM_REG_FIELDS
};

#undef DEF_REG
#undef DEF_GLOBAL_REG
#undef DEF_MMAP_REG
#undef DEF_REG_FIELD
#undef DEF_REG_MUTABILITY
#undef DEF_GLOBAL_REG_MUTABILITY

#endif
