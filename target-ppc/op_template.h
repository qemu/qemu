/*
 *  PowerPC emulation micro-operations for qemu.
 *
 *  Copyright (c) 2003-2007 Jocelyn Mayer
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

/* Condition register moves */
void OPPROTO glue(op_load_crf_T0_crf, REG) (void)
{
    T0 = env->crf[REG];
    RETURN();
}

void OPPROTO glue(op_load_crf_T1_crf, REG) (void)
{
    T1 = env->crf[REG];
    RETURN();
}

void OPPROTO glue(op_store_T0_crf_crf, REG) (void)
{
    env->crf[REG] = T0;
    RETURN();
}

#undef REG
