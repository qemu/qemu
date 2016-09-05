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

#ifndef TARGET_S390X_CPU_MODELS_H
#define TARGET_S390X_CPU_MODELS_H

#include "cpu_features.h"
#include "qom/cpu.h"

/* static CPU definition */
typedef struct S390CPUDef {
    const char *name;       /* name exposed to the user */
    const char *desc;       /* description exposed to the user */
    uint8_t gen;            /* hw generation identification */
    uint16_t type;          /* cpu type identification */
    uint8_t ec_ga;          /* EC GA version (on which also the BC is based) */
    uint8_t mha_pow;        /* Maximum Host Adress Power, mha = 2^pow-1 */
    uint32_t hmfai;         /* hypervisor-managed facilities */
    /* base/min features, must never be changed between QEMU versions */
    S390FeatBitmap base_feat;
    /* used to init base_feat from generated data */
    S390FeatInit base_init;
    /* deafault features, QEMU version specific */
    S390FeatBitmap default_feat;
    /* used to init default_feat from generated data */
    S390FeatInit default_init;
    /* max allowed features, QEMU version specific */
    S390FeatBitmap full_feat;
    /* used to init full_feat from generated data */
    S390FeatInit full_init;
} S390CPUDef;

/* CPU model based on a CPU definition */
typedef struct S390CPUModel {
    const S390CPUDef *def;
    S390FeatBitmap features;
    /* values copied from the "host" model, can change during migration */
    uint16_t lowest_ibc;    /* lowest IBC that the hardware supports */
    uint32_t cpu_id;        /* CPU id */
    uint8_t cpu_ver;        /* CPU version, usually "ff" for kvm */
} S390CPUModel;

void s390_get_feat_block(S390FeatType type, uint8_t *data);
bool s390_has_feat(S390Feat feat);

#endif /* TARGET_S390X_CPU_MODELS_H */
