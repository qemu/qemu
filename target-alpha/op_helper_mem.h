/*
 *  Alpha emulation cpu micro-operations helpers for memory accesses for qemu.
 *
 *  Copyright (c) 2007 Jocelyn Mayer
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

/* XXX: TODO */
double glue(helper_ldff, MEMSUFFIX) (target_ulong ea)
{
    return 0;
}

void glue(helper_stff, MEMSUFFIX) (target_ulong ea, double op)
{
}

double glue(helper_ldfg, MEMSUFFIX) (target_ulong ea)
{
    return 0;
}

void glue(helper_stfg, MEMSUFFIX) (target_ulong ea, double op)
{
}

#undef MEMSUFFIX
