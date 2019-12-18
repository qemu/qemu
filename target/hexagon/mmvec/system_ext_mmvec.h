/*
 *  Copyright (c) 2019 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef SYSTEM_EXT_MMVEC_H
#define SYSTEM_EXT_MMVEC_H

extern void mem_load_vector_oddva(CPUHexagonState *env, vaddr_t vaddr,
                           vaddr_t lookup_vaddr, int slot, int size,
                           size1u_t *data, int use_full_va);
extern void mem_store_vector_oddva(CPUHexagonState *env, vaddr_t vaddr,
                            vaddr_t lookup_vaddr, int slot, int size,
                            size1u_t *data, size1u_t* mask, unsigned invert,
                            int use_full_va);
extern void mem_vector_scatter_init(CPUHexagonState *env, int slot,
                                    vaddr_t base_vaddr, int length,
                                    int element_size);
extern void mem_vector_scatter_finish(CPUHexagonState *env, int slot, int op);
extern void mem_vector_gather_finish(CPUHexagonState *env, int slot);
extern void mem_vector_gather_init(CPUHexagonState *env, int slot,
                                   vaddr_t base_vaddr, int length,
                                   int element_size);


#endif
