/*
 * Common Float Helpers
 *
 * Copyright (c) 2019 Linaro
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <inttypes.h>

/* Some hosts do not have support for all of these; not required by ISO C. */
#ifndef FE_OVERFLOW
#define FE_OVERFLOW 0
#endif
#ifndef FE_UNDERFLOW
#define FE_UNDERFLOW 0
#endif
#ifndef FE_DIVBYZERO
#define FE_DIVBYZERO 0
#endif
#ifndef FE_INEXACT
#define FE_INEXACT 0
#endif
#ifndef FE_INVALID
#define FE_INVALID 0
#endif

/* Number of constants in each table */
int get_num_f16(void);
int get_num_f32(void);
int get_num_f64(void);

/* Accessor helpers, overflows will automatically wrap */
uint16_t get_f16(int i); /* use _Float16 when we can */
float    get_f32(int i);
double   get_f64(int i);

/* Return format strings, free after use */
char * fmt_f16(uint16_t);
char * fmt_f32(float);
char * fmt_f64(double);
/* exception flags */
char * fmt_flags(void);
