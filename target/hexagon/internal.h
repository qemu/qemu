/* Copyright (c) 2019 Qualcomm Innovation Center, Inc. All Rights Reserved. */

#ifndef HEXAGON_INTERNAL_H
#define HEXAGON_INTERNAL_H

/*
 * Change COUNT_HEX_HELPERS to 1 to count how many times each helper
 * is called.  This is useful to figure out which helpers would benefit
 * from writing an fWRAP macro.
 */
#define COUNT_HEX_HELPERS 0

extern int hexagon_gdb_read_register(CPUState *cpu, uint8_t *buf, int reg);
extern int hexagon_gdb_write_register(CPUState *cpu, uint8_t *buf, int reg);

extern void hexagon_debug_vreg(CPUHexagonState *env, int regnum);
extern void hexagon_debug_qreg(CPUHexagonState *env, int regnum);
extern void hexagon_debug(CPUHexagonState *env);

#if COUNT_HEX_HELPERS
extern void print_helper_counts(void);
#endif

extern const char * const hexagon_regnames[];

#endif
