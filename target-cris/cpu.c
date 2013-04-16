/*
 * QEMU CRIS CPU
 *
 * Copyright (c) 2008 AXIS Communications AB
 * Written by Edgar E. Iglesias.
 *
 * Copyright (c) 2012 SUSE LINUX Products GmbH
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
 * License along with this library; if not, see
 * <http://www.gnu.org/licenses/lgpl-2.1.html>
 */

#include "cpu.h"
#include "qemu-common.h"
#include "mmu.h"


/* CPUClass::reset() */
static void cris_cpu_reset(CPUState *s)
{
    CRISCPU *cpu = CRIS_CPU(s);
    CRISCPUClass *ccc = CRIS_CPU_GET_CLASS(cpu);
    CPUCRISState *env = &cpu->env;
    uint32_t vr;

    if (qemu_loglevel_mask(CPU_LOG_RESET)) {
        qemu_log("CPU Reset (CPU %d)\n", s->cpu_index);
        log_cpu_state(env, 0);
    }

    ccc->parent_reset(s);

    vr = env->pregs[PR_VR];
    memset(env, 0, offsetof(CPUCRISState, breakpoints));
    env->pregs[PR_VR] = vr;
    tlb_flush(env, 1);

#if defined(CONFIG_USER_ONLY)
    /* start in user mode with interrupts enabled.  */
    env->pregs[PR_CCS] |= U_FLAG | I_FLAG | P_FLAG;
#else
    cris_mmu_init(env);
    env->pregs[PR_CCS] = 0;
#endif
}

static ObjectClass *cris_cpu_class_by_name(const char *cpu_model)
{
    ObjectClass *oc;
    char *typename;

    if (cpu_model == NULL) {
        return NULL;
    }

    typename = g_strdup_printf("%s-" TYPE_CRIS_CPU, cpu_model);
    oc = object_class_by_name(typename);
    g_free(typename);
    if (oc != NULL && (!object_class_dynamic_cast(oc, TYPE_CRIS_CPU) ||
                       object_class_is_abstract(oc))) {
        oc = NULL;
    }
    return oc;
}

CRISCPU *cpu_cris_init(const char *cpu_model)
{
    CRISCPU *cpu;
    ObjectClass *oc;

    oc = cris_cpu_class_by_name(cpu_model);
    if (oc == NULL) {
        return NULL;
    }
    cpu = CRIS_CPU(object_new(object_class_get_name(oc)));

    object_property_set_bool(OBJECT(cpu), true, "realized", NULL);

    return cpu;
}

/* Sort alphabetically by VR. */
static gint cris_cpu_list_compare(gconstpointer a, gconstpointer b)
{
    CRISCPUClass *ccc_a = CRIS_CPU_CLASS(a);
    CRISCPUClass *ccc_b = CRIS_CPU_CLASS(b);

    /*  */
    if (ccc_a->vr > ccc_b->vr) {
        return 1;
    } else if (ccc_a->vr < ccc_b->vr) {
        return -1;
    } else {
        return 0;
    }
}

static void cris_cpu_list_entry(gpointer data, gpointer user_data)
{
    ObjectClass *oc = data;
    CPUListState *s = user_data;
    const char *typename = object_class_get_name(oc);
    char *name;

    name = g_strndup(typename, strlen(typename) - strlen("-" TYPE_CRIS_CPU));
    (*s->cpu_fprintf)(s->file, "  %s\n", name);
    g_free(name);
}

void cris_cpu_list(FILE *f, fprintf_function cpu_fprintf)
{
    CPUListState s = {
        .file = f,
        .cpu_fprintf = cpu_fprintf,
    };
    GSList *list;

    list = object_class_get_list(TYPE_CRIS_CPU, false);
    list = g_slist_sort(list, cris_cpu_list_compare);
    (*cpu_fprintf)(f, "Available CPUs:\n");
    g_slist_foreach(list, cris_cpu_list_entry, &s);
    g_slist_free(list);
}

static void cris_cpu_realizefn(DeviceState *dev, Error **errp)
{
    CRISCPU *cpu = CRIS_CPU(dev);
    CRISCPUClass *ccc = CRIS_CPU_GET_CLASS(dev);

    cpu_reset(CPU(cpu));
    qemu_init_vcpu(&cpu->env);

    ccc->parent_realize(dev, errp);
}

static void cris_cpu_initfn(Object *obj)
{
    CPUState *cs = CPU(obj);
    CRISCPU *cpu = CRIS_CPU(obj);
    CRISCPUClass *ccc = CRIS_CPU_GET_CLASS(obj);
    CPUCRISState *env = &cpu->env;
    static bool tcg_initialized;

    cs->env_ptr = env;
    cpu_exec_init(env);

    env->pregs[PR_VR] = ccc->vr;

    if (tcg_enabled() && !tcg_initialized) {
        tcg_initialized = true;
        if (env->pregs[PR_VR] < 32) {
            cris_initialize_crisv10_tcg();
        } else {
            cris_initialize_tcg();
        }
    }
}

static void crisv8_cpu_class_init(ObjectClass *oc, void *data)
{
    CPUClass *cc = CPU_CLASS(oc);
    CRISCPUClass *ccc = CRIS_CPU_CLASS(oc);

    ccc->vr = 8;
    cc->do_interrupt = crisv10_cpu_do_interrupt;
}

static void crisv9_cpu_class_init(ObjectClass *oc, void *data)
{
    CPUClass *cc = CPU_CLASS(oc);
    CRISCPUClass *ccc = CRIS_CPU_CLASS(oc);

    ccc->vr = 9;
    cc->do_interrupt = crisv10_cpu_do_interrupt;
}

static void crisv10_cpu_class_init(ObjectClass *oc, void *data)
{
    CPUClass *cc = CPU_CLASS(oc);
    CRISCPUClass *ccc = CRIS_CPU_CLASS(oc);

    ccc->vr = 10;
    cc->do_interrupt = crisv10_cpu_do_interrupt;
}

static void crisv11_cpu_class_init(ObjectClass *oc, void *data)
{
    CPUClass *cc = CPU_CLASS(oc);
    CRISCPUClass *ccc = CRIS_CPU_CLASS(oc);

    ccc->vr = 11;
    cc->do_interrupt = crisv10_cpu_do_interrupt;
}

static void crisv32_cpu_class_init(ObjectClass *oc, void *data)
{
    CRISCPUClass *ccc = CRIS_CPU_CLASS(oc);

    ccc->vr = 32;
}

#define TYPE(model) model "-" TYPE_CRIS_CPU

static const TypeInfo cris_cpu_model_type_infos[] = {
    {
        .name = TYPE("crisv8"),
        .parent = TYPE_CRIS_CPU,
        .class_init = crisv8_cpu_class_init,
    }, {
        .name = TYPE("crisv9"),
        .parent = TYPE_CRIS_CPU,
        .class_init = crisv9_cpu_class_init,
    }, {
        .name = TYPE("crisv10"),
        .parent = TYPE_CRIS_CPU,
        .class_init = crisv10_cpu_class_init,
    }, {
        .name = TYPE("crisv11"),
        .parent = TYPE_CRIS_CPU,
        .class_init = crisv11_cpu_class_init,
    }, {
        .name = TYPE("crisv32"),
        .parent = TYPE_CRIS_CPU,
        .class_init = crisv32_cpu_class_init,
    }
};

#undef TYPE

static void cris_cpu_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    CPUClass *cc = CPU_CLASS(oc);
    CRISCPUClass *ccc = CRIS_CPU_CLASS(oc);

    ccc->parent_realize = dc->realize;
    dc->realize = cris_cpu_realizefn;

    ccc->parent_reset = cc->reset;
    cc->reset = cris_cpu_reset;

    cc->class_by_name = cris_cpu_class_by_name;
    cc->do_interrupt = cris_cpu_do_interrupt;
}

static const TypeInfo cris_cpu_type_info = {
    .name = TYPE_CRIS_CPU,
    .parent = TYPE_CPU,
    .instance_size = sizeof(CRISCPU),
    .instance_init = cris_cpu_initfn,
    .abstract = true,
    .class_size = sizeof(CRISCPUClass),
    .class_init = cris_cpu_class_init,
};

static void cris_cpu_register_types(void)
{
    int i;

    type_register_static(&cris_cpu_type_info);
    for (i = 0; i < ARRAY_SIZE(cris_cpu_model_type_infos); i++) {
        type_register_static(&cris_cpu_model_type_infos[i]);
    }
}

type_init(cris_cpu_register_types)
