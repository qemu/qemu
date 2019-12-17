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

#include <stdio.h>
#include <string.h>
#include "qemu/osdep.h"
#include "opcodes.h"
#include "insn.h"
#include "mmvec/macros.h"
#include "qemu.h"

#define TYPE_LOAD 'L'
#define TYPE_STORE 'S'
#define TYPE_FETCH 'F'
#define TYPE_ICINVA 'I'

enum mem_access_types {
    access_type_INVALID = 0,
    access_type_unknown = 1,
    access_type_load = 2,
    access_type_store = 3,
    access_type_fetch = 4,
    access_type_dczeroa = 5,
    access_type_dccleana = 6,
    access_type_dcinva = 7,
    access_type_dccleaninva = 8,
    access_type_icinva = 9,
    access_type_ictagr = 10,
    access_type_ictagw = 11,
    access_type_icdatar = 12,
    access_type_dcfetch = 13,
    access_type_l2fetch = 14,
    access_type_l2cleanidx = 15,
    access_type_l2cleaninvidx = 16,
    access_type_l2tagr = 17,
    access_type_l2tagw = 18,
    access_type_dccleanidx = 19,
    access_type_dcinvidx = 20,
    access_type_dccleaninvidx = 21,
    access_type_dctagr = 22,
    access_type_dctagw = 23,
    access_type_k0unlock = 24,
    access_type_l2locka = 25,
    access_type_l2unlocka = 26,
    access_type_l2kill = 27,
    access_type_l2gclean = 28,
    access_type_l2gcleaninv = 29,
    access_type_l2gunlock = 30,
    access_type_synch = 31,
    access_type_isync = 32,
    access_type_pause = 33,
    access_type_load_phys = 34,
    access_type_load_locked = 35,
    access_type_store_conditional = 36,
    access_type_barrier = 37,
    access_type_memcpy_load = 39,
    access_type_memcpy_store = 40,
    access_type_hmx_load_act = 42,
    access_type_hmx_load_wei = 43,
    access_type_hmx_load_bias = 44,
    access_type_hmx_store = 45,
    access_type_hmx_store_bias = 46,
    access_type_udma_load = 47,
    access_type_udma_store = 48,

    NUM_CORE_ACCESS_TYPES
};

enum ext_mem_access_types {
    access_type_vload = NUM_CORE_ACCESS_TYPES,
    access_type_vstore,
    access_type_vload_nt,
    access_type_vstore_nt,
    access_type_vgather_load,
    access_type_vscatter_store,
    access_type_vscatter_release,
    access_type_vgather_release,
    access_type_vfetch,
    NUM_EXT_ACCESS_TYPES
};

static inline
target_ulong mem_init_access(CPUHexagonState *env, int slot, size4u_t vaddr,
                             int width, enum mem_access_types mtype,
                             int type_for_xlate)
{
#ifdef CONFIG_USER_ONLY
    /* Nothing to do for Linux user mode in qemu */
    return vaddr;
#else
#error System mode not yet implemented for Hexagon
#endif
}

static inline int check_gather_store(CPUHexagonState *env)
{
    /* First check to see if temp vreg has been updated */
    int check  = env->gather_issued;
    check &= env->is_gather_store_insn;

    /* In case we don't have store, suppress gather */
    if (!check) {
        env->gather_issued = 0;
        env->vtcm_pending = 0;   /* Suppress any gather writes to memory */
    }
    return check;
}

void mem_store_vector_oddva(CPUHexagonState *env, vaddr_t vaddr,
                            vaddr_t lookup_vaddr, int slot, int size,
                            size1u_t *data, size1u_t *mask, unsigned invert,
                            int use_full_va)
{
    int i;

    if (!use_full_va) {
        lookup_vaddr = vaddr;
    }

    if (!size) {
        return;
    }

    int is_gather_store = check_gather_store(env);
    if (is_gather_store) {
        memcpy(data, &env->tmp_VRegs[0].ub[0], size);
        env->VRegs_updated_tmp = 0;
        env->gather_issued = 0;
    }

    /*
     * If it's a gather store update store data from temporary register
     * And clear flag
     */
    env->vstore_pending[slot] = 1;
    env->vstore[slot].va   = vaddr;
    env->vstore[slot].size = size;
    memcpy(&env->vstore[slot].data.ub[0], data, size);
    if (!mask) {
        memset(&env->vstore[slot].mask.ub[0], invert ? 0 : -1, size);
    } else if (invert) {
        for (i = 0; i < size; i++) {
            env->vstore[slot].mask.ub[i] = !mask[i];
        }
    } else {
        memcpy(&env->vstore[slot].mask.ub[0], mask, size);
    }
    /* On a gather store, overwrite the store mask to emulate dropped gathers */
    if (is_gather_store) {
        memcpy(&env->vstore[slot].mask.ub[0], &env->vtcm_log.mask.ub[0], size);
    }
    for (i = 0; i < size; i++) {
        env->mem_access[slot].cdata[i] = data[i];
    }
}

void mem_load_vector_oddva(CPUHexagonState *env, vaddr_t vaddr,
                           vaddr_t lookup_vaddr, int slot, int size,
                           size1u_t *data, int use_full_va)
{
    int i;

    if (!use_full_va) {
        lookup_vaddr = vaddr;
    }

    if (!size) {
        return;
    }

    for (i = 0; i < size; i++) {
        get_user_u8(data[i], vaddr);
        vaddr++;
    }
}

void mem_vector_scatter_init(CPUHexagonState *env, int slot, vaddr_t base_vaddr,
                             int length, int element_size)
{
    enum ext_mem_access_types access_type = access_type_vscatter_store;
    int i;

    /* Translation for Store Address on Slot 1 - maybe any slot? */
    mem_init_access(env, slot, base_vaddr, 1, access_type, TYPE_STORE);
    mem_access_info_t *maptr = &env->mem_access[slot];
    if (EXCEPTION_DETECTED) {
        return;
    }

    maptr->range = length;

    for (i = 0; i < fVECSIZE(); i++) {
        env->vtcm_log.offsets.ub[i] = 0; /* Mark invalid */
        env->vtcm_log.data.ub[i] = 0;
        env->vtcm_log.mask.ub[i] = 0;
    }
    env->vtcm_log.va_base = base_vaddr;

    env->vtcm_pending = 1;
    env->vtcm_log.oob_access = 0;
    env->vtcm_log.op = 0;
    env->vtcm_log.op_size = 0;
    return;
}

void mem_vector_gather_init(CPUHexagonState *env, int slot, vaddr_t base_vaddr,
                            int length, int element_size)
{
    enum ext_mem_access_types access_type = access_type_vgather_load;
    int i;

    mem_init_access(env, slot, base_vaddr, 1,  access_type, TYPE_LOAD);
    mem_access_info_t *maptr = &env->mem_access[slot];

    if (EXCEPTION_DETECTED) {
        return;
    }

    maptr->range = length;

    for (i = 0; i < 2 * fVECSIZE(); i++) {
        env->vtcm_log.offsets.ub[i] = 0x0;
    }
    for (i = 0; i < fVECSIZE(); i++) {
        env->vtcm_log.data.ub[i] = 0;
        env->vtcm_log.mask.ub[i] = 0;
        env->vtcm_log.va[i] = 0;
        env->tmp_VRegs[0].ub[i] = 0;
    }
    env->vtcm_log.oob_access = 0;
    env->vtcm_log.op = 0;
    env->vtcm_log.op_size = 0;

    env->vtcm_log.va_base = base_vaddr;

    /*
     * Temp Reg gets updated
     * This allows Store .new to grab the correct result
     */
    env->VRegs_updated_tmp = 1;
    env->gather_issued = 1;

    return;
}

void mem_vector_scatter_finish(CPUHexagonState *env, int slot, int op)
{
    env->store_pending[slot] = 0;
    env->vstore_pending[slot] = 0;
    env->vtcm_log.size = fVECSIZE();

    memcpy(env->mem_access[slot].cdata, &env->vtcm_log.offsets.ub[0], 256);
}

void mem_vector_gather_finish(CPUHexagonState *env, int slot)
{
    memcpy(env->mem_access[slot].cdata, &env->vtcm_log.offsets.ub[0], 256);
}
