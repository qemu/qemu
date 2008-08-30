/*
 *  SH4 emulation
 *
 *  Copyright (c) 2005 Samuel Tardieu
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
void glue(op_ldfl_T0_FT0, MEMSUFFIX) (void) {
    FT0 = glue(ldfl, MEMSUFFIX) (T0);
    RETURN();
}

void glue(op_stfl_FT0_T1, MEMSUFFIX) (void) {
    glue(stfl, MEMSUFFIX) (T1, FT0);
    RETURN();
}

void glue(op_ldfq_T0_DT0, MEMSUFFIX) (void) {
    DT0 = glue(ldfq, MEMSUFFIX) (T0);
    RETURN();
}

void glue(op_stfq_DT0_T1, MEMSUFFIX) (void) {
    glue(stfq, MEMSUFFIX) (T1, DT0);
    RETURN();
}
