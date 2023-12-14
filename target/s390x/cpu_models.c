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
#include "s390x-internal.h"
#include "kvm/kvm_s390x.h"
#include "sysemu/kvm.h"
#include "sysemu/tcg.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qapi/visitor.h"
#include "qemu/module.h"
#include "qemu/hw-version.h"
#include "qemu/qemu-print.h"
#ifndef CONFIG_USER_ONLY
#include "sysemu/sysemu.h"
#include "target/s390x/kvm/pv.h"
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
 * CPU definition list in order of release. Up to generation 14 base features
 * of a following release have been a superset of the previous release. With
 * generation 15 one base feature and one optional feature have been deprecated.
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
    CPUDEF_INIT(0x3906, 14, 1, 47, 0x08000000U, "z14", "IBM z14 GA1"),
    CPUDEF_INIT(0x3906, 14, 2, 47, 0x08000000U, "z14.2", "IBM z14 GA2"),
    CPUDEF_INIT(0x3907, 14, 1, 47, 0x08000000U, "z14ZR1", "IBM z14 Model ZR1 GA1"),
    CPUDEF_INIT(0x8561, 15, 1, 47, 0x08000000U, "gen15a", "IBM z15 T01 GA1"),
    CPUDEF_INIT(0x8562, 15, 1, 47, 0x08000000U, "gen15b", "IBM z15 T02 GA1"),
    CPUDEF_INIT(0x3931, 16, 1, 47, 0x08000000U, "gen16a", "IBM 3931 GA1"),
    CPUDEF_INIT(0x3932, 16, 1, 47, 0x08000000U, "gen16b", "IBM 3932 GA1"),
};

#define QEMU_MAX_CPU_TYPE 0x8561
#define QEMU_MAX_CPU_GEN 15
#define QEMU_MAX_CPU_EC_GA 1
static S390FeatBitmap qemu_max_cpu_feat;

/* features part of a base model but not relevant for finding a base model */
S390FeatBitmap ignored_base_feat;

void s390_cpudef_featoff(uint8_t gen, uint8_t ec_ga, S390Feat feat)
{
    const S390CPUDef *def;

    def = s390_find_cpu_def(0, gen, ec_ga, NULL);
    clear_bit(feat, (unsigned long *)&def->default_feat);
}

void s390_cpudef_featoff_greater(uint8_t gen, uint8_t ec_ga, S390Feat feat)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(s390_cpu_defs); i++) {
        const S390CPUDef *def = &s390_cpu_defs[i];

        if (def->gen < gen) {
            continue;
        }
        if (def->gen == gen && def->ec_ga < ec_ga) {
            continue;
        }

        clear_bit(feat, (unsigned long *)&def->default_feat);
    }
}

void s390_cpudef_group_featoff_greater(uint8_t gen, uint8_t ec_ga,
                                       S390FeatGroup group)
{
    const S390FeatGroupDef *group_def = s390_feat_group_def(group);
    S390FeatBitmap group_def_off;
    int i;

    bitmap_complement(group_def_off, group_def->feat, S390_FEAT_MAX);

    for (i = 0; i < ARRAY_SIZE(s390_cpu_defs); i++) {
        const S390CPUDef *cpu_def = &s390_cpu_defs[i];

        if (cpu_def->gen < gen) {
            continue;
        }
        if (cpu_def->gen == gen && cpu_def->ec_ga < ec_ga) {
            continue;
        }

        bitmap_and((unsigned long *)&cpu_def->default_feat,
                   cpu_def->default_feat, group_def_off, S390_FEAT_MAX);
    }
}

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
    S390CPU *cpu = S390_CPU(first_cpu);

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
        if (feat == S390_FEAT_ZPCI) {
            return true;
        }
        return 0;
    }

#ifndef CONFIG_USER_ONLY
    if (s390_is_pv()) {
        switch (feat) {
        case S390_FEAT_DIAG_318:
        case S390_FEAT_HPMA2:
        case S390_FEAT_SIE_F2:
        case S390_FEAT_SIE_SKEY:
        case S390_FEAT_SIE_GPERE:
        case S390_FEAT_SIE_SIIF:
        case S390_FEAT_SIE_SIGPIF:
        case S390_FEAT_SIE_IB:
        case S390_FEAT_SIE_CEI:
        case S390_FEAT_SIE_KSS:
        case S390_FEAT_SIE_GSLS:
        case S390_FEAT_SIE_64BSCAO:
        case S390_FEAT_SIE_CMMA:
        case S390_FEAT_SIE_PFMFI:
        case S390_FEAT_SIE_IBS:
        case S390_FEAT_CONFIGURATION_TOPOLOGY:
            return false;
            break;
        default:
            break;
        }
    }
#endif
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
    const S390CPUDef *matching_cpu_type = NULL;
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
            /*
             * Ignore certain features that are in the base model, but not
             * relevant for the search (esp. MSA subfunctions).
             */
            bitmap_andnot(missing, missing, ignored_base_feat, S390_FEAT_MAX);
            if (!bitmap_empty(missing, S390_FEAT_MAX)) {
                break;
            }
        }

        /* stop the search if we found the exact model */
        if (def->type == type && def->ec_ga == ec_ga) {
            return def;
        }
        /* remember if we've at least seen one with the same cpu type */
        if (def->type == type) {
            matching_cpu_type = def;
        }
        last_compatible = def;
    }
    /* prefer the model with the same cpu type, esp. don't take the BC for EC */
    if (matching_cpu_type) {
        return matching_cpu_type;
    }
    return last_compatible;
}

static void s390_print_cpu_model_list_entry(gpointer data, gpointer user_data)
{
    const S390CPUClass *scc = S390_CPU_CLASS((ObjectClass *)data);
    CPUClass *cc = CPU_CLASS(scc);
    char *name = g_strdup(object_class_get_name((ObjectClass *)data));
    g_autoptr(GString) details = g_string_new("");

    if (scc->is_static) {
        g_string_append(details, "static, ");
    }
    if (scc->is_migration_safe) {
        g_string_append(details, "migration-safe, ");
    }
    if (cc->deprecation_note) {
        g_string_append(details, "deprecated, ");
    }
    if (details->len) {
        /* cull trailing ', ' */
        g_string_truncate(details, details->len - 2);
    }

    /* strip off the -s390x-cpu */
    g_strrstr(name, "-" TYPE_S390_CPU)[0] = 0;
    if (details->len) {
        qemu_printf("s390 %-15s %-35s (%s)\n", name, scc->desc, details->str);
    } else {
        qemu_printf("s390 %-15s %-35s\n", name, scc->desc);
    }
    g_free(name);
}

static gint s390_cpu_list_compare(gconstpointer a, gconstpointer b)
{
    const S390CPUClass *cc_a = S390_CPU_CLASS((ObjectClass *)a);
    const S390CPUClass *cc_b = S390_CPU_CLASS((ObjectClass *)b);
    const char *name_a = object_class_get_name((ObjectClass *)a);
    const char *name_b = object_class_get_name((ObjectClass *)b);

    /*
     * Move qemu, host and max to the top of the list, qemu first, host second,
     * max third.
     */
    if (name_a[0] == 'q') {
        return -1;
    } else if (name_b[0] == 'q') {
        return 1;
    } else if (name_a[0] == 'h') {
        return -1;
    } else if (name_b[0] == 'h') {
        return 1;
    } else if (name_a[0] == 'm') {
        return -1;
    } else if (name_b[0] == 'm') {
        return 1;
    }

    /* keep the same order we have in our table (sorted by release date) */
    if (cc_a->cpu_def != cc_b->cpu_def) {
        return cc_a->cpu_def - cc_b->cpu_def;
    }

    /* exact same definition - list base model first */
    return cc_a->is_static ? -1 : 1;
}

void s390_cpu_list(void)
{
    S390FeatGroup group;
    S390Feat feat;
    GSList *list;

    list = object_class_get_list(TYPE_S390_CPU, false);
    list = g_slist_sort(list, s390_cpu_list_compare);
    g_slist_foreach(list, s390_print_cpu_model_list_entry, NULL);
    g_slist_free(list);

    qemu_printf("\nRecognized feature flags:\n");
    for (feat = 0; feat < S390_FEAT_MAX; feat++) {
        const S390FeatDef *def = s390_feat_def(feat);

        qemu_printf("%-20s %s\n", def->name, def->desc);
    }

    qemu_printf("\nRecognized feature groups:\n");
    for (group = 0; group < S390_FEAT_GROUP_MAX; group++) {
        const S390FeatGroupDef *def = s390_feat_group_def(group);

        qemu_printf("%-20s %s\n", def->name, def->desc);
    }
}

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
        { S390_FEAT_MSA_EXT_8, S390_FEAT_MSA_EXT_3 },
        { S390_FEAT_MSA_EXT_9, S390_FEAT_MSA_EXT_3 },
        { S390_FEAT_MSA_EXT_9, S390_FEAT_MSA_EXT_4 },
        { S390_FEAT_MULTIPLE_EPOCH, S390_FEAT_TOD_CLOCK_STEERING },
        { S390_FEAT_VECTOR_PACKED_DECIMAL, S390_FEAT_VECTOR },
        { S390_FEAT_VECTOR_PACKED_DECIMAL_ENH, S390_FEAT_VECTOR_PACKED_DECIMAL },
        { S390_FEAT_VECTOR_PACKED_DECIMAL_ENH2, S390_FEAT_VECTOR_PACKED_DECIMAL_ENH },
        { S390_FEAT_VECTOR_ENH, S390_FEAT_VECTOR },
        { S390_FEAT_INSTRUCTION_EXEC_PROT, S390_FEAT_SIDE_EFFECT_ACCESS_ESOP2 },
        { S390_FEAT_SIDE_EFFECT_ACCESS_ESOP2, S390_FEAT_ESOP },
        { S390_FEAT_CMM_NT, S390_FEAT_CMM },
        { S390_FEAT_GUARDED_STORAGE, S390_FEAT_SIDE_EFFECT_ACCESS_ESOP2 },
        { S390_FEAT_MULTIPLE_EPOCH, S390_FEAT_STORE_CLOCK_FAST },
        { S390_FEAT_MULTIPLE_EPOCH, S390_FEAT_TOD_CLOCK_STEERING },
        { S390_FEAT_SEMAPHORE_ASSIST, S390_FEAT_STFLE_49 },
        { S390_FEAT_KIMD_SHA3_224, S390_FEAT_MSA },
        { S390_FEAT_KIMD_SHA3_256, S390_FEAT_MSA },
        { S390_FEAT_KIMD_SHA3_384, S390_FEAT_MSA },
        { S390_FEAT_KIMD_SHA3_512, S390_FEAT_MSA },
        { S390_FEAT_KIMD_SHAKE_128, S390_FEAT_MSA },
        { S390_FEAT_KIMD_SHAKE_256, S390_FEAT_MSA },
        { S390_FEAT_KLMD_SHA3_224, S390_FEAT_MSA },
        { S390_FEAT_KLMD_SHA3_256, S390_FEAT_MSA },
        { S390_FEAT_KLMD_SHA3_384, S390_FEAT_MSA },
        { S390_FEAT_KLMD_SHA3_512, S390_FEAT_MSA },
        { S390_FEAT_KLMD_SHAKE_128, S390_FEAT_MSA },
        { S390_FEAT_KLMD_SHAKE_256, S390_FEAT_MSA },
        { S390_FEAT_PRNO_TRNG_QRTCR, S390_FEAT_MSA_EXT_5 },
        { S390_FEAT_PRNO_TRNG, S390_FEAT_MSA_EXT_5 },
        { S390_FEAT_SIE_KSS, S390_FEAT_SIE_F2 },
        { S390_FEAT_AP_QUERY_CONFIG_INFO, S390_FEAT_AP },
        { S390_FEAT_AP_FACILITIES_TEST, S390_FEAT_AP },
        { S390_FEAT_PTFF_QSIE, S390_FEAT_MULTIPLE_EPOCH },
        { S390_FEAT_PTFF_QTOUE, S390_FEAT_MULTIPLE_EPOCH },
        { S390_FEAT_PTFF_STOE, S390_FEAT_MULTIPLE_EPOCH },
        { S390_FEAT_PTFF_STOUE, S390_FEAT_MULTIPLE_EPOCH },
        { S390_FEAT_AP_QUEUE_INTERRUPT_CONTROL, S390_FEAT_AP },
        { S390_FEAT_DIAG_318, S390_FEAT_EXTENDED_LENGTH_SCCB },
        { S390_FEAT_NNPA, S390_FEAT_VECTOR },
        { S390_FEAT_RDP, S390_FEAT_LOCAL_TLB_CLEARING },
        { S390_FEAT_UV_FEAT_AP, S390_FEAT_AP },
        { S390_FEAT_UV_FEAT_AP_INTR, S390_FEAT_UV_FEAT_AP },
    };
    int i;

    for (i = 0; i < ARRAY_SIZE(dep); i++) {
        if (test_bit(dep[i][0], model->features) &&
            !test_bit(dep[i][1], model->features)) {
            warn_report("\'%s\' requires \'%s\'.",
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

#ifndef CONFIG_USER_ONLY
    if (only_migratable && test_bit(S390_FEAT_UNPACK, model->features)) {
        error_setg(errp, "The unpack facility is not compatible with "
                   "the --only-migratable option. You must remove either "
                   "the 'unpack' facility or the --only-migratable option");
        return;
    }
#endif

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

S390CPUModel *get_max_cpu_model(Error **errp)
{
    Error *err = NULL;
    static S390CPUModel max_model;
    static bool cached;

    if (cached) {
        return &max_model;
    }

    if (kvm_enabled()) {
        kvm_s390_get_host_cpu_model(&max_model, &err);
    } else {
        max_model.def = s390_find_cpu_def(QEMU_MAX_CPU_TYPE, QEMU_MAX_CPU_GEN,
                                          QEMU_MAX_CPU_EC_GA, NULL);
        bitmap_copy(max_model.features, qemu_max_cpu_feat, S390_FEAT_MAX);
    }
    if (err) {
        error_propagate(errp, err);
        return NULL;
    }
    cached = true;
    return &max_model;
}

void s390_realize_cpu_model(CPUState *cs, Error **errp)
{
    Error *err = NULL;
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
    if (!max_model) {
        error_prepend(errp, "CPU models are not available: ");
        return;
    }

    /* copy over properties that can vary */
    cpu->model->lowest_ibc = max_model->lowest_ibc;
    cpu->model->cpu_id = max_model->cpu_id;
    cpu->model->cpu_id_format = max_model->cpu_id_format;
    cpu->model->cpu_ver = max_model->cpu_ver;

    check_consistency(cpu->model);
    check_compatibility(max_model, cpu->model, &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }

    apply_cpu_model(cpu->model, errp);

#if !defined(CONFIG_USER_ONLY)
    cpu->env.cpuid = s390_cpuid_from_cpu_model(cpu->model);
    if (tcg_enabled()) {
        cpu->env.cpuid = deposit64(cpu->env.cpuid, CPU_PHYS_ADDR_SHIFT,
                                   CPU_PHYS_ADDR_BITS, cpu->env.core_id);
    }
#endif
}

static void get_feature(Object *obj, Visitor *v, const char *name,
                        void *opaque, Error **errp)
{
    S390Feat feat = (S390Feat) (uintptr_t) opaque;
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
    S390Feat feat = (S390Feat) (uintptr_t) opaque;
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

    if (!visit_type_bool(v, name, &value, errp)) {
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
    S390FeatGroup group = (S390FeatGroup) (uintptr_t) opaque;
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
    S390FeatGroup group = (S390FeatGroup) (uintptr_t) opaque;
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

    if (!visit_type_bool(v, name, &value, errp)) {
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

static S390CPUModel s390_qemu_cpu_model;

/* Set the qemu CPU model (on machine initialization). Must not be called
 * once CPUs have been created.
 */
void s390_set_qemu_cpu_model(uint16_t type, uint8_t gen, uint8_t ec_ga,
                             const S390FeatInit feat_init)
{
    const S390CPUDef *def = s390_find_cpu_def(type, gen, ec_ga, NULL);

    g_assert(def);
    g_assert(QTAILQ_EMPTY_RCU(&cpus_queue));

    /* build the CPU model */
    s390_qemu_cpu_model.def = def;
    bitmap_zero(s390_qemu_cpu_model.features, S390_FEAT_MAX);
    s390_init_feat_bitmap(feat_init, s390_qemu_cpu_model.features);
}

static void s390_qemu_cpu_model_initfn(Object *obj)
{
    S390CPU *cpu = S390_CPU(obj);

    cpu->model = g_malloc0(sizeof(*cpu->model));
    /* copy the CPU model so we can modify it */
    memcpy(cpu->model, &s390_qemu_cpu_model, sizeof(*cpu->model));
}

static void s390_max_cpu_model_initfn(Object *obj)
{
    const S390CPUModel *max_model;
    S390CPU *cpu = S390_CPU(obj);
    Error *local_err = NULL;

    if (kvm_enabled() && !kvm_s390_cpu_models_supported()) {
        /* "max" and "host" always work, even without CPU model support */
        return;
    }

    max_model = get_max_cpu_model(&local_err);
    if (local_err) {
        /* we expect errors only under KVM, when actually querying the kernel */
        g_assert(kvm_enabled());
        error_report_err(local_err);
        /* fallback to unsupported CPU models */
        return;
    }

    cpu->model = g_new(S390CPUModel, 1);
    /* copy the CPU model so we can modify it */
    memcpy(cpu->model, max_model, sizeof(*cpu->model));
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
    S390FeatGroup group;
    S390Feat feat;

    object_class_property_add_bool(oc, "migration-safe", get_is_migration_safe,
                                   NULL);
    object_class_property_add_bool(oc, "static", get_is_static,
                                   NULL);
    object_class_property_add_str(oc, "description", get_description, NULL);

    for (feat = 0; feat < S390_FEAT_MAX; feat++) {
        const S390FeatDef *def = s390_feat_def(feat);
        object_class_property_add(oc, def->name, "bool", get_feature,
                                  set_feature, NULL, (void *) feat);
        object_class_property_set_description(oc, def->name, def->desc);
    }
    for (group = 0; group < S390_FEAT_GROUP_MAX; group++) {
        const S390FeatGroupDef *def = s390_feat_group_def(group);
        object_class_property_add(oc, def->name, "bool", get_feature_group,
                                  set_feature_group, NULL, (void *) group);
        object_class_property_set_description(oc, def->name, def->desc);
    }
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

static void s390_max_cpu_model_class_init(ObjectClass *oc, void *data)
{
    S390CPUClass *xcc = S390_CPU_CLASS(oc);

    /*
     * The "max" model is neither static nor migration safe. Under KVM
     * it represents the "host" model. Under TCG it represents the "qemu" CPU
     * model of the latest QEMU machine.
     */
    xcc->desc =
        "Enables all features supported by the accelerator in the current host";
}

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

static const TypeInfo max_s390_cpu_type_info = {
    .name = S390_CPU_TYPE_NAME("max"),
    .parent = TYPE_S390_CPU,
    .instance_init = s390_max_cpu_model_initfn,
    .instance_finalize = s390_cpu_model_finalize,
    .class_init = s390_max_cpu_model_class_init,
};

#ifdef CONFIG_KVM
static const TypeInfo host_s390_cpu_type_info = {
    .name = S390_CPU_TYPE_NAME("host"),
    .parent = S390_CPU_TYPE_NAME("max"),
    .class_init = s390_host_cpu_model_class_init,
};
#endif

static void init_ignored_base_feat(void)
{
    static const int feats[] = {
         /* MSA subfunctions that could not be available on certain machines */
         S390_FEAT_KMAC_DEA,
         S390_FEAT_KMAC_TDEA_128,
         S390_FEAT_KMAC_TDEA_192,
         S390_FEAT_KMC_DEA,
         S390_FEAT_KMC_TDEA_128,
         S390_FEAT_KMC_TDEA_192,
         S390_FEAT_KM_DEA,
         S390_FEAT_KM_TDEA_128,
         S390_FEAT_KM_TDEA_192,
         S390_FEAT_KIMD_SHA_1,
         S390_FEAT_KLMD_SHA_1,
         /* CSSKE is deprecated on newer generations */
         S390_FEAT_CONDITIONAL_SSKE,
    };
    int i;

    for (i = 0; i < ARRAY_SIZE(feats); i++) {
        set_bit(feats[i], ignored_base_feat);
    }
}

static void register_types(void)
{
    static const S390FeatInit qemu_max_init = { S390_FEAT_LIST_QEMU_MAX };
    int i;

    init_ignored_base_feat();

    /* init all bitmaps from generated data initially */
    s390_init_feat_bitmap(qemu_max_init, qemu_max_cpu_feat);
    for (i = 0; i < ARRAY_SIZE(s390_cpu_defs); i++) {
        s390_init_feat_bitmap(s390_cpu_defs[i].base_init,
                              s390_cpu_defs[i].base_feat);
        s390_init_feat_bitmap(s390_cpu_defs[i].default_init,
                              s390_cpu_defs[i].default_feat);
        s390_init_feat_bitmap(s390_cpu_defs[i].full_init,
                              s390_cpu_defs[i].full_feat);
    }

    /* initialize the qemu model with the maximum definition ("max" model) */
    s390_set_qemu_cpu_model(QEMU_MAX_CPU_TYPE, QEMU_MAX_CPU_GEN,
                            QEMU_MAX_CPU_EC_GA, qemu_max_init);

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
    type_register_static(&max_s390_cpu_type_info);
#ifdef CONFIG_KVM
    type_register_static(&host_s390_cpu_type_info);
#endif
}

type_init(register_types)
