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
    along with this program; if not, see <http://www.gnu.org/licenses/>.
*/

#ifndef __FPA11_H__
#define __FPA11_H__

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

#include <cpu.h>

#define GET_FPA11() (qemufpa)

/*
 * The processes registers are always at the very top of the 8K
 * stack+task struct.  Use the same method as 'current' uses to
 * reach them.
 */
extern CPUARMState *user_registers;

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
    float_status fp_status;      /* QEMU float emulator status */
} FPA11;

extern FPA11* qemufpa;

void resetFPA11(void);
void SetRoundingMode(const unsigned int);
void SetRoundingPrecision(const unsigned int);

static inline unsigned int readRegister(unsigned int reg)
{
    return (user_registers->regs[(reg)]);
}

static inline void writeRegister(unsigned int x, unsigned int y)
{
#if 0
	printf("writing %d to r%d\n",y,x);
#endif
        user_registers->regs[(x)]=(y);
}

static inline void writeConditionCodes(unsigned int x)
{
        cpsr_write(user_registers,x,CPSR_NZCV);
}

#define ARM_REG_PC 15

unsigned int EmulateAll(unsigned int opcode, FPA11* qfpa, CPUARMState* qregs);

unsigned int EmulateCPDO(const unsigned int);
unsigned int EmulateCPDT(const unsigned int);
unsigned int EmulateCPRT(const unsigned int);

unsigned int SingleCPDO(const unsigned int opcode);
unsigned int DoubleCPDO(const unsigned int opcode);
unsigned int ExtendedCPDO(const unsigned int opcode);


/* included only for get_user/put_user macros */
#include "qemu.h"

#endif
