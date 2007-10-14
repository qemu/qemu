/*
 *  CRIS memory access (load and store) micro operations.
 *
 *  Copyright (c) 2007 Edgar E. Iglesias, Axis Communications AB.
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

void glue(op_ldb_T0_T0, MEMSUFFIX) (void) {
    T0 = glue(ldsb, MEMSUFFIX) (T0);
    RETURN();
}

void glue(op_ldub_T0_T0, MEMSUFFIX) (void) {
    T0 = glue(ldub, MEMSUFFIX) (T0);
    RETURN();
}

void glue(op_stb_T0_T1, MEMSUFFIX) (void) {
    glue(stb, MEMSUFFIX) (T0, T1);
    RETURN();
}

void glue(op_ldw_T0_T0, MEMSUFFIX) (void) {
    T0 = glue(ldsw, MEMSUFFIX) (T0);
    RETURN();
}

void glue(op_lduw_T0_T0, MEMSUFFIX) (void) {
    T0 = glue(lduw, MEMSUFFIX) (T0);
    RETURN();
}

void glue(op_stw_T0_T1, MEMSUFFIX) (void) {
    glue(stw, MEMSUFFIX) (T0, T1);
    RETURN();
}

void glue(op_ldl_T0_T0, MEMSUFFIX) (void) {
    T0 = glue(ldl, MEMSUFFIX) (T0);
    RETURN();
}

void glue(op_stl_T0_T1, MEMSUFFIX) (void) {
    glue(stl, MEMSUFFIX) (T0, T1);
    RETURN();
}
