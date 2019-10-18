/* Copyright (c) 2019 Qualcomm Innovation Center, Inc. All Rights Reserved. */

#pragma once

#include "opcodes.h"

#define ICLASS_FROM_TYPE(TYPE) ICLASS_##TYPE

typedef enum {

#define DEF_PP_ICLASS32(TYPE,SLOTS,UNITS) ICLASS_FROM_TYPE(TYPE),
#define DEF_EE_ICLASS32(TYPE,SLOTS,UNITS)	/* nothing */
#include "iclass.def"
#undef DEF_PP_ICLASS32
#undef DEF_EE_ICLASS32

#define DEF_EE_ICLASS32(TYPE,SLOTS,UNITS) ICLASS_FROM_TYPE(TYPE),
#define DEF_PP_ICLASS32(TYPE,SLOTS,UNITS)	/* nothing */
#include "iclass.def"
#undef DEF_PP_ICLASS32
#undef DEF_EE_ICLASS32

    ICLASS_FROM_TYPE(COPROC_VX),
    ICLASS_FROM_TYPE(COPROC_VMEM),
	NUM_ICLASSES
} iclass_t;

const char *
find_iclass_slots(opcode_t opcode, int itype);

const char *
find_iclass_name(opcode_t opcode, int itype);
