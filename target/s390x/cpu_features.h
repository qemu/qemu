/*
 * CPU features/facilities helper structs and utility functions for s390
 *
 * Copyright 2016 IBM Corp.
 *
 * Author(s): Michael Mueller <mimu@linux.vnet.ibm.com>
 *            David Hildenbrand <dahi@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */

#ifndef TARGET_S390X_CPU_FEATURES_H
#define TARGET_S390X_CPU_FEATURES_H

#include "qemu/bitmap.h"
#include "cpu_features_def.h"
#include "gen-features.h"

/* CPU features are announced via different ways */
typedef enum {
    S390_FEAT_TYPE_STFL,
    S390_FEAT_TYPE_SCLP_CONF_CHAR,
    S390_FEAT_TYPE_SCLP_CONF_CHAR_EXT,
    S390_FEAT_TYPE_SCLP_CPU,
    S390_FEAT_TYPE_MISC,
    S390_FEAT_TYPE_PLO,
    S390_FEAT_TYPE_PTFF,
    S390_FEAT_TYPE_KMAC,
    S390_FEAT_TYPE_KMC,
    S390_FEAT_TYPE_KM,
    S390_FEAT_TYPE_KIMD,
    S390_FEAT_TYPE_KLMD,
    S390_FEAT_TYPE_PCKMO,
    S390_FEAT_TYPE_KMCTR,
    S390_FEAT_TYPE_KMF,
    S390_FEAT_TYPE_KMO,
    S390_FEAT_TYPE_PCC,
    S390_FEAT_TYPE_PPNO,
    S390_FEAT_TYPE_KMA,
    S390_FEAT_TYPE_KDSA,
    S390_FEAT_TYPE_SORTL,
    S390_FEAT_TYPE_DFLTCC,
} S390FeatType;

/* Definition of a CPU feature */
typedef struct {
    const char *name;       /* name exposed to the user */
    const char *desc;       /* description exposed to the user */
    S390FeatType type;      /* feature type (way of indication)*/
    int bit;                /* bit within the feature type area (fixed) */
} S390FeatDef;

/* use ordinary bitmap operations to work with features */
typedef unsigned long S390FeatBitmap[BITS_TO_LONGS(S390_FEAT_MAX)];

/* 64bit based bitmap used to init S390FeatBitmap from generated data */
typedef uint64_t S390FeatInit[S390_FEAT_MAX / 64 + 1];

const S390FeatDef *s390_feat_def(S390Feat feat);
S390Feat s390_feat_by_type_and_bit(S390FeatType type, int bit);
void s390_init_feat_bitmap(const S390FeatInit init, S390FeatBitmap bitmap);
void s390_fill_feat_block(const S390FeatBitmap features, S390FeatType type,
                          uint8_t *data);
void s390_add_from_feat_block(S390FeatBitmap features, S390FeatType type,
                          uint8_t *data);
void s390_feat_bitmap_to_ascii(const S390FeatBitmap features, void *opaque,
                               void (*fn)(const char *name, void *opaque));

/* Definition of a CPU feature group */
typedef struct {
    const char *name;       /* name exposed to the user */
    const char *desc;       /* description exposed to the user */
    S390FeatBitmap feat;    /* features contained in the group */
    S390FeatInit init;      /* used to init feat from generated data */
} S390FeatGroupDef;

const S390FeatGroupDef *s390_feat_group_def(S390FeatGroup group);

#define BE_BIT_NR(BIT) (BIT ^ (BITS_PER_LONG - 1))

static inline void set_be_bit(unsigned int bit_nr, uint8_t *array)
{
    array[bit_nr / 8] |= 0x80 >> (bit_nr % 8);
}
static inline bool test_be_bit(unsigned int bit_nr, const uint8_t *array)
{
    return array[bit_nr / 8] & (0x80 >> (bit_nr % 8));
}
#endif /* TARGET_S390X_CPU_FEATURES_H */
