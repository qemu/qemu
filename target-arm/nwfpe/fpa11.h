/*
    NetWinder Floating Point Emulator
    (c) Rebel.com, 1998-1999
    
    Direct questions, comments to Scott Bambrough <scottb@netwinder.org>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#ifndef __FPA11_H__
#define __FPA11_H__

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

#define GET_FPA11() (qemufpa)

/*
 * The processes registers are always at the very top of the 8K
 * stack+task struct.  Use the same method as 'current' uses to
 * reach them.
 */
extern unsigned int *user_registers;

#define GET_USERREG() (user_registers)

/* Need task_struct */
//#include <linux/sched.h>

/* includes */
#include "fpsr.h"		/* FP control and status register definitions */
#include "softfloat.h"

#define		typeNone		0x00
#define		typeSingle		0x01
#define		typeDouble		0x02
#define		typeExtended		0x03

/*
 * This must be no more and no less than 12 bytes.
 */
typedef union tagFPREG {
   floatx80 fExtended;
   float64  fDouble;
   float32  fSingle;
} FPREG;

/*
 * FPA11 device model.
 *
 * This structure is exported to user space.  Do not re-order.
 * Only add new stuff to the end, and do not change the size of
 * any element.  Elements of this structure are used by user
 * space, and must match struct user_fp in include/asm-arm/user.h.
 * We include the byte offsets below for documentation purposes.
 *
 * The size of this structure and FPREG are checked by fpmodule.c
 * on initialisation.  If the rules have been broken, NWFPE will
 * not initialise.
 */
typedef struct tagFPA11 {
/*   0 */  FPREG fpreg[8];		/* 8 floating point registers */
/*  96 */  FPSR fpsr;			/* floating point status register */
/* 100 */  FPCR fpcr;			/* floating point control register */
/* 104 */  unsigned char fType[8];	/* type of floating point value held in
					   floating point registers.  One of none
					   single, double or extended. */
/* 112 */  int initflag;		/* this is special.  The kernel guarantees
					   to set it to 0 when a thread is launched,
					   so we can use it to detect whether this
					   instance of the emulator needs to be
					   initialised. */
} FPA11;

extern FPA11* qemufpa;

extern void resetFPA11(void);
extern void SetRoundingMode(const unsigned int);
extern void SetRoundingPrecision(const unsigned int);

static inline unsigned int readRegister(unsigned int reg)
{
    return (user_registers[(reg)]);
}

static inline void writeRegister(unsigned int x, unsigned int y)
{
#if 0
	printf("writing %d to r%d\n",y,x);
#endif
        user_registers[(x)]=(y);
}

static inline void writeConditionCodes(unsigned int x)
{
#if 0
unsigned	int y;
unsigned    int ZF;
	printf("setting flags to %x from %x\n",x,user_registers[16]);
#endif
	user_registers[16]=(x);	// cpsr
	user_registers[17]=(x>>29)&1;	// cf
	user_registers[18]=(x<<3)&(1<<31);	// vf
	user_registers[19]=x&(1<<31);	// nzf
	if(!(x&(1<<30))) user_registers[19]++;	// nzf must be non-zero for zf to be cleared

#if 0
        ZF = (user_registers[19] == 0);
        y=user_registers[16] | (user_registers[19] & 0x80000000) | (ZF << 30) | 
                    (user_registers[17] << 29) | ((user_registers[18] & 0x80000000) >> 3);
        if(y != x)
        	printf("GODDAM SHIIIIIIIIIIIIIIIIT! %x %x nzf %x zf %x\n",x,y,user_registers[19],ZF);
#endif                    
}

#define REG_PC 15

unsigned int EmulateAll(unsigned int opcode, FPA11* qfpa, unsigned int* qregs);

/* included only for get_user/put_user macros */
#include "qemu.h"

#endif
