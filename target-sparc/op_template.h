/*
 *  SPARC micro operations (templates for various register related
 *  operations)
 *
 *  Copyright (c) 2003 Fabrice Bellard
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

void OPPROTO glue(op_movl_T0_, REGNAME)(void)
{
    T0 = REG;
}

void OPPROTO glue(op_movl_T1_, REGNAME)(void)
{
    T1 = REG;
}

void OPPROTO glue(op_movl_T2_, REGNAME)(void)
{
    T2 = REG;
}

void OPPROTO glue(glue(op_movl_, REGNAME), _T0)(void)
{
    REG = T0;
}

void OPPROTO glue(glue(op_movl_, REGNAME), _T1)(void)
{
    REG = T1;
}

#undef REG
#undef REGNAME
