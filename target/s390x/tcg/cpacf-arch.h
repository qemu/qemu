/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * s390x cpacf-arch
 *
 */

#ifndef S390X_CPACF_ARCH_H
#define S390X_CPACF_ARCH_H

/*
 * Function codes for the KM instruction
 */
#define CPACF_KM_QUERY          0x00
#define CPACF_KM_DEA            0x01
#define CPACF_KM_TDEA_128       0x02
#define CPACF_KM_TDEA_192       0x03
#define CPACF_KM_AES_128        0x12
#define CPACF_KM_AES_192        0x13
#define CPACF_KM_AES_256        0x14
#define CPACF_KM_PAES_128       0x1a
#define CPACF_KM_PAES_192       0x1b
#define CPACF_KM_PAES_256       0x1c
#define CPACF_KM_XTS_128        0x32
#define CPACF_KM_XTS_256        0x34
#define CPACF_KM_PXTS_128       0x3a
#define CPACF_KM_PXTS_256       0x3c
#define CPACF_KM_FULL_XTS_128   0x52
#define CPACF_KM_FULL_XTS_256   0x54
#define CPACF_KM_FULL_PXTS_128  0x5a
#define CPACF_KM_FULL_PXTS_256  0x5c

/*
 * Function codes for the KMC instruction
 */
#define CPACF_KMC_QUERY         0x00
#define CPACF_KMC_DEA           0x01
#define CPACF_KMC_TDEA_128      0x02
#define CPACF_KMC_TDEA_192      0x03
#define CPACF_KMC_AES_128       0x12
#define CPACF_KMC_AES_192       0x13
#define CPACF_KMC_AES_256       0x14
#define CPACF_KMC_PAES_128      0x1a
#define CPACF_KMC_PAES_192      0x1b
#define CPACF_KMC_PAES_256      0x1c
#define CPACF_KMC_PRNG          0x43

/*
 * Function codes for the KMCTR instruction
 */
#define CPACF_KMCTR_QUERY       0x00
#define CPACF_KMCTR_DEA         0x01
#define CPACF_KMCTR_TDEA_128    0x02
#define CPACF_KMCTR_TDEA_192    0x03
#define CPACF_KMCTR_AES_128     0x12
#define CPACF_KMCTR_AES_192     0x13
#define CPACF_KMCTR_AES_256     0x14
#define CPACF_KMCTR_PAES_128    0x1a
#define CPACF_KMCTR_PAES_192    0x1b
#define CPACF_KMCTR_PAES_256    0x1c

/*
 * Function codes for the KIMD instruction
 */
#define CPACF_KIMD_QUERY        0x00
#define CPACF_KIMD_SHA_1        0x01
#define CPACF_KIMD_SHA_256      0x02
#define CPACF_KIMD_SHA_512      0x03
#define CPACF_KIMD_SHA3_224     0x20
#define CPACF_KIMD_SHA3_256     0x21
#define CPACF_KIMD_SHA3_384     0x22
#define CPACF_KIMD_SHA3_512     0x23
#define CPACF_KIMD_SHAKE_128    0x24
#define CPACF_KIMD_SHAKE_256    0x25
#define CPACF_KIMD_GHASH        0x41

/*
 * Function codes for the KLMD instruction
 */
#define CPACF_KLMD_QUERY        0x00
#define CPACF_KLMD_SHA_1        0x01
#define CPACF_KLMD_SHA_256      0x02
#define CPACF_KLMD_SHA_512      0x03
#define CPACF_KLMD_SHA3_224     0x20
#define CPACF_KLMD_SHA3_256     0x21
#define CPACF_KLMD_SHA3_384     0x22
#define CPACF_KLMD_SHA3_512     0x23
#define CPACF_KLMD_SHAKE_128    0x24
#define CPACF_KLMD_SHAKE_256    0x25

/*
 * function codes for the KMAC instruction
 */
#define CPACF_KMAC_QUERY         0x00
#define CPACF_KMAC_DEA           0x01
#define CPACF_KMAC_TDEA_128      0x02
#define CPACF_KMAC_TDEA_192      0x03
#define CPACF_KMAC_AES_128       0x12
#define CPACF_KMAC_AES_192       0x13
#define CPACF_KMAC_AES_256       0x14
#define CPACF_KMAC_PAES_128      0x1A
#define CPACF_KMAC_PAES_192      0x1B
#define CPACF_KMAC_PAES_256      0x1C
#define CPACF_KMAC_HMAC_SHA_224  0x70
#define CPACF_KMAC_HMAC_SHA_256  0x71
#define CPACF_KMAC_HMAC_SHA_384  0x72
#define CPACF_KMAC_HMAC_SHA_512  0x73
#define CPACF_KMAC_PHMAC_SHA_224 0x78
#define CPACF_KMAC_PHMAC_SHA_256 0x79
#define CPACF_KMAC_PHMAC_SHA_384 0x7a
#define CPACF_KMAC_PHMAC_SHA_512 0x7b

/*
 * Function codes for the PCKMO instruction
 */
#define CPACF_PCKMO_QUERY                      0x00
#define CPACF_PCKMO_ENC_DES_KEY                0x01
#define CPACF_PCKMO_ENC_TDES_128_KEY           0x02
#define CPACF_PCKMO_ENC_TDES_192_KEY           0x03
#define CPACF_PCKMO_ENC_AES_128_KEY            0x12
#define CPACF_PCKMO_ENC_AES_192_KEY            0x13
#define CPACF_PCKMO_ENC_AES_256_KEY            0x14
#define CPACF_PCKMO_ENC_AES_XTS_128_DOUBLE_KEY 0x15
#define CPACF_PCKMO_ENC_AES_XTS_256_DOUBLE_KEY 0x16
#define CPACF_PCKMO_ENC_ECC_P256_KEY           0x20
#define CPACF_PCKMO_ENC_ECC_P384_KEY           0x21
#define CPACF_PCKMO_ENC_ECC_P521_KEY           0x22
#define CPACF_PCKMO_ENC_ECC_ED25519_KEY        0x28
#define CPACF_PCKMO_ENC_ECC_ED448_KEY          0x29
#define CPACF_PCKMO_ENC_HMAC_512_KEY           0x76
#define CPACF_PCKMO_ENC_HMAC_1024_KEY          0x7a

/*
 * Function codes for the PRNO instruction
 */
#define CPACF_PRNO_QUERY                0x00
#define CPACF_PRNO_SHA512_DRNG_GEN      0x03
#define CPACF_PRNO_SHA512_DRNG_SEED     0x83
#define CPACF_PRNO_TRNG_Q_R2C_RATIO     0x70
#define CPACF_PRNO_TRNG                 0x72

/*
 * Function codes for the KMA instruction
 */
#define CPACF_KMA_QUERY         0x00
#define CPACF_KMA_GCM_AES_128   0x12
#define CPACF_KMA_GCM_AES_192   0x13
#define CPACF_KMA_GCM_AES_256   0x14
#define CPACF_KMA_GCM_PAES_128  0x1A
#define CPACF_KMA_GCM_PAES_192  0x1B
#define CPACF_KMA_GCM_PAES_256  0x1C

/*
 * Function codes for the KMF instruction
 */
#define CPACF_KMF_QUERY      0
#define CPACF_KMF_DEA        1
#define CPACF_KMF_TDEA_128   2
#define CPACF_KMF_TDEA_192   3
#define CPACF_KMF_AES_128   18
#define CPACF_KMF_AES_192   19
#define CPACF_KMF_AES_256   20
#define CPACF_KMF_PAES_128  26
#define CPACF_KMF_PAES_192  27
#define CPACF_KMF_PAES_256  28

/*
 * Function codes for the KMO instruction
 */
#define CPACF_KMO_QUERY      0
#define CPACF_KMO_DEA        1
#define CPACF_KMO_TDEA_128   2
#define CPACF_KMO_TDEA_192   3
#define CPACF_KMO_AES_128   18
#define CPACF_KMO_AES_192   19
#define CPACF_KMO_AES_256   20
#define CPACF_KMO_PAES_128  26
#define CPACF_KMO_PAES_192  27
#define CPACF_KMO_PAES_256  28

/*
 * Function codes for the PCC instruction
 */
#define CPACF_PCC_QUERY            0
#define CPACF_PCC_CMAC_DEA         1
#define CPACF_PCC_CMAC_TDEA_128    2
#define CPACF_PCC_CMAC_TDEA_192    3
#define CPACF_PCC_CMAC_AES_128    18
#define CPACF_PCC_CMAC_AES_192    19
#define CPACF_PCC_CMAC_AES_256    20
#define CPACF_PCC_CMAC_PAES_128   26
#define CPACF_PCC_CMAC_PAES_192   27
#define CPACF_PCC_CMAC_PAES_256   28
#define CPACF_PCC_XTS_AES_128     50
#define CPACF_PCC_XTS_AES_256     52
#define CPACF_PCC_XTS_PAES_128    58
#define CPACF_PCC_XTS_PAES_256    60
#define CPACF_PCC_SM_P256         64
#define CPACF_PCC_SM_P384         65
#define CPACF_PCC_SM_P521         66
#define CPACF_PCC_SM_ED25519      72
#define CPACF_PCC_SM_ED448        73
#define CPACF_PCC_SM_X25519       80
#define CPACF_PCC_SM_X448         81

/*
 * Function codes for the KDSA instruction
 */
#define CPACF_KDSA_QUERY            0
#define CPACF_KDSA_VERIFY_P256      1
#define CPACF_KDSA_VERIFY_P384      2
#define CPACF_KDSA_VERIFY_P521      3
#define CPACF_KDSA_SIGN_P256        9
#define CPACF_KDSA_SIGN_P384       10
#define CPACF_KDSA_SIGN_P521       11
#define CPACF_KDSA_PSIGN_P256      17
#define CPACF_KDSA_PSIGN_P384      18
#define CPACF_KDSA_PSIGN_P521      19
#define CPACF_KDSA_VERIFY_ED25519  32
#define CPACF_KDSA_VERIFY_ED448    36
#define CPACF_KDSA_SIGN_ED25519    40
#define CPACF_KDSA_SIGN_ED448      44
#define CPACF_KDSA_PSIGN_ED25519   48
#define CPACF_KDSA_PSIGN_ED448     52

#endif /* S390X_CPACF_ARCH_H */
