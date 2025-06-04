/*
 * TPM configuration
 *
 * Copyright (C) 2011-2013 IBM Corporation
 *
 * Authors:
 *  Stefan Berger    <stefanb@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#ifndef BACKENDS_TPM_INT_H
#define BACKENDS_TPM_INT_H

#include "qemu/option.h"
#include "system/tpm.h"

#define HASH_COUNT 5

// Table 220 - Defines for Implementation Values
#define IMPLEMENTATION_PCR  24
#define PCR_SELECT_MAX      ((IMPLEMENTATION_PCR + 7) / 8)
#define MAX_CAP_BUFFER      1024

// Capability related MAX_ value
#define MAX_CAP_DATA        (MAX_CAP_BUFFER - sizeof(uint32_t) - sizeof(uint32_t))
#define MAX_CAP_ALGS        (MAX_CAP_DATA / sizeof(TPMS_ALG_PROPERTY))
#define MAX_CAP_HANDLES     (MAX_CAP_DATA / sizeof(uint32_t))
#define MAX_CAP_CC          (MAX_CAP_DATA / sizeof(uint32_t))
#define MAX_TPM_PROPERTIES  (MAX_CAP_DATA / sizeof(TPMS_TAGGED_PROPERTY))
#define MAX_PCR_PROPERTIES  (MAX_CAP_DATA / sizeof(TPMS_TAGGED_PCR_SELECT))
#define MAX_ECC_CURVES      (MAX_CAP_DATA / sizeof(uint16_t))

#define TPM_STANDARD_CMDLINE_OPTS \
    { \
        .name = "type", \
        .type = QEMU_OPT_STRING, \
        .help = "Type of TPM backend", \
    }

// Table 19 - TPM_SU Constants
#define TPM_SU_CLEAR  0x0000
#define TPM_SU_STATE  0x0001

// Table 205 - Defines for SHA1 Hash Values
#define SHA1_DIGEST_SIZE  20
#define SHA1_BLOCK_SIZE   64

// Table 206 - Defines for SHA256 Hash Values
#define SHA256_DIGEST_SIZE  32
#define SHA256_BLOCK_SIZE   64

// Table 207 - Defines for SHA384 Hash Values
#define SHA384_DIGEST_SIZE  48
#define SHA384_BLOCK_SIZE   128

// Table 208 - Defines for SHA512 Hash Values
#define SHA512_DIGEST_SIZE  64
#define SHA512_BLOCK_SIZE   128

// Table 209 - Defines for SM3_256 Hash Values
#define SM3_256_DIGEST_SIZE  32
#define SM3_256_BLOCK_SIZE   64

typedef union {
  uint8_t sha1[SHA1_DIGEST_SIZE];
  uint8_t sha256[SHA256_DIGEST_SIZE];
  uint8_t sm3_256[SM3_256_DIGEST_SIZE];
  uint8_t sha384[SHA384_DIGEST_SIZE];
  uint8_t sha512[SHA512_DIGEST_SIZE];
} TPMU_HA;

// Table 21 - TPM_CAP Constants
#define TPM_CAP_PCRS             0x00000005
#define TPM_CAP_PCR_PROPERTIES   0x00000007

// Table 29 - TPMA_ALGORITHM Bits
typedef struct {
  uint32_t  asymmetric    : 1;
  uint32_t  symmetric     : 1;
  uint32_t  hash          : 1;
  uint32_t  object        : 1;
  uint32_t  reserved4_7   : 4;
  uint32_t  signing       : 1;
  uint32_t  encrypting    : 1;
  uint32_t  method        : 1;
  uint32_t  reserved11_31 : 21;
} QEMU_PACKED TPMA_ALGORITHM;

// Table 36 - TPMA_CC Bits
typedef struct {
  uint32_t  commandIndex  : 16;
  uint32_t  reserved16_21 : 6;
  uint32_t  nv            : 1;
  uint32_t  extensive     : 1;
  uint32_t  flushed       : 1;
  uint32_t  cHandles      : 3;
  uint32_t  rHandle       : 1;
  uint32_t  V             : 1;
  uint32_t  Res           : 2;
} QEMU_PACKED TPMA_CC;

// Table 68 - TPM2B_DIGEST Structure
typedef struct {
    uint16_t    size;
    uint8_t     buffer[sizeof(TPMU_HA)];
} QEMU_PACKED TPM2B_DIGEST;

// Table 81 - TPMS_PCR_SELECTION Structure
typedef struct {
    uint16_t    hash;
    uint8_t     select_size;
    uint8_t     pcr_select[PCR_SELECT_MAX];
} QEMU_PACKED TPMS_PCR_SELECTION;

// Table 88 - TPMS_ALG_PROPERTY Structure
typedef struct {
  uint16_t          alg;
  TPMA_ALGORITHM    algProperties;
} QEMU_PACKED TPMS_ALG_PROPERTY;

// Table 89 - TPMS_TAGGED_PROPERTY Structure
typedef struct {
  uint32_t  property;
  uint32_t  value;
} QEMU_PACKED TPMS_TAGGED_PROPERTY;

// Table 90 - TPMS_TAGGED_PCR_SELECT Structure
typedef struct {
  uint32_t  tag;
  uint8_t   sizeofSelect;
  uint8_t   pcrSelect[PCR_SELECT_MAX];
} QEMU_PACKED TPMS_TAGGED_PCR_SELECT;

// Table 91 - TPML_CC Structure
typedef struct {
  uint32_t  count;
  uint32_t  commandCodes[MAX_CAP_CC];
} QEMU_PACKED TPML_CC;

// Table 92 - TPML_CCA Structure
typedef struct {
  uint32_t     count;
  TPMA_CC    commandAttributes[MAX_CAP_CC];
} QEMU_PACKED TPML_CCA;

// Table 94 - TPML_HANDLE Structure
typedef struct {
  uint32_t  count;
  uint32_t  handle[MAX_CAP_HANDLES];
} QEMU_PACKED TPML_HANDLE;

// Table 95 - TPML_DIGEST Structure
typedef struct {
    uint32_t        count;
    TPM2B_DIGEST    digests[8];
} QEMU_PACKED TPML_DIGEST;

// Table 98 - TPML_PCR_SELECTION Structure
typedef struct {
    uint32_t            count;
    TPMS_PCR_SELECTION  pcr_selection[HASH_COUNT];
} QEMU_PACKED TPML_PCR_SELECTION;

// Table 99 - TPML_ALG_PROPERTY Structure
typedef struct {
  uint32_t              count;
  TPMS_ALG_PROPERTY     algProperties[MAX_CAP_ALGS];
} QEMU_PACKED TPML_ALG_PROPERTY;

// Table 100 - TPML_TAGGED_TPM_PROPERTY Structure
typedef struct {
  uint32_t              count;
  TPMS_TAGGED_PROPERTY  tpmProperty[MAX_TPM_PROPERTIES];
} QEMU_PACKED TPML_TAGGED_TPM_PROPERTY;

// Table 101 - TPML_TAGGED_PCR_PROPERTY Structure
typedef struct {
  uint32_t                  count;
  TPMS_TAGGED_PCR_SELECT    pcrProperty[MAX_PCR_PROPERTIES];
} QEMU_PACKED TPML_TAGGED_PCR_PROPERTY;

// Table 102 - TPML_ECC_CURVE Structure
typedef struct {
  uint32_t count;
  uint16_t eccCurves[MAX_ECC_CURVES];
} QEMU_PACKED TPML_ECC_CURVE;

// Table 103 - TPMU_CAPABILITIES Union
typedef union {
  TPML_ALG_PROPERTY           algorithms;
  TPML_HANDLE                 handles;
  TPML_CCA                    command;
  TPML_CC                     ppCommands;
  TPML_CC                     auditCommands;
  TPML_PCR_SELECTION          assignedPCR;
  TPML_TAGGED_TPM_PROPERTY    tpmProperties;
  TPML_TAGGED_PCR_PROPERTY    pcrProperties;
  TPML_ECC_CURVE              eccCurves;
} TPMU_CAPABILITIES;

// Table 104 - TPMS_CAPABILITY_DATA Structure
typedef struct {
  uint32_t              capability;
  TPMU_CAPABILITIES     data;
} QEMU_PACKED TPMS_CAPABILITY_DATA;

struct tpm_req_hdr {
    uint16_t tag;
    uint32_t len;
    uint32_t ordinal;
} QEMU_PACKED;

struct tpm_resp_hdr {
    uint16_t tag;
    uint32_t len;
    uint32_t errcode;
} QEMU_PACKED;

#define TPM_TAG_RQU_COMMAND       0xc1
#define TPM_TAG_RQU_AUTH1_COMMAND 0xc2
#define TPM_TAG_RQU_AUTH2_COMMAND 0xc3

#define TPM_TAG_RSP_COMMAND       0xc4
#define TPM_TAG_RSP_AUTH1_COMMAND 0xc5
#define TPM_TAG_RSP_AUTH2_COMMAND 0xc6

#define TPM_BAD_PARAMETER         3
#define TPM_FAIL                  9
#define TPM_KEYNOTFOUND           13
#define TPM_BAD_PARAM_SIZE        25
#define TPM_ENCRYPT_ERROR         32
#define TPM_DECRYPT_ERROR         33
#define TPM_BAD_KEY_PROPERTY      40
#define TPM_BAD_MODE              44
#define TPM_BAD_VERSION           46
#define TPM_BAD_LOCALITY          61

#define TPM_ORD_ContinueSelfTest  0x53
#define TPM_ORD_GetTicks          0xf1
#define TPM_ORD_GetCapability     0x65

#define TPM_CAP_PROPERTY          0x05

#define TPM_CAP_PROP_INPUT_BUFFER 0x124

/* TPM2 defines */
#define TPM2_ST_NO_SESSIONS       0x8001

// Table 11 - TPM_CC Constants (Numeric Order)
#define TPM2_CC_Startup           0x00000144
#define TPM2_CC_Shutdown          0x00000145
#define TPM2_CC_PCR_Read          0x0000017e
#define TPM2_CC_GetCapability     0x0000017a
#define TPM2_CC_ReadClock         0x00000181

#define TPM2_CAP_TPM_PROPERTIES   0x6

#define TPM2_PT_MAX_COMMAND_SIZE  0x11e

#define TPM_RC_INSUFFICIENT       0x9a
#define TPM_RC_FAILURE            0x101
#define TPM_RC_LOCALITY           0x907

#define TPM_ALG_SHA1              0x0004
#define TPM_ALG_KEYEDHASH         0x0008
#define TPM_ALG_SHA256            0x000B
#define TPM_ALG_SHA384            0x000C
#define TPM_ALG_SHA512            0x000D

int tpm_util_get_buffer_size(int tpm_fd, TPMVersion tpm_version,
                             size_t *buffersize);

typedef struct TPMSizedBuffer {
    uint32_t size;
    uint8_t  *buffer;
} TPMSizedBuffer;

void tpm_sized_buffer_reset(TPMSizedBuffer *tsb);

#endif /* BACKENDS_TPM_INT_H */
