/* Copyright (c) 2019 Qualcomm Innovation Center, Inc. All Rights Reserved. */

#ifndef MYFENV_H
#define MYFENV_H 1

#ifdef VCPP
#define WINDOWS 1
#endif

#if defined(INTEL)
#include <mathimf.h>
#include <fenv.h>

#define bzero(x, y) memset(x, 0,y)

#elif defined(WIN32)
#include <math.h>

#define bzero(x, y) memset(x, 0,y)

typedef unsigned long fexcept_t;
typedef struct fenv_t {			/* FPP registers */
	fexcept_t _Fe_ctl, _Fe_stat, _Fe_pad[5];
} fenv_t;



#define FE_DOWNWARD     0x01
#define FE_TONEAREST    0x00
#define FE_TOWARDZERO   0x03
#define FE_UPWARD       0x02

#define _FE_EXCEPT_OFF	0
#define _FE_EXMASK_OFF	0
#define _FE_RND_OFF	10

#define _FE_AUTO_RAISE

#define _FE_DIVBYZERO   0x04
#define _FE_INEXACT     0x20
#define _FE_INVALID     0x01
#define _FE_OVERFLOW    0x08
#define _FE_UNDERFLOW   0x10

#define FE_DIVBYZERO	_FE_DIVBYZERO
#define FE_INEXACT	_FE_INEXACT
#define FE_INVALID	_FE_INVALID
#define FE_OVERFLOW	_FE_OVERFLOW
#define FE_UNDERFLOW	_FE_UNDERFLOW

#define FE_ALL_EXCEPT	(FE_DIVBYZERO | FE_INEXACT \
	| FE_INVALID | FE_OVERFLOW | FE_UNDERFLOW)

#define _FE_RND_MASK	0x03u

#define FE_DFL_ENV	(&_CSTD _Fenv0)

extern int fesetround(int mode);
extern int fegetround(void);
extern int feraiseexcept(int exception);
extern int fetestexcept(int excepts);
extern int feclearexcept(int exception);
extern int fegetexceptflag(fexcept_t * flag, int excepts);
extern int fesetexceptflag(const fexcept_t * flag, int exception);

extern int fegetenv(fenv_t *);
extern int feholdexcept(fenv_t *);
extern int fesetenv(const fenv_t *);
extern int feupdateenv(const fenv_t *);
extern int _isnan(double);

#else

/* SANITY! */
#include <fenv.h>
#include <math.h>
#include <float.h>

#endif

#endif
