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
 * opcodes.c 
 * 
 * data tables generated automatically 
 * Maybe some functions too 
 * 
 * 
 */

#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include "qemu/osdep.h"
#include "opcodes.h"

void decode_init(void);

//#if defined(NO_SILVER) || defined(DISASM_SILVER) || defined(ARCH_GENISET)
#define VEC_DESCR(A,B,C) DESCR(A,B,C)
#define DONAME(X) #X
//#else
//#define VEC_DESCR(A,B,C) DESCR(A,"coproc insn","coproc insn")
//#define DONAME(X) "unavailable"
//#endif


const char *opcode_names[] = {
#define OPCODE(IID) DONAME(IID),
#include "opcodes.odef"
	NULL
#undef OPCODE
};

const char *opcode_reginfo[] = {
#define IMMINFO(TAG,SIGN,SIZE,SHAMT,SIGN2,SIZE2,SHAMT2)	/* nothing */
#define REGINFO(TAG,REGINFO,RREGS,WREGS) REGINFO,
#include "op_regs.odef"
	NULL
#undef REGINFO
#undef IMMINFO
};


const char *opcode_rregs[] = {
#define IMMINFO(TAG,SIGN,SIZE,SHAMT,SIGN2,SIZE2,SHAMT2)	/* nothing */
#define REGINFO(TAG,REGINFO,RREGS,WREGS) RREGS,
#include "op_regs.odef"
	NULL
#undef REGINFO
#undef IMMINFO
};


const char *opcode_wregs[] = {
#define IMMINFO(TAG,SIGN,SIZE,SHAMT,SIGN2,SIZE2,SHAMT2)	/* nothing */
#define REGINFO(TAG,REGINFO,RREGS,WREGS) WREGS,
#include "op_regs.odef"
	NULL
#undef REGINFO
#undef IMMINFO
};

const char * opcode_short_semantics[] = {
#define OPCODE(X)              NULL,
#include "opcodes.odef"
#undef OPCODE
	NULL
};


size4u_t opcode_attribs[XX_LAST_OPCODE][(A_ZZ_LASTATTRIB / ATTRIB_WIDTH) + 1] = {0};

static void init_attribs(int tag, ...)
{
	va_list ap;
	int attr;
	va_start(ap, tag);
	while ((attr = va_arg(ap, int)) != 0) {
		opcode_attribs[tag][attr / ATTRIB_WIDTH] |=
			1 << (attr % ATTRIB_WIDTH);
	}
}

void opcode_init(void)
{
	init_attribs(0,0);

#define ATTRIBS(...) , ## __VA_ARGS__,0
#define OP_ATTRIB(TAG,ARGS) init_attribs(TAG ARGS);
#include "op_attribs.odef"
#undef OP_ATTRIB
#undef ATTRIBS
	decode_init();
	
#define DEF_QEMU(TAG, SHORTCODE, HELPER, GENFN, HELPFN) \
    opcode_short_semantics[TAG] = #SHORTCODE;
#include "qemu.odef"
#undef DEF_QEMU
}


/* Note: Commenting out sim_err_fatal for now. These conditions are
   already checked for before we enter the function. If a wrong
   opcode is read, an exception will be seen later on in the
   code. No need to do a sim_err_fatal here. */
#define NEEDLE "IMMEXT("
int opcode_which_immediate_is_extended(opcode_t opcode)
{
	const char *p;
	if (opcode >= XX_LAST_OPCODE) {
#if 0
		sim_err_fatal(NULL, NULL, 0, (char *) __FUNCTION__, __FILE__,
					  __LINE__, "Bad Opcode: 0x%x\n", opcode);
#endif
		return 0;
	}
	if (!GET_ATTRIB(opcode, A_EXTENDABLE)) {
#if 0
		sim_err_fatal(NULL, NULL, 0, (char *) __FUNCTION__, __FILE__,
					  __LINE__, "Bad Opcode: 0x%x\n", opcode);
		sim_err_fatal(NULL, NULL, 0, (char *) __FUNCTION__, __FILE__,
					  __LINE__, "Opcode not extendable: %s\n",
					  opcode_names[opcode]);
#endif
		return 0;
	}
	p = opcode_short_semantics[opcode];
	p = strstr(p, NEEDLE);
	if (p == NULL) {
#if 0
		sim_err_fatal(NULL, NULL, 0, (char *) __FUNCTION__, __FILE__,
					  __LINE__, "Could not find " NEEDLE " in %s",
					  opcode_short_semantics[opcode]);
#endif
		return 0;
	}
	p += strlen(NEEDLE);
	while (isspace(*p))
		p++;
	/* EJP: lower is always imm 0, upper always imm 1. */
	if (islower(*p)) {
		return 0;
	} else if (isupper(*p)) {
		return 1;
	} else {
#if 0
		sim_err_fatal(NULL, NULL, 0, (char *) __FUNCTION__, __FILE__,
					  __LINE__, "Character `%c' is unknown", *p);
#endif
		return 0;
	}
}
