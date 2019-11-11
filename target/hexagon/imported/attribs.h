/* Copyright (c) 2019 Qualcomm Innovation Center, Inc. All Rights Reserved. */


#ifndef _ATTRIBS_H
#define _ATTRIBS_H 1

enum {
#define DEF_ATTRIB(NAME,...) A_##NAME,
#include "attribs.def"
#undef DEF_ATTRIB
};

#define ATTRIB_WIDTH 32
#define GET_ATTRIB(opcode,attrib) \
	(((opcode_attribs[opcode][attrib/ATTRIB_WIDTH])\
	>>(attrib%ATTRIB_WIDTH))&0x1)

#endif							/* _ATTRIBS_H */
