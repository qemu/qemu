/*
 * S390 feature list generator
 *
 * Copyright IBM Corp. 2016, 2018
 *
 * Author(s): Michael Mueller <mimu@linux.vnet.ibm.com>
 *            David Hildenbrand <dahi@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include "cpu_features_def.h"

#define ARRAY_SIZE(array) (sizeof(array) / sizeof(array[0]))

/***** BEGIN FEATURE DEFS *****/

#define S390_FEAT_GROUP_PLO \
    S390_FEAT_PLO_CL, \
    S390_FEAT_PLO_CLG, \
    S390_FEAT_PLO_CLGR, \
    S390_FEAT_PLO_CLX, \
    S390_FEAT_PLO_CS, \
    S390_FEAT_PLO_CSG, \
    S390_FEAT_PLO_CSGR, \
    S390_FEAT_PLO_CSX, \
    S390_FEAT_PLO_DCS, \
    S390_FEAT_PLO_DCSG, \
    S390_FEAT_PLO_DCSGR, \
    S390_FEAT_PLO_DCSX, \
    S390_FEAT_PLO_CSST, \
    S390_FEAT_PLO_CSSTG, \
    S390_FEAT_PLO_CSSTGR, \
    S390_FEAT_PLO_CSSTX, \
    S390_FEAT_PLO_CSDST, \
    S390_FEAT_PLO_CSDSTG, \
    S390_FEAT_PLO_CSDSTGR, \
    S390_FEAT_PLO_CSDSTX, \
    S390_FEAT_PLO_CSTST, \
    S390_FEAT_PLO_CSTSTG, \
    S390_FEAT_PLO_CSTSTGR, \
    S390_FEAT_PLO_CSTSTX

#define S390_FEAT_GROUP_TOD_CLOCK_STEERING \
    S390_FEAT_TOD_CLOCK_STEERING, \
    S390_FEAT_PTFF_QTO, \
    S390_FEAT_PTFF_QSI, \
    S390_FEAT_PTFF_QPT, \
    S390_FEAT_PTFF_STO

#define S390_FEAT_GROUP_GEN13_PTFF \
    S390_FEAT_PTFF_QUI, \
    S390_FEAT_PTFF_QTOU, \
    S390_FEAT_PTFF_STOU

#define S390_FEAT_GROUP_MULTIPLE_EPOCH_PTFF \
    S390_FEAT_PTFF_QSIE, \
    S390_FEAT_PTFF_QTOUE, \
    S390_FEAT_PTFF_STOE, \
    S390_FEAT_PTFF_STOUE

#define S390_FEAT_GROUP_MSA \
    S390_FEAT_MSA, \
    S390_FEAT_KMAC_DEA, \
    S390_FEAT_KMAC_TDEA_128, \
    S390_FEAT_KMAC_TDEA_192, \
    S390_FEAT_KMC_DEA, \
    S390_FEAT_KMC_TDEA_128, \
    S390_FEAT_KMC_TDEA_192, \
    S390_FEAT_KM_DEA, \
    S390_FEAT_KM_TDEA_128, \
    S390_FEAT_KM_TDEA_192, \
    S390_FEAT_KIMD_SHA_1, \
    S390_FEAT_KLMD_SHA_1

#define S390_FEAT_GROUP_MSA_EXT_1 \
    S390_FEAT_KMC_AES_128, \
    S390_FEAT_KM_AES_128, \
    S390_FEAT_KIMD_SHA_256, \
    S390_FEAT_KLMD_SHA_256

#define S390_FEAT_GROUP_MSA_EXT_2 \
    S390_FEAT_KMC_AES_192, \
    S390_FEAT_KMC_AES_256, \
    S390_FEAT_KMC_PRNG, \
    S390_FEAT_KM_AES_192, \
    S390_FEAT_KM_AES_256, \
    S390_FEAT_KIMD_SHA_512, \
    S390_FEAT_KLMD_SHA_512

#define S390_FEAT_GROUP_MSA_EXT_3 \
    S390_FEAT_MSA_EXT_3, \
    S390_FEAT_KMAC_EDEA, \
    S390_FEAT_KMAC_ETDEA_128, \
    S390_FEAT_KMAC_ETDEA_192, \
    S390_FEAT_KMC_EAES_128, \
    S390_FEAT_KMC_EAES_192, \
    S390_FEAT_KMC_EAES_256, \
    S390_FEAT_KMC_EDEA, \
    S390_FEAT_KMC_ETDEA_128, \
    S390_FEAT_KMC_ETDEA_192, \
    S390_FEAT_KM_EDEA, \
    S390_FEAT_KM_ETDEA_128, \
    S390_FEAT_KM_ETDEA_192, \
    S390_FEAT_KM_EAES_128, \
    S390_FEAT_KM_EAES_192, \
    S390_FEAT_KM_EAES_256, \
    S390_FEAT_PCKMO_EDEA, \
    S390_FEAT_PCKMO_ETDEA_128, \
    S390_FEAT_PCKMO_ETDEA_256, \
    S390_FEAT_PCKMO_AES_128, \
    S390_FEAT_PCKMO_AES_192, \
    S390_FEAT_PCKMO_AES_256

#define S390_FEAT_GROUP_MSA_EXT_4 \
    S390_FEAT_MSA_EXT_4, \
    S390_FEAT_KMAC_AES_128, \
    S390_FEAT_KMAC_AES_192, \
    S390_FEAT_KMAC_AES_256, \
    S390_FEAT_KMAC_EAES_128, \
    S390_FEAT_KMAC_EAES_192, \
    S390_FEAT_KMAC_EAES_256, \
    S390_FEAT_KM_XTS_AES_128, \
    S390_FEAT_KM_XTS_AES_256, \
    S390_FEAT_KM_XTS_EAES_128, \
    S390_FEAT_KM_XTS_EAES_256, \
    S390_FEAT_KIMD_GHASH, \
    S390_FEAT_KMCTR_DEA, \
    S390_FEAT_KMCTR_TDEA_128, \
    S390_FEAT_KMCTR_TDEA_192, \
    S390_FEAT_KMCTR_EDEA, \
    S390_FEAT_KMCTR_ETDEA_128, \
    S390_FEAT_KMCTR_ETDEA_192, \
    S390_FEAT_KMCTR_AES_128, \
    S390_FEAT_KMCTR_AES_192, \
    S390_FEAT_KMCTR_AES_256, \
    S390_FEAT_KMCTR_EAES_128, \
    S390_FEAT_KMCTR_EAES_192, \
    S390_FEAT_KMCTR_EAES_256, \
    S390_FEAT_KMF_DEA, \
    S390_FEAT_KMF_TDEA_128, \
    S390_FEAT_KMF_TDEA_192, \
    S390_FEAT_KMF_EDEA, \
    S390_FEAT_KMF_ETDEA_128, \
    S390_FEAT_KMF_ETDEA_192, \
    S390_FEAT_KMF_AES_128, \
    S390_FEAT_KMF_AES_192, \
    S390_FEAT_KMF_AES_256, \
    S390_FEAT_KMF_EAES_128, \
    S390_FEAT_KMF_EAES_192, \
    S390_FEAT_KMF_EAES_256, \
    S390_FEAT_KMO_DEA, \
    S390_FEAT_KMO_TDEA_128, \
    S390_FEAT_KMO_TDEA_192, \
    S390_FEAT_KMO_EDEA, \
    S390_FEAT_KMO_ETDEA_128, \
    S390_FEAT_KMO_ETDEA_192, \
    S390_FEAT_KMO_AES_128, \
    S390_FEAT_KMO_AES_192, \
    S390_FEAT_KMO_AES_256, \
    S390_FEAT_KMO_EAES_128, \
    S390_FEAT_KMO_EAES_192, \
    S390_FEAT_KMO_EAES_256, \
    S390_FEAT_PCC_CMAC_DEA, \
    S390_FEAT_PCC_CMAC_TDEA_128, \
    S390_FEAT_PCC_CMAC_TDEA_192, \
    S390_FEAT_PCC_CMAC_ETDEA_128, \
    S390_FEAT_PCC_CMAC_ETDEA_192, \
    S390_FEAT_PCC_CMAC_TDEA, \
    S390_FEAT_PCC_CMAC_AES_128, \
    S390_FEAT_PCC_CMAC_AES_192, \
    S390_FEAT_PCC_CMAC_AES_256, \
    S390_FEAT_PCC_CMAC_EAES_128, \
    S390_FEAT_PCC_CMAC_EAES_192, \
    S390_FEAT_PCC_CMAC_EAES_256, \
    S390_FEAT_PCC_XTS_AES_128, \
    S390_FEAT_PCC_XTS_AES_256, \
    S390_FEAT_PCC_XTS_EAES_128, \
    S390_FEAT_PCC_XTS_EAES_256

#define S390_FEAT_GROUP_MSA_EXT_5 \
    S390_FEAT_MSA_EXT_5, \
    S390_FEAT_PPNO_SHA_512_DRNG

#define S390_FEAT_GROUP_MSA_EXT_6 \
    S390_FEAT_KIMD_SHA3_224, \
    S390_FEAT_KIMD_SHA3_256, \
    S390_FEAT_KIMD_SHA3_384, \
    S390_FEAT_KIMD_SHA3_512, \
    S390_FEAT_KIMD_SHAKE_128, \
    S390_FEAT_KIMD_SHAKE_256, \
    S390_FEAT_KLMD_SHA3_224, \
    S390_FEAT_KLMD_SHA3_256, \
    S390_FEAT_KLMD_SHA3_384, \
    S390_FEAT_KLMD_SHA3_512, \
    S390_FEAT_KLMD_SHAKE_128, \
    S390_FEAT_KLMD_SHAKE_256

#define S390_FEAT_GROUP_MSA_EXT_7 \
    S390_FEAT_PRNO_TRNG_QRTCR, \
    S390_FEAT_PRNO_TRNG

#define S390_FEAT_GROUP_MSA_EXT_8 \
    S390_FEAT_MSA_EXT_8, \
    S390_FEAT_KMA_GCM_AES_128, \
    S390_FEAT_KMA_GCM_AES_192, \
    S390_FEAT_KMA_GCM_AES_256 , \
    S390_FEAT_KMA_GCM_EAES_128, \
    S390_FEAT_KMA_GCM_EAES_192, \
    S390_FEAT_KMA_GCM_EAES_256

#define S390_FEAT_GROUP_MSA_EXT_9 \
    S390_FEAT_MSA_EXT_9, \
    S390_FEAT_KDSA_ECDSA_VERIFY_P256, \
    S390_FEAT_KDSA_ECDSA_VERIFY_P384, \
    S390_FEAT_KDSA_ECDSA_VERIFY_P512, \
    S390_FEAT_KDSA_ECDSA_SIGN_P256, \
    S390_FEAT_KDSA_ECDSA_SIGN_P384, \
    S390_FEAT_KDSA_ECDSA_SIGN_P512, \
    S390_FEAT_KDSA_EECDSA_SIGN_P256, \
    S390_FEAT_KDSA_EECDSA_SIGN_P384, \
    S390_FEAT_KDSA_EECDSA_SIGN_P512, \
    S390_FEAT_KDSA_EDDSA_VERIFY_ED25519, \
    S390_FEAT_KDSA_EDDSA_VERIFY_ED448, \
    S390_FEAT_KDSA_EDDSA_SIGN_ED25519, \
    S390_FEAT_KDSA_EDDSA_SIGN_ED448, \
    S390_FEAT_KDSA_EEDDSA_SIGN_ED25519, \
    S390_FEAT_KDSA_EEDDSA_SIGN_ED448, \
    S390_FEAT_PCC_SCALAR_MULT_P256, \
    S390_FEAT_PCC_SCALAR_MULT_P384, \
    S390_FEAT_PCC_SCALAR_MULT_P512, \
    S390_FEAT_PCC_SCALAR_MULT_ED25519, \
    S390_FEAT_PCC_SCALAR_MULT_ED448, \
    S390_FEAT_PCC_SCALAR_MULT_X25519, \
    S390_FEAT_PCC_SCALAR_MULT_X448

#define S390_FEAT_GROUP_MSA_EXT_9_PCKMO \
    S390_FEAT_PCKMO_ECC_P256, \
    S390_FEAT_PCKMO_ECC_P384, \
    S390_FEAT_PCKMO_ECC_P521, \
    S390_FEAT_PCKMO_ECC_ED25519, \
    S390_FEAT_PCKMO_ECC_ED448

#define S390_FEAT_GROUP_ENH_SORT \
    S390_FEAT_ESORT_BASE, \
    S390_FEAT_SORTL_SFLR, \
    S390_FEAT_SORTL_SVLR, \
    S390_FEAT_SORTL_32, \
    S390_FEAT_SORTL_128, \
    S390_FEAT_SORTL_F0


#define S390_FEAT_GROUP_DEFLATE_CONVERSION \
    S390_FEAT_DEFLATE_BASE, \
    S390_FEAT_DEFLATE_GHDT, \
    S390_FEAT_DEFLATE_CMPR, \
    S390_FEAT_DEFLATE_XPND, \
    S390_FEAT_DEFLATE_F0

/* cpu feature groups */
static uint16_t group_PLO[] = {
    S390_FEAT_GROUP_PLO,
};
static uint16_t group_TOD_CLOCK_STEERING[] = {
    S390_FEAT_GROUP_TOD_CLOCK_STEERING,
};
static uint16_t group_GEN13_PTFF[] = {
    S390_FEAT_GROUP_GEN13_PTFF,
};
static uint16_t group_MULTIPLE_EPOCH_PTFF[] = {
    S390_FEAT_GROUP_MULTIPLE_EPOCH_PTFF,
};
static uint16_t group_MSA[] = {
    S390_FEAT_GROUP_MSA,
};
static uint16_t group_MSA_EXT_1[] = {
    S390_FEAT_GROUP_MSA_EXT_1,
};
static uint16_t group_MSA_EXT_2[] = {
    S390_FEAT_GROUP_MSA_EXT_2,
};
static uint16_t group_MSA_EXT_3[] = {
    S390_FEAT_GROUP_MSA_EXT_3,
};
static uint16_t group_MSA_EXT_4[] = {
    S390_FEAT_GROUP_MSA_EXT_4,
};
static uint16_t group_MSA_EXT_5[] = {
    S390_FEAT_GROUP_MSA_EXT_5,
};
static uint16_t group_MSA_EXT_6[] = {
    S390_FEAT_GROUP_MSA_EXT_6,
};
static uint16_t group_MSA_EXT_7[] = {
    S390_FEAT_GROUP_MSA_EXT_7,
};
static uint16_t group_MSA_EXT_8[] = {
    S390_FEAT_GROUP_MSA_EXT_8,
};

static uint16_t group_MSA_EXT_9[] = {
    S390_FEAT_GROUP_MSA_EXT_9,
};

static uint16_t group_MSA_EXT_9_PCKMO[] = {
    S390_FEAT_GROUP_MSA_EXT_9_PCKMO,
};

static uint16_t group_ENH_SORT[] = {
    S390_FEAT_GROUP_ENH_SORT,
};

static uint16_t group_DEFLATE_CONVERSION[] = {
    S390_FEAT_GROUP_DEFLATE_CONVERSION,
};

/* Base features (in order of release)
 * Only non-hypervisor managed features belong here.
 * Base feature sets are static meaning they do not change in future QEMU
 * releases.
 */
static uint16_t base_GEN7_GA1[] = {
    S390_FEAT_GROUP_PLO,
    S390_FEAT_ESAN3,
    S390_FEAT_ZARCH,
};

#define base_GEN7_GA2 EmptyFeat
#define base_GEN7_GA3 EmptyFeat

static uint16_t base_GEN8_GA1[] = {
    S390_FEAT_DAT_ENH,
    S390_FEAT_EXTENDED_TRANSLATION_2,
    S390_FEAT_GROUP_MSA,
    S390_FEAT_LONG_DISPLACEMENT,
    S390_FEAT_LONG_DISPLACEMENT_FAST,
    S390_FEAT_HFP_MADDSUB,
};

#define base_GEN8_GA2 EmptyFeat
#define base_GEN8_GA3 EmptyFeat
#define base_GEN8_GA4 EmptyFeat
#define base_GEN8_GA5 EmptyFeat

static uint16_t base_GEN9_GA1[] = {
    S390_FEAT_IDTE_SEGMENT,
    S390_FEAT_ASN_LX_REUSE,
    S390_FEAT_STFLE,
    S390_FEAT_SENSE_RUNNING_STATUS,
    S390_FEAT_EXTENDED_IMMEDIATE,
    S390_FEAT_EXTENDED_TRANSLATION_3,
    S390_FEAT_HFP_UNNORMALIZED_EXT,
    S390_FEAT_ETF2_ENH,
    S390_FEAT_STORE_CLOCK_FAST,
    S390_FEAT_GROUP_TOD_CLOCK_STEERING,
    S390_FEAT_ETF3_ENH,
    S390_FEAT_DAT_ENH_2,
};

#define base_GEN9_GA2 EmptyFeat
#define base_GEN9_GA3 EmptyFeat

static uint16_t base_GEN10_GA1[] = {
    S390_FEAT_CONDITIONAL_SSKE,
    S390_FEAT_PARSING_ENH,
    S390_FEAT_MOVE_WITH_OPTIONAL_SPEC,
    S390_FEAT_EXTRACT_CPU_TIME,
    S390_FEAT_COMPARE_AND_SWAP_AND_STORE,
    S390_FEAT_COMPARE_AND_SWAP_AND_STORE_2,
    S390_FEAT_GENERAL_INSTRUCTIONS_EXT,
    S390_FEAT_EXECUTE_EXT,
    S390_FEAT_FLOATING_POINT_SUPPORT_ENH,
    S390_FEAT_DFP,
    S390_FEAT_DFP_FAST,
    S390_FEAT_PFPO,
};
#define base_GEN10_GA2 EmptyFeat
#define base_GEN10_GA3 EmptyFeat

static uint16_t base_GEN11_GA1[] = {
    S390_FEAT_NONQ_KEY_SETTING,
    S390_FEAT_ENHANCED_MONITOR,
    S390_FEAT_FLOATING_POINT_EXT,
    S390_FEAT_SET_PROGRAM_PARAMETERS,
    S390_FEAT_STFLE_45,
    S390_FEAT_CMPSC_ENH,
    S390_FEAT_INTERLOCKED_ACCESS_2,
};

#define base_GEN11_GA2 EmptyFeat

static uint16_t base_GEN12_GA1[] = {
    S390_FEAT_DFP_ZONED_CONVERSION,
    S390_FEAT_STFLE_49,
    S390_FEAT_LOCAL_TLB_CLEARING,
};

#define base_GEN12_GA2 EmptyFeat

static uint16_t base_GEN13_GA1[] = {
    S390_FEAT_STFLE_53,
    S390_FEAT_DFP_PACKED_CONVERSION,
    S390_FEAT_GROUP_GEN13_PTFF,
};

#define base_GEN13_GA2 EmptyFeat

static uint16_t base_GEN14_GA1[] = {
    S390_FEAT_ENTROPY_ENC_COMP,
    S390_FEAT_MISC_INSTRUCTION_EXT2,
    S390_FEAT_SEMAPHORE_ASSIST,
    S390_FEAT_TIME_SLICE_INSTRUMENTATION,
    S390_FEAT_ORDER_PRESERVING_COMPRESSION,
};

#define base_GEN14_GA2 EmptyFeat

static uint16_t base_GEN15_GA1[] = {
    S390_FEAT_MISC_INSTRUCTION_EXT3,
};

#define base_GEN16_GA1 EmptyFeat

/* Full features (in order of release)
 * Automatically includes corresponding base features.
 * Full features are all features this hardware supports even if kvm/QEMU do not
 * support these features yet.
 */
static uint16_t full_GEN7_GA1[] = {
    S390_FEAT_PPA15,
    S390_FEAT_BPB,
    S390_FEAT_SIE_F2,
    S390_FEAT_SIE_SKEY,
    S390_FEAT_SIE_GPERE,
    S390_FEAT_SIE_IB,
    S390_FEAT_SIE_CEI,
};

static uint16_t full_GEN7_GA2[] = {
    S390_FEAT_EXTENDED_TRANSLATION_2,
};

static uint16_t full_GEN7_GA3[] = {
    S390_FEAT_LONG_DISPLACEMENT,
    S390_FEAT_SIE_SIIF,
};

static uint16_t full_GEN8_GA1[] = {
    S390_FEAT_SIE_GSLS,
    S390_FEAT_SIE_64BSCAO,
};

#define full_GEN8_GA2 EmptyFeat

static uint16_t full_GEN8_GA3[] = {
    S390_FEAT_ASN_LX_REUSE,
    S390_FEAT_EXTENDED_TRANSLATION_3,
};

#define full_GEN8_GA4 EmptyFeat
#define full_GEN8_GA5 EmptyFeat

static uint16_t full_GEN9_GA1[] = {
    S390_FEAT_STORE_HYPERVISOR_INFO,
    S390_FEAT_GROUP_MSA_EXT_1,
    S390_FEAT_CMM,
    S390_FEAT_SIE_CMMA,
};

static uint16_t full_GEN9_GA2[] = {
    S390_FEAT_MOVE_WITH_OPTIONAL_SPEC,
    S390_FEAT_EXTRACT_CPU_TIME,
    S390_FEAT_COMPARE_AND_SWAP_AND_STORE,
    S390_FEAT_FLOATING_POINT_SUPPORT_ENH,
    S390_FEAT_DFP,
};

static uint16_t full_GEN9_GA3[] = {
    S390_FEAT_CONDITIONAL_SSKE,
    S390_FEAT_PFPO,
};

static uint16_t full_GEN10_GA1[] = {
    S390_FEAT_EDAT,
    S390_FEAT_CONFIGURATION_TOPOLOGY,
    S390_FEAT_GROUP_MSA_EXT_2,
    S390_FEAT_ESOP,
    S390_FEAT_SIE_PFMFI,
    S390_FEAT_SIE_SIGPIF,
};

static uint16_t full_GEN10_GA2[] = {
    S390_FEAT_SET_PROGRAM_PARAMETERS,
    S390_FEAT_SIE_IBS,
};

static uint16_t full_GEN10_GA3[] = {
    S390_FEAT_GROUP_MSA_EXT_3,
};

static uint16_t full_GEN11_GA1[] = {
    S390_FEAT_IPTE_RANGE,
    S390_FEAT_ACCESS_EXCEPTION_FS_INDICATION,
    S390_FEAT_GROUP_MSA_EXT_4,
};

#define full_GEN11_GA2 EmptyFeat

static uint16_t full_GEN12_GA1[] = {
    S390_FEAT_CONSTRAINT_TRANSACTIONAL_EXE,
    S390_FEAT_TRANSACTIONAL_EXE,
    S390_FEAT_RUNTIME_INSTRUMENTATION,
    S390_FEAT_ZPCI,
    S390_FEAT_ADAPTER_EVENT_NOTIFICATION,
    S390_FEAT_ADAPTER_INT_SUPPRESSION,
    S390_FEAT_EDAT_2,
    S390_FEAT_SIDE_EFFECT_ACCESS_ESOP2,
    S390_FEAT_AP_QUERY_CONFIG_INFO,
    S390_FEAT_AP_QUEUE_INTERRUPT_CONTROL,
    S390_FEAT_AP_FACILITIES_TEST,
    S390_FEAT_AP,
    S390_FEAT_EXTENDED_LENGTH_SCCB,
    S390_FEAT_DIAG_318,
};

static uint16_t full_GEN12_GA2[] = {
    S390_FEAT_GROUP_MSA_EXT_5,
};

static uint16_t full_GEN13_GA1[] = {
    S390_FEAT_VECTOR,
};

#define full_GEN13_GA2 EmptyFeat

static uint16_t full_GEN14_GA1[] = {
    S390_FEAT_INSTRUCTION_EXEC_PROT,
    S390_FEAT_GUARDED_STORAGE,
    S390_FEAT_VECTOR_PACKED_DECIMAL,
    S390_FEAT_VECTOR_ENH,
    S390_FEAT_MULTIPLE_EPOCH,
    S390_FEAT_TEST_PENDING_EXT_INTERRUPTION,
    S390_FEAT_INSERT_REFERENCE_BITS_MULT,
    S390_FEAT_GROUP_MSA_EXT_6,
    S390_FEAT_GROUP_MSA_EXT_7,
    S390_FEAT_GROUP_MSA_EXT_8,
    S390_FEAT_CMM_NT,
    S390_FEAT_ETOKEN,
    S390_FEAT_HPMA2,
    S390_FEAT_SIE_KSS,
    S390_FEAT_GROUP_MULTIPLE_EPOCH_PTFF,
};

#define full_GEN14_GA2 EmptyFeat

static uint16_t full_GEN15_GA1[] = {
    S390_FEAT_VECTOR_ENH2,
    S390_FEAT_GROUP_ENH_SORT,
    S390_FEAT_GROUP_DEFLATE_CONVERSION,
    S390_FEAT_VECTOR_PACKED_DECIMAL_ENH,
    S390_FEAT_GROUP_MSA_EXT_9,
    S390_FEAT_GROUP_MSA_EXT_9_PCKMO,
    S390_FEAT_ETOKEN,
    S390_FEAT_UNPACK,
};

static uint16_t full_GEN16_GA1[] = {
    S390_FEAT_NNPA,
    S390_FEAT_VECTOR_PACKED_DECIMAL_ENH2,
    S390_FEAT_BEAR_ENH,
    S390_FEAT_RDP,
    S390_FEAT_PAI,
    S390_FEAT_PAIE,
    S390_FEAT_UV_FEAT_AP,
    S390_FEAT_UV_FEAT_AP_INTR,
};


/* Default features (in order of release)
 * Automatically includes corresponding base features.
 * Default features are all features this version of QEMU supports for this
 * hardware model. Default feature sets can grow with new QEMU releases.
 */
#define default_GEN7_GA1 EmptyFeat
#define default_GEN7_GA2 EmptyFeat
#define default_GEN7_GA3 EmptyFeat
#define default_GEN8_GA1 EmptyFeat
#define default_GEN8_GA2 EmptyFeat
#define default_GEN8_GA3 EmptyFeat
#define default_GEN8_GA4 EmptyFeat
#define default_GEN8_GA5 EmptyFeat

static uint16_t default_GEN9_GA1[] = {
    S390_FEAT_STORE_HYPERVISOR_INFO,
    S390_FEAT_GROUP_MSA_EXT_1,
    S390_FEAT_CMM,
};

#define default_GEN9_GA2 EmptyFeat
#define default_GEN9_GA3 EmptyFeat

static uint16_t default_GEN10_GA1[] = {
    S390_FEAT_EDAT,
    S390_FEAT_GROUP_MSA_EXT_2,
};

#define default_GEN10_GA2 EmptyFeat
#define default_GEN10_GA3 EmptyFeat

static uint16_t default_GEN11_GA1[] = {
    S390_FEAT_GROUP_MSA_EXT_3,
    S390_FEAT_IPTE_RANGE,
    S390_FEAT_ACCESS_EXCEPTION_FS_INDICATION,
    S390_FEAT_GROUP_MSA_EXT_4,
    S390_FEAT_PPA15,
    S390_FEAT_BPB,
};

#define default_GEN11_GA2 EmptyFeat

static uint16_t default_GEN12_GA1[] = {
    S390_FEAT_CONSTRAINT_TRANSACTIONAL_EXE,
    S390_FEAT_TRANSACTIONAL_EXE,
    S390_FEAT_RUNTIME_INSTRUMENTATION,
    S390_FEAT_ZPCI,
    S390_FEAT_ADAPTER_EVENT_NOTIFICATION,
    S390_FEAT_EDAT_2,
    S390_FEAT_ESOP,
    S390_FEAT_SIDE_EFFECT_ACCESS_ESOP2,
};

#define default_GEN12_GA2 EmptyFeat

static uint16_t default_GEN13_GA1[] = {
    S390_FEAT_GROUP_MSA_EXT_5,
    S390_FEAT_VECTOR,
};

#define default_GEN13_GA2 EmptyFeat

static uint16_t default_GEN14_GA1[] = {
    S390_FEAT_INSTRUCTION_EXEC_PROT,
    S390_FEAT_GUARDED_STORAGE,
    S390_FEAT_VECTOR_PACKED_DECIMAL,
    S390_FEAT_VECTOR_ENH,
    S390_FEAT_GROUP_MSA_EXT_6,
    S390_FEAT_GROUP_MSA_EXT_7,
    S390_FEAT_GROUP_MSA_EXT_8,
    S390_FEAT_MULTIPLE_EPOCH,
    S390_FEAT_GROUP_MULTIPLE_EPOCH_PTFF,
};

#define default_GEN14_GA2 EmptyFeat

static uint16_t default_GEN15_GA1[] = {
    S390_FEAT_VECTOR_ENH2,
    S390_FEAT_GROUP_DEFLATE_CONVERSION,
    S390_FEAT_VECTOR_PACKED_DECIMAL_ENH,
    S390_FEAT_GROUP_MSA_EXT_9,
    S390_FEAT_GROUP_MSA_EXT_9_PCKMO,
    S390_FEAT_ETOKEN,
};

static uint16_t default_GEN16_GA1[] = {
    S390_FEAT_NNPA,
    S390_FEAT_VECTOR_PACKED_DECIMAL_ENH2,
    S390_FEAT_BEAR_ENH,
    S390_FEAT_RDP,
    S390_FEAT_PAI,
    S390_FEAT_PAIE,
};

/* QEMU (CPU model) features */

static uint16_t qemu_V2_11[] = {
    S390_FEAT_GROUP_PLO,
    S390_FEAT_ESAN3,
    S390_FEAT_ZARCH,
};

static uint16_t qemu_V3_1[] = {
    S390_FEAT_DAT_ENH,
    S390_FEAT_IDTE_SEGMENT,
    S390_FEAT_STFLE,
    S390_FEAT_SENSE_RUNNING_STATUS,
    S390_FEAT_EXTENDED_TRANSLATION_2,
    S390_FEAT_MSA,
    S390_FEAT_LONG_DISPLACEMENT,
    S390_FEAT_LONG_DISPLACEMENT_FAST,
    S390_FEAT_EXTENDED_IMMEDIATE,
    S390_FEAT_EXTENDED_TRANSLATION_3,
    S390_FEAT_ETF2_ENH,
    S390_FEAT_STORE_CLOCK_FAST,
    S390_FEAT_MOVE_WITH_OPTIONAL_SPEC,
    S390_FEAT_ETF3_ENH,
    S390_FEAT_EXTRACT_CPU_TIME,
    S390_FEAT_COMPARE_AND_SWAP_AND_STORE,
    S390_FEAT_COMPARE_AND_SWAP_AND_STORE_2,
    S390_FEAT_GENERAL_INSTRUCTIONS_EXT,
    S390_FEAT_EXECUTE_EXT,
    S390_FEAT_SET_PROGRAM_PARAMETERS,
    S390_FEAT_FLOATING_POINT_SUPPORT_ENH,
    S390_FEAT_STFLE_45,
    S390_FEAT_STFLE_49,
    S390_FEAT_LOCAL_TLB_CLEARING,
    S390_FEAT_INTERLOCKED_ACCESS_2,
    S390_FEAT_ADAPTER_EVENT_NOTIFICATION,
    S390_FEAT_ADAPTER_INT_SUPPRESSION,
    S390_FEAT_MSA_EXT_3,
    S390_FEAT_MSA_EXT_4,
};

static uint16_t qemu_V4_0[] = {
    /*
     * Only BFP bits are implemented (HFP, DFP, PFPO and DIVIDE TO INTEGER not
     * implemented yet).
     */
    S390_FEAT_FLOATING_POINT_EXT,
    S390_FEAT_ZPCI,
};

static uint16_t qemu_V4_1[] = {
    S390_FEAT_STFLE_53,
    S390_FEAT_VECTOR,
};

static uint16_t qemu_V6_0[] = {
    S390_FEAT_ACCESS_EXCEPTION_FS_INDICATION,
    S390_FEAT_SIDE_EFFECT_ACCESS_ESOP2,
    S390_FEAT_ESOP,
};

static uint16_t qemu_V6_2[] = {
    S390_FEAT_INSTRUCTION_EXEC_PROT,
    S390_FEAT_MISC_INSTRUCTION_EXT2,
    S390_FEAT_MSA_EXT_8,
    S390_FEAT_VECTOR_ENH,
};

static uint16_t qemu_V7_0[] = {
    S390_FEAT_MISC_INSTRUCTION_EXT3,
};

static uint16_t qemu_V7_1[] = {
    S390_FEAT_VECTOR_ENH2,
};

/*
 * Features for the "qemu" CPU model of the latest QEMU machine and the "max"
 * CPU model under TCG. Don't include features that are not part of the full
 * feature set of the current "max" CPU model generation.
 */
static uint16_t qemu_MAX[] = {
    S390_FEAT_MSA_EXT_5,
    S390_FEAT_KIMD_SHA_512,
    S390_FEAT_KLMD_SHA_512,
    S390_FEAT_PRNO_TRNG,
};

/****** END FEATURE DEFS ******/

#define _YEARS  "2016"
#define _NAME_H "TARGET_S390X_GEN_FEATURES_H"

#define CPU_FEAT_INITIALIZER(_name)                    \
    {                                                  \
        .name = "S390_FEAT_LIST_" #_name,              \
        .base_bits =                                   \
            { .data = base_##_name,                    \
              .len = ARRAY_SIZE(base_##_name) },       \
        .default_bits =                                \
            { .data = default_##_name,                 \
              .len = ARRAY_SIZE(default_##_name) },    \
        .full_bits =                                   \
            { .data = full_##_name,                    \
              .len = ARRAY_SIZE(full_##_name) },       \
    }

typedef struct BitSpec {
    uint16_t *data;
    uint32_t len;
} BitSpec;

typedef struct {
    const char *name;
    BitSpec base_bits;
    BitSpec default_bits;
    BitSpec full_bits;
} CpuFeatDefSpec;

static uint16_t EmptyFeat[] = {};

/*******************************
 * processor GA series
 *******************************/
static CpuFeatDefSpec CpuFeatDef[] = {
    CPU_FEAT_INITIALIZER(GEN7_GA1),
    CPU_FEAT_INITIALIZER(GEN7_GA2),
    CPU_FEAT_INITIALIZER(GEN7_GA3),
    CPU_FEAT_INITIALIZER(GEN8_GA1),
    CPU_FEAT_INITIALIZER(GEN8_GA2),
    CPU_FEAT_INITIALIZER(GEN8_GA3),
    CPU_FEAT_INITIALIZER(GEN8_GA4),
    CPU_FEAT_INITIALIZER(GEN8_GA5),
    CPU_FEAT_INITIALIZER(GEN9_GA1),
    CPU_FEAT_INITIALIZER(GEN9_GA2),
    CPU_FEAT_INITIALIZER(GEN9_GA3),
    CPU_FEAT_INITIALIZER(GEN10_GA1),
    CPU_FEAT_INITIALIZER(GEN10_GA2),
    CPU_FEAT_INITIALIZER(GEN10_GA3),
    CPU_FEAT_INITIALIZER(GEN11_GA1),
    CPU_FEAT_INITIALIZER(GEN11_GA2),
    CPU_FEAT_INITIALIZER(GEN12_GA1),
    CPU_FEAT_INITIALIZER(GEN12_GA2),
    CPU_FEAT_INITIALIZER(GEN13_GA1),
    CPU_FEAT_INITIALIZER(GEN13_GA2),
    CPU_FEAT_INITIALIZER(GEN14_GA1),
    CPU_FEAT_INITIALIZER(GEN14_GA2),
    CPU_FEAT_INITIALIZER(GEN15_GA1),
    CPU_FEAT_INITIALIZER(GEN16_GA1),
};

#define FEAT_GROUP_INITIALIZER(_name)                  \
    {                                                  \
        .name = "S390_FEAT_GROUP_LIST_" #_name,        \
        .enum_name = "S390_FEAT_GROUP_" #_name,        \
        .bits =                                        \
            { .data = group_##_name,                   \
              .len = ARRAY_SIZE(group_##_name) },      \
    }

typedef struct {
    const char *name;
    const char *enum_name;
    BitSpec bits;
} FeatGroupDefSpec;

/*******************************
 * feature groups
 *******************************/
static FeatGroupDefSpec FeatGroupDef[] = {
    FEAT_GROUP_INITIALIZER(PLO),
    FEAT_GROUP_INITIALIZER(TOD_CLOCK_STEERING),
    FEAT_GROUP_INITIALIZER(GEN13_PTFF),
    FEAT_GROUP_INITIALIZER(MSA),
    FEAT_GROUP_INITIALIZER(MSA_EXT_1),
    FEAT_GROUP_INITIALIZER(MSA_EXT_2),
    FEAT_GROUP_INITIALIZER(MSA_EXT_3),
    FEAT_GROUP_INITIALIZER(MSA_EXT_4),
    FEAT_GROUP_INITIALIZER(MSA_EXT_5),
    FEAT_GROUP_INITIALIZER(MSA_EXT_6),
    FEAT_GROUP_INITIALIZER(MSA_EXT_7),
    FEAT_GROUP_INITIALIZER(MSA_EXT_8),
    FEAT_GROUP_INITIALIZER(MSA_EXT_9),
    FEAT_GROUP_INITIALIZER(MSA_EXT_9_PCKMO),
    FEAT_GROUP_INITIALIZER(MULTIPLE_EPOCH_PTFF),
    FEAT_GROUP_INITIALIZER(ENH_SORT),
    FEAT_GROUP_INITIALIZER(DEFLATE_CONVERSION),
};

#define QEMU_FEAT_INITIALIZER(_name)                   \
    {                                                  \
        .name = "S390_FEAT_LIST_QEMU_" #_name,         \
        .bits =                                        \
            { .data = qemu_##_name,                    \
              .len = ARRAY_SIZE(qemu_##_name) },       \
    }

/*******************************
 * QEMU (CPU model) features
 *******************************/
static FeatGroupDefSpec QemuFeatDef[] = {
    QEMU_FEAT_INITIALIZER(V2_11),
    QEMU_FEAT_INITIALIZER(V3_1),
    QEMU_FEAT_INITIALIZER(V4_0),
    QEMU_FEAT_INITIALIZER(V4_1),
    QEMU_FEAT_INITIALIZER(V6_0),
    QEMU_FEAT_INITIALIZER(V6_2),
    QEMU_FEAT_INITIALIZER(V7_0),
    QEMU_FEAT_INITIALIZER(V7_1),
    QEMU_FEAT_INITIALIZER(MAX),
};


static void set_bits(uint64_t list[], BitSpec bits)
{
    uint32_t i;

    for (i = 0; i < bits.len; i++) {
        list[bits.data[i] / 64] |= 1ULL << (bits.data[i] % 64);
    }
}

static inline void clear_bit(uint64_t list[], unsigned long nr)
{
    list[nr / 64] &= ~(1ULL << (nr % 64));
}

static void print_feature_defs(void)
{
    uint64_t base_feat[S390_FEAT_MAX / 64 + 1] = {};
    uint64_t default_feat[S390_FEAT_MAX / 64 + 1] = {};
    uint64_t full_feat[S390_FEAT_MAX / 64 + 1] = {};
    int i, j;

    printf("\n/* CPU model feature list data */\n");

    for (i = 0; i < ARRAY_SIZE(CpuFeatDef); i++) {
        /* With gen15 CSSKE and BPB are deprecated */
        if (strcmp(CpuFeatDef[i].name, "S390_FEAT_LIST_GEN15_GA1") == 0) {
            clear_bit(base_feat, S390_FEAT_CONDITIONAL_SSKE);
            clear_bit(default_feat, S390_FEAT_CONDITIONAL_SSKE);
            clear_bit(default_feat, S390_FEAT_BPB);
        }
        set_bits(base_feat, CpuFeatDef[i].base_bits);
        /* add the base to the default features */
        set_bits(default_feat, CpuFeatDef[i].base_bits);
        set_bits(default_feat, CpuFeatDef[i].default_bits);
        /* add the base to the full features */
        set_bits(full_feat, CpuFeatDef[i].base_bits);
        set_bits(full_feat, CpuFeatDef[i].full_bits);

        printf("#define %s_BASE\t", CpuFeatDef[i].name);
        for (j = 0; j < ARRAY_SIZE(base_feat); j++) {
            printf("0x%016"PRIx64"ULL", base_feat[j]);
            if (j < ARRAY_SIZE(base_feat) - 1) {
                printf(",");
            } else {
                printf("\n");
            }
        }
        printf("#define %s_DEFAULT\t", CpuFeatDef[i].name);
        for (j = 0; j < ARRAY_SIZE(default_feat); j++) {
            printf("0x%016"PRIx64"ULL", default_feat[j]);
            if (j < ARRAY_SIZE(default_feat) - 1) {
                printf(",");
            } else {
                printf("\n");
            }
        }
        printf("#define %s_FULL\t\t", CpuFeatDef[i].name);
        for (j = 0; j < ARRAY_SIZE(full_feat); j++) {
            printf("0x%016"PRIx64"ULL", full_feat[j]);
            if (j < ARRAY_SIZE(full_feat) - 1) {
                printf(",");
            } else {
                printf("\n");
            }
        }
    }
}

static void print_qemu_feature_defs(void)
{
    uint64_t feat[S390_FEAT_MAX / 64 + 1] = {};
    int i, j;

    printf("\n/* QEMU (CPU model) feature list data */\n");

    /* for now we assume that we only add new features */
    for (i = 0; i < ARRAY_SIZE(QemuFeatDef); i++) {
        set_bits(feat, QemuFeatDef[i].bits);

        printf("#define %s\t", QemuFeatDef[i].name);
        for (j = 0; j < ARRAY_SIZE(feat); j++) {
            printf("0x%016"PRIx64"ULL", feat[j]);
            if (j < ARRAY_SIZE(feat) - 1) {
                printf(",");
            } else {
                printf("\n");
            }
        }
    }
}

static void print_feature_group_defs(void)
{
    int i, j;

    printf("\n/* CPU feature group list data */\n");

    for (i = 0; i < ARRAY_SIZE(FeatGroupDef); i++) {
        uint64_t feat[S390_FEAT_MAX / 64 + 1] = {};

        set_bits(feat, FeatGroupDef[i].bits);
        printf("#define %s\t", FeatGroupDef[i].name);
        for (j = 0; j < ARRAY_SIZE(feat); j++) {
            printf("0x%016"PRIx64"ULL", feat[j]);
            if (j < ARRAY_SIZE(feat) - 1) {
                printf(",");
            } else {
                printf("\n");
            }
        }
    }
}

static void print_feature_group_enum_type(void)
{
    int i;

    printf("\n/* CPU feature group enum type */\n"
           "typedef enum {\n");
    for (i = 0; i < ARRAY_SIZE(FeatGroupDef); i++) {
        printf("\t%s,\n", FeatGroupDef[i].enum_name);
    }
    printf("\tS390_FEAT_GROUP_MAX,\n"
           "} S390FeatGroup;\n");
}

int main(int argc, char *argv[])
{
    printf("/*\n"
           " * AUTOMATICALLY GENERATED, DO NOT MODIFY HERE, EDIT\n"
           " * SOURCE FILE \"%s\" INSTEAD.\n"
           " *\n"
           " * Copyright %s IBM Corp.\n"
           " *\n"
           " * This work is licensed under the terms of the GNU GPL, "
           "version 2 or (at\n * your option) any later version. See "
           "the COPYING file in the top-level\n * directory.\n"
           " */\n\n"
           "#ifndef %s\n#define %s\n", __FILE__, _YEARS, _NAME_H, _NAME_H);
    print_feature_defs();
    print_feature_group_defs();
    print_qemu_feature_defs();
    print_feature_group_enum_type();
    printf("\n#endif\n");
    return 0;
}
