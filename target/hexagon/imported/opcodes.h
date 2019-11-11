/* Copyright (c) 2019 Qualcomm Innovation Center, Inc. All Rights Reserved. */


#ifndef _OPCODES_H_
#define _OPCODES_H_ 1

/*
 * opcodes.h
 * 
 * Generate all the opcode values into the symbol table of the compiler
 */
#include "attribs.h"

struct ThreadState;
struct Instruction;

typedef enum {
#define OPCODE(IID) IID,
#include "opcodes.odef"
	XX_LAST_OPCODE
#undef OPCODE
} opcode_t;

typedef enum {
	NORMAL,
	HALF,
	SUBINSN_A,
	SUBINSN_L1,
	SUBINSN_L2,
	SUBINSN_S1,
	SUBINSN_S2,
#define DEF_EXT(NAME,START,NUM) EXT_##NAME,
#include "ext.def"
#undef DEF_EXT
	XX_LAST_ENC_CLASS
} enc_class_t;

#include "insn.h"

extern const char *opcode_names[];

extern const char *opcode_reginfo[];
extern const char *opcode_rregs[];
extern const char *opcode_wregs[];
extern const char * opcode_short_semantics[];

typedef struct {
	const char * const encoding;
	size4u_t vals;
	size4u_t dep_vals;
	const enc_class_t enc_class;
	size1u_t is_ee : 1;
} opcode_encoding_t;

extern opcode_encoding_t opcode_encodings[XX_LAST_OPCODE];

extern semantic_insn_t opcode_genptr[];
extern void init_opcode_genptr(void);

extern size4u_t
	opcode_attribs[XX_LAST_OPCODE][(A_ZZ_LASTATTRIB / ATTRIB_WIDTH) + 1];

#ifdef CYCLE_MODE
extern semantic_insn_t opcode_pipe_decptr[];
extern semantic_insn_t opcode_pipe_rfptr[];
extern semantic_insn_t opcode_pipe_exe1ptr[];
extern semantic_insn_t opcode_pipe_exe2ptr[];
extern semantic_insn_t opcode_pipe_exe3ptr[];
#endif							/* #ifdef CYCLE_MODE */

extern void opcode_init(void);

extern int opcode_which_immediate_is_extended(opcode_t opcode);

#endif							/* _OPCODES_H_ */
