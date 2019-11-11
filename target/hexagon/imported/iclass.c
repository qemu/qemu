/* Copyright (c) 2019 Qualcomm Innovation Center, Inc. All Rights Reserved. */


#include <stddef.h>
#include <stdio.h>
#include "qemu/osdep.h"
#include "iclass.h"

typedef struct {
	const char * const type;
	const char * const slots;
} iclass_info_t;

static const iclass_info_t iclass_info[] = {

#define DEF_EE_ICLASS32(TYPE,SLOTS,UNITS)	/* nothing */
#define DEF_PP_ICLASS32(TYPE,SLOTS,UNITS) \
	[ICLASS_FROM_TYPE(TYPE)] = { .type = #TYPE, .slots = #SLOTS },

#include "iclass.def"
#undef DEF_PP_ICLASS32
#undef DEF_EE_ICLASS32

#define DEF_PP_ICLASS32(TYPE,SLOTS,UNITS)	/* nothing */
#define DEF_EE_ICLASS32(TYPE,SLOTS,UNITS) \
	[ICLASS_FROM_TYPE(TYPE)] = { .type = #TYPE, .slots = #SLOTS },

#include "iclass.def"
#undef DEF_PP_ICLASS32
#undef DEF_EE_ICLASS32

	{0}
};

const char*
find_iclass_slots(opcode_t opcode, int itype)
{
	/* KLUDGES GO HERE */
	if (GET_ATTRIB(opcode, A_ICOP)) {
		return "2";
	} else if (GET_ATTRIB(opcode, A_RESTRICT_SLOT0ONLY)) {
		return "0";
	} else if (GET_ATTRIB(opcode, A_RESTRICT_SLOT1ONLY)) {
		return "1";
	} else if (GET_ATTRIB(opcode, A_RESTRICT_SLOT2ONLY)) {
		return "2";		
	} else if (GET_ATTRIB(opcode, A_RESTRICT_SLOT3ONLY)) {
		return "3";	
	} else if (GET_ATTRIB(opcode, A_COF) && GET_ATTRIB(opcode, A_INDIRECT)
			&& !GET_ATTRIB(opcode, A_MEMLIKE) && !GET_ATTRIB(opcode, A_MEMLIKE_PACKET_RULES)) {
		return "2";
	} else if (GET_ATTRIB(opcode, A_RESTRICT_NOSLOT1)) {
		return "0";
	} else if ((opcode == J2_trap0) || (opcode == J2_trap1)
			|| (opcode == Y2_isync) || (opcode == J2_rte)
			|| (opcode == J2_pause) ||
			(opcode == J4_hintjumpr)) {
		return "2";
	} else if ((itype == ICLASS_V2LDST) && (GET_ATTRIB(opcode, A_STORE))) {
		return "01";
	} else if ((itype == ICLASS_V2LDST) && (!GET_ATTRIB(opcode, A_STORE))) {
		return "01";
	} else if (GET_ATTRIB(opcode, A_CRSLOT23)) {
		return "23";
	} else if (GET_ATTRIB(opcode, A_RESTRICT_PREFERSLOT0)) {
		return "0";
	} else if (GET_ATTRIB(opcode, A_SUBINSN)) {
		return "01";
	} else if (GET_ATTRIB(opcode, A_CALL)) {
		return "23";
	} else if ((opcode == J4_jumpseti) || (opcode == J4_jumpsetr)) {
		return "23";
	} else if (GET_ATTRIB(opcode, A_EXTENSION) && GET_ATTRIB(opcode, A_CVI)) {
		/* CVI EXTENSIONS */
		if (GET_ATTRIB(opcode, A_CVI_VM)){
			return "01";
		} else if (GET_ATTRIB(opcode, A_RESTRICT_SLOT2ONLY)) {
			return "2";
		} else if (GET_ATTRIB(opcode, A_CVI_SLOT23)) {
			return "23";
		} else if (GET_ATTRIB(opcode, A_CVI_VX)) {
			return "23";
		} else if (GET_ATTRIB(opcode, A_CVI_VX_DV) ) {
			return "23";
		} else if (GET_ATTRIB(opcode, A_CVI_VS_VX) ) {
			return "23";
		} else if (GET_ATTRIB(opcode, A_MEMLIKE)) {
			return "01";
		} else return "0123";
	} else if (GET_ATTRIB(opcode, A_16BIT)) {
		if(GET_ATTRIB(opcode, A_LOAD) || GET_ATTRIB(opcode, A_STORE)) {
			return "01";
		} else {
			return "0123";
		}
	} else {
		return iclass_info[itype].slots;
	}
}

const char *
find_iclass_name(unsigned int opcode, int itype)
{
	/* KLUDGES GO HERE */
	if (GET_ATTRIB(opcode, A_SUBINSN)) {
		return "SUBINSN";
	} else if (GET_ATTRIB(opcode, A_16BIT)) {
		return "16BIT";
	} else if (GET_ATTRIB(opcode, A_MAPPING)) {
		return "MAPPING";
	} else if ((opcode == J2_endloop0) || (opcode == J2_endloop1)
			|| (opcode == J2_endloop01)) {
		return "J";
	} else if ((opcode_encodings[opcode].vals >> 27) == 3) {
		return "COPROC_VX";
	} else if ((opcode_encodings[opcode].vals >> 27) == 5) {
		return "COPROC_VMEM";
	} else {
		return iclass_info[itype].type;
	}
}
