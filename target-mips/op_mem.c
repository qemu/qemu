/*
 *  MIPS emulation memory micro-operations for qemu.
 * 
 *  Copyright (c) 2004-2005 Jocelyn Mayer
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

/* Standard loads and stores */
void glue(op_lb, MEMSUFFIX) (void)
{
    T0 = glue(ldsb, MEMSUFFIX)(T0);
    RETURN();
}

void glue(op_lbu, MEMSUFFIX) (void)
{
    T0 = glue(ldub, MEMSUFFIX)(T0);
    RETURN();
}

void glue(op_sb, MEMSUFFIX) (void)
{
    glue(stb, MEMSUFFIX)(T0, T1);
    RETURN();
}

void glue(op_lh, MEMSUFFIX) (void)
{
    T0 = glue(ldsw, MEMSUFFIX)(T0);
    RETURN();
}

void glue(op_lhu, MEMSUFFIX) (void)
{
    T0 = glue(lduw, MEMSUFFIX)(T0);
    RETURN();
}

void glue(op_sh, MEMSUFFIX) (void)
{
    glue(stw, MEMSUFFIX)(T0, T1);
    RETURN();
}

void glue(op_lw, MEMSUFFIX) (void)
{
    T0 = glue(ldl, MEMSUFFIX)(T0);
    RETURN();
}

void glue(op_lwu, MEMSUFFIX) (void)
{
    T0 = glue(ldl, MEMSUFFIX)(T0);
    RETURN();
}

void glue(op_sw, MEMSUFFIX) (void)
{
    glue(stl, MEMSUFFIX)(T0, T1);
    RETURN();
}

/* "half" load and stores.  We must do the memory access inline,
   or fault handling won't work.  */
void glue(op_lwl, MEMSUFFIX) (void)
{
    uint32_t tmp = glue(ldl, MEMSUFFIX)(T0 & ~3);
    CALL_FROM_TB1(glue(do_lwl, MEMSUFFIX), tmp);
    RETURN();
}

void glue(op_lwr, MEMSUFFIX) (void)
{
    uint32_t tmp = glue(ldl, MEMSUFFIX)(T0 & ~3);
    CALL_FROM_TB1(glue(do_lwr, MEMSUFFIX), tmp);
    RETURN();
}

void glue(op_swl, MEMSUFFIX) (void)
{
    uint32_t tmp = glue(ldl, MEMSUFFIX)(T0 & ~3);
    tmp = CALL_FROM_TB1(glue(do_swl, MEMSUFFIX), tmp);
    glue(stl, MEMSUFFIX)(T0 & ~3, tmp);
    RETURN();
}

void glue(op_swr, MEMSUFFIX) (void)
{
    uint32_t tmp = glue(ldl, MEMSUFFIX)(T0 & ~3);
    tmp = CALL_FROM_TB1(glue(do_swr, MEMSUFFIX), tmp);
    glue(stl, MEMSUFFIX)(T0 & ~3, tmp);
    RETURN();
}

void glue(op_ll, MEMSUFFIX) (void)
{
    T1 = T0;
    T0 = glue(ldl, MEMSUFFIX)(T0);
    env->CP0_LLAddr = T1;
    RETURN();
}

void glue(op_sc, MEMSUFFIX) (void)
{
    CALL_FROM_TB0(dump_sc);
    if (T0 == env->CP0_LLAddr) {
        glue(stl, MEMSUFFIX)(T0, T1);
        T0 = 1;
    } else {
        T0 = 0;
    }
    RETURN();
}

#ifdef MIPS_USES_FPU
void glue(op_lwc1, MEMSUFFIX) (void)
{
    WT0 = glue(ldl, MEMSUFFIX)(T0);
    RETURN();
}
void glue(op_swc1, MEMSUFFIX) (void)
{
    glue(stl, MEMSUFFIX)(T0, WT0);
    RETURN();
}
void glue(op_ldc1, MEMSUFFIX) (void)
{
    DT0 = glue(ldq, MEMSUFFIX)(T0);
    RETURN();
}
void glue(op_sdc1, MEMSUFFIX) (void)
{
    glue(stq, MEMSUFFIX)(T0, DT0);
    RETURN();
}
#endif
