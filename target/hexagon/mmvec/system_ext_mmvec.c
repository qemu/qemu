/*
 *  Copyright(c) 2019-2021 Qualcomm Innovation Center, Inc. All Rights Reserved.
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

#include "qemu/osdep.h"
#include "cpu.h"
#include "mmvec/system_ext_mmvec.h"

void mem_gather_store(CPUHexagonState *env, target_ulong vaddr, int slot)
{
    size_t size = sizeof(MMVector);

    env->vstore_pending[slot] = 1;
    env->vstore[slot].va   = vaddr;
    env->vstore[slot].size = size;
    memcpy(&env->vstore[slot].data.ub[0], &env->tmp_VRegs[0], size);

    /* On a gather store, overwrite the store mask to emulate dropped gathers */
    bitmap_copy(env->vstore[slot].mask, env->vtcm_log.mask, size);
}

void mem_vector_scatter_init(CPUHexagonState *env)
{
    bitmap_zero(env->vtcm_log.mask, MAX_VEC_SIZE_BYTES);

    env->vtcm_pending = true;
    env->vtcm_log.op = false;
    env->vtcm_log.op_size = 0;
}

void mem_vector_gather_init(CPUHexagonState *env)
{
    bitmap_zero(env->vtcm_log.mask, MAX_VEC_SIZE_BYTES);
}
