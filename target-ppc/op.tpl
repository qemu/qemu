/*
 *  PPC emulation micro-operations for qemu.
 * 
 *  Copyright (c) 2003 Jocelyn Mayer
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/* Host registers definitions */
$DEFH T 3
/* PPC registers definitions */
$DEF gpr 32
$DEF fpr 32
$DEF crf 8
$DEF spr 1024

/* PPC registers <-> host registers move */
/* GPR */
$OP load_gpr_T0 gpr
{
    T0 = regs->gpra;
    RETURN();
}
$ENDOP

$OP load_gpr_T1 gpr
{
    T1 = regs->gpra;
    RETURN();
}
$ENDOP

$OP load_gpr_T2 gpr
{
    T2 = regs->gpra;
    RETURN();
}
$ENDOP

$OP store_T0_gpr gpr
{
    regs->gpra = T0;
    RETURN();
}
$ENDOP

$OP store_T1_gpr gpr
{
    regs->gpra = T1;
    RETURN();
}
$ENDOP

$OP store_gpr_P gpr PARAM
{
    regs->gpra = PARAM(1);
    RETURN();
}
$ENDOP

/* crf */
$OP load_crf_T0 crf
{
    T0 = regs->crfa;
    RETURN();
}
$ENDOP

$OP load_crf_T1 crf
{
    T1 = regs->crfa;
    RETURN();
}
$ENDOP

$OP store_T0_crf crf
{
    regs->crfa = T0;
    RETURN();
}
$ENDOP

$OP store_T1_crf crf
{
    regs->crfa = T1;
    RETURN();
}
$ENDOP

/* SPR */
$OP load_spr spr
{
    T0 = regs->spra;
    RETURN();
}
$ENDOP

$OP store_spr spr
{
    regs->spra = T0;
    RETURN();
}
$ENDOP

/* FPSCR */
$OP load_fpscr fpr
{
    regs->fpra = do_load_fpscr();
    RETURN();
}
$ENDOP

$OP store_fpscr fpr PARAM
{
    do_store_fpscr(PARAM(1), regs->fpra);
    RETURN();
}
$ENDOP

/***                         Floating-point store                          ***/
/* candidate for helper (too long on x86 host) */
$OP stfd_z fpr PARAM
{
    st64(SPARAM(1), regs->fpra);
    RETURN();
}
$ENDOP

/* candidate for helper (too long on x86 host) */
$OP stfd fpr PARAM
{
    T0 += SPARAM(1);
    st64(T0, regs->fpra);
    RETURN();
}
$ENDOP

/* candidate for helper (too long on x86 host) */
$OP stfdx_z fpr
{
    st64(T0, regs->fpra);
    RETURN();
}
$ENDOP
/* candidate for helper (too long on x86 host) */
$OP stfdx fpr
{
    T0 += T1;
    st64(T0, regs->fpra);
    RETURN();
}
$ENDOP

/* candidate for helper (too long on x86 host) */
$OP lfd_z fpr PARAM
{
    regs->fpra = ld64(SPARAM(1));
    RETURN();
}
$ENDOP

/* candidate for helper (too long) */
$OP lfd fpr PARAM
{
    T0 += SPARAM(1);
    regs->fpra = ld64(T0);
    RETURN();
}
$ENDOP

$OP lfdx_z fpr
{
    regs->fpra = ld64(T0);
    RETURN();
}
$ENDOP

$OP lfdx fpr
{
    T0 += T1;
    regs->fpra = ld64(T0);
    RETURN();
}
$ENDOP
/*****************************************************************************/
