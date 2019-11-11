/* Copyright (c) 2019 Qualcomm Innovation Center, Inc. All Rights Reserved. */

/*
 * Execution functions
 * 
 * $Id: regs.c,v 
 *
 */
#include <string.h>
#include "arch.h"
#include "regs.h"

//size4u_t reg_mutability[ENDOFREGS];

reg_field_t reg_field_info[] = {
#define DEF_GLOBAL_REG_MUTABILITY(REG,MASK)
#define DEF_REG_MUTABILITY(REG,MASK)
#define DEF_GLOBAL_REG(TAG,NAME,SYMBOL,NUM,OFFSET)
#define DEF_MMAP_REG(TAG,NAME,SYMBOL,NUM,OFFSET)
#define DEF_REG(TAG,NAME,SYMBOL,NUM,OFFSET)
#define DEF_REG_FIELD(TAG,NAME,START,WIDTH,DESCRIPTION)    \
  {NAME,START,WIDTH,DESCRIPTION},
#include "regs.def"
	{NULL, 0, 0}
};

#undef DEF_REG
#undef DEF_GLOBAL_REG
#undef DEF_REG_FIELD
#undef DEF_REG_MUTABILITY
#undef DEF_GLOBAL_REG_MUTABILITY
#undef DEF_MMAP_REG

#if 0
void reg_init(processor_t * proc)
{
	memset(reg_mutability, 0xffffffff, sizeof(reg_mutability));
	memset(proc->reg_globalreg_mutability, 0xffffffff,
		   sizeof(proc->reg_globalreg_mutability));
#define DEF_REG_MUTABILITY(REG,MASK) reg_mutability[REG] = MASK;
#define DEF_GLOBAL_REG_MUTABILITY(REG,MASK) proc->reg_globalreg_mutability[REG] = MASK;
#define DEF_GLOBAL_REG(...)
#define DEF_MMAP_REG(...)
#define DEF_REG(...)
#define DEF_REG_FIELD(...)
#include "regs.def"
#undef DEF_REG_FIELD
#undef DEF_REG
#undef DEF_GLOBAL_REG
#undef DEF_MMAP_REG
#undef DEF_REG_MUTABILITY
#undef DEF_GLOBAL_REG_MUTABILITY
}
#endif
