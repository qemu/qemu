/*
 * CPU features/facilities for s390
 *
 * Copyright IBM Corp. 2016, 2018
 * Copyright Red Hat, Inc. 2019
 *
 * Author(s): Michael Mueller <mimu@linux.vnet.ibm.com>
 *            David Hildenbrand <david@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */

#ifndef TARGET_S390X_CPU_FEATURES_DEF_H
#define TARGET_S390X_CPU_FEATURES_DEF_H

#define DEF_FEAT(_FEAT, ...) S390_FEAT_##_FEAT,
typedef enum {
    #include "cpu_features_def.h.inc"
    S390_FEAT_MAX,
} S390Feat;
#undef DEF_FEAT

#endif /* TARGET_S390X_CPU_FEATURES_DEF_H */
