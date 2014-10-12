/*
 * Copyright (c) 2004-2007 Reyk Floeter <reyk@openbsd.org>
 * Copyright (c) 2006-2007 Nick Kossifidis <mickflemm@gmail.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * $Id$
 */

/*
 * Gain settings
 */

typedef enum {
    AR5K_RFGAIN_INACTIVE = 0,
    AR5K_RFGAIN_READ_REQUESTED,
    AR5K_RFGAIN_NEED_CHANGE,
} AR5K_RFGAIN;

#define AR5K_GAIN_CRN_FIX_BITS_5111     4
#define AR5K_GAIN_CRN_FIX_BITS_5112     7
#define AR5K_GAIN_CRN_MAX_FIX_BITS      AR5K_GAIN_CRN_FIX_BITS_5112
#define AR5K_GAIN_DYN_ADJUST_HI_MARGIN      15
#define AR5K_GAIN_DYN_ADJUST_LO_MARGIN      20
#define AR5K_GAIN_CCK_PROBE_CORR        5
#define AR5K_GAIN_CCK_OFDM_GAIN_DELTA       15
#define AR5K_GAIN_STEP_COUNT            10
#define AR5K_GAIN_PARAM_TX_CLIP         0
#define AR5K_GAIN_PARAM_PD_90           1
#define AR5K_GAIN_PARAM_PD_84           2
#define AR5K_GAIN_PARAM_GAIN_SEL        3
#define AR5K_GAIN_PARAM_MIX_ORN         0
#define AR5K_GAIN_PARAM_PD_138          1
#define AR5K_GAIN_PARAM_PD_137          2
#define AR5K_GAIN_PARAM_PD_136          3
#define AR5K_GAIN_PARAM_PD_132          4
#define AR5K_GAIN_PARAM_PD_131          5
#define AR5K_GAIN_PARAM_PD_130          6
#define AR5K_GAIN_CHECK_ADJUST(_g)      \
    ((_g)->g_current <= (_g)->g_low || (_g)->g_current >= (_g)->g_high)

struct ath5k_gain_opt_step {
    int16_t             gos_param[AR5K_GAIN_CRN_MAX_FIX_BITS];
    int32_t             gos_gain;
};

struct ath5k_gain_opt {
    uint32_t           go_default;
    uint32_t           go_steps_count;
    const struct ath5k_gain_opt_step    go_step[AR5K_GAIN_STEP_COUNT];
};

struct ath5k_gain {
    uint32_t           g_step_idx;
    uint32_t           g_current;
    uint32_t           g_target;
    uint32_t           g_low;
    uint32_t           g_high;
    uint32_t           g_f_corr;
    uint32_t           g_active;
    const struct ath5k_gain_opt_step    *g_step;
};

/*
 * Gain optimization tables...
 */
#define AR5K_AR5111_GAIN_OPT    {       \
    4,                  \
    9,                  \
    {                   \
        { { 4, 1, 1, 1 }, 6 },      \
        { { 4, 0, 1, 1 }, 4 },      \
        { { 3, 1, 1, 1 }, 3 },      \
        { { 4, 0, 0, 1 }, 1 },      \
        { { 4, 1, 1, 0 }, 0 },      \
        { { 4, 0, 1, 0 }, -2 },     \
        { { 3, 1, 1, 0 }, -3 },     \
        { { 4, 0, 0, 0 }, -4 },     \
        { { 2, 1, 1, 0 }, -6 }      \
    }                   \
}

#define AR5K_AR5112_GAIN_OPT    {           \
    1,                      \
    8,                      \
    {                       \
        { { 3, 0, 0, 0, 0, 0, 0 }, 6 },     \
        { { 2, 0, 0, 0, 0, 0, 0 }, 0 },     \
        { { 1, 0, 0, 0, 0, 0, 0 }, -3 },    \
        { { 0, 0, 0, 0, 0, 0, 0 }, -6 },    \
        { { 0, 1, 1, 0, 0, 0, 0 }, -8 },    \
        { { 0, 1, 1, 0, 1, 1, 0 }, -10 },   \
        { { 0, 1, 0, 1, 1, 1, 0 }, -13 },   \
        { { 0, 1, 0, 1, 1, 0, 1 }, -16 },   \
    }                       \
}

/* Some EEPROM defines */
#define AR5K_EEPROM_EEP_SCALE       100
#define AR5K_EEPROM_EEP_DELTA       10
#define AR5K_EEPROM_N_MODES     3
#define AR5K_EEPROM_N_5GHZ_CHAN     10
#define AR5K_EEPROM_N_2GHZ_CHAN     3
#define AR5K_EEPROM_MAX_CHAN        10
#define AR5K_EEPROM_N_PCDAC     11
#define AR5K_EEPROM_N_TEST_FREQ     8
#define AR5K_EEPROM_N_EDGES     8
#define AR5K_EEPROM_N_INTERCEPTS    11
#define AR5K_EEPROM_FREQ_M(_v)      AR5K_EEPROM_OFF(_v, 0x7f, 0xff)
#define AR5K_EEPROM_PCDAC_M     0x3f
#define AR5K_EEPROM_PCDAC_START     1
#define AR5K_EEPROM_PCDAC_STOP      63
#define AR5K_EEPROM_PCDAC_STEP      1
#define AR5K_EEPROM_NON_EDGE_M      0x40
#define AR5K_EEPROM_CHANNEL_POWER   8
#define AR5K_EEPROM_N_OBDB      4
#define AR5K_EEPROM_OBDB_DIS        0xffff
#define AR5K_EEPROM_CHANNEL_DIS     0xff
#define AR5K_EEPROM_SCALE_OC_DELTA(_x)  (((_x) * 2) / 10)
#define AR5K_EEPROM_N_CTLS(_v)      AR5K_EEPROM_OFF(_v, 16, 32)
#define AR5K_EEPROM_MAX_CTLS        32
#define AR5K_EEPROM_N_XPD_PER_CHANNEL   4
#define AR5K_EEPROM_N_XPD0_POINTS   4
#define AR5K_EEPROM_N_XPD3_POINTS   3
#define AR5K_EEPROM_N_INTERCEPT_10_2GHZ 35
#define AR5K_EEPROM_N_INTERCEPT_10_5GHZ 55
#define AR5K_EEPROM_POWER_M     0x3f
#define AR5K_EEPROM_POWER_MIN       0
#define AR5K_EEPROM_POWER_MAX       3150
#define AR5K_EEPROM_POWER_STEP      50
#define AR5K_EEPROM_POWER_TABLE_SIZE    64
#define AR5K_EEPROM_N_POWER_LOC_11B 4
#define AR5K_EEPROM_N_POWER_LOC_11G 6
#define AR5K_EEPROM_I_GAIN      10
#define AR5K_EEPROM_CCK_OFDM_DELTA  15
#define AR5K_EEPROM_N_IQ_CAL        2

struct ath5k_eeprom_info {
    uint16_t   ee_magic;
    uint16_t   ee_protect;
    uint16_t   ee_regdomain;
    uint16_t   ee_version;
    uint16_t   ee_header;
    uint16_t   ee_ant_gain;
    uint16_t   ee_misc0;
    uint16_t   ee_misc1;
    uint16_t   ee_cck_ofdm_gain_delta;
    uint16_t   ee_cck_ofdm_power_delta;
    uint16_t   ee_scaled_cck_delta;
    uint16_t   ee_tx_clip;
    uint16_t   ee_pwd_84;
    uint16_t   ee_pwd_90;
    uint16_t   ee_gain_select;

    uint16_t   ee_i_cal[AR5K_EEPROM_N_MODES];
    uint16_t   ee_q_cal[AR5K_EEPROM_N_MODES];
    uint16_t   ee_fixed_bias[AR5K_EEPROM_N_MODES];
    uint16_t   ee_turbo_max_power[AR5K_EEPROM_N_MODES];
    uint16_t   ee_xr_power[AR5K_EEPROM_N_MODES];
    uint16_t   ee_switch_settling[AR5K_EEPROM_N_MODES];
    uint16_t   ee_ant_tx_rx[AR5K_EEPROM_N_MODES];
    uint16_t   ee_ant_control[AR5K_EEPROM_N_MODES][AR5K_EEPROM_N_PCDAC];
    uint16_t   ee_ob[AR5K_EEPROM_N_MODES][AR5K_EEPROM_N_OBDB];
    uint16_t   ee_db[AR5K_EEPROM_N_MODES][AR5K_EEPROM_N_OBDB];
    uint16_t   ee_tx_end2xlna_enable[AR5K_EEPROM_N_MODES];
    uint16_t   ee_tx_end2xpa_disable[AR5K_EEPROM_N_MODES];
    uint16_t   ee_tx_frm2xpa_enable[AR5K_EEPROM_N_MODES];
    uint16_t   ee_thr_62[AR5K_EEPROM_N_MODES];
    uint16_t   ee_xlna_gain[AR5K_EEPROM_N_MODES];
    uint16_t   ee_xpd[AR5K_EEPROM_N_MODES];
    uint16_t   ee_x_gain[AR5K_EEPROM_N_MODES];
    uint16_t   ee_i_gain[AR5K_EEPROM_N_MODES];
    uint16_t   ee_margin_tx_rx[AR5K_EEPROM_N_MODES];
    uint16_t   ee_false_detect[AR5K_EEPROM_N_MODES];
    uint16_t   ee_cal_pier[AR5K_EEPROM_N_MODES][AR5K_EEPROM_N_2GHZ_CHAN];
    uint16_t   ee_channel[AR5K_EEPROM_N_MODES][AR5K_EEPROM_MAX_CHAN];

    uint16_t   ee_ctls;
    uint16_t   ee_ctl[AR5K_EEPROM_MAX_CTLS];

    int16_t     ee_noise_floor_thr[AR5K_EEPROM_N_MODES];
    int8_t      ee_adc_desired_size[AR5K_EEPROM_N_MODES];
    int8_t      ee_pga_desired_size[AR5K_EEPROM_N_MODES];
};

/*
 * AR5k register access
 */

/*Swap RX/TX Descriptor for big endian archs*/
#if BYTE_ORDER == BIG_ENDIAN
#define AR5K_INIT_CFG   (       \
    AR5K_CFG_SWTD | AR5K_CFG_SWRD   \
)
#else
#define AR5K_INIT_CFG   0x00000000
#endif

#define AR5K_REG_READ(_reg) ath5k_hw_reg_read(hal, _reg)

#define AR5K_REG_WRITE(_reg, _val)  ath5k_hw_reg_write(hal, _val, _reg)

#define AR5K_REG_SM(_val, _flags)                   \
    (((_val) << _flags##_S) & (_flags))

#define AR5K_REG_MS(_val, _flags)                   \
    (((_val) & (_flags)) >> _flags##_S)

#define AR5K_REG_WRITE_BITS(_reg, _flags, _val)             \
    AR5K_REG_WRITE(_reg, (AR5K_REG_READ(_reg) & ~(_flags)) | \
        (((_val) << _flags##_S) & (_flags)))

#define AR5K_REG_MASKED_BITS(_reg, _flags, _mask)           \
    AR5K_REG_WRITE(_reg, (AR5K_REG_READ(_reg) & (_mask)) | (_flags))

#define AR5K_REG_ENABLE_BITS(_reg, _flags)              \
    AR5K_REG_WRITE(_reg, AR5K_REG_READ(_reg) | (_flags))

#define AR5K_REG_DISABLE_BITS(_reg, _flags)             \
    AR5K_REG_WRITE(_reg, AR5K_REG_READ(_reg) & ~(_flags))

#define AR5K_PHY_WRITE(_reg, _val)                  \
    AR5K_REG_WRITE(hal->ah_phy + ((_reg) << 2), _val)

#define AR5K_PHY_READ(_reg)                     \
    AR5K_REG_READ(hal->ah_phy + ((_reg) << 2))

#define AR5K_REG_WAIT(_i)                       \
    if (_i % 64) { \
        AR5K_DELAY(1); \
    }

#define AR5K_EEPROM_READ(_o, _v)    {               \
    if ((ret = hal->ah_eeprom_read(hal, (_o), &(_v))) != 0) { \
        return ret; \
    } \
}

#define AR5K_EEPROM_READ_HDR(_o, _v)                    \
    AR5K_EEPROM_READ(_o, hal->ah_capabilities.cap_eeprom._v)

/* Read status of selected queue */
#define AR5K_REG_READ_Q(_reg, _queue)                   \
    (AR5K_REG_READ(_reg) & (1 << _queue))

#define AR5K_REG_WRITE_Q(_reg, _queue)                  \
    AR5K_REG_WRITE(_reg, (1 << _queue))

#define AR5K_Q_ENABLE_BITS(_reg, _queue) do {               \
    _reg |= 1 << _queue;                        \
} while (0)

#define AR5K_Q_DISABLE_BITS(_reg, _queue) do {              \
    _reg &= ~(1 << _queue);                     \
} while (0)

/*
 * Unaligned little endian access
 */
#define AR5K_LE_READ_2  ath5k_hw_read_unaligned_16
#define AR5K_LE_READ_4  ath5k_hw_read_unaligned_32
#define AR5K_LE_WRITE_2 ath5k_hw_write_unaligned_16
#define AR5K_LE_WRITE_4 ath5k_hw_write_unaligned_32

#define AR5K_LOW_ID(_a)(                \
(_a)[0] | (_a)[1] << 8 | (_a)[2] << 16 | (_a)[3] << 24  \
)

#define AR5K_HIGH_ID(_a)    ((_a)[4] | (_a)[5] << 8)

/*
 * Initial register values
 */

/*
 * Common initial register values
 */
#define AR5K_INIT_MODE              CHANNEL_B

#define AR5K_INIT_TX_LATENCY            502
#define AR5K_INIT_USEC              39
#define AR5K_INIT_USEC_TURBO            79
#define AR5K_INIT_USEC_32           31
#define AR5K_INIT_CARR_SENSE_EN         1
#define AR5K_INIT_PROG_IFS          920
#define AR5K_INIT_PROG_IFS_TURBO        960
#define AR5K_INIT_EIFS              3440
#define AR5K_INIT_EIFS_TURBO            6880
#define AR5K_INIT_SLOT_TIME         396
#define AR5K_INIT_SLOT_TIME_TURBO       480
#define AR5K_INIT_ACK_CTS_TIMEOUT       1024
#define AR5K_INIT_ACK_CTS_TIMEOUT_TURBO     0x08000800
#define AR5K_INIT_SIFS              560
#define AR5K_INIT_SIFS_TURBO            480
#define AR5K_INIT_SH_RETRY          10
#define AR5K_INIT_LG_RETRY          AR5K_INIT_SH_RETRY
#define AR5K_INIT_SSH_RETRY         32
#define AR5K_INIT_SLG_RETRY         AR5K_INIT_SSH_RETRY
#define AR5K_INIT_TX_RETRY          10
#define AR5K_INIT_TOPS              8
#define AR5K_INIT_RXNOFRM           8
#define AR5K_INIT_RPGTO             0
#define AR5K_INIT_TXNOFRM           0
#define AR5K_INIT_BEACON_PERIOD         65535
#define AR5K_INIT_TIM_OFFSET            0
#define AR5K_INIT_BEACON_EN         0
#define AR5K_INIT_RESET_TSF         0

#define AR5K_INIT_TRANSMIT_LATENCY      (           \
    (AR5K_INIT_TX_LATENCY << 14) | (AR5K_INIT_USEC_32 << 7) |   \
    (AR5K_INIT_USEC)                        \
)
#define AR5K_INIT_TRANSMIT_LATENCY_TURBO    (           \
    (AR5K_INIT_TX_LATENCY << 14) | (AR5K_INIT_USEC_32 << 7) |   \
    (AR5K_INIT_USEC_TURBO)                      \
)
#define AR5K_INIT_PROTO_TIME_CNTRL      (           \
    (AR5K_INIT_CARR_SENSE_EN << 26) | (AR5K_INIT_EIFS << 12) |  \
    (AR5K_INIT_PROG_IFS)                        \
)
#define AR5K_INIT_PROTO_TIME_CNTRL_TURBO    (           \
    (AR5K_INIT_CARR_SENSE_EN << 26) | (AR5K_INIT_EIFS_TURBO << 12) |\
    (AR5K_INIT_PROG_IFS_TURBO)                  \
)
#define AR5K_INIT_BEACON_CONTROL        (           \
    (AR5K_INIT_RESET_TSF << 24) | (AR5K_INIT_BEACON_EN << 23) | \
    (AR5K_INIT_TIM_OFFSET << 16) | (AR5K_INIT_BEACON_PERIOD)    \
)

/*
 * Non - common initial register values
 */
struct ath5k_ini {
    uint16_t   ini_register;
    uint32_t   ini_value;

    enum {
        AR5K_INI_WRITE = 0,
        AR5K_INI_READ = 1,
    } ini_mode;
};

#define AR5K_INI_VAL_11A        0
#define AR5K_INI_VAL_11A_TURBO      1
#define AR5K_INI_VAL_11B        2
#define AR5K_INI_VAL_11G        3
#define AR5K_INI_VAL_11G_TURBO      4
#define AR5K_INI_VAL_XR         0
#define AR5K_INI_VAL_MAX        5

#define AR5K_INI_PHY_5111       0
#define AR5K_INI_PHY_5112       1
#define AR5K_INI_PHY_511X       1

#define AR5K_AR5111_INI_RF_MAX_BANKS    AR5K_MAX_RF_BANKS
#define AR5K_AR5112_INI_RF_MAX_BANKS    AR5K_MAX_RF_BANKS

struct ath5k_ini_rf {
    uint8_t    rf_bank;
    uint16_t   rf_register;
    uint32_t   rf_value[5];
};

#define AR5K_AR5111_INI_RF  {                       \
    { 0, 0x989c,                                \
        { 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000 } },   \
    { 0, 0x989c,                                \
        { 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000 } },   \
    { 0, 0x989c,                                \
        { 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000 } },   \
    { 0, 0x989c,                                \
        { 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000 } },   \
    { 0, 0x989c,                                \
        { 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000 } },   \
    { 0, 0x989c,                                \
        { 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000 } },   \
    { 0, 0x989c,                                \
        { 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000 } },   \
    { 0, 0x989c,                                \
        { 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000 } },   \
    { 0, 0x989c,                                \
        { 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000 } },   \
    { 0, 0x989c,                                \
        { 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000 } },   \
    { 0, 0x989c,                                \
        { 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000 } },   \
    { 0, 0x989c,                                \
        { 0x00380000, 0x00380000, 0x00380000, 0x00380000, 0x00380000 } },   \
    { 0, 0x989c,                                \
        { 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000 } },   \
    { 0, 0x989c,                                \
        { 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000 } },   \
    { 0, 0x989c,                                \
        { 0x00000000, 0x00000000, 0x000000c0, 0x00000080, 0x00000080 } },   \
    { 0, 0x989c,                                \
        { 0x000400f9, 0x000400f9, 0x000400ff, 0x000400fd, 0x000400fd } },   \
    { 0, 0x98d4,                                \
        { 0x00000000, 0x00000000, 0x00000004, 0x00000004, 0x00000004 } },   \
    { 1, 0x98d4,                                \
        { 0x00000020, 0x00000020, 0x00000020, 0x00000020, 0x00000020 } },   \
    { 2, 0x98d4,                                \
        { 0x00000010, 0x00000014, 0x00000010, 0x00000010, 0x00000014 } },   \
    { 3, 0x98d8,                                \
        { 0x00601068, 0x00601068, 0x00601068, 0x00601068, 0x00601068 } },   \
    { 6, 0x989c,                                \
        { 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000 } },   \
    { 6, 0x989c,                                \
        { 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000 } },   \
    { 6, 0x989c,                                \
        { 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000 } },   \
    { 6, 0x989c,                                \
        { 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000 } },   \
    { 6, 0x989c,                                \
        { 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000 } },   \
    { 6, 0x989c,                                \
        { 0x10000000, 0x10000000, 0x10000000, 0x10000000, 0x10000000 } },   \
    { 6, 0x989c,                                \
        { 0x04000000, 0x04000000, 0x04000000, 0x04000000, 0x04000000 } },   \
    { 6, 0x989c,                                \
        { 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000 } },   \
    { 6, 0x989c,                                \
        { 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000 } },   \
    { 6, 0x989c,                                \
        { 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000 } },   \
    { 6, 0x989c,                                \
        { 0x00000000, 0x00000000, 0x0a000000, 0x00000000, 0x00000000 } },   \
    { 6, 0x989c,                                \
        { 0x003800c0, 0x00380080, 0x023800c0, 0x003800c0, 0x003800c0 } },   \
    { 6, 0x989c,                                \
        { 0x00020006, 0x00020006, 0x00000006, 0x00020006, 0x00020006 } },   \
    { 6, 0x989c,                                \
        { 0x00000089, 0x00000089, 0x00000089, 0x00000089, 0x00000089 } },   \
    { 6, 0x989c,                                \
        { 0x000000a0, 0x000000a0, 0x000000a0, 0x000000a0, 0x000000a0 } },   \
    { 6, 0x989c,                                \
        { 0x00040007, 0x00040007, 0x00040007, 0x00040007, 0x00040007 } },   \
    { 6, 0x98d4,                                \
        { 0x0000001a, 0x0000001a, 0x0000001a, 0x0000001a, 0x0000001a } },   \
    { 7, 0x989c,                                \
        { 0x00000040, 0x00000048, 0x00000040, 0x00000040, 0x00000040 } },   \
    { 7, 0x989c,                                \
        { 0x00000010, 0x00000010, 0x00000010, 0x00000010, 0x00000010 } },   \
    { 7, 0x989c,                                \
        { 0x00000008, 0x00000008, 0x00000008, 0x00000008, 0x00000008 } },   \
    { 7, 0x989c,                                \
        { 0x0000004f, 0x0000004f, 0x0000004f, 0x0000004f, 0x0000004f } },   \
    { 7, 0x989c,                                \
        { 0x000000f1, 0x000000f1, 0x00000061, 0x000000f1, 0x000000f1 } },   \
    { 7, 0x989c,                                \
        { 0x0000904f, 0x0000904f, 0x0000904c, 0x0000904f, 0x0000904f } },   \
    { 7, 0x989c,                                \
        { 0x0000125a, 0x0000125a, 0x0000129a, 0x0000125a, 0x0000125a } },   \
    { 7, 0x98cc,                                \
        { 0x0000000e, 0x0000000e, 0x0000000f, 0x0000000e, 0x0000000e } },   \
}

#define AR5K_AR5112_INI_RF  {                       \
    { 1, 0x98d4,                                \
        { 0x00000020, 0x00000020, 0x00000020, 0x00000020, 0x00000020 } },   \
    { 2, 0x98d0,                                \
        { 0x03060408, 0x03070408, 0x03060408, 0x03060408, 0x03070408 } },   \
    { 3, 0x98dc,                                \
        { 0x00a0c0c0, 0x00a0c0c0, 0x00e0c0c0, 0x00e0c0c0, 0x00e0c0c0 } },   \
    { 6, 0x989c,                                \
        { 0x00a00000, 0x00a00000, 0x00a00000, 0x00a00000, 0x00a00000 } },   \
    { 6, 0x989c,                                \
        { 0x000a0000, 0x000a0000, 0x000a0000, 0x000a0000, 0x000a0000 } },   \
    { 6, 0x989c,                                \
        { 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000 } },   \
    { 6, 0x989c,                                \
        { 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000 } },   \
    { 6, 0x989c,                                \
        { 0x00660000, 0x00660000, 0x00660000, 0x00660000, 0x00660000 } },   \
    { 6, 0x989c,                                \
        { 0x00db0000, 0x00db0000, 0x00db0000, 0x00db0000, 0x00db0000 } },   \
    { 6, 0x989c,                                \
        { 0x00f10000, 0x00f10000, 0x00f10000, 0x00f10000, 0x00f10000 } },   \
    { 6, 0x989c,                                \
        { 0x00120000, 0x00120000, 0x00120000, 0x00120000, 0x00120000 } },   \
    { 6, 0x989c,                                \
        { 0x00120000, 0x00120000, 0x00120000, 0x00120000, 0x00120000 } },   \
    { 6, 0x989c,                                \
        { 0x00730000, 0x00730000, 0x00730000, 0x00730000, 0x00730000 } },   \
    { 6, 0x989c,                                \
        { 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000 } },   \
    { 6, 0x989c,                                \
        { 0x000c0000, 0x000c0000, 0x000c0000, 0x000c0000, 0x000c0000 } },   \
    { 6, 0x989c,                                \
        { 0x00ff0000, 0x00ff0000, 0x00ff0000, 0x00ff0000, 0x00ff0000 } },   \
    { 6, 0x989c,                                \
        { 0x00ff0000, 0x00ff0000, 0x00ff0000, 0x00ff0000, 0x00ff0000 } },   \
    { 6, 0x989c,                                \
        { 0x008b0000, 0x008b0000, 0x008b0000, 0x008b0000, 0x008b0000 } },   \
    { 6, 0x989c,                                \
        { 0x00600000, 0x00600000, 0x00600000, 0x00600000, 0x00600000 } },   \
    { 6, 0x989c,                                \
        { 0x000c0000, 0x000c0000, 0x000c0000, 0x000c0000, 0x000c0000 } },   \
    { 6, 0x989c,                                \
        { 0x00840000, 0x00840000, 0x00840000, 0x00840000, 0x00840000 } },   \
    { 6, 0x989c,                                \
        { 0x00640000, 0x00640000, 0x00640000, 0x00640000, 0x00640000 } },   \
    { 6, 0x989c,                                \
        { 0x00200000, 0x00200000, 0x00200000, 0x00200000, 0x00200000 } },   \
    { 6, 0x989c,                                \
        { 0x00240000, 0x00240000, 0x00240000, 0x00240000, 0x00240000 } },   \
    { 6, 0x989c,                                \
        { 0x00250000, 0x00250000, 0x00250000, 0x00250000, 0x00250000 } },   \
    { 6, 0x989c,                                \
        { 0x00110000, 0x00110000, 0x00110000, 0x00110000, 0x00110000 } },   \
    { 6, 0x989c,                                \
        { 0x00110000, 0x00110000, 0x00110000, 0x00110000, 0x00110000 } },   \
    { 6, 0x989c,                                \
        { 0x00510000, 0x00510000, 0x00510000, 0x00510000, 0x00510000 } },   \
    { 6, 0x989c,                                \
        { 0x1c040000, 0x1c040000, 0x1c040000, 0x1c040000, 0x1c040000 } },   \
    { 6, 0x989c,                                \
        { 0x000a0000, 0x000a0000, 0x000a0000, 0x000a0000, 0x000a0000 } },   \
    { 6, 0x989c,                                \
        { 0x00a10000, 0x00a10000, 0x00a10000, 0x00a10000, 0x00a10000 } },   \
    { 6, 0x989c,                                \
        { 0x00400000, 0x00400000, 0x00400000, 0x00400000, 0x00400000 } },   \
    { 6, 0x989c,                                \
        { 0x03090000, 0x03090000, 0x03090000, 0x03090000, 0x03090000 } },   \
    { 6, 0x989c,                                \
        { 0x06000000, 0x06000000, 0x06000000, 0x06000000, 0x06000000 } },   \
    { 6, 0x989c,                                \
        { 0x000000b0, 0x000000b0, 0x000000a8, 0x000000a8, 0x000000a8 } },   \
    { 6, 0x989c,                                \
        { 0x0000002e, 0x0000002e, 0x0000002e, 0x0000002e, 0x0000002e } },   \
    { 6, 0x989c,                                \
        { 0x006c4a41, 0x006c4a41, 0x006c4af1, 0x006c4a61, 0x006c4a61 } },   \
    { 6, 0x989c,                                \
        { 0x0050892a, 0x0050892a, 0x0050892b, 0x0050892b, 0x0050892b } },   \
    { 6, 0x989c,                                \
        { 0x00842400, 0x00842400, 0x00842400, 0x00842400, 0x00842400 } },   \
    { 6, 0x989c,                                \
        { 0x00c69200, 0x00c69200, 0x00c69200, 0x00c69200, 0x00c69200 } },   \
    { 6, 0x98d0,                                \
        { 0x0002000c, 0x0002000c, 0x0002000c, 0x0002000c, 0x0002000c } },   \
    { 7, 0x989c,                                \
        { 0x00000094, 0x00000094, 0x00000094, 0x00000094, 0x00000094 } },   \
    { 7, 0x989c,                                \
        { 0x00000091, 0x00000091, 0x00000091, 0x00000091, 0x00000091 } },   \
    { 7, 0x989c,                                \
        { 0x0000000a, 0x0000000a, 0x00000012, 0x00000012, 0x00000012 } },   \
    { 7, 0x989c,                                \
        { 0x00000080, 0x00000080, 0x00000080, 0x00000080, 0x00000080 } },   \
    { 7, 0x989c,                                \
        { 0x000000c1, 0x000000c1, 0x000000c1, 0x000000c1, 0x000000c1 } },   \
    { 7, 0x989c,                                \
        { 0x00000060, 0x00000060, 0x00000060, 0x00000060, 0x00000060 } },   \
    { 7, 0x989c,                                \
        { 0x000000f0, 0x000000f0, 0x000000f0, 0x000000f0, 0x000000f0 } },   \
    { 7, 0x989c,                                \
        { 0x00000022, 0x00000022, 0x00000022, 0x00000022, 0x00000022 } },   \
    { 7, 0x989c,                                \
        { 0x00000092, 0x00000092, 0x00000092, 0x00000092, 0x00000092 } },   \
    { 7, 0x989c,                                \
        { 0x000000d4, 0x000000d4, 0x000000d4, 0x000000d4, 0x000000d4 } },   \
    { 7, 0x989c,                                \
        { 0x000014cc, 0x000014cc, 0x000014cc, 0x000014cc, 0x000014cc } },   \
    { 7, 0x989c,                                \
        { 0x0000048c, 0x0000048c, 0x0000048c, 0x0000048c, 0x0000048c } },   \
    { 7, 0x98c4,                                \
        { 0x00000003, 0x00000003, 0x00000003, 0x00000003, 0x00000003 } },   \
    }

#define AR5K_AR5112A_INI_RF     {                       \
    { 1, 0x98d4,                                \
        { 0x00000020, 0x00000020, 0x00000020, 0x00000020, 0x00000020 } },   \
    { 2, 0x98d0,                                \
        { 0x03060408, 0x03070408, 0x03060408, 0x03060408, 0x03070408 } },   \
    { 3, 0x98dc,                                \
        { 0x00a0c0c0, 0x00a0c0c0, 0x00e0c0c0, 0x00e0c0c0, 0x00e0c0c0 } },   \
    { 6, 0x989c,                                \
        { 0x0f000000, 0x0f000000, 0x0f000000, 0x0f000000, 0x0f000000 } },   \
    { 6, 0x989c,                                \
        { 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000 } },   \
    { 6, 0x989c,                                \
        { 0x00800000, 0x00800000, 0x00800000, 0x00800000, 0x00800000 } },   \
    { 6, 0x989c,                                \
        { 0x002a0000, 0x002a0000, 0x002a0000, 0x002a0000, 0x002a0000 } },   \
    { 6, 0x989c,                                \
        { 0x00010000, 0x00010000, 0x00010000, 0x00010000, 0x00010000 } },   \
    { 6, 0x989c,                                \
        { 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000 } },   \
    { 6, 0x989c,                                \
        { 0x00180000, 0x00180000, 0x00180000, 0x00180000, 0x00180000 } },   \
    { 6, 0x989c,                                \
        { 0x00600000, 0x00600000, 0x006e0000, 0x006e0000, 0x006e0000 } },   \
    { 6, 0x989c,                                \
        { 0x00c70000, 0x00c70000, 0x00c70000, 0x00c70000, 0x00c70000 } },   \
    { 6, 0x989c,                                \
        { 0x004b0000, 0x004b0000, 0x004b0000, 0x004b0000, 0x004b0000 } },   \
    { 6, 0x989c,                                \
        { 0x04480000, 0x04480000, 0x04480000, 0x04480000, 0x04480000 } },   \
    { 6, 0x989c,                                \
        { 0x00220000, 0x00220000, 0x00220000, 0x00220000, 0x00220000 } },   \
    { 6, 0x989c,                                \
        { 0x00e40000, 0x00e40000, 0x00e40000, 0x00e40000, 0x00e40000 } },   \
    { 6, 0x989c,                                \
        { 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000 } },   \
    { 6, 0x989c,                                \
        { 0x00fc0000, 0x00fc0000, 0x00fc0000, 0x00fc0000, 0x00fc0000 } },   \
    { 6, 0x989c,                                \
        { 0x00ff0000, 0x00ff0000, 0x00ff0000, 0x00ff0000, 0x00ff0000 } },   \
    { 6, 0x989c,                                \
        { 0x043f0000, 0x043f0000, 0x043f0000, 0x043f0000, 0x043f0000 } },   \
    { 6, 0x989c,                                \
        { 0x000c0000, 0x000c0000, 0x000c0000, 0x000c0000, 0x000c0000 } },   \
    { 6, 0x989c,                                \
        { 0x00190000, 0x00190000, 0x00190000, 0x00190000, 0x00190000 } },   \
    { 6, 0x989c,                                \
        { 0x00240000, 0x00240000, 0x00240000, 0x00240000, 0x00240000 } },   \
    { 6, 0x989c,                                \
        { 0x00b40000, 0x00b40000, 0x00b40000, 0x00b40000, 0x00b40000 } },   \
    { 6, 0x989c,                                \
        { 0x00990000, 0x00990000, 0x00990000, 0x00990000, 0x00990000 } },   \
    { 6, 0x989c,                                \
        { 0x00500000, 0x00500000, 0x00500000, 0x00500000, 0x00500000 } },   \
    { 6, 0x989c,                                \
        { 0x002a0000, 0x002a0000, 0x002a0000, 0x002a0000, 0x002a0000 } },   \
    { 6, 0x989c,                                \
        { 0x00120000, 0x00120000, 0x00120000, 0x00120000, 0x00120000 } },   \
    { 6, 0x989c,                                \
        { 0xc0320000, 0xc0320000, 0xc0320000, 0xc0320000, 0xc0320000 } },   \
    { 6, 0x989c,                                \
        { 0x01740000, 0x01740000, 0x01740000, 0x01740000, 0x01740000 } },   \
    { 6, 0x989c,                                \
        { 0x00110000, 0x00110000, 0x00110000, 0x00110000, 0x00110000 } },   \
    { 6, 0x989c,                                \
        { 0x86280000, 0x86280000, 0x86280000, 0x86280000, 0x86280000 } },   \
    { 6, 0x989c,                                \
        { 0x31840000, 0x31840000, 0x31840000, 0x31840000, 0x31840000 } },   \
    { 6, 0x989c,                                \
        { 0x00020080, 0x00020080, 0x00020080, 0x00020080, 0x00020080 } },   \
    { 6, 0x989c,                                \
        { 0x00080009, 0x00080009, 0x00080009, 0x00080009, 0x00080009 } },   \
    { 6, 0x989c,                                \
        { 0x00000003, 0x00000003, 0x00000003, 0x00000003, 0x00000003 } },   \
    { 6, 0x989c,                                \
        { 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000 } },   \
    { 6, 0x989c,                                \
        { 0x000000b2, 0x000000b2, 0x000000b2, 0x000000b2, 0x000000b2 } },   \
    { 6, 0x989c,                                \
        { 0x00b02084, 0x00b02084, 0x00b02084, 0x00b02084, 0x00b02084 } },   \
    { 6, 0x989c,                                \
        { 0x004125a4, 0x004125a4, 0x004125a4, 0x004125a4, 0x004125a4 } },   \
    { 6, 0x989c,                                \
        { 0x00119220, 0x00119220, 0x00119220, 0x00119220, 0x00119220 } },   \
    { 6, 0x989c,                                \
        { 0x001a4800, 0x001a4800, 0x001a4800, 0x001a4800, 0x001a4800 } },   \
    { 6, 0x98d8,                                \
        { 0x000b0230, 0x000b0230, 0x000b0230, 0x000b0230, 0x000b0230 } },   \
    { 7, 0x989c,                                \
        { 0x00000094, 0x00000094, 0x00000094, 0x00000094, 0x00000094 } },   \
    { 7, 0x989c,                                \
        { 0x00000091, 0x00000091, 0x00000091, 0x00000091, 0x00000091 } },   \
    { 7, 0x989c,                                \
        { 0x00000012, 0x00000012, 0x00000012, 0x00000012, 0x00000012 } },   \
    { 7, 0x989c,                                \
        { 0x00000080, 0x00000080, 0x00000080, 0x00000080, 0x00000080 } },   \
    { 7, 0x989c,                                \
        { 0x000000d9, 0x000000d9, 0x000000d9, 0x000000d9, 0x000000d9 } },   \
    { 7, 0x989c,                                \
        { 0x00000060, 0x00000060, 0x00000060, 0x00000060, 0x00000060 } },   \
    { 7, 0x989c,                                \
        { 0x000000f0, 0x000000f0, 0x000000f0, 0x000000f0, 0x000000f0 } },   \
    { 7, 0x989c,                                \
        { 0x000000a2, 0x000000a2, 0x000000a2, 0x000000a2, 0x000000a2 } },   \
    { 7, 0x989c,                                \
        { 0x00000052, 0x00000052, 0x00000052, 0x00000052, 0x00000052 } },   \
    { 7, 0x989c,                                \
        { 0x000000d4, 0x000000d4, 0x000000d4, 0x000000d4, 0x000000d4 } },   \
    { 7, 0x989c,                                \
        { 0x000014cc, 0x000014cc, 0x000014cc, 0x000014cc, 0x000014cc } },   \
    { 7, 0x989c,                                \
        { 0x0000048c, 0x0000048c, 0x0000048c, 0x0000048c, 0x0000048c } },   \
    { 7, 0x98c4,                                \
        { 0x00000003, 0x00000003, 0x00000003, 0x00000003, 0x00000003 } },   \
}

struct ath5k_ini_rfgain {
    uint16_t   rfg_register;
    uint32_t   rfg_value[2][2];

#define AR5K_INI_RFGAIN_5GHZ    0
#define AR5K_INI_RFGAIN_2GHZ    1
};

#define AR5K_INI_RFGAIN {                           \
    { 0x9a00, {                             \
        { 0x000001a9, 0x00000000 }, { 0x00000007, 0x00000007 } } }, \
    { 0x9a04, {                             \
        { 0x000001e9, 0x00000040 }, { 0x00000047, 0x00000047 } } }, \
    { 0x9a08, {                             \
        { 0x00000029, 0x00000080 }, { 0x00000087, 0x00000087 } } }, \
    { 0x9a0c, {                             \
        { 0x00000069, 0x00000150 }, { 0x000001a0, 0x000001a0 } } }, \
    { 0x9a10, {                             \
        { 0x00000199, 0x00000190 }, { 0x000001e0, 0x000001e0 } } }, \
    { 0x9a14, {                             \
        { 0x000001d9, 0x000001d0 }, { 0x00000020, 0x00000020 } } }, \
    { 0x9a18, {                             \
        { 0x00000019, 0x00000010 }, { 0x00000060, 0x00000060 } } }, \
    { 0x9a1c, {                             \
        { 0x00000059, 0x00000044 }, { 0x000001a1, 0x000001a1 } } }, \
    { 0x9a20, {                             \
        { 0x00000099, 0x00000084 }, { 0x000001e1, 0x000001e1 } } }, \
    { 0x9a24, {                             \
        { 0x000001a5, 0x00000148 }, { 0x00000021, 0x00000021 } } }, \
    { 0x9a28, {                             \
        { 0x000001e5, 0x00000188 }, { 0x00000061, 0x00000061 } } }, \
    { 0x9a2c, {                             \
        { 0x00000025, 0x000001c8 }, { 0x00000162, 0x00000162 } } }, \
    { 0x9a30, {                             \
        { 0x000001c8, 0x00000014 }, { 0x000001a2, 0x000001a2 } } }, \
    { 0x9a34, {                             \
        { 0x00000008, 0x00000042 }, { 0x000001e2, 0x000001e2 } } }, \
    { 0x9a38, {                             \
        { 0x00000048, 0x00000082 }, { 0x00000022, 0x00000022 } } }, \
    { 0x9a3c, {                             \
        { 0x00000088, 0x00000178 }, { 0x00000062, 0x00000062 } } }, \
    { 0x9a40, {                             \
        { 0x00000198, 0x000001b8 }, { 0x00000163, 0x00000163 } } }, \
    { 0x9a44, {                             \
        { 0x000001d8, 0x000001f8 }, { 0x000001a3, 0x000001a3 } } }, \
    { 0x9a48, {                             \
        { 0x00000018, 0x00000012 }, { 0x000001e3, 0x000001e3 } } }, \
    { 0x9a4c, {                             \
        { 0x00000058, 0x00000052 }, { 0x00000023, 0x00000023 } } }, \
    { 0x9a50, {                             \
        { 0x00000098, 0x00000092 }, { 0x00000063, 0x00000063 } } }, \
    { 0x9a54, {                             \
        { 0x000001a4, 0x0000017c }, { 0x00000184, 0x00000184 } } }, \
    { 0x9a58, {                             \
        { 0x000001e4, 0x000001bc }, { 0x000001c4, 0x000001c4 } } }, \
    { 0x9a5c, {                             \
        { 0x00000024, 0x000001fc }, { 0x00000004, 0x00000004 } } }, \
    { 0x9a60, {                             \
        { 0x00000064, 0x0000000a }, { 0x000001ea, 0x0000000b } } }, \
    { 0x9a64, {                             \
        { 0x000000a4, 0x0000004a }, { 0x0000002a, 0x0000004b } } }, \
    { 0x9a68, {                             \
        { 0x000000e4, 0x0000008a }, { 0x0000006a, 0x0000008b } } }, \
    { 0x9a6c, {                             \
        { 0x0000010a, 0x0000015a }, { 0x000000aa, 0x000001ac } } }, \
    { 0x9a70, {                             \
        { 0x0000014a, 0x0000019a }, { 0x000001ab, 0x000001ec } } }, \
    { 0x9a74, {                             \
        { 0x0000018a, 0x000001da }, { 0x000001eb, 0x0000002c } } }, \
    { 0x9a78, {                             \
        { 0x000001ca, 0x0000000e }, { 0x0000002b, 0x00000012 } } }, \
    { 0x9a7c, {                             \
        { 0x0000000a, 0x0000004e }, { 0x0000006b, 0x00000052 } } }, \
    { 0x9a80, {                             \
        { 0x0000004a, 0x0000008e }, { 0x000000ab, 0x00000092 } } }, \
    { 0x9a84, {                             \
        { 0x0000008a, 0x0000015e }, { 0x000001ac, 0x00000193 } } }, \
    { 0x9a88, {                             \
        { 0x000001ba, 0x0000019e }, { 0x000001ec, 0x000001d3 } } }, \
    { 0x9a8c, {                             \
        { 0x000001fa, 0x000001de }, { 0x0000002c, 0x00000013 } } }, \
    { 0x9a90, {                             \
        { 0x0000003a, 0x00000009 }, { 0x0000003a, 0x00000053 } } }, \
    { 0x9a94, {                             \
        { 0x0000007a, 0x00000049 }, { 0x0000007a, 0x00000093 } } }, \
    { 0x9a98, {                             \
        { 0x00000186, 0x00000089 }, { 0x000000ba, 0x00000194 } } }, \
    { 0x9a9c, {                             \
        { 0x000001c6, 0x00000179 }, { 0x000001bb, 0x000001d4 } } }, \
    { 0x9aa0, {                             \
        { 0x00000006, 0x000001b9 }, { 0x000001fb, 0x00000014 } } }, \
    { 0x9aa4, {                             \
        { 0x00000046, 0x000001f9 }, { 0x0000003b, 0x0000003a } } }, \
    { 0x9aa8, {                             \
        { 0x00000086, 0x00000039 }, { 0x0000007b, 0x0000007a } } }, \
    { 0x9aac, {                             \
        { 0x000000c6, 0x00000079 }, { 0x000000bb, 0x000000ba } } }, \
    { 0x9ab0, {                             \
        { 0x000000c6, 0x000000b9 }, { 0x000001bc, 0x000001bb } } }, \
    { 0x9ab4, {                             \
        { 0x000000c6, 0x000001bd }, { 0x000001fc, 0x000001fb } } }, \
    { 0x9ab8, {                             \
        { 0x000000c6, 0x000001fd }, { 0x0000003c, 0x0000003b } } }, \
    { 0x9abc, {                             \
        { 0x000000c6, 0x0000003d }, { 0x0000007c, 0x0000007b } } }, \
    { 0x9ac0, {                             \
        { 0x000000c6, 0x0000007d }, { 0x000000bc, 0x000000bb } } }, \
    { 0x9ac4, {                             \
        { 0x000000c6, 0x000000bd }, { 0x000000fc, 0x000001bc } } }, \
    { 0x9ac8, {                             \
        { 0x000000c6, 0x000000fd }, { 0x000000fc, 0x000001fc } } }, \
    { 0x9acc, {                             \
        { 0x000000c6, 0x000000fd }, { 0x000000fc, 0x0000003c } } }, \
    { 0x9ad0, {                             \
        { 0x000000c6, 0x000000fd }, { 0x000000fc, 0x0000007c } } }, \
    { 0x9ad4, {                             \
        { 0x000000c6, 0x000000fd }, { 0x000000fc, 0x000000bc } } }, \
    { 0x9ad8, {                             \
        { 0x000000c6, 0x000000fd }, { 0x000000fc, 0x000000fc } } }, \
    { 0x9adc, {                             \
        { 0x000000c6, 0x000000fd }, { 0x000000fc, 0x000000fc } } }, \
    { 0x9ae0, {                             \
        { 0x000000c6, 0x000000fd }, { 0x000000fc, 0x000000fc } } }, \
    { 0x9ae4, {                             \
        { 0x000000c6, 0x000000fd }, { 0x000000fc, 0x000000fc } } }, \
    { 0x9ae8, {                             \
        { 0x000000c6, 0x000000fd }, { 0x000000fc, 0x000000fc } } }, \
    { 0x9aec, {                             \
        { 0x000000c6, 0x000000fd }, { 0x000000fc, 0x000000fc } } }, \
    { 0x9af0, {                             \
        { 0x000000c6, 0x000000fd }, { 0x000000fc, 0x000000fc } } }, \
    { 0x9af4, {                             \
        { 0x000000c6, 0x000000fd }, { 0x000000fc, 0x000000fc } } }, \
    { 0x9af8, {                             \
        { 0x000000c6, 0x000000fd }, { 0x000000fc, 0x000000fc } } }, \
    { 0x9afc, {                             \
        { 0x000000c6, 0x000000fd }, { 0x000000fc, 0x000000fc } } }, \
}

#define AR5K_DESC_RX_PHY_ERROR_NONE     0x00
#define AR5K_DESC_RX_PHY_ERROR_TIMING       0x20
#define AR5K_DESC_RX_PHY_ERROR_PARITY       0x40
#define AR5K_DESC_RX_PHY_ERROR_RATE     0x60
#define AR5K_DESC_RX_PHY_ERROR_LENGTH       0x80
#define AR5K_DESC_RX_PHY_ERROR_64QAM        0xa0
#define AR5K_DESC_RX_PHY_ERROR_SERVICE      0xc0
#define AR5K_DESC_RX_PHY_ERROR_TRANSMITOVR  0xe0

/*
 * Initial register values which have to be loaded into the
 * card at boot time and after each reset.
 */

#define AR5K_AR5211_INI {                       \
    { 0x000c, 0x00000000 },                     \
    { 0x0028, 0x84849c9c },                     \
    { 0x002c, 0x7c7c7c7c },                     \
    { 0x0034, 0x00000005 },                     \
    { 0x0040, 0x00000000 },                     \
    { 0x0044, 0x00000008 },                     \
    { 0x0048, 0x00000008 },                     \
    { 0x004c, 0x00000010 },                     \
    { 0x0050, 0x00000000 },                     \
    { 0x0054, 0x0000001f },                     \
    { 0x0800, 0x00000000 },                     \
    { 0x0804, 0x00000000 },                     \
    { 0x0808, 0x00000000 },                     \
    { 0x080c, 0x00000000 },                     \
    { 0x0810, 0x00000000 },                     \
    { 0x0814, 0x00000000 },                     \
    { 0x0818, 0x00000000 },                     \
    { 0x081c, 0x00000000 },                     \
    { 0x0820, 0x00000000 },                     \
    { 0x0824, 0x00000000 },                     \
    { 0x1230, 0x00000000 },                     \
    { 0x8004, 0x00000000 },                     \
    { 0x8008, 0x00000000 },                     \
    { 0x800c, 0x00000000 },                     \
    { 0x8018, 0x00000000 },                     \
    { 0x8024, 0x00000000 },                     \
    { 0x8028, 0x00000030 },                     \
    { 0x802c, 0x0007ffff },                     \
    { 0x8030, 0x01ffffff },                     \
    { 0x8034, 0x00000031 },                     \
    { 0x8038, 0x00000000 },                     \
    { 0x803c, 0x00000000 },                     \
    { 0x8040, 0x00000000 },                     \
    { 0x8044, 0x00000002 },                     \
    { 0x8048, 0x00000000 },                     \
    { 0x8054, 0x00000000 },                     \
    { 0x8058, 0x00000000 },                     \
        /* PHY registers */                     \
    { 0x9808, 0x00000000 },                     \
    { 0x980c, 0x2d849093 },                     \
    { 0x9810, 0x7d32e000 },                     \
    { 0x9814, 0x00000f6b },                     \
    { 0x981c, 0x00000000 },                     \
    { 0x982c, 0x00026ffe },                     \
    { 0x9830, 0x00000000 },                     \
    { 0x983c, 0x00020100 },                     \
    { 0x9840, 0x206a017a },                     \
    { 0x984c, 0x1284613c },                     \
    { 0x9854, 0x00000859 },                     \
    { 0x9868, 0x409a4190 },                     \
    { 0x986c, 0x050cb081 },                     \
    { 0x9870, 0x0000000f },                     \
    { 0x9874, 0x00000080 },                     \
    { 0x9878, 0x0000000c },                     \
    { 0x9900, 0x00000000 },                     \
    { 0x9904, 0x00000000 },                     \
    { 0x9908, 0x00000000 },                     \
    { 0x990c, 0x00800000 },                     \
    { 0x9910, 0x00000001 },                     \
    { 0x991c, 0x0000092a },                     \
    { 0x9920, 0x00000000 },                     \
    { 0x9924, 0x00058a05 },                     \
    { 0x9928, 0x00000001 },                     \
    { 0x992c, 0x00000000 },                     \
    { 0x9930, 0x00000000 },                     \
    { 0x9934, 0x00000000 },                     \
    { 0x9938, 0x00000000 },                     \
    { 0x993c, 0x0000003f },                     \
    { 0x9940, 0x00000004 },                     \
    { 0x9948, 0x00000000 },                     \
    { 0x994c, 0x00000000 },                     \
    { 0x9950, 0x00000000 },                     \
    { 0x9954, 0x5d50f14c },                     \
    { 0x9958, 0x00000018 },                     \
    { 0x995c, 0x004b6a8e },                     \
    { 0xa184, 0x06ff05ff },                     \
    { 0xa188, 0x07ff07ff },                     \
    { 0xa18c, 0x08ff08ff },                     \
    { 0xa190, 0x09ff09ff },                     \
    { 0xa194, 0x0aff0aff },                     \
    { 0xa198, 0x0bff0bff },                     \
    { 0xa19c, 0x0cff0cff },                     \
    { 0xa1a0, 0x0dff0dff },                     \
    { 0xa1a4, 0x0fff0eff },                     \
    { 0xa1a8, 0x12ff12ff },                     \
    { 0xa1ac, 0x14ff13ff },                     \
    { 0xa1b0, 0x16ff15ff },                     \
    { 0xa1b4, 0x19ff17ff },                     \
    { 0xa1b8, 0x1bff1aff },                     \
    { 0xa1bc, 0x1eff1dff },                     \
    { 0xa1c0, 0x23ff20ff },                     \
    { 0xa1c4, 0x27ff25ff },                     \
    { 0xa1c8, 0x2cff29ff },                     \
    { 0xa1cc, 0x31ff2fff },                     \
    { 0xa1d0, 0x37ff34ff },                     \
    { 0xa1d4, 0x3aff3aff },                     \
    { 0xa1d8, 0x3aff3aff },                     \
    { 0xa1dc, 0x3aff3aff },                     \
    { 0xa1e0, 0x3aff3aff },                     \
    { 0xa1e4, 0x3aff3aff },                     \
    { 0xa1e8, 0x3aff3aff },                     \
    { 0xa1ec, 0x3aff3aff },                     \
    { 0xa1f0, 0x3aff3aff },                     \
    { 0xa1f4, 0x3aff3aff },                     \
    { 0xa1f8, 0x3aff3aff },                     \
    { 0xa1fc, 0x3aff3aff },                     \
        /* BB gain table (64bytes) */                   \
    { 0x9b00, 0x00000000 },                     \
    { 0x9b04, 0x00000020 },                     \
    { 0x9b08, 0x00000010 },                     \
    { 0x9b0c, 0x00000030 },                     \
    { 0x9b10, 0x00000008 },                     \
    { 0x9b14, 0x00000028 },                     \
    { 0x9b18, 0x00000004 },                     \
    { 0x9b1c, 0x00000024 },                     \
    { 0x9b20, 0x00000014 },                     \
    { 0x9b24, 0x00000034 },                     \
    { 0x9b28, 0x0000000c },                     \
    { 0x9b2c, 0x0000002c },                     \
    { 0x9b30, 0x00000002 },                     \
    { 0x9b34, 0x00000022 },                     \
    { 0x9b38, 0x00000012 },                     \
    { 0x9b3c, 0x00000032 },                     \
    { 0x9b40, 0x0000000a },                     \
    { 0x9b44, 0x0000002a },                     \
    { 0x9b48, 0x00000006 },                     \
    { 0x9b4c, 0x00000026 },                     \
    { 0x9b50, 0x00000016 },                     \
    { 0x9b54, 0x00000036 },                     \
    { 0x9b58, 0x0000000e },                     \
    { 0x9b5c, 0x0000002e },                     \
    { 0x9b60, 0x00000001 },                     \
    { 0x9b64, 0x00000021 },                     \
    { 0x9b68, 0x00000011 },                     \
    { 0x9b6c, 0x00000031 },                     \
    { 0x9b70, 0x00000009 },                     \
    { 0x9b74, 0x00000029 },                     \
    { 0x9b78, 0x00000005 },                     \
    { 0x9b7c, 0x00000025 },                     \
    { 0x9b80, 0x00000015 },                     \
    { 0x9b84, 0x00000035 },                     \
    { 0x9b88, 0x0000000d },                     \
    { 0x9b8c, 0x0000002d },                     \
    { 0x9b90, 0x00000003 },                     \
    { 0x9b94, 0x00000023 },                     \
    { 0x9b98, 0x00000013 },                     \
    { 0x9b9c, 0x00000033 },                     \
    { 0x9ba0, 0x0000000b },                     \
    { 0x9ba4, 0x0000002b },                     \
    { 0x9ba8, 0x0000002b },                     \
    { 0x9bac, 0x0000002b },                     \
    { 0x9bb0, 0x0000002b },                     \
    { 0x9bb4, 0x0000002b },                     \
    { 0x9bb8, 0x0000002b },                     \
    { 0x9bbc, 0x0000002b },                     \
    { 0x9bc0, 0x0000002b },                     \
    { 0x9bc4, 0x0000002b },                     \
    { 0x9bc8, 0x0000002b },                     \
    { 0x9bcc, 0x0000002b },                     \
    { 0x9bd0, 0x0000002b },                     \
    { 0x9bd4, 0x0000002b },                     \
    { 0x9bd8, 0x0000002b },                     \
    { 0x9bdc, 0x0000002b },                     \
    { 0x9be0, 0x0000002b },                     \
    { 0x9be4, 0x0000002b },                     \
    { 0x9be8, 0x0000002b },                     \
    { 0x9bec, 0x0000002b },                     \
    { 0x9bf0, 0x0000002b },                     \
    { 0x9bf4, 0x0000002b },                     \
    { 0x9bf8, 0x00000002 },                     \
    { 0x9bfc, 0x00000016 },                     \
        /* PHY activation */                        \
    { 0x98d4, 0x00000020 },                     \
    { 0x98d8, 0x00601068 },                     \
}

struct ath5k_ar5212_ini {
    uint8_t    ini_flags;
    uint16_t   ini_register;
    uint32_t   ini_value;

#define AR5K_INI_FLAG_511X  0x00
#define AR5K_INI_FLAG_5111  0x01
#define AR5K_INI_FLAG_5112  0x02
#define AR5K_INI_FLAG_BOTH  (AR5K_INI_FLAG_5111 | AR5K_INI_FLAG_5112)
};

#define AR5K_AR5212_INI {                       \
    { AR5K_INI_FLAG_BOTH, 0x000c, 0x00000000 },         \
    { AR5K_INI_FLAG_BOTH, 0x0034, 0x00000005 },         \
    { AR5K_INI_FLAG_BOTH, 0x0040, 0x00000000 },         \
    { AR5K_INI_FLAG_BOTH, 0x0044, 0x00000008 },         \
    { AR5K_INI_FLAG_BOTH, 0x0048, 0x00000008 },         \
    { AR5K_INI_FLAG_BOTH, 0x004c, 0x00000010 },         \
    { AR5K_INI_FLAG_BOTH, 0x0050, 0x00000000 },         \
    { AR5K_INI_FLAG_BOTH, 0x0054, 0x0000001f },         \
    { AR5K_INI_FLAG_BOTH, 0x0800, 0x00000000 },         \
    { AR5K_INI_FLAG_BOTH, 0x0804, 0x00000000 },         \
    { AR5K_INI_FLAG_BOTH, 0x0808, 0x00000000 },         \
    { AR5K_INI_FLAG_BOTH, 0x080c, 0x00000000 },         \
    { AR5K_INI_FLAG_BOTH, 0x0810, 0x00000000 },         \
    { AR5K_INI_FLAG_BOTH, 0x0814, 0x00000000 },         \
    { AR5K_INI_FLAG_BOTH, 0x0818, 0x00000000 },         \
    { AR5K_INI_FLAG_BOTH, 0x081c, 0x00000000 },         \
    { AR5K_INI_FLAG_BOTH, 0x0820, 0x00000000 },         \
    { AR5K_INI_FLAG_BOTH, 0x0824, 0x00000000 },         \
    { AR5K_INI_FLAG_BOTH, 0x1230, 0x00000000 },         \
    { AR5K_INI_FLAG_BOTH, 0x1270, 0x00000000 },         \
    { AR5K_INI_FLAG_BOTH, 0x1038, 0x00000000 },         \
    { AR5K_INI_FLAG_BOTH, 0x1078, 0x00000000 },         \
    { AR5K_INI_FLAG_BOTH, 0x10b8, 0x00000000 },         \
    { AR5K_INI_FLAG_BOTH, 0x10f8, 0x00000000 },         \
    { AR5K_INI_FLAG_BOTH, 0x1138, 0x00000000 },         \
    { AR5K_INI_FLAG_BOTH, 0x1178, 0x00000000 },         \
    { AR5K_INI_FLAG_BOTH, 0x11b8, 0x00000000 },         \
    { AR5K_INI_FLAG_BOTH, 0x11f8, 0x00000000 },         \
    { AR5K_INI_FLAG_BOTH, 0x1238, 0x00000000 },         \
    { AR5K_INI_FLAG_BOTH, 0x1278, 0x00000000 },         \
    { AR5K_INI_FLAG_BOTH, 0x12b8, 0x00000000 },         \
    { AR5K_INI_FLAG_BOTH, 0x12f8, 0x00000000 },         \
    { AR5K_INI_FLAG_BOTH, 0x1338, 0x00000000 },         \
    { AR5K_INI_FLAG_BOTH, 0x1378, 0x00000000 },         \
    { AR5K_INI_FLAG_BOTH, 0x13b8, 0x00000000 },         \
    { AR5K_INI_FLAG_BOTH, 0x13f8, 0x00000000 },         \
    { AR5K_INI_FLAG_BOTH, 0x1438, 0x00000000 },         \
    { AR5K_INI_FLAG_BOTH, 0x1478, 0x00000000 },         \
    { AR5K_INI_FLAG_BOTH, 0x14b8, 0x00000000 },         \
    { AR5K_INI_FLAG_BOTH, 0x14f8, 0x00000000 },         \
    { AR5K_INI_FLAG_BOTH, 0x1538, 0x00000000 },         \
    { AR5K_INI_FLAG_BOTH, 0x1578, 0x00000000 },         \
    { AR5K_INI_FLAG_BOTH, 0x15b8, 0x00000000 },         \
    { AR5K_INI_FLAG_BOTH, 0x15f8, 0x00000000 },         \
    { AR5K_INI_FLAG_BOTH, 0x1638, 0x00000000 },         \
    { AR5K_INI_FLAG_BOTH, 0x1678, 0x00000000 },         \
    { AR5K_INI_FLAG_BOTH, 0x16b8, 0x00000000 },         \
    { AR5K_INI_FLAG_BOTH, 0x16f8, 0x00000000 },         \
    { AR5K_INI_FLAG_BOTH, 0x1738, 0x00000000 },         \
    { AR5K_INI_FLAG_BOTH, 0x1778, 0x00000000 },         \
    { AR5K_INI_FLAG_BOTH, 0x17b8, 0x00000000 },         \
    { AR5K_INI_FLAG_BOTH, 0x17f8, 0x00000000 },         \
    { AR5K_INI_FLAG_BOTH, 0x103c, 0x00000000 },         \
    { AR5K_INI_FLAG_BOTH, 0x107c, 0x00000000 },         \
    { AR5K_INI_FLAG_BOTH, 0x10bc, 0x00000000 },         \
    { AR5K_INI_FLAG_BOTH, 0x10fc, 0x00000000 },         \
    { AR5K_INI_FLAG_BOTH, 0x113c, 0x00000000 },         \
    { AR5K_INI_FLAG_BOTH, 0x117c, 0x00000000 },         \
    { AR5K_INI_FLAG_BOTH, 0x11bc, 0x00000000 },         \
    { AR5K_INI_FLAG_BOTH, 0x11fc, 0x00000000 },         \
    { AR5K_INI_FLAG_BOTH, 0x123c, 0x00000000 },         \
    { AR5K_INI_FLAG_BOTH, 0x127c, 0x00000000 },         \
    { AR5K_INI_FLAG_BOTH, 0x12bc, 0x00000000 },         \
    { AR5K_INI_FLAG_BOTH, 0x12fc, 0x00000000 },         \
    { AR5K_INI_FLAG_BOTH, 0x133c, 0x00000000 },         \
    { AR5K_INI_FLAG_BOTH, 0x137c, 0x00000000 },         \
    { AR5K_INI_FLAG_BOTH, 0x13bc, 0x00000000 },         \
    { AR5K_INI_FLAG_BOTH, 0x13fc, 0x00000000 },         \
    { AR5K_INI_FLAG_BOTH, 0x143c, 0x00000000 },         \
    { AR5K_INI_FLAG_BOTH, 0x147c, 0x00000000 },         \
    { AR5K_INI_FLAG_BOTH, 0x8004, 0x00000000 },         \
    { AR5K_INI_FLAG_BOTH, 0x8008, 0x00000000 },         \
    { AR5K_INI_FLAG_BOTH, 0x800c, 0x00000000 },         \
    { AR5K_INI_FLAG_BOTH, 0x8018, 0x00000000 },         \
    { AR5K_INI_FLAG_BOTH, 0x8020, 0x00000000 },         \
    { AR5K_INI_FLAG_BOTH, 0x8024, 0x00000000 },         \
    { AR5K_INI_FLAG_BOTH, 0x8028, 0x00000030 },         \
    { AR5K_INI_FLAG_BOTH, 0x802c, 0x0007ffff },         \
    { AR5K_INI_FLAG_BOTH, 0x8030, 0x01ffffff },         \
    { AR5K_INI_FLAG_BOTH, 0x8034, 0x00000031 },         \
    { AR5K_INI_FLAG_BOTH, 0x8038, 0x00000000 },         \
    { AR5K_INI_FLAG_BOTH, 0x803c, 0x00000000 },         \
    { AR5K_INI_FLAG_BOTH, 0x8048, 0x00000000 },         \
    { AR5K_INI_FLAG_BOTH, 0x8054, 0x00000000 },         \
    { AR5K_INI_FLAG_BOTH, 0x8058, 0x00000000 },         \
    { AR5K_INI_FLAG_BOTH, 0x805c, 0xffffc7ff },         \
    { AR5K_INI_FLAG_BOTH, 0x8080, 0x00000000 },         \
    { AR5K_INI_FLAG_BOTH, 0x8084, 0x00000000 },         \
    { AR5K_INI_FLAG_BOTH, 0x8088, 0x00000000 },         \
    { AR5K_INI_FLAG_BOTH, 0x808c, 0x00000000 },         \
    { AR5K_INI_FLAG_BOTH, 0x8090, 0x00000000 },         \
    { AR5K_INI_FLAG_BOTH, 0x8094, 0x00000000 },         \
    { AR5K_INI_FLAG_BOTH, 0x8098, 0x00000000 },         \
    { AR5K_INI_FLAG_BOTH, 0x80c0, 0x2a82301a },         \
    { AR5K_INI_FLAG_BOTH, 0x80c4, 0x05dc01e0 },         \
    { AR5K_INI_FLAG_BOTH, 0x80c8, 0x1f402710 },         \
    { AR5K_INI_FLAG_BOTH, 0x80cc, 0x01f40000 },         \
    { AR5K_INI_FLAG_BOTH, 0x80d0, 0x00001e1c },         \
    { AR5K_INI_FLAG_BOTH, 0x80d4, 0x0002aaaa },         \
    { AR5K_INI_FLAG_BOTH, 0x80d8, 0x02005555 },         \
    { AR5K_INI_FLAG_BOTH, 0x80dc, 0x00000000 },         \
    { AR5K_INI_FLAG_BOTH, 0x80e0, 0xffffffff },         \
    { AR5K_INI_FLAG_BOTH, 0x80e4, 0x0000ffff },         \
    { AR5K_INI_FLAG_BOTH, 0x80e8, 0x00000000 },         \
    { AR5K_INI_FLAG_BOTH, 0x80ec, 0x00000000 },         \
    { AR5K_INI_FLAG_BOTH, 0x80f0, 0x00000000 },         \
    { AR5K_INI_FLAG_BOTH, 0x80f4, 0x00000000 },         \
    { AR5K_INI_FLAG_BOTH, 0x80f8, 0x00000000 },         \
    { AR5K_INI_FLAG_BOTH, 0x80fc, 0x00000088 },         \
    { AR5K_INI_FLAG_BOTH, 0x8700, 0x00000000 },         \
    { AR5K_INI_FLAG_BOTH, 0x8704, 0x0000008c },         \
    { AR5K_INI_FLAG_BOTH, 0x8708, 0x000000e4 },         \
    { AR5K_INI_FLAG_BOTH, 0x870c, 0x000002d5 },         \
    { AR5K_INI_FLAG_BOTH, 0x8710, 0x00000000 },         \
    { AR5K_INI_FLAG_BOTH, 0x8714, 0x00000000 },         \
    { AR5K_INI_FLAG_BOTH, 0x8718, 0x000000a0 },         \
    { AR5K_INI_FLAG_BOTH, 0x871c, 0x000001c9 },         \
    { AR5K_INI_FLAG_BOTH, 0x8720, 0x0000002c },         \
    { AR5K_INI_FLAG_BOTH, 0x8724, 0x0000002c },         \
    { AR5K_INI_FLAG_BOTH, 0x8728, 0x00000030 },         \
    { AR5K_INI_FLAG_BOTH, 0x872c, 0x0000003c },         \
    { AR5K_INI_FLAG_BOTH, 0x8730, 0x0000002c },         \
    { AR5K_INI_FLAG_BOTH, 0x8734, 0x0000002c },         \
    { AR5K_INI_FLAG_BOTH, 0x8738, 0x00000030 },         \
    { AR5K_INI_FLAG_BOTH, 0x873c, 0x0000003c },         \
    { AR5K_INI_FLAG_BOTH, 0x8740, 0x00000000 },         \
    { AR5K_INI_FLAG_BOTH, 0x8744, 0x00000000 },         \
    { AR5K_INI_FLAG_BOTH, 0x8748, 0x00000000 },         \
    { AR5K_INI_FLAG_BOTH, 0x874c, 0x00000000 },         \
    { AR5K_INI_FLAG_BOTH, 0x8750, 0x00000000 },         \
    { AR5K_INI_FLAG_BOTH, 0x8754, 0x00000000 },         \
    { AR5K_INI_FLAG_BOTH, 0x8758, 0x00000000 },         \
    { AR5K_INI_FLAG_BOTH, 0x875c, 0x00000000 },         \
    { AR5K_INI_FLAG_BOTH, 0x8760, 0x000000d5 },         \
    { AR5K_INI_FLAG_BOTH, 0x8764, 0x000000df },         \
    { AR5K_INI_FLAG_BOTH, 0x8768, 0x00000102 },         \
    { AR5K_INI_FLAG_BOTH, 0x876c, 0x0000013a },         \
    { AR5K_INI_FLAG_BOTH, 0x8770, 0x00000075 },         \
    { AR5K_INI_FLAG_BOTH, 0x8774, 0x0000007f },         \
    { AR5K_INI_FLAG_BOTH, 0x8778, 0x000000a2 },         \
    { AR5K_INI_FLAG_BOTH, 0x877c, 0x00000000 },         \
    { AR5K_INI_FLAG_BOTH, 0x8100, 0x00010002 },         \
    { AR5K_INI_FLAG_BOTH, 0x8104, 0x00000001 },         \
    { AR5K_INI_FLAG_BOTH, 0x8108, 0x000000c0 },         \
    { AR5K_INI_FLAG_BOTH, 0x810c, 0x00000000 },         \
    { AR5K_INI_FLAG_BOTH, 0x8110, 0x00000168 },         \
    { AR5K_INI_FLAG_BOTH, 0x8114, 0x00000000 },         \
    { AR5K_INI_FLAG_BOTH, 0x87c0, 0x03020100 },         \
    { AR5K_INI_FLAG_BOTH, 0x87c4, 0x07060504 },         \
    { AR5K_INI_FLAG_BOTH, 0x87c8, 0x0b0a0908 },         \
    { AR5K_INI_FLAG_BOTH, 0x87cc, 0x0f0e0d0c },         \
    { AR5K_INI_FLAG_BOTH, 0x87d0, 0x13121110 },         \
    { AR5K_INI_FLAG_BOTH, 0x87d4, 0x17161514 },         \
    { AR5K_INI_FLAG_BOTH, 0x87d8, 0x1b1a1918 },         \
    { AR5K_INI_FLAG_BOTH, 0x87dc, 0x1f1e1d1c },         \
    { AR5K_INI_FLAG_BOTH, 0x87e0, 0x03020100 },         \
    { AR5K_INI_FLAG_BOTH, 0x87e4, 0x07060504 },         \
    { AR5K_INI_FLAG_BOTH, 0x87e8, 0x0b0a0908 },         \
    { AR5K_INI_FLAG_BOTH, 0x87ec, 0x0f0e0d0c },         \
    { AR5K_INI_FLAG_BOTH, 0x87f0, 0x13121110 },         \
    { AR5K_INI_FLAG_BOTH, 0x87f4, 0x17161514 },         \
    { AR5K_INI_FLAG_BOTH, 0x87f8, 0x1b1a1918 },         \
    { AR5K_INI_FLAG_BOTH, 0x87fc, 0x1f1e1d1c },         \
    /* PHY registers */                     \
    { AR5K_INI_FLAG_BOTH, 0x9808, 0x00000000 },         \
    { AR5K_INI_FLAG_BOTH, 0x980c, 0xad848e19 },         \
    { AR5K_INI_FLAG_BOTH, 0x9810, 0x7d28e000 },         \
    { AR5K_INI_FLAG_BOTH, 0x9814, 0x9c0a9f6b },         \
    { AR5K_INI_FLAG_BOTH, 0x981c, 0x00000000 },         \
    { AR5K_INI_FLAG_BOTH, 0x982c, 0x00022ffe },         \
    { AR5K_INI_FLAG_BOTH, 0x983c, 0x00020100 },         \
    { AR5K_INI_FLAG_BOTH, 0x9840, 0x206a017a },         \
    { AR5K_INI_FLAG_BOTH, 0x984c, 0x1284613c },         \
    { AR5K_INI_FLAG_BOTH, 0x9854, 0x00000859 },         \
    { AR5K_INI_FLAG_BOTH, 0x9900, 0x00000000 },         \
    { AR5K_INI_FLAG_BOTH, 0x9904, 0x00000000 },         \
    { AR5K_INI_FLAG_BOTH, 0x9908, 0x00000000 },         \
    { AR5K_INI_FLAG_BOTH, 0x990c, 0x00800000 },         \
    { AR5K_INI_FLAG_BOTH, 0x9910, 0x00000001 },         \
    { AR5K_INI_FLAG_BOTH, 0x991c, 0x0000092a },         \
    { AR5K_INI_FLAG_BOTH, 0x9920, 0x05100000 },         \
    { AR5K_INI_FLAG_BOTH, 0x9928, 0x00000001 },         \
    { AR5K_INI_FLAG_BOTH, 0x992c, 0x00000004 },         \
    { AR5K_INI_FLAG_BOTH, 0x9934, 0x1e1f2022 },         \
    { AR5K_INI_FLAG_BOTH, 0x9938, 0x0a0b0c0d },         \
    { AR5K_INI_FLAG_BOTH, 0x993c, 0x0000003f },         \
    { AR5K_INI_FLAG_BOTH, 0x9940, 0x00000004 },         \
    { AR5K_INI_FLAG_BOTH, 0x9948, 0x9280b212 },         \
    { AR5K_INI_FLAG_BOTH, 0x9954, 0x5d50e188 },         \
    { AR5K_INI_FLAG_BOTH, 0x9958, 0x000000ff },         \
    { AR5K_INI_FLAG_BOTH, 0x995c, 0x004b6a8e },         \
    { AR5K_INI_FLAG_BOTH, 0x9968, 0x000003ce },         \
    { AR5K_INI_FLAG_BOTH, 0x9970, 0x192fb515 },         \
    { AR5K_INI_FLAG_BOTH, 0x9974, 0x00000000 },         \
    { AR5K_INI_FLAG_BOTH, 0x9978, 0x00000001 },         \
    { AR5K_INI_FLAG_BOTH, 0x997c, 0x00000000 },         \
    { AR5K_INI_FLAG_BOTH, 0xa184, 0x10ff10ff },         \
    { AR5K_INI_FLAG_BOTH, 0xa188, 0x10ff10ff },         \
    { AR5K_INI_FLAG_BOTH, 0xa18c, 0x10ff10ff },         \
    { AR5K_INI_FLAG_BOTH, 0xa190, 0x10ff10ff },         \
    { AR5K_INI_FLAG_BOTH, 0xa194, 0x10ff10ff },         \
    { AR5K_INI_FLAG_BOTH, 0xa198, 0x10ff10ff },         \
    { AR5K_INI_FLAG_BOTH, 0xa19c, 0x10ff10ff },         \
    { AR5K_INI_FLAG_BOTH, 0xa1a0, 0x10ff10ff },         \
    { AR5K_INI_FLAG_BOTH, 0xa1a4, 0x10ff10ff },         \
    { AR5K_INI_FLAG_BOTH, 0xa1a8, 0x10ff10ff },         \
    { AR5K_INI_FLAG_BOTH, 0xa1ac, 0x10ff10ff },         \
    { AR5K_INI_FLAG_BOTH, 0xa1b0, 0x10ff10ff },         \
    { AR5K_INI_FLAG_BOTH, 0xa1b4, 0x10ff10ff },         \
    { AR5K_INI_FLAG_BOTH, 0xa1b8, 0x10ff10ff },         \
    { AR5K_INI_FLAG_BOTH, 0xa1bc, 0x10ff10ff },         \
    { AR5K_INI_FLAG_BOTH, 0xa1c0, 0x10ff10ff },         \
    { AR5K_INI_FLAG_BOTH, 0xa1c4, 0x10ff10ff },         \
    { AR5K_INI_FLAG_BOTH, 0xa1c8, 0x10ff10ff },         \
    { AR5K_INI_FLAG_BOTH, 0xa1cc, 0x10ff10ff },         \
    { AR5K_INI_FLAG_BOTH, 0xa1d0, 0x10ff10ff },         \
    { AR5K_INI_FLAG_BOTH, 0xa1d4, 0x10ff10ff },         \
    { AR5K_INI_FLAG_BOTH, 0xa1d8, 0x10ff10ff },         \
    { AR5K_INI_FLAG_BOTH, 0xa1dc, 0x10ff10ff },         \
    { AR5K_INI_FLAG_BOTH, 0xa1e0, 0x10ff10ff },         \
    { AR5K_INI_FLAG_BOTH, 0xa1e4, 0x10ff10ff },         \
    { AR5K_INI_FLAG_BOTH, 0xa1e8, 0x10ff10ff },         \
    { AR5K_INI_FLAG_BOTH, 0xa1ec, 0x10ff10ff },         \
    { AR5K_INI_FLAG_BOTH, 0xa1f0, 0x10ff10ff },         \
    { AR5K_INI_FLAG_BOTH, 0xa1f4, 0x10ff10ff },         \
    { AR5K_INI_FLAG_BOTH, 0xa1f8, 0x10ff10ff },         \
    { AR5K_INI_FLAG_BOTH, 0xa1fc, 0x10ff10ff },         \
    { AR5K_INI_FLAG_BOTH, 0xa210, 0x0080a333 },         \
    { AR5K_INI_FLAG_BOTH, 0xa214, 0x00206c10 },         \
    { AR5K_INI_FLAG_BOTH, 0xa218, 0x009c4060 },         \
    { AR5K_INI_FLAG_BOTH, 0xa21c, 0x1483800a },         \
    { AR5K_INI_FLAG_BOTH, 0xa220, 0x01831061 },         \
    { AR5K_INI_FLAG_BOTH, 0xa224, 0x00000400 },         \
    { AR5K_INI_FLAG_BOTH, 0xa228, 0x000001b5 },         \
    { AR5K_INI_FLAG_BOTH, 0xa22c, 0x00000000 },         \
    { AR5K_INI_FLAG_BOTH, 0xa234, 0x20202020 },         \
    { AR5K_INI_FLAG_BOTH, 0xa238, 0x20202020 },         \
    { AR5K_INI_FLAG_BOTH, 0xa23c, 0x13c889af },         \
    { AR5K_INI_FLAG_BOTH, 0xa240, 0x38490a20 },         \
    { AR5K_INI_FLAG_BOTH, 0xa244, 0x00007bb6 },         \
    { AR5K_INI_FLAG_BOTH, 0xa248, 0x0fff3ffc },         \
    { AR5K_INI_FLAG_BOTH, 0x9b00, 0x00000000 },         \
    { AR5K_INI_FLAG_BOTH, 0x9b28, 0x0000000c },         \
    { AR5K_INI_FLAG_BOTH, 0x9b38, 0x00000012 },         \
    { AR5K_INI_FLAG_BOTH, 0x9b64, 0x00000021 },         \
    { AR5K_INI_FLAG_BOTH, 0x9b8c, 0x0000002d },         \
    { AR5K_INI_FLAG_BOTH, 0x9b9c, 0x00000033 },         \
    /* AR5111 specific */                       \
    { AR5K_INI_FLAG_5111, 0x9930, 0x00004883 },         \
    { AR5K_INI_FLAG_5111, 0xa204, 0x00000000 },         \
    { AR5K_INI_FLAG_5111, 0xa208, 0xd03e6788 },         \
    { AR5K_INI_FLAG_5111, 0xa20c, 0x6448416a },         \
    { AR5K_INI_FLAG_5111, 0x9b04, 0x00000020 },         \
    { AR5K_INI_FLAG_5111, 0x9b08, 0x00000010 },         \
    { AR5K_INI_FLAG_5111, 0x9b0c, 0x00000030 },         \
    { AR5K_INI_FLAG_5111, 0x9b10, 0x00000008 },         \
    { AR5K_INI_FLAG_5111, 0x9b14, 0x00000028 },         \
    { AR5K_INI_FLAG_5111, 0x9b18, 0x00000004 },         \
    { AR5K_INI_FLAG_5111, 0x9b1c, 0x00000024 },         \
    { AR5K_INI_FLAG_5111, 0x9b20, 0x00000014 },         \
    { AR5K_INI_FLAG_5111, 0x9b24, 0x00000034 },         \
    { AR5K_INI_FLAG_5111, 0x9b2c, 0x0000002c },         \
    { AR5K_INI_FLAG_5111, 0x9b30, 0x00000002 },         \
    { AR5K_INI_FLAG_5111, 0x9b34, 0x00000022 },         \
    { AR5K_INI_FLAG_5111, 0x9b3c, 0x00000032 },         \
    { AR5K_INI_FLAG_5111, 0x9b40, 0x0000000a },         \
    { AR5K_INI_FLAG_5111, 0x9b44, 0x0000002a },         \
    { AR5K_INI_FLAG_5111, 0x9b48, 0x00000006 },         \
    { AR5K_INI_FLAG_5111, 0x9b4c, 0x00000026 },         \
    { AR5K_INI_FLAG_5111, 0x9b50, 0x00000016 },         \
    { AR5K_INI_FLAG_5111, 0x9b54, 0x00000036 },         \
    { AR5K_INI_FLAG_5111, 0x9b58, 0x0000000e },         \
    { AR5K_INI_FLAG_5111, 0x9b5c, 0x0000002e },         \
    { AR5K_INI_FLAG_5111, 0x9b60, 0x00000001 },         \
    { AR5K_INI_FLAG_5111, 0x9b68, 0x00000011 },         \
    { AR5K_INI_FLAG_5111, 0x9b6c, 0x00000031 },         \
    { AR5K_INI_FLAG_5111, 0x9b70, 0x00000009 },         \
    { AR5K_INI_FLAG_5111, 0x9b74, 0x00000029 },         \
    { AR5K_INI_FLAG_5111, 0x9b78, 0x00000005 },         \
    { AR5K_INI_FLAG_5111, 0x9b7c, 0x00000025 },         \
    { AR5K_INI_FLAG_5111, 0x9b80, 0x00000015 },         \
    { AR5K_INI_FLAG_5111, 0x9b84, 0x00000035 },         \
    { AR5K_INI_FLAG_5111, 0x9b88, 0x0000000d },         \
    { AR5K_INI_FLAG_5111, 0x9b90, 0x00000003 },         \
    { AR5K_INI_FLAG_5111, 0x9b94, 0x00000023 },         \
    { AR5K_INI_FLAG_5111, 0x9b98, 0x00000013 },         \
    { AR5K_INI_FLAG_5111, 0x9ba0, 0x0000000b },         \
    { AR5K_INI_FLAG_5111, 0x9ba4, 0x0000002b },         \
    { AR5K_INI_FLAG_5111, 0x9ba8, 0x0000002b },         \
    { AR5K_INI_FLAG_5111, 0x9bac, 0x0000002b },         \
    { AR5K_INI_FLAG_5111, 0x9bb0, 0x0000002b },         \
    { AR5K_INI_FLAG_5111, 0x9bb4, 0x0000002b },         \
    { AR5K_INI_FLAG_5111, 0x9bb8, 0x0000002b },         \
    { AR5K_INI_FLAG_5111, 0x9bbc, 0x0000002b },         \
    { AR5K_INI_FLAG_5111, 0x9bc0, 0x0000002b },         \
    { AR5K_INI_FLAG_5111, 0x9bc4, 0x0000002b },         \
    { AR5K_INI_FLAG_5111, 0x9bc8, 0x0000002b },         \
    { AR5K_INI_FLAG_5111, 0x9bcc, 0x0000002b },         \
    { AR5K_INI_FLAG_5111, 0x9bd0, 0x0000002b },         \
    { AR5K_INI_FLAG_5111, 0x9bd4, 0x0000002b },         \
    { AR5K_INI_FLAG_5111, 0x9bd8, 0x0000002b },         \
    { AR5K_INI_FLAG_5111, 0x9bdc, 0x0000002b },         \
    { AR5K_INI_FLAG_5111, 0x9be0, 0x0000002b },         \
    { AR5K_INI_FLAG_5111, 0x9be4, 0x0000002b },         \
    { AR5K_INI_FLAG_5111, 0x9be8, 0x0000002b },         \
    { AR5K_INI_FLAG_5111, 0x9bec, 0x0000002b },         \
    { AR5K_INI_FLAG_5111, 0x9bf0, 0x0000002b },         \
    { AR5K_INI_FLAG_5111, 0x9bf4, 0x0000002b },         \
    { AR5K_INI_FLAG_5111, 0x9bf8, 0x00000002 },         \
    { AR5K_INI_FLAG_5111, 0x9bfc, 0x00000016 },         \
    /* AR5112 specific */                       \
    { AR5K_INI_FLAG_5112, 0x9930, 0x00004882 },         \
    { AR5K_INI_FLAG_5112, 0x9b04, 0x00000001 },         \
    { AR5K_INI_FLAG_5112, 0x9b08, 0x00000002 },         \
    { AR5K_INI_FLAG_5112, 0x9b0c, 0x00000003 },         \
    { AR5K_INI_FLAG_5112, 0x9b10, 0x00000004 },         \
    { AR5K_INI_FLAG_5112, 0x9b14, 0x00000005 },         \
    { AR5K_INI_FLAG_5112, 0x9b18, 0x00000008 },         \
    { AR5K_INI_FLAG_5112, 0x9b1c, 0x00000009 },         \
    { AR5K_INI_FLAG_5112, 0x9b20, 0x0000000a },         \
    { AR5K_INI_FLAG_5112, 0x9b24, 0x0000000b },         \
    { AR5K_INI_FLAG_5112, 0x9b2c, 0x0000000d },         \
    { AR5K_INI_FLAG_5112, 0x9b30, 0x00000010 },         \
    { AR5K_INI_FLAG_5112, 0x9b34, 0x00000011 },         \
    { AR5K_INI_FLAG_5112, 0x9b3c, 0x00000013 },         \
    { AR5K_INI_FLAG_5112, 0x9b40, 0x00000014 },         \
    { AR5K_INI_FLAG_5112, 0x9b44, 0x00000015 },         \
    { AR5K_INI_FLAG_5112, 0x9b48, 0x00000018 },         \
    { AR5K_INI_FLAG_5112, 0x9b4c, 0x00000019 },         \
    { AR5K_INI_FLAG_5112, 0x9b50, 0x0000001a },         \
    { AR5K_INI_FLAG_5112, 0x9b54, 0x0000001b },         \
    { AR5K_INI_FLAG_5112, 0x9b58, 0x0000001c },         \
    { AR5K_INI_FLAG_5112, 0x9b5c, 0x0000001d },         \
    { AR5K_INI_FLAG_5112, 0x9b60, 0x00000020 },         \
    { AR5K_INI_FLAG_5112, 0x9b68, 0x00000022 },         \
    { AR5K_INI_FLAG_5112, 0x9b6c, 0x00000023 },         \
    { AR5K_INI_FLAG_5112, 0x9b70, 0x00000024 },         \
    { AR5K_INI_FLAG_5112, 0x9b74, 0x00000025 },         \
    { AR5K_INI_FLAG_5112, 0x9b78, 0x00000028 },         \
    { AR5K_INI_FLAG_5112, 0x9b7c, 0x00000029 },         \
    { AR5K_INI_FLAG_5112, 0x9b80, 0x0000002a },         \
    { AR5K_INI_FLAG_5112, 0x9b84, 0x0000002b },         \
    { AR5K_INI_FLAG_5112, 0x9b88, 0x0000002c },         \
    { AR5K_INI_FLAG_5112, 0x9b90, 0x00000030 },         \
    { AR5K_INI_FLAG_5112, 0x9b94, 0x00000031 },         \
    { AR5K_INI_FLAG_5112, 0x9b98, 0x00000032 },         \
    { AR5K_INI_FLAG_5112, 0x9ba0, 0x00000034 },         \
    { AR5K_INI_FLAG_5112, 0x9ba4, 0x00000035 },         \
    { AR5K_INI_FLAG_5112, 0x9ba8, 0x00000035 },         \
    { AR5K_INI_FLAG_5112, 0x9bac, 0x00000035 },         \
    { AR5K_INI_FLAG_5112, 0x9bb0, 0x00000035 },         \
    { AR5K_INI_FLAG_5112, 0x9bb4, 0x00000035 },         \
    { AR5K_INI_FLAG_5112, 0x9bb8, 0x00000035 },         \
    { AR5K_INI_FLAG_5112, 0x9bbc, 0x00000035 },         \
    { AR5K_INI_FLAG_5112, 0x9bc0, 0x00000035 },         \
    { AR5K_INI_FLAG_5112, 0x9bc4, 0x00000035 },         \
    { AR5K_INI_FLAG_5112, 0x9bc8, 0x00000035 },         \
    { AR5K_INI_FLAG_5112, 0x9bcc, 0x00000035 },         \
    { AR5K_INI_FLAG_5112, 0x9bd0, 0x00000035 },         \
    { AR5K_INI_FLAG_5112, 0x9bd4, 0x00000035 },         \
    { AR5K_INI_FLAG_5112, 0x9bd8, 0x00000035 },         \
    { AR5K_INI_FLAG_5112, 0x9bdc, 0x00000035 },         \
    { AR5K_INI_FLAG_5112, 0x9be0, 0x00000035 },         \
    { AR5K_INI_FLAG_5112, 0x9be4, 0x00000035 },         \
    { AR5K_INI_FLAG_5112, 0x9be8, 0x00000035 },         \
    { AR5K_INI_FLAG_5112, 0x9bec, 0x00000035 },         \
    { AR5K_INI_FLAG_5112, 0x9bf0, 0x00000035 },         \
    { AR5K_INI_FLAG_5112, 0x9bf4, 0x00000035 },         \
    { AR5K_INI_FLAG_5112, 0x9bf8, 0x00000010 },         \
    { AR5K_INI_FLAG_5112, 0x9bfc, 0x0000001a },         \
}

struct ath5k_ar5211_ini_mode {
    uint16_t   mode_register;
    uint32_t   mode_value[4];
};

#define AR5K_AR5211_INI_MODE {                      \
    { 0x0030, { 0x00000017, 0x00000017, 0x00000017, 0x00000017 } }, \
    { 0x1040, { 0x002ffc0f, 0x002ffc0f, 0x002ffc1f, 0x002ffc0f } }, \
    { 0x1044, { 0x002ffc0f, 0x002ffc0f, 0x002ffc1f, 0x002ffc0f } }, \
    { 0x1048, { 0x002ffc0f, 0x002ffc0f, 0x002ffc1f, 0x002ffc0f } }, \
    { 0x104c, { 0x002ffc0f, 0x002ffc0f, 0x002ffc1f, 0x002ffc0f } }, \
    { 0x1050, { 0x002ffc0f, 0x002ffc0f, 0x002ffc1f, 0x002ffc0f } }, \
    { 0x1054, { 0x002ffc0f, 0x002ffc0f, 0x002ffc1f, 0x002ffc0f } }, \
    { 0x1058, { 0x002ffc0f, 0x002ffc0f, 0x002ffc1f, 0x002ffc0f } }, \
    { 0x105c, { 0x002ffc0f, 0x002ffc0f, 0x002ffc1f, 0x002ffc0f } }, \
    { 0x1060, { 0x002ffc0f, 0x002ffc0f, 0x002ffc1f, 0x002ffc0f } }, \
    { 0x1064, { 0x002ffc0f, 0x002ffc0f, 0x002ffc1f, 0x002ffc0f } }, \
    { 0x1070, { 0x00000168, 0x000001e0, 0x000001b8, 0x00000168 } }, \
    { 0x1030, { 0x00000230, 0x000001e0, 0x000000b0, 0x00000230 } }, \
    { 0x10b0, { 0x00000d98, 0x00001180, 0x00001f48, 0x00000d98 } }, \
    { 0x10f0, { 0x0000a0e0, 0x00014068, 0x00005880, 0x0000a0e0 } }, \
    { 0x8014, { 0x04000400, 0x08000800, 0x20003000, 0x04000400 } }, \
    { 0x801c, { 0x0e8d8fa7, 0x0e8d8fcf, 0x01608f95, 0x0e8d8fa7 } }, \
    { 0x9804, { 0x00000000, 0x00000003, 0x00000000, 0x00000000 } }, \
    { 0x9820, { 0x02020200, 0x02020200, 0x02010200, 0x02020200 } }, \
    { 0x9824, { 0x00000e0e, 0x00000e0e, 0x00000707, 0x00000e0e } }, \
    { 0x9828, { 0x0a020001, 0x0a020001, 0x05010000, 0x0a020001 } }, \
    { 0x9834, { 0x00000e0e, 0x00000e0e, 0x00000e0e, 0x00000e0e } }, \
    { 0x9838, { 0x00000007, 0x00000007, 0x0000000b, 0x0000000b } }, \
    { 0x9844, { 0x1372169c, 0x137216a5, 0x137216a8, 0x1372169c } }, \
    { 0x9848, { 0x0018ba67, 0x0018ba67, 0x0018ba69, 0x0018ba69 } }, \
    { 0x9850, { 0x0c28b4e0, 0x0c28b4e0, 0x0c28b4e0, 0x0c28b4e0 } }, \
    { 0x9858, { 0x7e800d2e, 0x7e800d2e, 0x7ec00d2e, 0x7e800d2e } }, \
    { 0x985c, { 0x31375d5e, 0x31375d5e, 0x313a5d5e, 0x31375d5e } }, \
    { 0x9860, { 0x0000bd10, 0x0000bd10, 0x0000bd38, 0x0000bd10 } }, \
    { 0x9864, { 0x0001ce00, 0x0001ce00, 0x0001ce00, 0x0001ce00 } }, \
    { 0x9914, { 0x00002710, 0x00002710, 0x0000157c, 0x00002710 } }, \
    { 0x9918, { 0x00000190, 0x00000190, 0x00000084, 0x00000190 } }, \
    { 0x9944, { 0x6fe01020, 0x6fe01020, 0x6fe00920, 0x6fe01020 } }, \
    { 0xa180, { 0x05ff14ff, 0x05ff14ff, 0x05ff14ff, 0x05ff19ff } }, \
    { 0x98d4, { 0x00000010, 0x00000014, 0x00000010, 0x00000010 } }, \
}

struct ath5k_ar5212_ini_mode {
    uint16_t   mode_register;
    uint8_t    mode_flags;
    uint32_t   mode_value[2][5];
};

#define AR5K_AR5212_INI_MODE {                          \
    { 0x0030, AR5K_INI_FLAG_511X, {                     \
        { 0, },                             \
        { 0x00008107, 0x00008107, 0x00008107, 0x00008107, 0x00008107 }  \
    } },                                    \
    { 0x1040, AR5K_INI_FLAG_511X, {                     \
        { 0, },                             \
        { 0x002ffc0f, 0x002ffc0f, 0x002ffc1f, 0x002ffc0f, 0x002ffc0f }  \
    } },                                    \
    { 0x1044, AR5K_INI_FLAG_511X, {                     \
        { 0, },                             \
        { 0x002ffc0f, 0x002ffc0f, 0x002ffc1f, 0x002ffc0f, 0x002ffc0f }  \
    } },                                    \
    { 0x1048, AR5K_INI_FLAG_511X, {                     \
        { 0, },                             \
        { 0x002ffc0f, 0x002ffc0f, 0x002ffc1f, 0x002ffc0f, 0x002ffc0f }  \
    } },                                    \
    { 0x104c, AR5K_INI_FLAG_511X, {                     \
        { 0, },                             \
        { 0x002ffc0f, 0x002ffc0f, 0x002ffc1f, 0x002ffc0f, 0x002ffc0f }  \
    } },                                    \
    { 0x1050, AR5K_INI_FLAG_511X, {                     \
        { 0, },                             \
        { 0x002ffc0f, 0x002ffc0f, 0x002ffc1f, 0x002ffc0f, 0x002ffc0f }  \
    } },                                    \
    { 0x1054, AR5K_INI_FLAG_511X, {                     \
        { 0, },                             \
        { 0x002ffc0f, 0x002ffc0f, 0x002ffc1f, 0x002ffc0f, 0x002ffc0f }  \
    } },                                    \
    { 0x1058, AR5K_INI_FLAG_511X, {                     \
        { 0, },                             \
        { 0x002ffc0f, 0x002ffc0f, 0x002ffc1f, 0x002ffc0f, 0x002ffc0f }  \
    } },                                    \
    { 0x105c, AR5K_INI_FLAG_511X, {                     \
        { 0, },                             \
        { 0x002ffc0f, 0x002ffc0f, 0x002ffc1f, 0x002ffc0f, 0x002ffc0f }  \
    } },                                    \
    { 0x1060, AR5K_INI_FLAG_511X, {                     \
        { 0, },                             \
        { 0x002ffc0f, 0x002ffc0f, 0x002ffc1f, 0x002ffc0f, 0x002ffc0f }  \
    } },                                    \
    { 0x1064, AR5K_INI_FLAG_511X, {                     \
        { 0, },                             \
        { 0x002ffc0f, 0x002ffc0f, 0x002ffc1f, 0x002ffc0f, 0x002ffc0f }  \
    } },                                    \
    { 0x1030, AR5K_INI_FLAG_511X, {                     \
        { 0, },                             \
        { 0x00000230, 0x000001e0, 0x000000b0, 0x00000160, 0x000001e0 }  \
    } },                                    \
    { 0x1070, AR5K_INI_FLAG_511X, {                     \
        { 0, },                             \
        { 0x00000168, 0x000001e0, 0x000001b8, 0x0000018c, 0x000001e0 }  \
    } },                                    \
    { 0x10b0, AR5K_INI_FLAG_511X, {                     \
        { 0, },                             \
        { 0x00000e60, 0x00001180, 0x00001f1c, 0x00003e38, 0x00001180 }  \
    } },                                    \
    { 0x10f0, AR5K_INI_FLAG_511X, {                     \
        { 0, },                             \
        { 0x0000a0e0, 0x00014068, 0x00005880, 0x0000b0e0, 0x00014068 }  \
    } },                                    \
    { 0x8014, AR5K_INI_FLAG_511X, {                     \
        { 0, },                             \
        { 0x03e803e8, 0x06e006e0, 0x04200420, 0x08400840, 0x06e006e0 }  \
    } },                                    \
    { 0x9804, AR5K_INI_FLAG_511X, {                     \
        { 0, },                             \
        { 0x00000000, 0x00000003, 0x00000000, 0x00000000, 0x00000003 }  \
    } },                                    \
    { 0x9820, AR5K_INI_FLAG_511X, {                     \
        { 0, },                             \
        { 0x02020200, 0x02020200, 0x02010200, 0x02020200, 0x02020200 }  \
    } },                                    \
    { 0x9834, AR5K_INI_FLAG_511X, {                     \
        { 0, },                             \
        { 0x00000e0e, 0x00000e0e, 0x00000e0e, 0x00000e0e, 0x00000e0e }  \
    } },                                    \
    { 0x9838, AR5K_INI_FLAG_511X, {                     \
        { 0, },                             \
        { 0x00000007, 0x00000007, 0x0000000b, 0x0000000b, 0x0000000b }  \
    } },                                    \
    { 0x9844, AR5K_INI_FLAG_511X, {                     \
        { 0, },                             \
        { 0x1372161c, 0x13721c25, 0x13721728, 0x137216a2, 0x13721c25 }  \
    } },                                    \
    { 0x9850, AR5K_INI_FLAG_511X, {                     \
        { 0, },                             \
        { 0x0de8b4e0, 0x0de8b4e0, 0x0de8b4e0, 0x0de8b4e0, 0x0de8b4e0 }  \
    } },                                    \
    { 0x9858, AR5K_INI_FLAG_511X, {                     \
        { 0, },                             \
        { 0x7e800d2e, 0x7e800d2e, 0x7ee84d2e, 0x7ee84d2e, 0x7e800d2e }  \
    } },                                    \
    { 0x9860, AR5K_INI_FLAG_511X, {                     \
        { 0, },                             \
        { 0x00009d10, 0x00009d10, 0x00009d18, 0x00009d10, 0x00009d10 }  \
    } },                                    \
    { 0x9864, AR5K_INI_FLAG_511X, {                     \
        { 0, },                             \
        { 0x0001ce00, 0x0001ce00, 0x0001ce00, 0x0001ce00, 0x0001ce00 }  \
    } },                                    \
    { 0x9868, AR5K_INI_FLAG_511X, {                     \
        { 0, },                             \
        { 0x409a4190, 0x409a4190, 0x409a4190, 0x409a4190, 0x409a4190 }  \
    } },                                    \
    { 0x9918, AR5K_INI_FLAG_511X, {                     \
        { 0, },                             \
        { 0x000001b8, 0x000001b8, 0x00000084, 0x00000108, 0x000001b8 }  \
    } },                                    \
    { 0x9924, AR5K_INI_FLAG_511X, {                     \
        { 0, },                             \
        { 0x10058a05, 0x10058a05, 0x10058a05, 0x10058a05, 0x10058a05 }  \
    } },                                    \
    { 0xa180, AR5K_INI_FLAG_511X, {                     \
        { 0, },                             \
        { 0x10ff14ff, 0x10ff14ff, 0x10ff10ff, 0x10ff19ff, 0x10ff19ff }  \
    } },                                    \
    { 0xa230, AR5K_INI_FLAG_511X, {                     \
        { 0, },                             \
        { 0x00000000, 0x00000000, 0x00000000, 0x00000108, 0x00000000 }  \
    } },                                    \
    { 0x801c, AR5K_INI_FLAG_BOTH, {                     \
        { 0x128d8fa7, 0x09880fcf, 0x04e00f95, 0x128d8fab, 0x09880fcf }, \
        { 0x128d93a7, 0x098813cf, 0x04e01395, 0x128d93ab, 0x098813cf }  \
    } },                                    \
    { 0x9824, AR5K_INI_FLAG_BOTH, {                     \
        { 0x00000e0e, 0x00000e0e, 0x00000707, 0x00000e0e, 0x00000e0e }, \
        { 0x00000e0e, 0x00000e0e, 0x00000e0e, 0x00000e0e, 0x00000e0e }  \
    } },                                    \
    { 0x9828, AR5K_INI_FLAG_BOTH, {                     \
        { 0x0a020001, 0x0a020001, 0x05010100, 0x0a020001, 0x0a020001 }, \
        { 0x0a020001, 0x0a020001, 0x05020100, 0x0a020001, 0x0a020001 }  \
    } },                                    \
    { 0x9848, AR5K_INI_FLAG_BOTH, {                     \
        { 0x0018da5a, 0x0018da5a, 0x0018ca69, 0x0018ca69, 0x0018ca69 }, \
        { 0x0018da6d, 0x0018da6d, 0x0018ca75, 0x0018ca75, 0x0018ca75 }  \
    } },                                    \
    { 0x985c, AR5K_INI_FLAG_BOTH, {                     \
        { 0x3137665e, 0x3137665e, 0x3137665e, 0x3137665e, 0x3137615e }, \
        { 0x3137665e, 0x3137665e, 0x3137665e, 0x3137665e, 0x3137665e }  \
    } },                                    \
    { 0x986c, AR5K_INI_FLAG_BOTH, {                     \
        { 0x050cb081, 0x050cb081, 0x050cb081, 0x050cb080, 0x050cb080 }, \
        { 0x050cb081, 0x050cb081, 0x050cb081, 0x050cb081, 0x050cb081 }  \
    } },                                    \
    { 0x9914, AR5K_INI_FLAG_BOTH, {                     \
        { 0x00002710, 0x00002710, 0x0000157c, 0x00002af8, 0x00002710 }, \
        { 0x000007d0, 0x000007d0, 0x0000044c, 0x00000898, 0x000007d0 }  \
    } },                                    \
    { 0x9944, AR5K_INI_FLAG_BOTH, {                     \
        { 0xffb81020, 0xffb81020, 0xffb80d20, 0xffb81020, 0xffb81020 }, \
        { 0xffb81020, 0xffb81020, 0xffb80d10, 0xffb81010, 0xffb81010 }  \
    } },                                    \
    { 0xa204, AR5K_INI_FLAG_5112, {                     \
        { 0, },                             \
        { 0x00000000, 0x00000000, 0x00000004, 0x00000004, 0x00000004 }  \
    } },                                    \
    { 0xa208, AR5K_INI_FLAG_5112, {                     \
        { 0, },                             \
        { 0xd6be6788, 0xd6be6788, 0xd03e6788, 0xd03e6788, 0xd03e6788 }  \
    } },                                    \
    { 0xa20c, AR5K_INI_FLAG_5112, {                     \
        { 0, },                             \
        { 0x642c0140, 0x642c0140, 0x6442c160, 0x6442c160, 0x6442c160 }  \
    } },                                    \
}

struct ath5k_ar5211_ini_rf {
    uint16_t   rf_register;
    uint32_t   rf_value[2];
};

#define AR5K_AR5211_INI_RF  {                   \
    { 0x0000a204, { 0x00000000, 0x00000000 } },         \
    { 0x0000a208, { 0x503e4646, 0x503e4646 } },         \
    { 0x0000a20c, { 0x6480416c, 0x6480416c } },         \
    { 0x0000a210, { 0x0199a003, 0x0199a003 } },         \
    { 0x0000a214, { 0x044cd610, 0x044cd610 } },         \
    { 0x0000a218, { 0x13800040, 0x13800040 } },         \
    { 0x0000a21c, { 0x1be00060, 0x1be00060 } },         \
    { 0x0000a220, { 0x0c53800a, 0x0c53800a } },         \
    { 0x0000a224, { 0x0014df3b, 0x0014df3b } },         \
    { 0x0000a228, { 0x000001b5, 0x000001b5 } },         \
    { 0x0000a22c, { 0x00000020, 0x00000020 } },         \
    { 0x0000989c, { 0x00000000, 0x00000000 } },         \
    { 0x0000989c, { 0x00000000, 0x00000000 } },         \
    { 0x0000989c, { 0x00000000, 0x00000000 } },         \
    { 0x0000989c, { 0x00000000, 0x00000000 } },         \
    { 0x0000989c, { 0x00000000, 0x00000000 } },         \
    { 0x0000989c, { 0x00000000, 0x00000000 } },         \
    { 0x0000989c, { 0x00000000, 0x00000000 } },         \
    { 0x0000989c, { 0x00000000, 0x00000000 } },         \
    { 0x0000989c, { 0x00000000, 0x00000000 } },         \
    { 0x0000989c, { 0x00000000, 0x00000000 } },         \
    { 0x0000989c, { 0x00000000, 0x00000000 } },         \
    { 0x0000989c, { 0x00380000, 0x00380000 } },         \
    { 0x0000989c, { 0x00000000, 0x00000000 } },         \
    { 0x0000989c, { 0x00000000, 0x00000000 } },         \
    { 0x0000989c, { 0x00000000, 0x00000000 } },         \
    { 0x0000989c, { 0x000400f9, 0x000400f9 } },         \
    { 0x000098d4, { 0x00000000, 0x00000004 } },         \
                                    \
    { 0x0000989c, { 0x00000000, 0x00000000 } },         \
    { 0x0000989c, { 0x00000000, 0x00000000 } },         \
    { 0x0000989c, { 0x00000000, 0x00000000 } },         \
    { 0x0000989c, { 0x00000000, 0x00000000 } },         \
    { 0x0000989c, { 0x00000000, 0x00000000 } },         \
    { 0x0000989c, { 0x10000000, 0x10000000 } },         \
    { 0x0000989c, { 0x04000000, 0x04000000 } },         \
    { 0x0000989c, { 0x00000000, 0x00000000 } },         \
    { 0x0000989c, { 0x00000000, 0x00000000 } },         \
    { 0x0000989c, { 0x00000000, 0x00000000 } },         \
    { 0x0000989c, { 0x00000000, 0x0a000000 } },         \
    { 0x0000989c, { 0x00380080, 0x02380080 } },         \
    { 0x0000989c, { 0x00020006, 0x00000006 } },         \
    { 0x0000989c, { 0x00000092, 0x00000092 } },         \
    { 0x0000989c, { 0x000000a0, 0x000000a0 } },         \
    { 0x0000989c, { 0x00040007, 0x00040007 } },         \
    { 0x000098d4, { 0x0000001a, 0x0000001a } },         \
    { 0x0000989c, { 0x00000048, 0x00000048 } },         \
    { 0x0000989c, { 0x00000010, 0x00000010 } },         \
    { 0x0000989c, { 0x00000008, 0x00000008 } },         \
    { 0x0000989c, { 0x0000000f, 0x0000000f } },         \
    { 0x0000989c, { 0x000000f2, 0x00000062 } },         \
    { 0x0000989c, { 0x0000904f, 0x0000904c } },         \
    { 0x0000989c, { 0x0000125a, 0x0000129a } },         \
    { 0x000098cc, { 0x0000000e, 0x0000000f } },         \
}




/*
 * Internal RX/TX descriptor structures
 * (rX: reserved fields possibily used by future versions of the ar5k chipset)
 */

struct ath5k_rx_desc {
    /*
     * RX control word 0
     */
    uint32_t   rx_control_0;

#define AR5K_DESC_RX_CTL0           0x00000000

    /*
     * RX control word 1
     */
    uint32_t   rx_control_1;

#define AR5K_DESC_RX_CTL1_BUF_LEN       0x00000fff
#define AR5K_DESC_RX_CTL1_INTREQ            0x00002000
} __attribute__((packed));

struct ath5k_ar5211_rx_status {
    /*
     * RX status word 0
     */
    uint32_t   rx_status_0;

#define AR5K_AR5211_DESC_RX_STATUS0_DATA_LEN        0x00000fff
#define AR5K_AR5211_DESC_RX_STATUS0_MORE        0x00001000
#define AR5K_AR5211_DESC_RX_STATUS0_RECEIVE_RATE    0x00078000
#define AR5K_AR5211_DESC_RX_STATUS0_RECEIVE_RATE_S  15
#define AR5K_AR5211_DESC_RX_STATUS0_RECEIVE_SIGNAL  0x07f80000
#define AR5K_AR5211_DESC_RX_STATUS0_RECEIVE_SIGNAL_S    19
#define AR5K_AR5211_DESC_RX_STATUS0_RECEIVE_ANTENNA 0x38000000
#define AR5K_AR5211_DESC_RX_STATUS0_RECEIVE_ANTENNA_S   27

    /*
     * RX status word 1
     */
    uint32_t   rx_status_1;

#define AR5K_AR5211_DESC_RX_STATUS1_DONE        0x00000001
#define AR5K_AR5211_DESC_RX_STATUS1_FRAME_RECEIVE_OK    0x00000002
#define AR5K_AR5211_DESC_RX_STATUS1_CRC_ERROR       0x00000004
#define AR5K_AR5211_DESC_RX_STATUS1_FIFO_OVERRUN    0x00000008
#define AR5K_AR5211_DESC_RX_STATUS1_DECRYPT_CRC_ERROR   0x00000010
#define AR5K_AR5211_DESC_RX_STATUS1_PHY_ERROR       0x000000e0
#define AR5K_AR5211_DESC_RX_STATUS1_PHY_ERROR_S     5
#define AR5K_AR5211_DESC_RX_STATUS1_KEY_INDEX_VALID 0x00000100
#define AR5K_AR5211_DESC_RX_STATUS1_KEY_INDEX       0x00007e00
#define AR5K_AR5211_DESC_RX_STATUS1_KEY_INDEX_S     9
#define AR5K_AR5211_DESC_RX_STATUS1_RECEIVE_TIMESTAMP   0x0fff8000
#define AR5K_AR5211_DESC_RX_STATUS1_RECEIVE_TIMESTAMP_S 15
#define AR5K_AR5211_DESC_RX_STATUS1_KEY_CACHE_MISS  0x10000000
} __attribute__((packed));

struct ath5k_ar5212_rx_status {
    /*
     * RX status word 0
     */
    uint32_t   rx_status_0;

#define AR5K_AR5212_DESC_RX_STATUS0_DATA_LEN        0x00000fff
#define AR5K_AR5212_DESC_RX_STATUS0_MORE        0x00001000
#define AR5K_AR5212_DESC_RX_STATUS0_DECOMP_CRC_ERROR    0x00002000
#define AR5K_AR5212_DESC_RX_STATUS0_RECEIVE_RATE    0x000f8000
#define AR5K_AR5212_DESC_RX_STATUS0_RECEIVE_RATE_S  15
#define AR5K_AR5212_DESC_RX_STATUS0_RECEIVE_SIGNAL  0x0ff00000
#define AR5K_AR5212_DESC_RX_STATUS0_RECEIVE_SIGNAL_S    20
#define AR5K_AR5212_DESC_RX_STATUS0_RECEIVE_ANTENNA 0xf0000000
#define AR5K_AR5212_DESC_RX_STATUS0_RECEIVE_ANTENNA_S   28

    /*
     * RX status word 1
     */
    uint32_t   rx_status_1;

#define AR5K_AR5212_DESC_RX_STATUS1_DONE        0x00000001
#define AR5K_AR5212_DESC_RX_STATUS1_FRAME_RECEIVE_OK    0x00000002
#define AR5K_AR5212_DESC_RX_STATUS1_CRC_ERROR       0x00000004
#define AR5K_AR5212_DESC_RX_STATUS1_DECRYPT_CRC_ERROR   0x00000008
#define AR5K_AR5212_DESC_RX_STATUS1_PHY_ERROR       0x00000010
#define AR5K_AR5212_DESC_RX_STATUS1_MIC_ERROR       0x00000020
#define AR5K_AR5212_DESC_RX_STATUS1_KEY_INDEX_VALID 0x00000100
#define AR5K_AR5212_DESC_RX_STATUS1_KEY_INDEX       0x0000fe00
#define AR5K_AR5212_DESC_RX_STATUS1_KEY_INDEX_S     9
#define AR5K_AR5212_DESC_RX_STATUS1_RECEIVE_TIMESTAMP   0x7fff0000
#define AR5K_AR5212_DESC_RX_STATUS1_RECEIVE_TIMESTAMP_S 16
#define AR5K_AR5212_DESC_RX_STATUS1_KEY_CACHE_MISS  0x80000000
} __attribute__((packed));

struct ath5k_ar5212_rx_error {
    /*
     * RX error word 0
     */
    uint32_t   rx_error_0;

#define AR5K_AR5212_DESC_RX_ERROR0          0x00000000

    /*
     * RX error word 1
     */
    uint32_t   rx_error_1;

#define AR5K_AR5212_DESC_RX_ERROR1_PHY_ERROR_CODE   0x0000ff00
#define AR5K_AR5212_DESC_RX_ERROR1_PHY_ERROR_CODE_S 8
} __attribute__((packed));

#define AR5K_DESC_RX_PHY_ERROR_NONE     0x00
#define AR5K_DESC_RX_PHY_ERROR_TIMING       0x20
#define AR5K_DESC_RX_PHY_ERROR_PARITY       0x40
#define AR5K_DESC_RX_PHY_ERROR_RATE     0x60
#define AR5K_DESC_RX_PHY_ERROR_LENGTH       0x80
#define AR5K_DESC_RX_PHY_ERROR_64QAM        0xa0
#define AR5K_DESC_RX_PHY_ERROR_SERVICE      0xc0
#define AR5K_DESC_RX_PHY_ERROR_TRANSMITOVR  0xe0

struct ath5k_ar5211_tx_desc {
    /*
     * TX control word 0
     */
    uint32_t   tx_control_0;

#define AR5K_AR5211_DESC_TX_CTL0_FRAME_LEN      0x00000fff
#define AR5K_AR5211_DESC_TX_CTL0_XMIT_RATE      0x003c0000
#define AR5K_AR5211_DESC_TX_CTL0_XMIT_RATE_S        18
#define AR5K_AR5211_DESC_TX_CTL0_RTSENA         0x00400000
#define AR5K_AR5211_DESC_TX_CTL0_VEOL           0x00800000
#define AR5K_AR5211_DESC_TX_CTL0_CLRDMASK       0x01000000
#define AR5K_AR5211_DESC_TX_CTL0_ANT_MODE_XMIT      0x1e000000
#define AR5K_AR5211_DESC_TX_CTL0_ANT_MODE_XMIT_S    25
#define AR5K_AR5211_DESC_TX_CTL0_INTREQ         0x20000000
#define AR5K_AR5211_DESC_TX_CTL0_ENCRYPT_KEY_VALID  0x40000000

    /*
     * TX control word 1
     */
    uint32_t   tx_control_1;

#define AR5K_AR5211_DESC_TX_CTL1_BUF_LEN        0x00000fff
#define AR5K_AR5211_DESC_TX_CTL1_MORE           0x00001000
#define AR5K_AR5211_DESC_TX_CTL1_ENCRYPT_KEY_INDEX  0x000fe000
#define AR5K_AR5211_DESC_TX_CTL1_ENCRYPT_KEY_INDEX_S    13
#define AR5K_AR5211_DESC_TX_CTL1_FRAME_TYPE     0x00700000
#define AR5K_AR5211_DESC_TX_CTL1_FRAME_TYPE_S       20
#define AR5K_AR5211_DESC_TX_CTL1_NOACK          0x00800000
} __attribute__((packed));

struct ath5k_ar5212_tx_desc {
    /*
     * TX control word 0
     */
    uint32_t   tx_control_0;

#define AR5K_AR5212_DESC_TX_CTL0_FRAME_LEN      0x00000fff
#define AR5K_AR5212_DESC_TX_CTL0_XMIT_POWER     0x003f0000
#define AR5K_AR5212_DESC_TX_CTL0_XMIT_POWER_S       16
#define AR5K_AR5212_DESC_TX_CTL0_RTSENA         0x00400000
#define AR5K_AR5212_DESC_TX_CTL0_VEOL           0x00800000
#define AR5K_AR5212_DESC_TX_CTL0_CLRDMASK       0x01000000
#define AR5K_AR5212_DESC_TX_CTL0_ANT_MODE_XMIT      0x1e000000
#define AR5K_AR5212_DESC_TX_CTL0_ANT_MODE_XMIT_S    25
#define AR5K_AR5212_DESC_TX_CTL0_INTREQ         0x20000000
#define AR5K_AR5212_DESC_TX_CTL0_ENCRYPT_KEY_VALID  0x40000000
#define AR5K_AR5212_DESC_TX_CTL0_CTSENA         0x80000000

    /*
     * TX control word 1
     */
    uint32_t   tx_control_1;

#define AR5K_AR5212_DESC_TX_CTL1_BUF_LEN        0x00000fff
#define AR5K_AR5212_DESC_TX_CTL1_MORE           0x00001000
#define AR5K_AR5212_DESC_TX_CTL1_ENCRYPT_KEY_INDEX  0x000fe000
#define AR5K_AR5212_DESC_TX_CTL1_ENCRYPT_KEY_INDEX_S    13
#define AR5K_AR5212_DESC_TX_CTL1_FRAME_TYPE     0x00f00000
#define AR5K_AR5212_DESC_TX_CTL1_FRAME_TYPE_S       20
#define AR5K_AR5212_DESC_TX_CTL1_NOACK          0x01000000
#define AR5K_AR5212_DESC_TX_CTL1_COMP_PROC      0x06000000
#define AR5K_AR5212_DESC_TX_CTL1_COMP_PROC_S        25
#define AR5K_AR5212_DESC_TX_CTL1_COMP_IV_LEN        0x18000000
#define AR5K_AR5212_DESC_TX_CTL1_COMP_IV_LEN_S      27
#define AR5K_AR5212_DESC_TX_CTL1_COMP_ICV_LEN       0x60000000
#define AR5K_AR5212_DESC_TX_CTL1_COMP_ICV_LEN_S     29

    /*
     * TX control word 2
     */
    uint32_t   tx_control_2;

#define AR5K_AR5212_DESC_TX_CTL2_RTS_DURATION       0x00007fff
#define AR5K_AR5212_DESC_TX_CTL2_DURATION_UPDATE_ENABLE 0x00008000
#define AR5K_AR5212_DESC_TX_CTL2_XMIT_TRIES0        0x000f0000
#define AR5K_AR5212_DESC_TX_CTL2_XMIT_TRIES0_S      16
#define AR5K_AR5212_DESC_TX_CTL2_XMIT_TRIES1        0x00f00000
#define AR5K_AR5212_DESC_TX_CTL2_XMIT_TRIES1_S      20
#define AR5K_AR5212_DESC_TX_CTL2_XMIT_TRIES2        0x0f000000
#define AR5K_AR5212_DESC_TX_CTL2_XMIT_TRIES2_S      24
#define AR5K_AR5212_DESC_TX_CTL2_XMIT_TRIES3        0xf0000000
#define AR5K_AR5212_DESC_TX_CTL2_XMIT_TRIES3_S      28

    /*
     * TX control word 3
     */
    uint32_t   tx_control_3;

#define AR5K_AR5212_DESC_TX_CTL3_XMIT_RATE0     0x0000001f
#define AR5K_AR5212_DESC_TX_CTL3_XMIT_RATE1     0x000003e0
#define AR5K_AR5212_DESC_TX_CTL3_XMIT_RATE1_S       5
#define AR5K_AR5212_DESC_TX_CTL3_XMIT_RATE2     0x00007c00
#define AR5K_AR5212_DESC_TX_CTL3_XMIT_RATE2_S       10
#define AR5K_AR5212_DESC_TX_CTL3_XMIT_RATE3     0x000f8000
#define AR5K_AR5212_DESC_TX_CTL3_XMIT_RATE3_S       15
#define AR5K_AR5212_DESC_TX_CTL3_RTS_CTS_RATE       0x01f00000
#define AR5K_AR5212_DESC_TX_CTL3_RTS_CTS_RATE_S     20
} __attribute__((packed));

struct ath5k_tx_status {
    /*
     * TX status word 0
     */
    uint32_t   tx_status_0;

#define AR5K_DESC_TX_STATUS0_FRAME_XMIT_OK  0x00000001
#define AR5K_DESC_TX_STATUS0_EXCESSIVE_RETRIES  0x00000002
#define AR5K_DESC_TX_STATUS0_FIFO_UNDERRUN  0x00000004
#define AR5K_DESC_TX_STATUS0_FILTERED       0x00000008
#define AR5K_DESC_TX_STATUS0_RTS_FAIL_COUNT 0x000000f0
#define AR5K_DESC_TX_STATUS0_RTS_FAIL_COUNT_S   4
#define AR5K_DESC_TX_STATUS0_DATA_FAIL_COUNT    0x00000f00
#define AR5K_DESC_TX_STATUS0_DATA_FAIL_COUNT_S  8
#define AR5K_DESC_TX_STATUS0_VIRT_COLL_COUNT    0x0000f000
#define AR5K_DESC_TX_STATUS0_VIRT_COLL_COUNT_S  12
#define AR5K_DESC_TX_STATUS0_SEND_TIMESTAMP 0xffff0000
#define AR5K_DESC_TX_STATUS0_SEND_TIMESTAMP_S   16

    /*
     * TX status word 1
     */
    uint32_t   tx_status_1;

#define AR5K_DESC_TX_STATUS1_DONE       0x00000001
#define AR5K_DESC_TX_STATUS1_SEQ_NUM        0x00001ffe
#define AR5K_DESC_TX_STATUS1_SEQ_NUM_S      1
#define AR5K_DESC_TX_STATUS1_ACK_SIG_STRENGTH   0x001fe000
#define AR5K_DESC_TX_STATUS1_ACK_SIG_STRENGTH_S 13
#define AR5K_DESC_TX_STATUS1_FINAL_TS_INDEX 0x00600000
#define AR5K_DESC_TX_STATUS1_FINAL_TS_INDEX_S   21
#define AR5K_DESC_TX_STATUS1_COMP_SUCCESS   0x00800000
#define AR5K_DESC_TX_STATUS1_XMIT_ANTENNA   0x01000000
} __attribute__((packed));

