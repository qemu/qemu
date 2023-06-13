/*
 *  Copyright (c) 2012-2014 Bastian Koppelmann C-Lab/University Paderborn
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

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/registerfields.h"
#include "cpu.h"
#include "exec/exec-all.h"
#include "fpu/softfloat-helpers.h"
#include "qemu/qemu-print.h"

enum {
    TLBRET_DIRTY = -4,
    TLBRET_INVALID = -3,
    TLBRET_NOMATCH = -2,
    TLBRET_BADADDR = -1,
    TLBRET_MATCH = 0
};

static int get_physical_address(CPUTriCoreState *env, hwaddr *physical,
                                int *prot, target_ulong address,
                                MMUAccessType access_type, int mmu_idx)
{
    int ret = TLBRET_MATCH;

    *physical = address & 0xFFFFFFFF;
    *prot = PAGE_READ | PAGE_WRITE | PAGE_EXEC;

    return ret;
}

hwaddr tricore_cpu_get_phys_page_debug(CPUState *cs, vaddr addr)
{
    TriCoreCPU *cpu = TRICORE_CPU(cs);
    hwaddr phys_addr;
    int prot;
    int mmu_idx = cpu_mmu_index(&cpu->env, false);

    if (get_physical_address(&cpu->env, &phys_addr, &prot, addr,
                             MMU_DATA_LOAD, mmu_idx)) {
        return -1;
    }
    return phys_addr;
}

/* TODO: Add exeption support*/
static void raise_mmu_exception(CPUTriCoreState *env, target_ulong address,
                                int rw, int tlb_error)
{
}

bool tricore_cpu_tlb_fill(CPUState *cs, vaddr address, int size,
                          MMUAccessType rw, int mmu_idx,
                          bool probe, uintptr_t retaddr)
{
    TriCoreCPU *cpu = TRICORE_CPU(cs);
    CPUTriCoreState *env = &cpu->env;
    hwaddr physical;
    int prot;
    int ret = 0;

    rw &= 1;
    ret = get_physical_address(env, &physical, &prot,
                               address, rw, mmu_idx);

    qemu_log_mask(CPU_LOG_MMU, "%s address=" TARGET_FMT_lx " ret %d physical "
                  HWADDR_FMT_plx " prot %d\n",
                  __func__, (target_ulong)address, ret, physical, prot);

    if (ret == TLBRET_MATCH) {
        tlb_set_page(cs, address & TARGET_PAGE_MASK,
                     physical & TARGET_PAGE_MASK, prot | PAGE_EXEC,
                     mmu_idx, TARGET_PAGE_SIZE);
        return true;
    } else {
        assert(ret < 0);
        if (probe) {
            return false;
        }
        raise_mmu_exception(env, address, rw, ret);
        cpu_loop_exit_restore(cs, retaddr);
    }
}

static void tricore_cpu_list_entry(gpointer data, gpointer user_data)
{
    ObjectClass *oc = data;
    const char *typename;
    char *name;

    typename = object_class_get_name(oc);
    name = g_strndup(typename, strlen(typename) - strlen("-" TYPE_TRICORE_CPU));
    qemu_printf("  %s\n", name);
    g_free(name);
}

void tricore_cpu_list(void)
{
    GSList *list;

    list = object_class_get_list_sorted(TYPE_TRICORE_CPU, false);
    qemu_printf("Available CPUs:\n");
    g_slist_foreach(list, tricore_cpu_list_entry, NULL);
    g_slist_free(list);
}

void fpu_set_state(CPUTriCoreState *env)
{
    set_float_rounding_mode(env->PSW & MASK_PSW_FPU_RM, &env->fp_status);
    set_flush_inputs_to_zero(1, &env->fp_status);
    set_flush_to_zero(1, &env->fp_status);
    set_default_nan_mode(1, &env->fp_status);
}

uint32_t psw_read(CPUTriCoreState *env)
{
    /* clear all USB bits */
    env->PSW &= 0x6ffffff;
    /* now set them from the cache */
    env->PSW |= ((env->PSW_USB_C != 0) << 31);
    env->PSW |= ((env->PSW_USB_V   & (1 << 31))  >> 1);
    env->PSW |= ((env->PSW_USB_SV  & (1 << 31))  >> 2);
    env->PSW |= ((env->PSW_USB_AV  & (1 << 31))  >> 3);
    env->PSW |= ((env->PSW_USB_SAV & (1 << 31))  >> 4);

    return env->PSW;
}

void psw_write(CPUTriCoreState *env, uint32_t val)
{
    env->PSW_USB_C = (val & MASK_USB_C);
    env->PSW_USB_V = (val & MASK_USB_V) << 1;
    env->PSW_USB_SV = (val & MASK_USB_SV) << 2;
    env->PSW_USB_AV = (val & MASK_USB_AV) << 3;
    env->PSW_USB_SAV = (val & MASK_USB_SAV) << 4;
    env->PSW = val;

    fpu_set_state(env);
}

#define FIELD_GETTER_WITH_FEATURE(NAME, REG, FIELD, FEATURE)     \
uint32_t NAME(CPUTriCoreState *env)                             \
{                                                                \
    if (tricore_feature(env, TRICORE_FEATURE_##FEATURE)) {       \
        return FIELD_EX32(env->REG, REG, FIELD ## _ ## FEATURE); \
    }                                                            \
    return FIELD_EX32(env->REG, REG, FIELD ## _13);              \
}

#define FIELD_GETTER(NAME, REG, FIELD)       \
uint32_t NAME(CPUTriCoreState *env)         \
{                                            \
    return FIELD_EX32(env->REG, REG, FIELD); \
}

#define FIELD_SETTER_WITH_FEATURE(NAME, REG, FIELD, FEATURE)              \
void NAME(CPUTriCoreState *env, uint32_t val)                            \
{                                                                         \
    if (tricore_feature(env, TRICORE_FEATURE_##FEATURE)) {                \
        env->REG = FIELD_DP32(env->REG, REG, FIELD ## _ ## FEATURE, val); \
    }                                                                     \
    env->REG = FIELD_DP32(env->REG, REG, FIELD ## _13, val);              \
}

#define FIELD_SETTER(NAME, REG, FIELD)                \
void NAME(CPUTriCoreState *env, uint32_t val)        \
{                                                     \
    env->REG = FIELD_DP32(env->REG, REG, FIELD, val); \
}

FIELD_GETTER_WITH_FEATURE(pcxi_get_pcpn, PCXI, PCPN, 161)
FIELD_SETTER_WITH_FEATURE(pcxi_set_pcpn, PCXI, PCPN, 161)
FIELD_GETTER_WITH_FEATURE(pcxi_get_pie, PCXI, PIE, 161)
FIELD_SETTER_WITH_FEATURE(pcxi_set_pie, PCXI, PIE, 161)
FIELD_GETTER_WITH_FEATURE(pcxi_get_ul, PCXI, UL, 161)
FIELD_SETTER_WITH_FEATURE(pcxi_set_ul, PCXI, UL, 161)
FIELD_GETTER(pcxi_get_pcxs, PCXI, PCXS)
FIELD_GETTER(pcxi_get_pcxo, PCXI, PCXO)

FIELD_GETTER_WITH_FEATURE(icr_get_ie, ICR, IE, 161)
FIELD_SETTER_WITH_FEATURE(icr_set_ie, ICR, IE, 161)
FIELD_GETTER(icr_get_ccpn, ICR, CCPN)
FIELD_SETTER(icr_set_ccpn, ICR, CCPN)
