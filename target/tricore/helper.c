/*
 *  Copyright (c) 2012-2014 Bastian Koppelmann C-Lab/University Paderborn
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
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"

#include "cpu.h"
#include "exec/exec-all.h"

enum {
    TLBRET_DIRTY = -4,
    TLBRET_INVALID = -3,
    TLBRET_NOMATCH = -2,
    TLBRET_BADADDR = -1,
    TLBRET_MATCH = 0
};

#if defined(CONFIG_SOFTMMU)
static int get_physical_address(CPUTriCoreState *env, hwaddr *physical,
                                int *prot, target_ulong address,
                                int rw, int access_type)
{
    int ret = TLBRET_MATCH;

    *physical = address & 0xFFFFFFFF;
    *prot = PAGE_READ | PAGE_WRITE | PAGE_EXEC;

    return ret;
}
#endif

/* TODO: Add exeption support*/
static void raise_mmu_exception(CPUTriCoreState *env, target_ulong address,
                                int rw, int tlb_error)
{
}

int cpu_tricore_handle_mmu_fault(CPUState *cs, target_ulong address,
                                 int rw, int mmu_idx)
{
    TriCoreCPU *cpu = TRICORE_CPU(cs);
    CPUTriCoreState *env = &cpu->env;
    hwaddr physical;
    int prot;
    int access_type;
    int ret = 0;

    rw &= 1;
    access_type = ACCESS_INT;
    ret = get_physical_address(env, &physical, &prot,
                               address, rw, access_type);
    qemu_log_mask(CPU_LOG_MMU, "%s address=" TARGET_FMT_lx " ret %d physical " TARGET_FMT_plx
                  " prot %d\n", __func__, address, ret, physical, prot);

    if (ret == TLBRET_MATCH) {
        tlb_set_page(cs, address & TARGET_PAGE_MASK,
                     physical & TARGET_PAGE_MASK, prot | PAGE_EXEC,
                     mmu_idx, TARGET_PAGE_SIZE);
        ret = 0;
    } else if (ret < 0) {
        raise_mmu_exception(env, address, rw, ret);
        ret = 1;
    }

    return ret;
}

TriCoreCPU *cpu_tricore_init(const char *cpu_model)
{
    return TRICORE_CPU(cpu_generic_init(TYPE_TRICORE_CPU, cpu_model));
}

static void tricore_cpu_list_entry(gpointer data, gpointer user_data)
{
    ObjectClass *oc = data;
    CPUListState *s = user_data;
    const char *typename;
    char *name;

    typename = object_class_get_name(oc);
    name = g_strndup(typename, strlen(typename) - strlen("-" TYPE_TRICORE_CPU));
    (*s->cpu_fprintf)(s->file, "  %s\n",
                      name);
    g_free(name);
}

void tricore_cpu_list(FILE *f, fprintf_function cpu_fprintf)
{
    CPUListState s = {
        .file = f,
        .cpu_fprintf = cpu_fprintf,
    };
    GSList *list;

    list = object_class_get_list(TYPE_TRICORE_CPU, false);
    (*cpu_fprintf)(f, "Available CPUs:\n");
    g_slist_foreach(list, tricore_cpu_list_entry, &s);
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
