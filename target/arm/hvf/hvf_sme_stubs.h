/* SPDX-License-Identifier: GPL-2.0-or-later */

typedef int32_t hv_return_t;
typedef uint64_t hv_vcpu_t;

static inline bool hvf_arm_sme2_supported(void)
{
    return false;
}

static inline uint32_t hvf_arm_sme2_get_svl(void)
{
    g_assert_not_reached();
}

typedef enum hv_sme_p_reg_t {
    HV_SME_P_REG_0,
    HV_SME_P_REG_1,
    HV_SME_P_REG_2,
    HV_SME_P_REG_3,
    HV_SME_P_REG_4,
    HV_SME_P_REG_5,
    HV_SME_P_REG_6,
    HV_SME_P_REG_7,
    HV_SME_P_REG_8,
    HV_SME_P_REG_9,
    HV_SME_P_REG_10,
    HV_SME_P_REG_11,
    HV_SME_P_REG_12,
    HV_SME_P_REG_13,
    HV_SME_P_REG_14,
    HV_SME_P_REG_15,
} hv_sme_p_reg_t;

/*
 * The system version of this type declares it with
 *    __attribute__((ext_vector_type(64)))
 * However, that is clang specific and not supported by GCC.
 * Since these headers are only here for the case where the system
 * headers do not provide these types (including both older macos
 * and non-macos hosts), we don't need to make the type match
 * exactly, so we declare it as a uint8_t array.
 */
typedef uint8_t hv_sme_zt0_uchar64_t[64];

typedef enum hv_sme_z_reg_t {
    HV_SME_Z_REG_0,
    HV_SME_Z_REG_1,
    HV_SME_Z_REG_2,
    HV_SME_Z_REG_3,
    HV_SME_Z_REG_4,
    HV_SME_Z_REG_5,
    HV_SME_Z_REG_6,
    HV_SME_Z_REG_7,
    HV_SME_Z_REG_8,
    HV_SME_Z_REG_9,
    HV_SME_Z_REG_10,
    HV_SME_Z_REG_11,
    HV_SME_Z_REG_12,
    HV_SME_Z_REG_13,
    HV_SME_Z_REG_14,
    HV_SME_Z_REG_15,
    HV_SME_Z_REG_16,
    HV_SME_Z_REG_17,
    HV_SME_Z_REG_18,
    HV_SME_Z_REG_19,
    HV_SME_Z_REG_20,
    HV_SME_Z_REG_21,
    HV_SME_Z_REG_22,
    HV_SME_Z_REG_23,
    HV_SME_Z_REG_24,
    HV_SME_Z_REG_25,
    HV_SME_Z_REG_26,
    HV_SME_Z_REG_27,
    HV_SME_Z_REG_28,
    HV_SME_Z_REG_29,
    HV_SME_Z_REG_30,
    HV_SME_Z_REG_31,
} hv_sme_z_reg_t;

enum {
  HV_SYS_REG_SMCR_EL1,
  HV_SYS_REG_SMPRI_EL1,
  HV_SYS_REG_TPIDR2_EL0,
  HV_SYS_REG_ID_AA64ZFR0_EL1,
  HV_SYS_REG_ID_AA64SMFR0_EL1,
};

enum {
  HV_FEATURE_REG_ID_AA64SMFR0_EL1,
  HV_FEATURE_REG_ID_AA64ZFR0_EL1,
};

typedef struct {
    bool streaming_sve_mode_enabled;
    bool za_storage_enabled;
} hv_vcpu_sme_state_t;

static inline hv_return_t hv_sme_config_get_max_svl_bytes(size_t *value)
{
    g_assert_not_reached();
}

static inline hv_return_t hv_vcpu_get_sme_state(hv_vcpu_t vcpu,
                                                hv_vcpu_sme_state_t *sme_state)
{
    g_assert_not_reached();
}

static inline hv_return_t hv_vcpu_set_sme_state(hv_vcpu_t vcpu,
                                                const hv_vcpu_sme_state_t *sme_state)
{
    g_assert_not_reached();
}

static inline hv_return_t hv_vcpu_get_sme_z_reg(hv_vcpu_t vcpu,
                                                hv_sme_z_reg_t reg,
                                                uint8_t *value,
                                                size_t length)
{
    g_assert_not_reached();
}

static inline hv_return_t hv_vcpu_set_sme_z_reg(hv_vcpu_t vcpu,
                                                hv_sme_z_reg_t reg,
                                                const uint8_t *value,
                                                size_t length)
{
    g_assert_not_reached();
}

static inline hv_return_t hv_vcpu_get_sme_p_reg(hv_vcpu_t vcpu,
                                                hv_sme_p_reg_t reg,
                                                uint8_t *value,
                                                size_t length)
{
    g_assert_not_reached();
}

static inline hv_return_t hv_vcpu_set_sme_p_reg(hv_vcpu_t vcpu,
                                                hv_sme_p_reg_t reg,
                                                const uint8_t *value,
                                                size_t length)
{
    g_assert_not_reached();
}

static inline hv_return_t hv_vcpu_get_sme_za_reg(hv_vcpu_t vcpu,
                                                 uint8_t *value,
                                                 size_t length)
{
    g_assert_not_reached();
}

static inline hv_return_t hv_vcpu_set_sme_za_reg(hv_vcpu_t vcpu,
                                                 const uint8_t *value,
                                                 size_t length)
{
    g_assert_not_reached();
}

static inline hv_return_t hv_vcpu_get_sme_zt0_reg(hv_vcpu_t vcpu,
                                                  hv_sme_zt0_uchar64_t *value)
{
    g_assert_not_reached();
}

static inline hv_return_t hv_vcpu_set_sme_zt0_reg(hv_vcpu_t vcpu,
                                                  const hv_sme_zt0_uchar64_t *value)
{
    g_assert_not_reached();
}
