/*
 *  i386 micro operations (templates for various register related
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
/* mov T1 to REG if T0 is true */
void OPPROTO glue(glue(op_cmovw,REGNAME),_T1_T0)(void)
{
    if (T0)
        REG = (REG & ~0xffff) | (T1 & 0xffff);
    FORCE_RET();
}

void OPPROTO glue(glue(op_cmovl,REGNAME),_T1_T0)(void)
{
#ifdef TARGET_X86_64
    if (T0)
        REG = (uint32_t)T1;
    else
        REG = (uint32_t)REG;
#else
    if (T0)
        REG = (uint32_t)T1;
#endif
    FORCE_RET();
}

#ifdef TARGET_X86_64
void OPPROTO glue(glue(op_cmovq,REGNAME),_T1_T0)(void)
{
    if (T0)
        REG = T1;
    FORCE_RET();
}
#endif
