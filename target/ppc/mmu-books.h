/*
 *  PowerPC BookS emulation generic mmu definitions for qemu.
 *
 *  Copyright (c) 2021 Instituto de Pesquisas Eldorado (eldorado.org.br)
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef PPC_MMU_BOOKS_H
#define PPC_MMU_BOOKS_H

/*
 * These correspond to the mmu_idx values computed in
 * hreg_compute_hflags_value. See the tables therein
 */
static inline bool mmuidx_pr(int idx) { return !(idx & 1); }
static inline bool mmuidx_real(int idx) { return idx & 2; }
static inline bool mmuidx_hv(int idx) { return idx & 4; }
#endif /* PPC_MMU_BOOKS_H */
