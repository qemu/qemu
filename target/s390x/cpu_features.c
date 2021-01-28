/*
 * CPU features/facilities for s390x
 *
 * Copyright IBM Corp. 2016, 2018
 * Copyright Red Hat, Inc. 2019
 *
 * Author(s): David Hildenbrand <david@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "cpu_features.h"
#include "hw/s390x/pv.h"

#define DEF_FEAT(_FEAT, _NAME, _TYPE, _BIT, _DESC) \
    [S390_FEAT_##_FEAT] = {                        \
        .name = _NAME,                             \
        .type = S390_FEAT_TYPE_##_TYPE,            \
        .bit = _BIT,                               \
        .desc = _DESC,                             \
    },
static const S390FeatDef s390_features[S390_FEAT_MAX] = {
    #include "cpu_features_def.h.inc"
};
#undef DEF_FEAT

const S390FeatDef *s390_feat_def(S390Feat feat)
{
    return &s390_features[feat];
}

S390Feat s390_feat_by_type_and_bit(S390FeatType type, int bit)
{
    S390Feat feat;

    for (feat = 0; feat < ARRAY_SIZE(s390_features); feat++) {
        if (s390_features[feat].type == type &&
            s390_features[feat].bit == bit) {
            return feat;
        }
    }
    return S390_FEAT_MAX;
}

void s390_init_feat_bitmap(const S390FeatInit init, S390FeatBitmap bitmap)
{
    int i, j;

    for (i = 0; i < (S390_FEAT_MAX / 64 + 1); i++) {
        if (init[i]) {
            for (j = 0; j < 64; j++) {
                if (init[i] & 1ULL << j) {
                    set_bit(i * 64 + j, bitmap);
                }
            }
        }
    }
}

void s390_fill_feat_block(const S390FeatBitmap features, S390FeatType type,
                          uint8_t *data)
{
    S390Feat feat;
    int bit_nr;

    switch (type) {
    case S390_FEAT_TYPE_STFL:
        if (test_bit(S390_FEAT_ZARCH, features)) {
            /* Features that are always active */
            set_be_bit(2, data);   /* z/Architecture */
            set_be_bit(138, data); /* Configuration-z-architectural-mode */
        }
        break;
    case S390_FEAT_TYPE_PTFF:
    case S390_FEAT_TYPE_KMAC:
    case S390_FEAT_TYPE_KMC:
    case S390_FEAT_TYPE_KM:
    case S390_FEAT_TYPE_KIMD:
    case S390_FEAT_TYPE_KLMD:
    case S390_FEAT_TYPE_PCKMO:
    case S390_FEAT_TYPE_KMCTR:
    case S390_FEAT_TYPE_KMF:
    case S390_FEAT_TYPE_KMO:
    case S390_FEAT_TYPE_PCC:
    case S390_FEAT_TYPE_PPNO:
    case S390_FEAT_TYPE_KMA:
    case S390_FEAT_TYPE_KDSA:
    case S390_FEAT_TYPE_SORTL:
    case S390_FEAT_TYPE_DFLTCC:
        set_be_bit(0, data); /* query is always available */
        break;
    default:
        break;
    };

    feat = find_first_bit(features, S390_FEAT_MAX);
    while (feat < S390_FEAT_MAX) {
        if (s390_features[feat].type == type) {
            bit_nr = s390_features[feat].bit;
            /* big endian on uint8_t array */
            set_be_bit(bit_nr, data);
        }
        feat = find_next_bit(features, S390_FEAT_MAX, feat + 1);
    }

    if (!s390_is_pv()) {
        return;
    }

    /*
     * Some facilities are not available for CPUs in protected mode:
     * - All SIE facilities because SIE is not available
     * - DIAG318
     *
     * As VMs can move in and out of protected mode the CPU model
     * doesn't protect us from that problem because it is only
     * validated at the start of the VM.
     */
    switch (type) {
    case S390_FEAT_TYPE_SCLP_CPU:
        clear_be_bit(s390_feat_def(S390_FEAT_SIE_F2)->bit, data);
        clear_be_bit(s390_feat_def(S390_FEAT_SIE_SKEY)->bit, data);
        clear_be_bit(s390_feat_def(S390_FEAT_SIE_GPERE)->bit, data);
        clear_be_bit(s390_feat_def(S390_FEAT_SIE_SIIF)->bit, data);
        clear_be_bit(s390_feat_def(S390_FEAT_SIE_SIGPIF)->bit, data);
        clear_be_bit(s390_feat_def(S390_FEAT_SIE_IB)->bit, data);
        clear_be_bit(s390_feat_def(S390_FEAT_SIE_CEI)->bit, data);
        break;
    case S390_FEAT_TYPE_SCLP_CONF_CHAR:
        clear_be_bit(s390_feat_def(S390_FEAT_SIE_GSLS)->bit, data);
        clear_be_bit(s390_feat_def(S390_FEAT_HPMA2)->bit, data);
        clear_be_bit(s390_feat_def(S390_FEAT_SIE_KSS)->bit, data);
        break;
    case S390_FEAT_TYPE_SCLP_CONF_CHAR_EXT:
        clear_be_bit(s390_feat_def(S390_FEAT_SIE_64BSCAO)->bit, data);
        clear_be_bit(s390_feat_def(S390_FEAT_SIE_CMMA)->bit, data);
        clear_be_bit(s390_feat_def(S390_FEAT_SIE_PFMFI)->bit, data);
        clear_be_bit(s390_feat_def(S390_FEAT_SIE_IBS)->bit, data);
        break;
    case S390_FEAT_TYPE_SCLP_FAC134:
        clear_be_bit(s390_feat_def(S390_FEAT_DIAG_318)->bit, data);
        break;
    default:
        return;
    }
}

void s390_add_from_feat_block(S390FeatBitmap features, S390FeatType type,
                              uint8_t *data)
{
    int nr_bits, le_bit;

    switch (type) {
    case S390_FEAT_TYPE_STFL:
       nr_bits = 16384;
       break;
    case S390_FEAT_TYPE_PLO:
    case S390_FEAT_TYPE_SORTL:
    case S390_FEAT_TYPE_DFLTCC:
       nr_bits = 256;
       break;
    default:
       /* all cpu subfunctions have 128 bit */
       nr_bits = 128;
    };

    le_bit = find_first_bit((unsigned long *) data, nr_bits);
    while (le_bit < nr_bits) {
        /* convert the bit number to a big endian bit nr */
        S390Feat feat = s390_feat_by_type_and_bit(type, BE_BIT_NR(le_bit));
        /* ignore unknown bits */
        if (feat < S390_FEAT_MAX) {
            set_bit(feat, features);
        }
        le_bit = find_next_bit((unsigned long *) data, nr_bits, le_bit + 1);
    }
}

void s390_feat_bitmap_to_ascii(const S390FeatBitmap features, void *opaque,
                               void (*fn)(const char *name, void *opaque))
{
    S390FeatBitmap bitmap, tmp;
    S390FeatGroup group;
    S390Feat feat;

    bitmap_copy(bitmap, features, S390_FEAT_MAX);

    /* process whole groups first */
    for (group = 0; group < S390_FEAT_GROUP_MAX; group++) {
        const S390FeatGroupDef *def = s390_feat_group_def(group);

        bitmap_and(tmp, bitmap, def->feat, S390_FEAT_MAX);
        if (bitmap_equal(tmp, def->feat, S390_FEAT_MAX)) {
            bitmap_andnot(bitmap, bitmap, def->feat, S390_FEAT_MAX);
            fn(def->name, opaque);
        }
    }

    /* report leftovers as separate features */
    feat = find_first_bit(bitmap, S390_FEAT_MAX);
    while (feat < S390_FEAT_MAX) {
        fn(s390_feat_def(feat)->name, opaque);
        feat = find_next_bit(bitmap, S390_FEAT_MAX, feat + 1);
    };
}

#define FEAT_GROUP_INIT(_name, _group, _desc)        \
    {                                                \
        .name = _name,                               \
        .desc = _desc,                               \
        .init = { S390_FEAT_GROUP_LIST_ ## _group }, \
    }

/* indexed by feature group number for easy lookup */
static S390FeatGroupDef s390_feature_groups[] = {
    FEAT_GROUP_INIT("plo", PLO, "Perform-locked-operation facility"),
    FEAT_GROUP_INIT("tods", TOD_CLOCK_STEERING, "Tod-clock-steering facility"),
    FEAT_GROUP_INIT("gen13ptff", GEN13_PTFF, "PTFF enhancements introduced with z13"),
    FEAT_GROUP_INIT("msa", MSA, "Message-security-assist facility"),
    FEAT_GROUP_INIT("msa1", MSA_EXT_1, "Message-security-assist-extension 1 facility"),
    FEAT_GROUP_INIT("msa2", MSA_EXT_2, "Message-security-assist-extension 2 facility"),
    FEAT_GROUP_INIT("msa3", MSA_EXT_3, "Message-security-assist-extension 3 facility"),
    FEAT_GROUP_INIT("msa4", MSA_EXT_4, "Message-security-assist-extension 4 facility"),
    FEAT_GROUP_INIT("msa5", MSA_EXT_5, "Message-security-assist-extension 5 facility"),
    FEAT_GROUP_INIT("msa6", MSA_EXT_6, "Message-security-assist-extension 6 facility"),
    FEAT_GROUP_INIT("msa7", MSA_EXT_7, "Message-security-assist-extension 7 facility"),
    FEAT_GROUP_INIT("msa8", MSA_EXT_8, "Message-security-assist-extension 8 facility"),
    FEAT_GROUP_INIT("msa9", MSA_EXT_9, "Message-security-assist-extension 9 facility"),
    FEAT_GROUP_INIT("msa9_pckmo", MSA_EXT_9_PCKMO, "Message-security-assist-extension 9 PCKMO subfunctions"),
    FEAT_GROUP_INIT("mepochptff", MULTIPLE_EPOCH_PTFF, "PTFF enhancements introduced with Multiple-epoch facility"),
    FEAT_GROUP_INIT("esort", ENH_SORT, "Enhanced-sort facility"),
    FEAT_GROUP_INIT("deflate", DEFLATE_CONVERSION, "Deflate-conversion facility"),
};

const S390FeatGroupDef *s390_feat_group_def(S390FeatGroup group)
{
    return &s390_feature_groups[group];
}

static void init_groups(void)
{
    int i;

    /* init all bitmaps from gnerated data initially */
    for (i = 0; i < ARRAY_SIZE(s390_feature_groups); i++) {
        s390_init_feat_bitmap(s390_feature_groups[i].init,
                              s390_feature_groups[i].feat);
    }
}

type_init(init_groups)
