/*
 *  Save/restore host registrs.
 *
 *  Copyright (c) 2007 CodeSourcery
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA  02110-1301 USA
 */

/* The GCC global register variable extension is used to reserve some
   host registers for use by generated code.  However only the core parts of
   the translation engine are compiled with these settings.  We must manually
   save/restore these registers when called from regular code.
   It is not sufficient to save/restore T0 et. al. as these may be declared
   with a datatype smaller than the actual register.  */

#if defined(DECLARE_HOST_REGS)

#define DO_REG(REG)					\
    register host_reg_t reg_AREG##REG asm(AREG##REG);	\
    volatile host_reg_t saved_AREG##REG;

#elif defined(SAVE_HOST_REGS)

#define DO_REG(REG)					\
    __asm__ __volatile__ ("" : "=r" (reg_AREG##REG));	\
    saved_AREG##REG = reg_AREG##REG;

#else

#define DO_REG(REG)                                     \
    reg_AREG##REG = saved_AREG##REG;		        \
    __asm__ __volatile__ ("" : : "r" (reg_AREG##REG));

#endif

#ifdef AREG0
DO_REG(0)
#endif

#ifdef AREG1
DO_REG(1)
#endif

#ifdef AREG2
DO_REG(2)
#endif

#undef SAVE_HOST_REGS
#undef DECLARE_HOST_REGS
#undef DO_REG
