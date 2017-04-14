/*
 * CPU models for s390x
 *
 * Copyright 2016 IBM Corp.
 *
 * Author(s): David Hildenbrand <dahi@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "gen-features.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "qemu/error-report.h"
#include "qapi/qmp/qerror.h"
#include "qapi/qobject-input-visitor.h"
#include "qapi/qmp/qbool.h"
#ifndef CONFIG_USER_ONLY
#include "sysemu/arch_init.h"
#endif

#define CPUDEF_INIT(_type, _gen, _ec_ga, _mha_pow, _hmfai, _name, _desc) \
    {                                                                    \
        .name = _name,                                                   \
        .type = _type,                                                   \
        .gen = _gen,                                                     \
        .ec_ga = _ec_ga,                                                 \
        .mha_pow = _mha_pow,                                             \
        .hmfai = _hmfai,                                                 \
        .desc = _desc,                                                   \
        .base_init = { S390_FEAT_LIST_GEN ## _gen ## _GA ## _ec_ga ## _BASE },  \
        .default_init = { S390_FEAT_LIST_GEN ## _gen ## _GA ## _ec_ga ## _DEFAULT },  \
        .full_init = { S390_FEAT_LIST_GEN ## _gen ## _GA ## _ec_ga ## _FULL },  \
    }

/*
 * CPU definiton list in order of release. For now, base features of a
 * following release are always a subset of base features of the previous
 * release. Same is correct for the other feature sets.
 * A BC release always follows the corresponding EC release.
 */
static S390CPUDef s390_cpu_defs[] = {
    CPUDEF_INIT(0x2064, 7, 1, 38, 0x00000000U, "z900", "IBM zSeries 900 GA1"),
    CPUDEF_INIT(0x2064, 7, 2, 38, 0x00000000U, "z900.2", "IBM zSeries 900 GA2"),
    CPUDEF_INIT(0x2064, 7, 3, 38, 0x00000000U, "z900.3", "IBM zSeries 900 GA3"),
    CPUDEF_INIT(0x2066, 7, 3, 38, 0x00000000U, "z800", "IBM zSeries 800 GA1"),
    CPUDEF_INIT(0x2084, 8, 1, 38, 0x00000000U, "z990", "IBM zSeries 990 GA1"),
    CPUDEF_INIT(0x2084, 8, 2, 38, 0x00000000U, "z990.2", "IBM zSeries 990 GA2"),
    CPUDEF_INIT(0x2084, 8, 3, 38, 0x00000000U, "z990.3", "IBM zSeries 990 GA3"),
    CPUDEF_INIT(0x2086, 8, 3, 38, 0x00000000U, "z890", "IBM zSeries 880 GA1"),
    CPUDEF_INIT(0x2084, 8, 4, 38, 0x00000000U, "z990.4", "IBM zSeries 990 GA4"),
    CPUDEF_INIT(0x2086, 8, 4, 38, 0x00000000U, "z890.2", "IBM zSeries 880 GA2"),
    CPUDEF_INIT(0x2084, 8, 5, 38, 0x00000000U, "z990.5", "IBM zSeries 990 GA5"),
    CPUDEF_INIT(0x2086, 8, 5, 38, 0x00000000U, "z890.3", "IBM zSeries 880 GA3"),
    CPUDEF_INIT(0x2094, 9, 1, 40, 0x00000000U, "z9EC", "IBM System z9 EC GA1"),
    CPUDEF_INIT(0x2094, 9, 2, 40, 0x00000000U, "z9EC.2", "IBM System z9 EC GA2"),
    CPUDEF_INIT(0x2096, 9, 2, 40, 0x00000000U, "z9BC", "IBM System z9 BC GA1"),
    CPUDEF_INIT(0x2094, 9, 3, 40, 0x00000000U, "z9EC.3", "IBM System z9 EC GA3"),
    CPUDEF_INIT(0x2096, 9, 3, 40, 0x00000000U, "z9BC.2", "IBM System z9 BC GA2"),
    CPUDEF_INIT(0x2097, 10, 1, 43, 0x00000000U, "z10EC", "IBM System z10 EC GA1"),
    CPUDEF_INIT(0x2097, 10, 2, 43, 0x00000000U, "z10EC.2", "IBM System z10 EC GA2"),
    CPUDEF_INIT(0x2098, 10, 2, 43, 0x00000000U, "z10BC", "IBM System z10 BC GA1"),
    CPUDEF_INIT(0x2097, 10, 3, 43, 0x00000000U, "z10EC.3", "IBM System z10 EC GA3"),
    CPUDEF_INIT(0x2098, 10, 3, 43, 0x00000000U, "z10BC.2", "IBM System z10 BC GA2"),
    CPUDEF_INIT(0x2817, 11, 1, 44, 0x08000000U, "z196", "IBM zEnterprise 196 GA1"),
    CPUDEF_INIT(0x2817, 11, 2, 44, 0x08000000U, "z196.2", "IBM zEnterprise 196 GA2"),
    CPUDEF_INIT(0x2818, 11, 2, 44, 0x08000000U, "z114", "IBM zEnterprise 114 GA1"),
    CPUDEF_INIT(0x2827, 12, 1, 44, 0x08000000U, "zEC12", "IBM zEnterprise EC12 GA1"),
    CPUDEF_INIT(0x2827, 12, 2, 44, 0x08000000U, "zEC12.2", "IBM zEnterprise EC12 GA2"),
    CPUDEF_INIT(0x2828, 12, 2, 44, 0x08000000U, "zBC12", "IBM zEnterprise BC12 GA1"),
    CPUDEF_INIT(0x2964, 13, 1, 47, 0x08000000U, "z13", "IBM z13 GA1"),
    CPUDEF_INIT(0x2964, 13, 2, 47, 0x08000000U, "z13.2", "IBM z13 GA2"),
    CPUDEF_INIT(0x2965, 13, 2, 47, 0x08000000U, "z13s", "IBM z13s GA1"),
};

uint32_t s390_get_hmfai(void)
{
    static S390CPU *cpu;

    if (!cpu) {
        cpu = S390_CPU(qemu_get_cpu(0));
    }

    if (!cpu || !cpu->model) {
        return 0;
    }
    return cpu->model->def->hmfai;
}

uint8_t s390_get_mha_pow(void)
{
    static S390CPU *cpu;

    if (!cpu) {
        cpu = S390_CPU(qemu_get_cpu(0));
    }

    if (!cpu || !cpu->model) {
        return 0;
    }
    return cpu->model->def->mha_pow;
}

uint32_t s390_get_ibc_val(void)
{
    uint16_t unblocked_ibc, lowest_ibc;
    static S390CPU *cpu;

    if (!cpu) {
        cpu = S390_CPU(qemu_get_cpu(0));
    }

    if (!cpu || !cpu->model) {
        return 0;
    }
    unblocked_ibc = s390_ibc_from_cpu_model(cpu->model);
    lowest_ibc = cpu->model->lowest_ibc;
    /* the lowest_ibc always has to be <= unblocked_ibc */
    if (!lowest_ibc || lowest_ibc > unblocked_ibc) {
        return 0;
    }
    return ((uint32_t) lowest_ibc << 16) | unblocked_ibc;
}

void s390_get_feat_block(S390FeatType type, uint8_t *data)
{
    static S390CPU *cpu;

    if (!cpu) {
        cpu = S390_CPU(qemu_get_cpu(0));
    }

    if (!cpu || !cpu->model) {
        return;
    }
    s390_fill_feat_block(cpu->model->features, type, data);
}

bool s390_has_feat(S390Feat feat)
{
    static S390CPU *cpu;

    if (!cpu) {
        cpu = S390_CPU(qemu_get_cpu(0));
    }

    if (!cpu || !cpu->model) {
#ifdef CONFIG_KVM
        if (kvm_enabled()) {
            if (feat == S390_FEAT_VECTOR) {
                return kvm_check_extension(kvm_state,
                                           KVM_CAP_S390_VECTOR_REGISTERS);
            }
            if (feat == S390_FEAT_RUNTIME_INSTRUMENTATION) {
                return kvm_s390_get_ri();
            }
            if (feat == S390_FEAT_MSA_EXT_3) {
                return true;
            }
        }
#endif
        return 0;
    }
    return test_bit(feat, cpu->model->features);
}

uint8_t s390_get_gen_for_cpu_type(uint16_t type)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(s390_cpu_defs); i++) {
        if (s390_cpu_defs[i].type == type) {
            return s390_cpu_defs[i].gen;
        }
    }
    return 0;
}

const S390CPUDef *s390_find_cpu_def(uint16_t type, uint8_t gen, uint8_t ec_ga,
                                    S390FeatBitmap features)
{
    const S390CPUDef *last_compatible = NULL;
    int i;

    if (!gen) {
        ec_ga = 0;
    }
    if (!gen && type) {
        gen = s390_get_gen_for_cpu_type(type);
    }

    for (i = 0; i < ARRAY_SIZE(s390_cpu_defs); i++) {
        const S390CPUDef *def = &s390_cpu_defs[i];
        S390FeatBitmap missing;

        /* don't even try newer generations if we know the generation */
        if (gen) {
            if (def->gen > gen) {
                break;
            } else if (def->gen == gen && ec_ga && def->ec_ga > ec_ga) {
                break;
            }
        }

        if (features) {
            /* see if the model satisfies the minimum features */
            bitmap_andnot(missing, def->base_feat, features, S390_FEAT_MAX);
            if (!bitmap_empty(missing, S390_FEAT_MAX)) {
                break;
            }
        }

        /* stop the search if we found the exact model */
        if (def->type == type && def->ec_ga == ec_ga) {
            return def;
        }
        last_compatible = def;
    }
    return last_compatible;
}

struct S390PrintCpuListInfo {
    FILE *f;
    fprintf_function print;
};

static void print_cpu_model_list(ObjectClass *klass, void *opaque)
{
    struct S390PrintCpuListInfo *info = opaque;
    S390CPUClass *scc = S390_CPU_CLASS(klass);
    char *name = g_strdup(object_class_get_name(klass));
    const char *details = "";

    if (scc->is_static) {
        details = "(static, migration-safe)";
    } else if (scc->is_migration_safe) {
        details = "(migration-safe)";
    }

    /* strip off the -s390-cpu */
    g_strrstr(name, "-" TYPE_S390_CPU)[0] = 0;
    (*info->print)(info->f, "s390 %-15s %-35s %s\n", name, scc->desc,
                   details);
    g_free(name);
}

void s390_cpu_list(FILE *f, fprintf_function print)
{
    struct S390PrintCpuListInfo info = {
        .f = f,
        .print = print,
    };
    S390FeatGroup group;
    S390Feat feat;

    object_class_foreach(print_cpu_model_list, TYPE_S390_CPU, false, &info);

    (*print)(f, "\nRecognized feature flags:\n");
    for (feat = 0; feat < S390_FEAT_MAX; feat++) {
        const S390FeatDef *def = s390_feat_def(feat);

        (*print)(f, "%-20s %-50s\n", def->name, def->desc);
    }

    (*print)(f, "\nRecognized feature groups:\n");
    for (group = 0; group < S390_FEAT_GROUP_MAX; group++) {
        const S390FeatGroupDef *def = s390_feat_group_def(group);

        (*print)(f, "%-20s %-50s\n", def->name, def->desc);
    }
}

#ifndef CONFIG_USER_ONLY
static void create_cpu_model_list(ObjectClass *klass, void *opaque)
{
    CpuDefinitionInfoList **cpu_list = opaque;
    CpuDefinitionInfoList *entry;
    CpuDefinitionInfo *info;
    char *name = g_strdup(object_class_get_name(klass));
    S390CPUClass *scc = S390_CPU_CLASS(klass);

    /* strip off the -s390-cpu */
    g_strrstr(name, "-" TYPE_S390_CPU)[0] = 0;
    info = g_malloc0(sizeof(*info));
    info->name = name;
    info->has_migration_safe = true;
    info->migration_safe = scc->is_migration_safe;
    info->q_static = scc->is_static;
    info->q_typename = g_strdup(object_class_get_name(klass));


    entry = g_malloc0(sizeof(*entry));
    entry->value = info;
    entry->next = *cpu_list;
    *cpu_list = entry;
}

CpuDefinitionInfoList *arch_query_cpu_definitions(Error **errp)
{
    CpuDefinitionInfoList *list = NULL;

    object_class_foreach(create_cpu_model_list, TYPE_S390_CPU, false, &list);

    return list;
}

static void cpu_model_from_info(S390CPUModel *model, const CpuModelInfo *info,
                                Error **errp)
{
    const QDict *qdict = NULL;
    const QDictEntry *e;
    Visitor *visitor;
    ObjectClass *oc;
    S390CPU *cpu;
    Object *obj;

    if (info->props) {
        qdict = qobject_to_qdict(info->props);
        if (!qdict) {
            error_setg(errp, QERR_INVALID_PARAMETER_TYPE, "props", "dict");
            return;
        }
    }

    oc = cpu_class_by_name(TYPE_S390_CPU, info->name);
    if (!oc) {
        error_setg(errp, "The CPU definition \'%s\' is unknown.", info->name);
        return;
    }
    if (S390_CPU_CLASS(oc)->kvm_required && !kvm_enabled()) {
        error_setg(errp, "The CPU definition '%s' requires KVM", info->name);
        return;
    }
    obj = object_new(object_class_get_name(oc));
    cpu = S390_CPU(obj);

    if (!cpu->model) {
        error_setg(errp, "Details about the host CPU model are not available, "
                         "it cannot be used.");
        object_unref(obj);
        return;
    }

    if (qdict) {
        visitor = qobject_input_visitor_new(info->props);
        visit_start_struct(visitor, NULL, NULL, 0, errp);
        if (*errp) {
            object_unref(obj);
            return;
        }
        for (e = qdict_first(qdict); e; e = qdict_next(qdict, e)) {
            object_property_set(obj, visitor, e->key, errp);
            if (*errp) {
                break;
            }
        }
        if (!*errp) {
            visit_check_struct(visitor, errp);
        }
        visit_end_struct(visitor, NULL);
        visit_free(visitor);
        if (*errp) {
            object_unref(obj);
            return;
        }
    }

    /* copy the model and throw the cpu away */
    memcpy(model, cpu->model, sizeof(*model));
    object_unref(obj);
}

static void qdict_add_disabled_feat(const char *name, void *opaque)
{
    qdict_put((QDict *) opaque, name, qbool_from_bool(false));
}

static void qdict_add_enabled_feat(const char *name, void *opaque)
{
    qdict_put((QDict *) opaque, name, qbool_from_bool(true));
}

/* convert S390CPUDef into a static CpuModelInfo */
static void cpu_info_from_model(CpuModelInfo *info, const S390CPUModel *model,
                                bool delta_changes)
{
    QDict *qdict = qdict_new();
    S390FeatBitmap bitmap;

    /* always fallback to the static base model */
    info->name = g_strdup_printf("%s-base", model->def->name);

    if (delta_changes) {
        /* features deleted from the base feature set */
        bitmap_andnot(bitmap, model->def->base_feat, model->features,
                      S390_FEAT_MAX);
        if (!bitmap_empty(bitmap, S390_FEAT_MAX)) {
            s390_feat_bitmap_to_ascii(bitmap, qdict, qdict_add_disabled_feat);
        }

        /* features added to the base feature set */
        bitmap_andnot(bitmap, model->features, model->def->base_feat,
                      S390_FEAT_MAX);
        if (!bitmap_empty(bitmap, S390_FEAT_MAX)) {
            s390_feat_bitmap_to_ascii(bitmap, qdict, qdict_add_enabled_feat);
        }
    } else {
        /* expand all features */
        s390_feat_bitmap_to_ascii(model->features, qdict,
                                  qdict_add_enabled_feat);
        bitmap_complement(bitmap, model->features, S390_FEAT_MAX);
        s390_feat_bitmap_to_ascii(bitmap, qdict, qdict_add_disabled_feat);
    }

    if (!qdict_size(qdict)) {
        QDECREF(qdict);
    } else {
        info->props = QOBJECT(qdict);
        info->has_props = true;
    }
}

CpuModelExpansionInfo *arch_query_cpu_model_expansion(CpuModelExpansionType type,
                                                      CpuModelInfo *model,
                                                      Error **errp)
{
    CpuModelExpansionInfo *expansion_info = NULL;
    S390CPUModel s390_model;
    bool delta_changes = false;

    /* convert it to our internal representation */
    cpu_model_from_info(&s390_model, model, errp);
    if (*errp) {
        return NULL;
    }

    if (type == CPU_MODEL_EXPANSION_TYPE_STATIC) {
        delta_changes = true;
    } else if (type != CPU_MODEL_EXPANSION_TYPE_FULL) {
        error_setg(errp, "The requested expansion type is not supported.");
        return NULL;
    }

    /* convert it back to a static representation */
    expansion_info = g_malloc0(sizeof(*expansion_info));
    expansion_info->model = g_malloc0(sizeof(*expansion_info->model));
    cpu_info_from_model(expansion_info->model, &s390_model, delta_changes);
    return expansion_info;
}

static void list_add_feat(const char *name, void *opaque)
{
    strList **last = (strList **) opaque;
    strList *entry;

    entry = g_malloc0(sizeof(*entry));
    entry->value = g_strdup(name);
    entry->next = *last;
    *last = entry;
}

CpuModelCompareInfo *arch_query_cpu_model_comparison(CpuModelInfo *infoa,
                                                     CpuModelInfo *infob,
                                                     Error **errp)
{
    CpuModelCompareResult feat_result, gen_result;
    CpuModelCompareInfo *compare_info;
    S390FeatBitmap missing, added;
    S390CPUModel modela, modelb;

    /* convert both models to our internal representation */
    cpu_model_from_info(&modela, infoa, errp);
    if (*errp) {
        return NULL;
    }
    cpu_model_from_info(&modelb, infob, errp);
    if (*errp) {
        return NULL;
    }
    compare_info = g_malloc0(sizeof(*compare_info));

    /* check the cpu generation and ga level */
    if (modela.def->gen == modelb.def->gen) {
        if (modela.def->ec_ga == modelb.def->ec_ga) {
            /* ec and corresponding bc are identical */
            gen_result = CPU_MODEL_COMPARE_RESULT_IDENTICAL;
        } else if (modela.def->ec_ga < modelb.def->ec_ga) {
            gen_result = CPU_MODEL_COMPARE_RESULT_SUBSET;
        } else {
            gen_result = CPU_MODEL_COMPARE_RESULT_SUPERSET;
        }
    } else if (modela.def->gen < modelb.def->gen) {
        gen_result = CPU_MODEL_COMPARE_RESULT_SUBSET;
    } else {
        gen_result = CPU_MODEL_COMPARE_RESULT_SUPERSET;
    }
    if (gen_result != CPU_MODEL_COMPARE_RESULT_IDENTICAL) {
        /* both models cannot be made identical */
        list_add_feat("type", &compare_info->responsible_properties);
    }

    /* check the feature set */
    if (bitmap_equal(modela.features, modelb.features, S390_FEAT_MAX)) {
        feat_result = CPU_MODEL_COMPARE_RESULT_IDENTICAL;
    } else {
        bitmap_andnot(missing, modela.features, modelb.features, S390_FEAT_MAX);
        s390_feat_bitmap_to_ascii(missing,
                                  &compare_info->responsible_properties,
                                  list_add_feat);
        bitmap_andnot(added, modelb.features, modela.features, S390_FEAT_MAX);
        s390_feat_bitmap_to_ascii(added, &compare_info->responsible_properties,
                                  list_add_feat);
        if (bitmap_empty(missing, S390_FEAT_MAX)) {
            feat_result = CPU_MODEL_COMPARE_RESULT_SUBSET;
        } else if (bitmap_empty(added, S390_FEAT_MAX)) {
            feat_result = CPU_MODEL_COMPARE_RESULT_SUPERSET;
        } else {
            feat_result = CPU_MODEL_COMPARE_RESULT_INCOMPATIBLE;
        }
    }

    /* combine the results */
    if (gen_result == feat_result) {
        compare_info->result = gen_result;
    } else if (feat_result == CPU_MODEL_COMPARE_RESULT_IDENTICAL) {
        compare_info->result = gen_result;
    } else if (gen_result == CPU_MODEL_COMPARE_RESULT_IDENTICAL) {
        compare_info->result = feat_result;
    } else {
        compare_info->result = CPU_MODEL_COMPARE_RESULT_INCOMPATIBLE;
    }
    return compare_info;
}

CpuModelBaselineInfo *arch_query_cpu_model_baseline(CpuModelInfo *infoa,
                                                    CpuModelInfo *infob,
                                                    Error **errp)
{
    CpuModelBaselineInfo *baseline_info;
    S390CPUModel modela, modelb, model;
    uint16_t cpu_type;
    uint8_t max_gen_ga;
    uint8_t max_gen;

    /* convert both models to our internal representation */
    cpu_model_from_info(&modela, infoa, errp);
    if (*errp) {
        return NULL;
    }

    cpu_model_from_info(&modelb, infob, errp);
    if (*errp) {
        return NULL;
    }

    /* features both models support */
    bitmap_and(model.features, modela.features, modelb.features, S390_FEAT_MAX);

    /* detect the maximum model not regarding features */
    if (modela.def->gen == modelb.def->gen) {
        if (modela.def->type == modelb.def->type) {
            cpu_type = modela.def->type;
        } else {
            cpu_type = 0;
        }
        max_gen = modela.def->gen;
        max_gen_ga = MIN(modela.def->ec_ga, modelb.def->ec_ga);
    } else if (modela.def->gen > modelb.def->gen) {
        cpu_type = modelb.def->type;
        max_gen = modelb.def->gen;
        max_gen_ga = modelb.def->ec_ga;
    } else {
        cpu_type = modela.def->type;
        max_gen = modela.def->gen;
        max_gen_ga = modela.def->ec_ga;
    }

    model.def = s390_find_cpu_def(cpu_type, max_gen, max_gen_ga,
                                  model.features);
    /* strip off features not part of the max model */
    bitmap_and(model.features, model.features, model.def->full_feat,
               S390_FEAT_MAX);

    baseline_info = g_malloc0(sizeof(*baseline_info));
    baseline_info->model = g_malloc0(sizeof(*baseline_info->model));
    cpu_info_from_model(baseline_info->model, &model, true);
    return baseline_info;
}
#endif

static void check_consistency(const S390CPUModel *model)
{
    static int dep[][2] = {
        { S390_FEAT_IPTE_RANGE, S390_FEAT_DAT_ENH },
        { S390_FEAT_IDTE_SEGMENT, S390_FEAT_DAT_ENH },
        { S390_FEAT_IDTE_REGION, S390_FEAT_DAT_ENH },
        { S390_FEAT_IDTE_REGION, S390_FEAT_IDTE_SEGMENT },
        { S390_FEAT_LOCAL_TLB_CLEARING, S390_FEAT_DAT_ENH},
        { S390_FEAT_LONG_DISPLACEMENT_FAST, S390_FEAT_LONG_DISPLACEMENT },
        { S390_FEAT_DFP_FAST, S390_FEAT_DFP },
        { S390_FEAT_TRANSACTIONAL_EXE, S390_FEAT_STFLE_49 },
        { S390_FEAT_EDAT_2, S390_FEAT_EDAT},
        { S390_FEAT_MSA_EXT_5, S390_FEAT_KIMD_SHA_512 },
        { S390_FEAT_MSA_EXT_5, S390_FEAT_KLMD_SHA_512 },
        { S390_FEAT_MSA_EXT_4, S390_FEAT_MSA_EXT_3 },
        { S390_FEAT_SIE_CMMA, S390_FEAT_CMM },
        { S390_FEAT_SIE_CMMA, S390_FEAT_SIE_GSLS },
        { S390_FEAT_SIE_PFMFI, S390_FEAT_EDAT },
    };
    int i;

    for (i = 0; i < ARRAY_SIZE(dep); i++) {
        if (test_bit(dep[i][0], model->features) &&
            !test_bit(dep[i][1], model->features)) {
            error_report("Warning: \'%s\' requires \'%s\'.",
                         s390_feat_def(dep[i][0])->name,
                         s390_feat_def(dep[i][1])->name);
        }
    }
}

static void error_prepend_missing_feat(const char *name, void *opaque)
{
    error_prepend((Error **) opaque, "%s ", name);
}

static void check_compatibility(const S390CPUModel *max_model,
                                const S390CPUModel *model, Error **errp)
{
    S390FeatBitmap missing;

    if (model->def->gen > max_model->def->gen) {
        error_setg(errp, "Selected CPU generation is too new. Maximum "
                   "supported model in the configuration: \'%s\'",
                   max_model->def->name);
        return;
    } else if (model->def->gen == max_model->def->gen &&
               model->def->ec_ga > max_model->def->ec_ga) {
        error_setg(errp, "Selected CPU GA level is too new. Maximum "
                   "supported model in the configuration: \'%s\'",
                   max_model->def->name);
        return;
    }

    /* detect the missing features to properly report them */
    bitmap_andnot(missing, model->features, max_model->features, S390_FEAT_MAX);
    if (bitmap_empty(missing, S390_FEAT_MAX)) {
        return;
    }

    error_setg(errp, " ");
    s390_feat_bitmap_to_ascii(missing, errp, error_prepend_missing_feat);
    error_prepend(errp, "Some features requested in the CPU model are not "
                  "available in the configuration: ");
}

static S390CPUModel *get_max_cpu_model(Error **errp)
{
    static S390CPUModel max_model;
    static bool cached;

    if (cached) {
        return &max_model;
    }

    if (kvm_enabled()) {
        kvm_s390_get_host_cpu_model(&max_model, errp);
    } else {
        /* TCG emulates a z900 */
        max_model.def = &s390_cpu_defs[0];
        bitmap_copy(max_model.features, max_model.def->default_feat,
                    S390_FEAT_MAX);
    }
    if (!*errp) {
        cached = true;
        return &max_model;
    }
    return NULL;
}

static inline void apply_cpu_model(const S390CPUModel *model, Error **errp)
{
#ifndef CONFIG_USER_ONLY
    static S390CPUModel applied_model;
    static bool applied;

    /*
     * We have the same model for all VCPUs. KVM can only be configured before
     * any VCPUs are defined in KVM.
     */
    if (applied) {
        if (model && memcmp(&applied_model, model, sizeof(S390CPUModel))) {
            error_setg(errp, "Mixed CPU models are not supported on s390x.");
        }
        return;
    }

    if (kvm_enabled()) {
        kvm_s390_apply_cpu_model(model, errp);
    } else if (model) {
        /* FIXME TCG - use data for stdip/stfl */
    }

    if (!*errp) {
        applied = true;
        if (model) {
            applied_model = *model;
        }
    }
#endif
}

void s390_realize_cpu_model(CPUState *cs, Error **errp)
{
    S390CPUClass *xcc = S390_CPU_GET_CLASS(cs);
    S390CPU *cpu = S390_CPU(cs);
    const S390CPUModel *max_model;

    if (xcc->kvm_required && !kvm_enabled()) {
        error_setg(errp, "CPU definition requires KVM");
        return;
    }

    if (!cpu->model) {
        /* no host model support -> perform compatibility stuff */
        apply_cpu_model(NULL, errp);
        return;
    }

    max_model = get_max_cpu_model(errp);
    if (*errp) {
        error_prepend(errp, "CPU models are not available: ");
        return;
    }

    /* copy over properties that can vary */
    cpu->model->lowest_ibc = max_model->lowest_ibc;
    cpu->model->cpu_id = max_model->cpu_id;
    cpu->model->cpu_ver = max_model->cpu_ver;

    check_consistency(cpu->model);
    check_compatibility(max_model, cpu->model, errp);
    if (*errp) {
        return;
    }

    apply_cpu_model(cpu->model, errp);
}

static void get_feature(Object *obj, Visitor *v, const char *name,
                        void *opaque, Error **errp)
{
    S390Feat feat = (S390Feat) opaque;
    S390CPU *cpu = S390_CPU(obj);
    bool value;

    if (!cpu->model) {
        error_setg(errp, "Details about the host CPU model are not available, "
                         "features cannot be queried.");
        return;
    }

    value = test_bit(feat, cpu->model->features);
    visit_type_bool(v, name, &value, errp);
}

static void set_feature(Object *obj, Visitor *v, const char *name,
                        void *opaque, Error **errp)
{
    S390Feat feat = (S390Feat) opaque;
    DeviceState *dev = DEVICE(obj);
    S390CPU *cpu = S390_CPU(obj);
    bool value;

    if (dev->realized) {
        error_setg(errp, "Attempt to set property '%s' on '%s' after "
                   "it was realized", name, object_get_typename(obj));
        return;
    } else if (!cpu->model) {
        error_setg(errp, "Details about the host CPU model are not available, "
                         "features cannot be changed.");
        return;
    }

    visit_type_bool(v, name, &value, errp);
    if (*errp) {
        return;
    }
    if (value) {
        if (!test_bit(feat, cpu->model->def->full_feat)) {
            error_setg(errp, "Feature '%s' is not available for CPU model '%s',"
                       " it was introduced with later models.",
                       name, cpu->model->def->name);
            return;
        }
        set_bit(feat, cpu->model->features);
    } else {
        clear_bit(feat, cpu->model->features);
    }
}

static void get_feature_group(Object *obj, Visitor *v, const char *name,
                              void *opaque, Error **errp)
{
    S390FeatGroup group = (S390FeatGroup) opaque;
    const S390FeatGroupDef *def = s390_feat_group_def(group);
    S390CPU *cpu = S390_CPU(obj);
    S390FeatBitmap tmp;
    bool value;

    if (!cpu->model) {
        error_setg(errp, "Details about the host CPU model are not available, "
                         "features cannot be queried.");
        return;
    }

    /* a group is enabled if all features are enabled */
    bitmap_and(tmp, cpu->model->features, def->feat, S390_FEAT_MAX);
    value = bitmap_equal(tmp, def->feat, S390_FEAT_MAX);
    visit_type_bool(v, name, &value, errp);
}

static void set_feature_group(Object *obj, Visitor *v, const char *name,
                              void *opaque, Error **errp)
{
    S390FeatGroup group = (S390FeatGroup) opaque;
    const S390FeatGroupDef *def = s390_feat_group_def(group);
    DeviceState *dev = DEVICE(obj);
    S390CPU *cpu = S390_CPU(obj);
    bool value;

    if (dev->realized) {
        error_setg(errp, "Attempt to set property '%s' on '%s' after "
                   "it was realized", name, object_get_typename(obj));
        return;
    } else if (!cpu->model) {
        error_setg(errp, "Details about the host CPU model are not available, "
                         "features cannot be changed.");
        return;
    }

    visit_type_bool(v, name, &value, errp);
    if (*errp) {
        return;
    }
    if (value) {
        /* groups are added in one shot, so an intersect is sufficient */
        if (!bitmap_intersects(def->feat, cpu->model->def->full_feat,
                               S390_FEAT_MAX)) {
            error_setg(errp, "Group '%s' is not available for CPU model '%s',"
                       " it was introduced with later models.",
                       name, cpu->model->def->name);
            return;
        }
        bitmap_or(cpu->model->features, cpu->model->features, def->feat,
                  S390_FEAT_MAX);
    } else {
        bitmap_andnot(cpu->model->features, cpu->model->features, def->feat,
                      S390_FEAT_MAX);
    }
}

void s390_cpu_model_register_props(Object *obj)
{
    S390FeatGroup group;
    S390Feat feat;

    for (feat = 0; feat < S390_FEAT_MAX; feat++) {
        const S390FeatDef *def = s390_feat_def(feat);
        object_property_add(obj, def->name, "bool", get_feature,
                            set_feature, NULL, (void *) feat, NULL);
        object_property_set_description(obj, def->name, def->desc , NULL);
    }
    for (group = 0; group < S390_FEAT_GROUP_MAX; group++) {
        const S390FeatGroupDef *def = s390_feat_group_def(group);
        object_property_add(obj, def->name, "bool", get_feature_group,
                            set_feature_group, NULL, (void *) group, NULL);
        object_property_set_description(obj, def->name, def->desc , NULL);
    }
}

static void s390_cpu_model_initfn(Object *obj)
{
    S390CPU *cpu = S390_CPU(obj);
    S390CPUClass *xcc = S390_CPU_GET_CLASS(cpu);

    cpu->model = g_malloc0(sizeof(*cpu->model));
    /* copy the model, so we can modify it */
    cpu->model->def = xcc->cpu_def;
    if (xcc->is_static) {
        /* base model - features will never change */
        bitmap_copy(cpu->model->features, cpu->model->def->base_feat,
                    S390_FEAT_MAX);
    } else {
        /* latest model - features can change */
        bitmap_copy(cpu->model->features,
                    cpu->model->def->default_feat, S390_FEAT_MAX);
    }
}

#ifdef CONFIG_KVM
static void s390_host_cpu_model_initfn(Object *obj)
{
    S390CPU *cpu = S390_CPU(obj);
    Error *err = NULL;

    if (!kvm_enabled() || !kvm_s390_cpu_models_supported()) {
        return;
    }

    cpu->model = g_malloc0(sizeof(*cpu->model));
    kvm_s390_get_host_cpu_model(cpu->model, &err);
    if (err) {
        error_report_err(err);
        g_free(cpu->model);
        /* fallback to unsupported cpu models */
        cpu->model = NULL;
    }
}
#endif

static void s390_qemu_cpu_model_initfn(Object *obj)
{
    S390CPU *cpu = S390_CPU(obj);

    cpu->model = g_malloc0(sizeof(*cpu->model));
    /* TCG emulates a z900 */
    cpu->model->def = &s390_cpu_defs[0];
    bitmap_copy(cpu->model->features, cpu->model->def->default_feat,
                S390_FEAT_MAX);
}

static void s390_cpu_model_finalize(Object *obj)
{
    S390CPU *cpu = S390_CPU(obj);

    g_free(cpu->model);
    cpu->model = NULL;
}

static bool get_is_migration_safe(Object *obj, Error **errp)
{
    return S390_CPU_GET_CLASS(obj)->is_migration_safe;
}

static bool get_is_static(Object *obj, Error **errp)
{
    return S390_CPU_GET_CLASS(obj)->is_static;
}

static char *get_description(Object *obj, Error **errp)
{
    return g_strdup(S390_CPU_GET_CLASS(obj)->desc);
}

void s390_cpu_model_class_register_props(ObjectClass *oc)
{
    object_class_property_add_bool(oc, "migration-safe", get_is_migration_safe,
                                   NULL, NULL);
    object_class_property_add_bool(oc, "static", get_is_static,
                                   NULL, NULL);
    object_class_property_add_str(oc, "description", get_description, NULL,
                                  NULL);
}

#ifdef CONFIG_KVM
static void s390_host_cpu_model_class_init(ObjectClass *oc, void *data)
{
    S390CPUClass *xcc = S390_CPU_CLASS(oc);

    xcc->kvm_required = true;
    xcc->desc = "KVM only: All recognized features";
}
#endif

static void s390_base_cpu_model_class_init(ObjectClass *oc, void *data)
{
    S390CPUClass *xcc = S390_CPU_CLASS(oc);

    /* all base models are migration safe */
    xcc->cpu_def = (const S390CPUDef *) data;
    xcc->is_migration_safe = true;
    xcc->is_static = true;
    xcc->desc = xcc->cpu_def->desc;
}

static void s390_cpu_model_class_init(ObjectClass *oc, void *data)
{
    S390CPUClass *xcc = S390_CPU_CLASS(oc);

    /* model that can change between QEMU versions */
    xcc->cpu_def = (const S390CPUDef *) data;
    xcc->is_migration_safe = true;
    xcc->desc = xcc->cpu_def->desc;
}

static void s390_qemu_cpu_model_class_init(ObjectClass *oc, void *data)
{
    S390CPUClass *xcc = S390_CPU_CLASS(oc);

    xcc->is_migration_safe = true;
    xcc->desc = g_strdup_printf("QEMU Virtual CPU version %s",
                                qemu_hw_version());
}

#define S390_CPU_TYPE_SUFFIX "-" TYPE_S390_CPU
#define S390_CPU_TYPE_NAME(name) (name S390_CPU_TYPE_SUFFIX)

/* Generate type name for a cpu model. Caller has to free the string. */
static char *s390_cpu_type_name(const char *model_name)
{
    return g_strdup_printf(S390_CPU_TYPE_NAME("%s"), model_name);
}

/* Generate type name for a base cpu model. Caller has to free the string. */
static char *s390_base_cpu_type_name(const char *model_name)
{
    return g_strdup_printf(S390_CPU_TYPE_NAME("%s-base"), model_name);
}

ObjectClass *s390_cpu_class_by_name(const char *name)
{
    char *typename = s390_cpu_type_name(name);
    ObjectClass *oc;

    oc = object_class_by_name(typename);
    g_free(typename);
    return oc;
}

static const TypeInfo qemu_s390_cpu_type_info = {
    .name = S390_CPU_TYPE_NAME("qemu"),
    .parent = TYPE_S390_CPU,
    .instance_init = s390_qemu_cpu_model_initfn,
    .instance_finalize = s390_cpu_model_finalize,
    .class_init = s390_qemu_cpu_model_class_init,
};

#ifdef CONFIG_KVM
static const TypeInfo host_s390_cpu_type_info = {
    .name = S390_CPU_TYPE_NAME("host"),
    .parent = TYPE_S390_CPU,
    .instance_init = s390_host_cpu_model_initfn,
    .instance_finalize = s390_cpu_model_finalize,
    .class_init = s390_host_cpu_model_class_init,
};
#endif

static void register_types(void)
{
    int i;

    /* init all bitmaps from gnerated data initially */
    for (i = 0; i < ARRAY_SIZE(s390_cpu_defs); i++) {
        s390_init_feat_bitmap(s390_cpu_defs[i].base_init,
                              s390_cpu_defs[i].base_feat);
        s390_init_feat_bitmap(s390_cpu_defs[i].default_init,
                              s390_cpu_defs[i].default_feat);
        s390_init_feat_bitmap(s390_cpu_defs[i].full_init,
                              s390_cpu_defs[i].full_feat);
    }

    for (i = 0; i < ARRAY_SIZE(s390_cpu_defs); i++) {
        char *base_name = s390_base_cpu_type_name(s390_cpu_defs[i].name);
        TypeInfo ti_base = {
            .name = base_name,
            .parent = TYPE_S390_CPU,
            .instance_init = s390_cpu_model_initfn,
            .instance_finalize = s390_cpu_model_finalize,
            .class_init = s390_base_cpu_model_class_init,
            .class_data = (void *) &s390_cpu_defs[i],
        };
        char *name = s390_cpu_type_name(s390_cpu_defs[i].name);
        TypeInfo ti = {
            .name = name,
            .parent = TYPE_S390_CPU,
            .instance_init = s390_cpu_model_initfn,
            .instance_finalize = s390_cpu_model_finalize,
            .class_init = s390_cpu_model_class_init,
            .class_data = (void *) &s390_cpu_defs[i],
        };

        type_register_static(&ti_base);
        type_register_static(&ti);
        g_free(base_name);
        g_free(name);
    }

    type_register_static(&qemu_s390_cpu_type_info);
#ifdef CONFIG_KVM
    type_register_static(&host_s390_cpu_type_info);
#endif
}

type_init(register_types)
