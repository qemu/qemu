/*
 * CPU models for s390x - System Emulation-only
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
#include "s390x-internal.h"
#include "kvm/kvm_s390x.h"
#include "sysemu/kvm.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "qapi/qmp/qerror.h"
#include "qapi/qobject-input-visitor.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qapi-commands-machine-target.h"

static void list_add_feat(const char *name, void *opaque);

static void check_unavailable_features(const S390CPUModel *max_model,
                                       const S390CPUModel *model,
                                       strList **unavailable)
{
    S390FeatBitmap missing;

    /* check general model compatibility */
    if (max_model->def->gen < model->def->gen ||
        (max_model->def->gen == model->def->gen &&
         max_model->def->ec_ga < model->def->ec_ga)) {
        list_add_feat("type", unavailable);
    }

    /* detect missing features if any to properly report them */
    bitmap_andnot(missing, model->features, max_model->features,
                  S390_FEAT_MAX);
    if (!bitmap_empty(missing, S390_FEAT_MAX)) {
        s390_feat_bitmap_to_ascii(missing, unavailable, list_add_feat);
    }
}

struct CpuDefinitionInfoListData {
    CpuDefinitionInfoList *list;
    S390CPUModel *model;
};

static void create_cpu_model_list(ObjectClass *klass, void *opaque)
{
    struct CpuDefinitionInfoListData *cpu_list_data = opaque;
    CpuDefinitionInfoList **cpu_list = &cpu_list_data->list;
    CpuDefinitionInfo *info;
    char *name = g_strdup(object_class_get_name(klass));
    S390CPUClass *scc = S390_CPU_CLASS(klass);

    /* strip off the -s390x-cpu */
    g_strrstr(name, "-" TYPE_S390_CPU)[0] = 0;
    info = g_new0(CpuDefinitionInfo, 1);
    info->name = name;
    info->has_migration_safe = true;
    info->migration_safe = scc->is_migration_safe;
    info->q_static = scc->is_static;
    info->q_typename = g_strdup(object_class_get_name(klass));
    /* check for unavailable features */
    if (cpu_list_data->model) {
        Object *obj;
        S390CPU *sc;
        obj = object_new_with_class(klass);
        sc = S390_CPU(obj);
        if (sc->model) {
            info->has_unavailable_features = true;
            check_unavailable_features(cpu_list_data->model, sc->model,
                                       &info->unavailable_features);
        }
        object_unref(obj);
    }

    QAPI_LIST_PREPEND(*cpu_list, info);
}

CpuDefinitionInfoList *qmp_query_cpu_definitions(Error **errp)
{
    struct CpuDefinitionInfoListData list_data = {
        .list = NULL,
    };

    list_data.model = get_max_cpu_model(NULL);

    object_class_foreach(create_cpu_model_list, TYPE_S390_CPU, false,
                         &list_data);

    return list_data.list;
}

static void cpu_model_from_info(S390CPUModel *model, const CpuModelInfo *info,
                                Error **errp)
{
    Error *err = NULL;
    const QDict *qdict = NULL;
    const QDictEntry *e;
    Visitor *visitor;
    ObjectClass *oc;
    S390CPU *cpu;
    Object *obj;

    if (info->props) {
        qdict = qobject_to(QDict, info->props);
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
    obj = object_new_with_class(oc);
    cpu = S390_CPU(obj);

    if (!cpu->model) {
        error_setg(errp, "Details about the host CPU model are not available, "
                         "it cannot be used.");
        object_unref(obj);
        return;
    }

    if (qdict) {
        visitor = qobject_input_visitor_new(info->props);
        if (!visit_start_struct(visitor, NULL, NULL, 0, errp)) {
            visit_free(visitor);
            object_unref(obj);
            return;
        }
        for (e = qdict_first(qdict); e; e = qdict_next(qdict, e)) {
            if (!object_property_set(obj, e->key, visitor, &err)) {
                break;
            }
        }
        if (!err) {
            visit_check_struct(visitor, &err);
        }
        visit_end_struct(visitor, NULL);
        visit_free(visitor);
        if (err) {
            error_propagate(errp, err);
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
    qdict_put_bool(opaque, name, false);
}

static void qdict_add_enabled_feat(const char *name, void *opaque)
{
    qdict_put_bool(opaque, name, true);
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
        qobject_unref(qdict);
    } else {
        info->props = QOBJECT(qdict);
    }
}

CpuModelExpansionInfo *qmp_query_cpu_model_expansion(CpuModelExpansionType type,
                                                      CpuModelInfo *model,
                                                      Error **errp)
{
    Error *err = NULL;
    CpuModelExpansionInfo *expansion_info = NULL;
    S390CPUModel s390_model;
    bool delta_changes = false;

    /* convert it to our internal representation */
    cpu_model_from_info(&s390_model, model, &err);
    if (err) {
        error_propagate(errp, err);
        return NULL;
    }

    if (type == CPU_MODEL_EXPANSION_TYPE_STATIC) {
        delta_changes = true;
    } else if (type != CPU_MODEL_EXPANSION_TYPE_FULL) {
        error_setg(errp, "The requested expansion type is not supported.");
        return NULL;
    }

    /* convert it back to a static representation */
    expansion_info = g_new0(CpuModelExpansionInfo, 1);
    expansion_info->model = g_malloc0(sizeof(*expansion_info->model));
    cpu_info_from_model(expansion_info->model, &s390_model, delta_changes);
    return expansion_info;
}

static void list_add_feat(const char *name, void *opaque)
{
    strList **last = (strList **) opaque;

    QAPI_LIST_PREPEND(*last, g_strdup(name));
}

CpuModelCompareInfo *qmp_query_cpu_model_comparison(CpuModelInfo *infoa,
                                                     CpuModelInfo *infob,
                                                     Error **errp)
{
    Error *err = NULL;
    CpuModelCompareResult feat_result, gen_result;
    CpuModelCompareInfo *compare_info;
    S390FeatBitmap missing, added;
    S390CPUModel modela, modelb;

    /* convert both models to our internal representation */
    cpu_model_from_info(&modela, infoa, &err);
    if (err) {
        error_propagate(errp, err);
        return NULL;
    }
    cpu_model_from_info(&modelb, infob, &err);
    if (err) {
        error_propagate(errp, err);
        return NULL;
    }
    compare_info = g_new0(CpuModelCompareInfo, 1);

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

CpuModelBaselineInfo *qmp_query_cpu_model_baseline(CpuModelInfo *infoa,
                                                    CpuModelInfo *infob,
                                                    Error **errp)
{
    Error *err = NULL;
    CpuModelBaselineInfo *baseline_info;
    S390CPUModel modela, modelb, model;
    uint16_t cpu_type;
    uint8_t max_gen_ga;
    uint8_t max_gen;

    /* convert both models to our internal representation */
    cpu_model_from_info(&modela, infoa, &err);
    if (err) {
        error_propagate(errp, err);
        return NULL;
    }

    cpu_model_from_info(&modelb, infob, &err);
    if (err) {
        error_propagate(errp, err);
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

    /* models without early base features (esan3) are bad */
    if (!model.def) {
        error_setg(errp, "No compatible CPU model could be created as"
                   " important base features are disabled");
        return NULL;
    }

    /* strip off features not part of the max model */
    bitmap_and(model.features, model.features, model.def->full_feat,
               S390_FEAT_MAX);

    baseline_info = g_new0(CpuModelBaselineInfo, 1);
    baseline_info->model = g_malloc0(sizeof(*baseline_info->model));
    cpu_info_from_model(baseline_info->model, &model, true);
    return baseline_info;
}

void apply_cpu_model(const S390CPUModel *model, Error **errp)
{
    Error *err = NULL;
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
        kvm_s390_apply_cpu_model(model, &err);
        if (err) {
            error_propagate(errp, err);
            return;
        }
    }

    applied = true;
    if (model) {
        applied_model = *model;
    }
}
